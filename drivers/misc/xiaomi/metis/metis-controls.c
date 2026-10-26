// SPDX-License-Identifier: GPL-2.0-only
/* Link/boost knob get/set ops. The stock getters returned the knob value
 * as a byte count without touching the buffer; a kernel_param get must
 * actually fill it, so these emit properly now. The fas suspend clock and
 * the mi_sched_enable registry died with their only readers. */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/sysfs.h>
#include "metis_common.h"

int get_fboost_enable(char *buffer, const struct kernel_param *param)
{
	return sysfs_emit(buffer, "%d\n", mi_fboost_enable);
}

int get_mi_fboost_enable(void)
{
	return mi_fboost_enable;
}

int set_fboost_enable(const char *value, const struct kernel_param *param)
{
	u32 enabled = 0;

	if (sscanf(value, "%u\n", &enabled) != 1)
		return -EINVAL;
	mi_fboost_enable = enabled;
	return 0;
}
