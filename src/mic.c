/**
 * collectd - src/mic.c
 * Copyright (C) 2013 Battelle Memorial Institute
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Evan Felix <evan.felix at pnnl.gov>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"

#include <MicAccessApi.h>
#include <MicAccessErrorTypes.h>
#include <MicAccessTypes.h>
#include <MicPowerManagerAPI.h>
#include <MicThermalAPI.h>

#define MAX_MICS 32
#define MAX_CORES 256

static MicDeviceOnSystem mics[MAX_MICS];
static U32 num_mics;
static HANDLE mic_handle;

static int const therm_ids[] = {
    eMicThermalDie,  eMicThermalDevMem, eMicThermalFin, eMicThermalFout,
    eMicThermalVccp, eMicThermalVddg,   eMicThermalVddq};
static char const *const therm_names[] = {"die",  "devmem", "fin", "fout",

static const char *config_keys[] = {
    "ShowCPU",          "ShowCPUCores", "ShowMemory",
    "ShowTemperatures", "Temperature",  "IgnoreSelectedTemperature",
    "ShowPower",        "Power",        "IgnoreSelectedPower"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static bool show_cpu = true;
static bool show_cpu_cores = true;
static bool show_memory = true;
static bool show_temps = true;
static ignorelist_t *temp_ignore;
static bool show_power = true;
static ignorelist_t *power_ignore;

static int mic_init(void) {
  U32 ret;
  U32 mic_count;

  if (mics)
    return (0);

  mic_count = 0;
  ret = mic_get_devices(&mics);
  if (ret != E_MIC_SUCCESS) {
    ERROR("mic plugin: Problem getting mic device list: %s",
          mic_get_error_string());
  }
  ret = mic_get_ndevices(mics, &mic_count);
  if (ret != E_MIC_SUCCESS) {
    ERROR("mic plugin: Problem getting numer of mic's: %s",
          mic_get_error_string());
  }
  DEBUG("mic plugin: found: %" PRIu32 " MIC(s)", mic_count);

  if (mic_count < 0 || mic_count >= MAX_MICS) {
    ERROR("mic plugin: No Intel MICs in system");
    return (1);
  } else {
    num_mics = mic_count;
    return (0);
  }
}

static int mic_config(const char *key, const char *value) {
  if (temp_ignore == NULL)
    temp_ignore = ignorelist_create(1);
  if (power_ignore == NULL)
    power_ignore = ignorelist_create(1);
  if (temp_ignore == NULL || power_ignore == NULL)
    return 1;

  if (strcasecmp("ShowCPU", key) == 0) {
    show_cpu = IS_TRUE(value);
  } else if (strcasecmp("ShowCPUCores", key) == 0) {
    show_cpu_cores = IS_TRUE(value);
  } else if (strcasecmp("ShowTemperatures", key) == 0) {
    show_temps = IS_TRUE(value);
  } else if (strcasecmp("ShowMemory", key) == 0) {
    show_memory = IS_TRUE(value);
  } else if (strcasecmp("ShowPower", key) == 0) {
    show_power = IS_TRUE(value);
  } else if (strcasecmp("Temperature", key) == 0) {
    ignorelist_add(temp_ignore, value);
  } else if (strcasecmp("IgnoreSelectedTemperature", key) == 0) {
    int invert = 1;
    if (IS_TRUE(value))
      invert = 0;
    ignorelist_set_invert(temp_ignore, invert);
  } else if (strcasecmp("Power", key) == 0) {
    ignorelist_add(power_ignore, value);
  } else if (strcasecmp("IgnoreSelectedPower", key) == 0) {
    int invert = 1;
    if (IS_TRUE(value))
      invert = 0;
    ignorelist_set_invert(power_ignore, invert);
  } else {
    return -1;
  }
  return 0;
}

static void mic_submit_memory_use(int micnumber, const char *type_instance,
                                  uint64_t val) {
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  DEBUG("mic plugin: Memory Value Report; %u %lf", value,
        ((gauge_t)value) * 1024.0);
  values[0].gauge = ((gauge_t)val) * 1024.0;

  vl.values = &(value_t){.gauge = ((gauge_t)value) * 1024.0};
  vl.values_len = 1;

  strncpy(vl.plugin, "mic", sizeof(vl.plugin));
  ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%i", micnumber);
  strncpy(vl.type, "memory", sizeof(vl.type));
  strncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

/* Gather memory Utilization */
static int mic_read_memory(int mic) {

  struct mic_memory_util_info *mui;
  U32 ret, error;
  U32 mem_free, mem_bufs, mem_total;

  ret = mic_get_memory_utilization_info(mic_dev, &mui);
  if (ret != E_MIC_SUCCESS) {
    ERROR("mic plugin: Problem getting Memory Utilization: %s",
          mic_get_error_string());
    return (1);
  }
  error = 0;
  ret = mic_get_total_memory_size(mui, &mem_total);
  if (ret != E_MIC_SUCCESS)
    error += 1;

  ret = mic_get_available_memory_size(mui, &mem_free);
  if (ret != E_MIC_SUCCESS)
    error += 1;

  ret = mic_get_memory_buffers_size(mui, &mem_bufs);
  if (ret != E_MIC_SUCCESS)
    error += 1;

  if (error == 0) {
    mic_submit_memory_use(mic, "free", mem_free);
    mic_submit_memory_use(mic, "used", mem_total - mem_free - mem_bufs);
    mic_submit_memory_use(mic, "buffered", mem_bufs);
    DEBUG("mic plugin: Memory Read: %u %u %u", mem_total, mem_free, mem_bufs);
  }

  mic_free_memory_utilization_info(mui);
  return (0);
}

static void mic_submit_temp(int micnumber, const char *type, gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;

  strncpy(vl.plugin, "mic", sizeof(vl.plugin));
  ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%i", micnumber);
  strncpy(vl.type, "temperature", sizeof(vl.type));
  strncpy(vl.type_instance, type, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

/* Gather Temperature Information */
static int mic_read_temps(int mic) {
  struct mic_thermal_info *mti;
  U32 ret, valid;

  ret = mic_get_thermal_info(mic_dev, &mti);
  if (ret != E_MIC_SUCCESS) {
    ERROR("mic plugin: Problem getting thermal Information: %s",
          mic_get_error_string());
    return (-1);
  }

#define SUB_TEMP(name, bits)                                                   \
  do {                                                                         \
    uint##bits##_t temp;                                                       \
    if (ignorelist_match(temp_ignore, #name) == 0) {                           \
      ret = mic_is_##name##_temp_valid(mti, &valid);                           \
      if (ret == E_MIC_SUCCESS && valid != 0) {                                \
        ret = mic_get_##name##_temp(mti, &temp);                               \
        if (ret == E_MIC_SUCCESS)                                              \
          mic_submit_temp(mic, #name, temp);                                   \
      }                                                                        \
    }                                                                          \
  } while (0);

  SUB_TEMP(die, 32);
  SUB_TEMP(gddr, 16);
  SUB_TEMP(fanin, 16);
  SUB_TEMP(fanout, 16);
  SUB_TEMP(vccp, 16);
  SUB_TEMP(vddg, 16);
  SUB_TEMP(vddq, 16);

  mic_free_thermal_info(mti);

  return (0);
}

static void mic_submit_cpu(int micnumber, const char *type_instance, int core,
                           derive_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.derive = value};
  vl.values_len = 1;

  strncpy(vl.plugin, "mic", sizeof(vl.plugin));
  if (core < 0) /* global aggregation */
    ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%i", micnumber);
  else /* per-core statistics */
    ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%i-cpu-%i",
              micnumber, core);
  strncpy(vl.type, "cpu", sizeof(vl.type));
  strncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

/*Gather CPU Utilization Information */
static int mic_read_cpu(int mic) {
  struct mic_core_util *mcu;
  U32 ret;
  uint16_t cores, threads;
  uint64_t value;
  uint64_t *counters;

  ret = mic_alloc_core_util(&mcu);
  if (ret != E_MIC_SUCCESS) {
    ERROR("mic plugin: Problem allocating core util: %s",
          mic_get_error_string());
    return (-1);
  }
  ret = mic_update_core_util(mic_dev, mcu);
  if (ret != E_MIC_SUCCESS) {
    ERROR("mic plugin: Problem getting device cpu utilization: %s",
          mic_get_error_string());
    mic_free_core_util(mcu);
    return (-1);
  }

  if (show_cpu) {
    ret = mic_get_user_sum(mcu, &value);
    if (ret == E_MIC_SUCCESS)
      mic_submit_cpu(mic, "user", -1, value);

    ret = mic_get_sys_sum(mcu, &value);
    if (ret == E_MIC_SUCCESS)
      mic_submit_cpu(mic, "sys", -1, value);

    ret = mic_get_nice_sum(mcu, &value);
    if (ret == E_MIC_SUCCESS)
      mic_submit_cpu(mic, "nice", -1, value);

    ret = mic_get_idle_sum(mcu, &value);
    if (ret == E_MIC_SUCCESS)
      mic_submit_cpu(mic, "idle", -1, value);
    mic_get_jiffy_counter(mcu, &value);
  }

  if (show_cpu_cores) {
    int j;
    ret = mic_get_num_cores(mcu, &cores);
    if (ret != E_MIC_SUCCESS) {
      ERROR("mic plugin: Problem getting core count: %s",
            mic_get_error_string());
      cores = 0;
    }
    ret = mic_get_threads_core(mcu, &threads);
    if (ret != E_MIC_SUCCESS) {
      ERROR("mic plugin: Problem getting thread count: %s",
            mic_get_error_string());
      threads = 0;
    }
#define PER_CPU_COUNTERS(type)                                                 \
  do {                                                                         \
    ret = mic_get_##type##_counters(mcu, counters);                            \
    if (ret == E_MIC_SUCCESS) {                                                \
      for (j = 0; j < cores * threads; j++) {                                  \
        mic_submit_cpu(mic, #type, j, counters[j]);                            \
      }                                                                        \
    }                                                                          \
  } while (0);
    if (cores * threads > 0) {
      counters = calloc(cores * threads, sizeof(uint64_t));
      if (counters) {
        PER_CPU_COUNTERS(user);
        PER_CPU_COUNTERS(idle);
        PER_CPU_COUNTERS(sys);
        PER_CPU_COUNTERS(nice);
        free(counters);
      }
    }
  }

  mic_free_core_util(mcu);
  return (0);
}

static void mic_submit_power(int micnumber, const char *type,
                             const char *type_instance, gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;

  strncpy(vl.plugin, "mic", sizeof(vl.plugin));
  ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%i", micnumber);
  strncpy(vl.type, type, sizeof(vl.type));
  strncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

/* Gather Power Information */
static int mic_read_power(int mic) {
  U32 ret;
  U32 value, valid;
  struct mic_power_util_info *mpui;

  ret = mic_get_power_utilization_info(mic_dev, &mpui);
  if (ret != E_MIC_SUCCESS) {
    ERROR("mic plugin: Problem getting power Information: %s",
          mic_get_error_string());
    return (-1);
  }

/* power is in uWatts, current in uA, voltage in uVolts..   convert to
 * base unit */
#define SUB_POWER(name)                                                        \
  do {                                                                         \
    if (ignorelist_match(power_ignore, #name) == 0) {                          \
      ret = mic_get_##name##_power_sensor_sts(mpui, &valid);                   \
      if (ret == E_MIC_SUCCESS && valid == 0) {                                \
        ret = mic_get_##name##_power_readings(mpui, &value);                   \
        if (ret == E_MIC_SUCCESS)                                              \
          mic_submit_power(mic, "power", #name, (gauge_t)value * 0.000001);    \
      }                                                                        \
    }                                                                          \
  } while (0)
#define SUB_POWERTOTAL(num)                                                    \
  do {                                                                         \
    if (ignorelist_match(power_ignore, "total" #num) == 0) {                   \
      ret = mic_get_total_power_sensor_sts_w##num(mpui, &valid);               \
      if (ret == E_MIC_SUCCESS && valid == 0) {                                \
        ret = mic_get_total_power_readings_w##num(mpui, &value);               \
        if (ret == E_MIC_SUCCESS)                                              \
          mic_submit_power(mic, "power", "total" #num,                         \
                           (gauge_t)value * 0.000001);                         \
      }                                                                        \
    }                                                                          \
  } while (0)
#define SUB_VOLTS(name)                                                        \
  do {                                                                         \
    if (ignorelist_match(power_ignore, #name) == 0) {                          \
      ret = mic_get_##name##_power_sensor_sts(mpui, &valid);                   \
      if (ret == E_MIC_SUCCESS && valid == 0) {                                \
        ret = mic_get_##name##_power_readings(mpui, &value);                   \
        if (ret == E_MIC_SUCCESS)                                              \
          mic_submit_power(mic, "power", #name, (gauge_t)value * 0.000001);    \
      }                                                                        \
      ret = mic_get_##name##_current_sensor_sts(mpui, &valid);                 \
      if (ret == E_MIC_SUCCESS && valid == 0) {                                \
        ret = mic_get_##name##_current_readings(mpui, &value);                 \
        if (ret == E_MIC_SUCCESS)                                              \
          mic_submit_power(mic, "current", #name, (gauge_t)value * 0.000001);  \
      }                                                                        \
      ret = mic_get_##name##_voltage_sensor_sts(mpui, &valid);                 \
      if (ret == E_MIC_SUCCESS && valid == 0) {                                \
        ret = mic_get_##name##_voltage_readings(mpui, &value);                 \
        if (ret == E_MIC_SUCCESS)                                              \
          mic_submit_power(mic, "voltage", #name, (gauge_t)value * 0.000001);  \
      }                                                                        \
    }                                                                          \
  } while (0)

  SUB_POWERTOTAL(0);
  SUB_POWERTOTAL(1);
  SUB_POWER(inst);
  SUB_POWER(max_inst);
  SUB_POWER(pcie);
  SUB_POWER(c2x3);
  SUB_POWER(c2x4);
  SUB_VOLTS(vccp);
  SUB_VOLTS(vddg);
  SUB_VOLTS(vddq);

  mic_free_power_utilization_info(mpui);
  return (0);
}

static int mic_read(void) {
  int i, device;
  int ret;
  int error;

  error = 0;
  for (i = 0; i < num_mics; i++) {
    ret = mic_get_device_at_index(mics, i, &device);
    if (ret != E_MIC_SUCCESS) {
      ERROR("mic plugin: Problem getting device number: %s",
            mic_get_error_string());
      error = 1;
    }
    ret = mic_open_device(&mic_dev, device);
    if (ret != E_MIC_SUCCESS) {
      ERROR("mic plugin: Problem opening device: %s", mic_get_error_string());
      error = 1;
    }

    if (error == 0 && show_memory)
      error = mic_read_memory(i);

    if (error == 0 && show_temps)
      error = mic_read_temps(i);

    if (error == 0 && (show_cpu || show_cpu_cores))
      error = mic_read_cpu(i);

    if (error == 0 && (show_power))
      error = mic_read_power(i);

    ret = mic_close_device(mic_dev);
    if (ret != E_MIC_SUCCESS) {
      ERROR("mic plugin: Problem closing device: %s", mic_get_error_string());
      error = 2;
      break;
    }
  }
  if (num_mics == 0)
    error = 3;
  return error;
}

static int mic_shutdown(void) {
  if (mic_handle)
    MicCloseAPI(&mic_handle);
  mic_handle = NULL;

  static int mic_shutdown(void) {
    if (mics)
      mic_free_devices(mics);
    return (0);
  }

  void module_register(void) {
    plugin_register_init("mic", mic_init);
    plugin_register_shutdown("mic", mic_shutdown);
    plugin_register_read("mic", mic_read);
    plugin_register_config("mic", mic_config, config_keys, config_keys_num);
  } /* void module_register */
