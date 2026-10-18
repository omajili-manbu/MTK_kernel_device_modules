/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rodin vendor firmware gate
 *
 * Built-in vendor drivers probe before first-stage init mounts /vendor, so a
 * firmware request made during probe is bound to fail and the driver spends
 * its retry budget on -ENOENT.  This gate gives those drivers one shared,
 * quiet place to wait for the vendor firmware directory to appear.
 *
 * The gate never loads anything itself: callers keep their own retry loops
 * as the fallback for files that are genuinely missing.
 */
#ifndef _LINUX_RODIN_VENDOR_FW_H
#define _LINUX_RODIN_VENDOR_FW_H

#include <linux/types.h>

/*
 * Process context only (the first call allocates the poll workqueue).
 * Returns true once the vendor firmware directory is reachable and starts
 * the gate on first use.  Never blocks.
 */
bool rodin_vendor_fw_ready(void);

/*
 * Process context only.  Sleeps until the vendor firmware directory is
 * reachable, or until the gate gives up.
 *
 * Returns 0 when ready, -ETIMEDOUT when the gate gave up.  Callers must
 * still handle a failed request afterwards: the gate only covers the
 * "partition not mounted yet" window, it does not guarantee the file.
 */
int rodin_vendor_fw_wait(void);

#endif /* _LINUX_RODIN_VENDOR_FW_H */
