/**
 * collectd - src/target_rate.c
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
 *   Florian Forster <octo at verplant.org>
 *   Evan Felix <evan dot felix at pnnl.gov>
 **/

#include "collectd.h"
#include "common.h"
#include "utils_cache.h"
#include "filter_chain.h"

struct trate_data_s
{
};
typedef struct trate_data_s trate_data_t;


static int trate_destroy (void **user_data) /* {{{ */
{
  trate_data_t *data;

  if (user_data == NULL)
    return (-EINVAL);

  data = *user_data;
  if (data == NULL)
    return (0);

  INFO("Rate Target Exiting");

  return (0);
} /* }}} int trate_destroy */

static int trate_create (const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  trate_data_t *data;
  int status;
  

  data = (trate_data_t *) malloc (sizeof (*data));
  if (data == NULL)
  {
    ERROR ("trate_create: malloc failed.");
    return (-ENOMEM);
  }
  memset (data, 0, sizeof (*data));

  status = 0;

  if (status != 0)
  {
    trate_destroy ((void *) &data);
    return (status);
  }

  *user_data = data;
  return (0);
} /* }}} int ts_create */

static int trate_invoke (const data_set_t *ds, value_list_t *vl, /* {{{ */
    notification_meta_t __attribute__((unused)) **meta, void **user_data)
{
  int i,status;
  trate_data_t *data;
  gauge_t *rates;

  if ((ds == NULL) || (vl == NULL) || (user_data == NULL))
    return (-EINVAL);

  data = *user_data;
  if (data == NULL)
  {
    ERROR ("Target `set': Invoke: `data' is NULL.");
    return (-EINVAL);
  }

  status=FC_TARGET_CONTINUE;
  rates = uc_get_rate(ds,vl);
  if (rates!=NULL && !isnan(rates[0])) {
    for (i=0;i<ds->ds_num;i++) {
      switch (ds->ds->type) {
        case DS_TYPE_GAUGE:
            vl->values[i].gauge = (gauge_t)rates[i];
            break;
        case DS_TYPE_DERIVE:
            vl->values[i].derive = (derive_t)rates[i];
            break;
        case DS_TYPE_COUNTER:
            vl->values[i].counter = (counter_t)rates[i];
            break;
        case DS_TYPE_ABSOLUTE:
            vl->values[i].absolute = (absolute_t)rates[i];
            break;
        default:
            ERROR("Bad DS");
            status=FC_TARGET_STOP;
            break;
            }
        }
    free(rates);
  }
  else {
    status=FC_TARGET_STOP;
  }

  return (status);
} /* }}} int ts_invoke */

void module_register (void)
{
	target_proc_t tproc;

	memset (&tproc, 0, sizeof (tproc));
	tproc.create  = trate_create;
	tproc.destroy = trate_destroy;
	tproc.invoke  = trate_invoke;
	fc_register_target ("rate", tproc);
} /* module_register */

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */

