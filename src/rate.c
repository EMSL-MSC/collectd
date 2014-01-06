/**
 * collectd - src/rate.c 
 * Copyright (C) 2014 Battelle Memorial Institute
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Evan Felix <evan dot felix at pnnl.gov>
 **/

#include "collectd.h"

#include <pthread.h>

#include "plugin.h"
#include "common.h"
#include "utils_cache.h" /* for uc_get_rate() */
typedef struct rate_list_s rate_list_t;
struct rate_list_s
{
  value_list_t *vl;
  rate_list_t *next;
}; 
static pthread_mutex_t rate_list_lock = PTHREAD_MUTEX_INITIALIZER;
static rate_list_t *rate_list_head = NULL;

static const char *config_keys[] =
{

};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int rate_config (const char *key, const char *value) 
{
  INFO("rate config: %s=%s",key,value);
  return (0);
}

static int rate_read (void) /* {{{ */
{
  int success=1;
  rate_list_t *this,*old;
  //walk the list, emiting values, destroying as we go.
  pthread_mutex_lock (&rate_list_lock);
  if (rate_list_head == NULL)
  {
    pthread_mutex_unlock (&rate_list_lock);
    return (0);
  }

  for (this = rate_list_head; this != NULL;)
  {
      //INFO("read: %s %s %s %s %s %lf %llu %"PRIi64" %"PRIu64,this->vl->host,this->vl->plugin,this->vl->plugin_instance,this->vl->type,this->vl->type_instance,this->vl->values[0].gauge,this->vl->values[0].counter,this->vl->values[0].derive,this->vl->values[0].absolute);
      plugin_dispatch_values (this->vl);
      old = this;
      this = this->next;
      free(old->vl->values);
      meta_data_destroy(old->vl->meta);
      free(old->vl);
      free(old);
      success++;
  }
  rate_list_head=NULL;
  pthread_mutex_unlock (&rate_list_lock);

   return ((success > 0) ? 0 : -1);
}  /* }}} int rate_read */

static int rate_write (data_set_t const *ds, value_list_t const *vl, /* {{{ */
    __attribute__((unused)) user_data_t *user_data)
{
  int i,status=0;
  value_list_t *vlcopy;
  value_t *vrates;
  gauge_t *rates;
  rate_list_t *newrate;

  //INFO("write: %s %s %s %s %s %d",vl->host,vl->plugin,vl->plugin_instance,vl->type,vl->type_instance,ds->ds_num);
  //copy the value for our own use, get its rate, then put it on the list
  vrates = calloc(vl->values_len,sizeof(value_t));
  vlcopy = malloc(sizeof(value_list_t));
  newrate = malloc(sizeof(rate_list_t));
  if (vrates == NULL || vlcopy == NULL || newrate == NULL) {
    ERROR("error mallocing data for copying vl");
    return (-1);
  }

  memcpy(vlcopy,vl,sizeof(value_list_t));
  vlcopy->values=vrates;
  strncpy (vlcopy->plugin, "rate", sizeof (vl->plugin));
  if (vl->meta)
    vlcopy->meta = meta_data_clone(vl->meta);

  rates = uc_get_rate(ds,vl);
  if (rates!=NULL && !isnan(rates[0])) {
    for (i=0;i<ds->ds_num;i++) {
      //INFO("Newrate(%d): %f %f %p",i,vrates[i].gauge,rates[i],rates);
      switch (ds->ds->type) {
        case DS_TYPE_GAUGE:
          vrates[i].gauge = (gauge_t)rates[i];
          break;
        case DS_TYPE_DERIVE:
          vrates[i].derive = (derive_t)rates[i];
          break;
        case DS_TYPE_COUNTER:
          vrates[i].counter = (counter_t)rates[i];
          break;
        case DS_TYPE_ABSOLUTE:
          vrates[i].absolute = (absolute_t)rates[i];
          break;
        default:
          ERROR("Bad DS");
          break;
      }
    }
    newrate->vl=vlcopy;
  
    //add to the protected list
    pthread_mutex_lock (&rate_list_lock);
    newrate->next = rate_list_head;
    rate_list_head = newrate;
    pthread_mutex_unlock (&rate_list_lock);
  }
  return (status);
} /* }}} int rate_write */

void module_register (void)
{
  plugin_register_config ("rate", rate_config,config_keys,config_keys_num);
  plugin_register_read ("rate", rate_read);
  plugin_register_write ("rate", rate_write, /* user_data = */ NULL);
}

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */
