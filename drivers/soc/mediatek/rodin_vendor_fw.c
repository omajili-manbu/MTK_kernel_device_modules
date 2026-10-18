// SPDX-License-Identifier: GPL-2.0-only
/*
 * rodin vendor firmware gate
 *
 * The vendor partition is mounted by first-stage init, long after the
 * built-in drivers have probed.  A driver that requests a file from
 * /vendor/firmware during probe gets -ENOENT and starts retrying, which
 * makes every boot log look like a failure even though the file lands a
 * few seconds later.
 *
 * Drivers call this gate before their first request.  The first caller
 * starts a silent poll of the vendor firmware directory; waiters sleep
 * until the directory resolves.  Nothing is logged per poll, and the
 * gate opens at most once, so the boot log carries one line instead of
 * a retry storm.
 */

#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/printk.h>
#include <linux/rodin_vendor_fw.h>
#include <linux/workqueue.h>

#define RODIN_FW_GATE_POLL_MS		250U
#define RODIN_FW_GATE_TIMEOUT_MS	30000U

enum rodin_fw_gate_state {
	RODIN_FW_GATE_CLOSED = 0,
	RODIN_FW_GATE_OPEN,
	RODIN_FW_GATE_GAVE_UP,
};

static char *rodin_fw_gate_path = "/vendor/firmware";
static unsigned int rodin_fw_gate_timeout_ms = RODIN_FW_GATE_TIMEOUT_MS;

module_param_named(path, rodin_fw_gate_path, charp, 0444);
MODULE_PARM_DESC(path, "Vendor firmware directory used as the mount gate");
module_param_named(timeout_ms, rodin_fw_gate_timeout_ms, uint, 0644);
MODULE_PARM_DESC(timeout_ms, "Milliseconds to wait for the vendor firmware directory");

static DECLARE_COMPLETION(rodin_fw_gate_done);
static atomic_t rodin_fw_gate_state = ATOMIC_INIT(RODIN_FW_GATE_CLOSED);
static void rodin_fw_gate_poll(struct work_struct *work);
static DECLARE_DELAYED_WORK(rodin_fw_gate_work, rodin_fw_gate_poll);
static struct workqueue_struct *rodin_fw_gate_wq;
static DEFINE_MUTEX(rodin_fw_gate_lock);
static ktime_t rodin_fw_gate_started_at;
static ktime_t rodin_fw_gate_deadline;
static bool rodin_fw_gate_started;

static bool rodin_fw_gate_path_present(void)
{
	struct path path;

	if (kern_path(rodin_fw_gate_path, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
		      &path))
		return false;

	path_put(&path);
	return true;
}

static void rodin_fw_gate_poll(struct work_struct *work)
{
	s64 elapsed_ms;

	if (rodin_fw_gate_path_present()) {
		elapsed_ms = ktime_ms_delta(ktime_get(), rodin_fw_gate_started_at);
		atomic_set(&rodin_fw_gate_state, RODIN_FW_GATE_OPEN);
		pr_info("rodin_vendor_fw: %s ready after %lld ms\n",
			rodin_fw_gate_path, elapsed_ms);
		complete_all(&rodin_fw_gate_done);
		return;
	}

	if (ktime_after(ktime_get(), rodin_fw_gate_deadline)) {
		atomic_set(&rodin_fw_gate_state, RODIN_FW_GATE_GAVE_UP);
		pr_warn("rodin_vendor_fw: %s not ready after %u ms, callers keep their own retries\n",
			rodin_fw_gate_path, rodin_fw_gate_timeout_ms);
		complete_all(&rodin_fw_gate_done);
		return;
	}

	queue_delayed_work(rodin_fw_gate_wq, &rodin_fw_gate_work,
			   msecs_to_jiffies(RODIN_FW_GATE_POLL_MS));
}

static void rodin_fw_gate_start(void)
{
	mutex_lock(&rodin_fw_gate_lock);
	if (rodin_fw_gate_started)
		goto out;

	rodin_fw_gate_started = true;

	if (atomic_read(&rodin_fw_gate_state) != RODIN_FW_GATE_CLOSED)
		goto out;

	rodin_fw_gate_wq = alloc_ordered_workqueue("rodin_fw_gate", 0);
	if (!rodin_fw_gate_wq) {
		atomic_set(&rodin_fw_gate_state, RODIN_FW_GATE_GAVE_UP);
		complete_all(&rodin_fw_gate_done);
		goto out;
	}

	rodin_fw_gate_started_at = ktime_get();
	rodin_fw_gate_deadline = ktime_add_ms(rodin_fw_gate_started_at,
					      rodin_fw_gate_timeout_ms);
	queue_delayed_work(rodin_fw_gate_wq, &rodin_fw_gate_work, 0);
out:
	mutex_unlock(&rodin_fw_gate_lock);
}

bool rodin_vendor_fw_ready(void)
{
	if (atomic_read(&rodin_fw_gate_state) == RODIN_FW_GATE_OPEN)
		return true;

	rodin_fw_gate_start();

	return atomic_read(&rodin_fw_gate_state) == RODIN_FW_GATE_OPEN;
}
EXPORT_SYMBOL(rodin_vendor_fw_ready);

int rodin_vendor_fw_wait(void)
{
	if (atomic_read(&rodin_fw_gate_state) == RODIN_FW_GATE_OPEN)
		return 0;
	if (atomic_read(&rodin_fw_gate_state) == RODIN_FW_GATE_GAVE_UP)
		return -ETIMEDOUT;

	rodin_fw_gate_start();
	wait_for_completion(&rodin_fw_gate_done);

	return atomic_read(&rodin_fw_gate_state) == RODIN_FW_GATE_OPEN ? 0
							       : -ETIMEDOUT;
}
EXPORT_SYMBOL(rodin_vendor_fw_wait);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Wait for the vendor firmware directory before loading files from it");
