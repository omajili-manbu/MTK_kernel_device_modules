// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 MediaTek Inc.
 */

#undef pr_fmt
#define pr_fmt(fmt) "MKP: " fmt

#include <trace/hooks/vendor_hooks.h>
#include <linux/platform_device.h> /* rodin r25: 6.18 header pruning */
#include <trace/hooks/avc.h>
#include <trace/hooks/creds.h>
#include <trace/hooks/selinux.h>
#include <trace/hooks/syscall_check.h>
#include <linux/types.h> // for list_head
#include <linux/module.h> // module_layout
#include <linux/init.h> // rodata_enable support
#include <linux/mutex.h>
#include <linux/kernel.h> // round_up
#include <linux/reboot.h>
#include <linux/workqueue.h>
#include <linux/tracepoint.h>
#include <linux/of.h>
#include <linux/libfdt.h> // fdt32_ld
#include <linux/vmalloc.h>
#include <linux/mm.h>

#include "selinux/mkp_security.h"
#include "selinux/mkp_policycap.h"

#include "mkp_demo.h"

#include "mkp.h"
#include "trace_mkp.h"
#define CREATE_TRACE_POINTS
#include "trace_mtk_mkp.h"

#define mkp_debug 0
DEBUG_SET_LEVEL(DEBUG_LEVEL_ERR);

#define SUPPORT_FULL_KERNEL_CODE_2M
#define DEFAULT_MAX_PID 32768

struct work_struct *avc_work;

static uint32_t g_ro_avc_handle __ro_after_init;
static uint32_t g_ro_cred_handle __ro_after_init;
static struct page *avc_pages __ro_after_init;
static struct page *cred_pages __ro_after_init;
int avc_array_sz __ro_after_init;
int cred_array_sz __ro_after_init;
int rem;
static bool g_initialized;
static struct selinux_avc *g_avc;
static struct selinux_policy __rcu *g_policy;
const struct selinux_state *g_selinux_state;

static DEFINE_PER_CPU(struct avc_sbuf_cache, cpu_avc_sbuf);

#if mkp_debug
static void mkp_trace_event_func(struct timer_list *unused);
static DEFINE_TIMER(mkp_trace_event_timer, mkp_trace_event_func);
#define MKP_TRACE_EVENT_TIME 10
#endif

#include <debug_kinfo.h>
#define DEBUG_COMPATIBLE "mediatek,aee_debug_kinfo"

const char *mkp_trace_print_array(void)
{
	static char str[30] = "mkp test trace point\n";

	return str;
}

#if mkp_debug
static void mkp_trace_event_func(struct timer_list *unused) // do not use sleep
{
	char test[1024];

	memset(test, 0, 1024);
	memcpy(test, "hello world.", 13);
	trace_mkp_trace_event_test(test);
	MKP_DEBUG("timer start\n");
	mod_timer(&mkp_trace_event_timer, jiffies
		+ MKP_TRACE_EVENT_TIME * HZ);

}
#endif

struct rb_root mkp_rbtree = RB_ROOT;
DEFINE_RWLOCK(mkp_rbtree_rwlock);

#if !IS_ENABLED(CONFIG_KASAN_GENERIC) && !IS_ENABLED(CONFIG_KASAN_SW_TAGS)
#if !IS_ENABLED(CONFIG_GCOV_KERNEL)
static void *p_stext;
static void *p_etext;
static void *p__init_begin;
#endif
#endif

int mkp_hook_trace_on;
module_param(mkp_hook_trace_on, int, 0600);

bool mkp_hook_trace_enabled(void)
{
	return !!mkp_hook_trace_on;
}

static void set_memory_rw(unsigned long addr, int nr_pages)
{
	int ret;
	bool valid_addr = false;

	if (THIS_MODULE &&
	    (unsigned long)THIS_MODULE->mem[MOD_INIT_TEXT].base == addr)
		return;
	valid_addr = !!(is_vmalloc_or_module_addr((void *)addr));
	if (valid_addr) {
		ret = mkp_set_mapping_xxx_helper(addr, nr_pages, MKP_POLICY_DRV,
			HELPER_MAPPING_RW);
	} else
		MKP_WARN("addr is not a module or vmalloc address\n");
}

static void set_memory_nx(unsigned long addr, int nr_pages)
{
	int ret;
	bool valid_addr = false;
	int i = 0;
	unsigned long pfn;
	struct mkp_rb_node *found = NULL;
	phys_addr_t phys_addr;
	uint32_t policy;
	unsigned long flags;

	if (THIS_MODULE &&
	    (unsigned long)THIS_MODULE->mem[MOD_INIT_TEXT].base == addr)
		return;

	valid_addr = !!(is_vmalloc_or_module_addr((void *)addr));
	if (valid_addr) {
		ret = mkp_set_mapping_xxx_helper(addr, nr_pages, MKP_POLICY_DRV,
			HELPER_MAPPING_NX);
		policy = MKP_POLICY_DRV;
	} else {
		MKP_WARN("addr is not a module or vmalloc address\n");
		return;
	}

	for (i = 0; i < nr_pages; i++) {
		pfn = vmalloc_to_pfn((void *)(addr+i*PAGE_SIZE));
		phys_addr = pfn << PAGE_SHIFT;
		write_lock_irqsave(&mkp_rbtree_rwlock, flags);
		found = mkp_rbtree_search(&mkp_rbtree, phys_addr);
		if (found != NULL && found->addr != 0 && found->size != 0) {
			ret = mkp_destroy_handle(policy, found->handle);
			ret = mkp_rbtree_erase(&mkp_rbtree, phys_addr);
		}
		write_unlock_irqrestore(&mkp_rbtree_rwlock, flags);
	}
}

static void probe_android_rvh_set_module_core_rw_nx(void *ignore,
		const struct module *mod)
{
	for_class_mod_mem_type(type, core) {
		const struct module_memory *mod_mem = &mod->mem[type];

		if (mod_mem->size) {

			/* RO_AFTER_INIT will not be set as RO, so no need to recover to RW */
			if (type != MOD_RO_AFTER_INIT)
				set_memory_rw((unsigned long)mod_mem->base, (mod_mem->size) >> PAGE_SHIFT);

			set_memory_nx((unsigned long)mod_mem->base, (mod_mem->size) >> PAGE_SHIFT);
		}
	}
}

#ifdef SUPPORT_FULL_KERNEL_CODE_2M
bool full_kernel_code_2m;
#endif

/* rodin b57: krn 面 rodata（含 grant-ticket 槽页 subscribe）保护落地标志。
 * 必须是普通 .bss 变量——__ro_after_init 落在保护范围 [_etext,__init_begin]
 * 内，保护生效后再写会触发 GZ fault 路径。=y 内建态以它为 ESS_1 的门。 */
static bool mkp_krn_protect_done;

#if !IS_ENABLED(CONFIG_KASAN_GENERIC) && !IS_ENABLED(CONFIG_KASAN_SW_TAGS)
#if !IS_ENABLED(CONFIG_GCOV_KERNEL)
static void mkp_protect_kernel_work_fn(struct work_struct *work);

static DECLARE_DELAYED_WORK(mkp_pk_work, mkp_protect_kernel_work_fn);
static int retry_num = 100;
static void mkp_protect_kernel_work_fn(struct work_struct *work)
{
	int ret = 0;
	uint32_t policy = 0;
	uint32_t handle = 0;
	unsigned long addr_start;
	unsigned long addr_end;
	phys_addr_t phys_addr;
	int nr_pages;
	int init = 0;

#ifdef SUPPORT_FULL_KERNEL_CODE_2M
	/* Map all kernel code in the EL1S2 with the granularity of 2M */
	bool kernel_code_perf = false;
	unsigned long addr_start_2m = 0, addr_end_2m = 0;
#endif

	/* rodin b57: =y 内建态必须等到 system_state == SYSTEM_RUNNING（内核
	 * mark_readonly() 之后）才落地 rodata 保护：__ro_after_init 全内核在
	 * mark_readonly 前持续被写（OEM =m 的保护发生在用户态装载期，远晚于
	 * mark_readonly，从无此窗口），保护早于它会把 OEM 不存在的
	 * "保护后写 ro_after_init" 变成 GZ fault/注入（#105 实证）。=m 时
	 * 本门被 THIS_MODULE 恒假折叠，OEM 时序不变。 */
	if (!THIS_MODULE && system_state != SYSTEM_RUNNING) {
		if (--retry_num >= 0)
			schedule_delayed_work(&mkp_pk_work, HZ);
		else
			MKP_ERR("protect krn: give up waiting SYSTEM_RUNNING\n");
		return;
	}

	if (policy_ctrl[MKP_POLICY_KERNEL_CODE] &&
		policy_ctrl[MKP_POLICY_KERNEL_RODATA]) {
		mkp_get_krn_info(&p_stext, &p_etext, &p__init_begin);
		if (p_stext == NULL || p_etext == NULL || p__init_begin == NULL) {
			pr_info("%s: retry in 0.1 second", __func__);
			if (--retry_num >= 0)
				schedule_delayed_work(&mkp_pk_work, HZ / 10);
			else
				MKP_ERR("protect krn failed\n");
			return;
		}
		init = 1;
	}
	if (policy_ctrl[MKP_POLICY_KERNEL_CODE] != 0) {
		if (!init)
			mkp_get_krn_code(&p_stext, &p_etext);
		if (p_stext == NULL || p_etext == NULL) {
			pr_info("%s: retry in 0.1 second", __func__);
			if (--retry_num >= 0)
				schedule_delayed_work(&mkp_pk_work, HZ / 10);
			else
				MKP_ERR("protect krn failed\n");
			return;
		}
	}
	if (policy_ctrl[MKP_POLICY_KERNEL_RODATA] != 0) {
		if (!init)
			mkp_get_krn_rodata(&p_etext, &p__init_begin);
		if (p_etext == NULL || p__init_begin == NULL) {
			pr_info("%s: retry in 0.1 second", __func__);
			if (--retry_num >= 0)
				schedule_delayed_work(&mkp_pk_work, HZ / 10);
			else
				MKP_ERR("protect krn failed\n");
			return;
		}
	}

	if (policy_ctrl[MKP_POLICY_KERNEL_CODE] &&
		policy_ctrl[MKP_POLICY_KERNEL_RODATA]) {

#ifdef SUPPORT_FULL_KERNEL_CODE_2M
		/* It may ONLY take effects when BOTH KERNEL_CODE & KERNEL_RODATA are enabled */
		if (full_kernel_code_2m)
			kernel_code_perf = true;
#endif
	}

	if (policy_ctrl[MKP_POLICY_KERNEL_CODE] != 0) {
		// round down addr before minus operation
		addr_start = (unsigned long)p_stext;
		addr_end = (unsigned long)p_etext;
		addr_start = round_up(addr_start, PAGE_SIZE);
		addr_end = round_down(addr_end, PAGE_SIZE);

#ifdef SUPPORT_FULL_KERNEL_CODE_2M
		/* Try to round_down/up the boundary in 2M */
		if (kernel_code_perf) {
			addr_start_2m = round_down(addr_start, SZ_2M);
			/* The range size of _text and _stext should SEGMENT_ALIGN */
			if ((addr_start - addr_start_2m) == SEGMENT_ALIGN) {
				addr_start = addr_start_2m;
				addr_end_2m = round_up(addr_end, SZ_2M);
				addr_end = addr_end_2m;
			}
		}
#endif
		if (addr_start == 0) {
			MKP_ERR("Cannot find the kernel text\n");
			goto protect_krn_fail;
		}

		nr_pages = (addr_end-addr_start)>>PAGE_SHIFT;
		phys_addr = __pa_symbol((void *)addr_start);
		policy = MKP_POLICY_KERNEL_CODE;
		handle = mkp_create_handle(policy, (unsigned long)phys_addr, nr_pages<<12);
		if (handle == 0) {
			MKP_ERR("%s:%d: Create handle fail\n", __func__, __LINE__);
		} else {
			ret = mkp_set_mapping_x(policy, handle);
			ret = mkp_set_mapping_ro(policy, handle);
			pr_info("mkp: protect krn code done\n");
		}
	}

	if (policy_ctrl[MKP_POLICY_KERNEL_RODATA] != 0) {
		// round down addr before minus operation
		addr_start = (unsigned long)p_etext;
		addr_end = (unsigned long)p__init_begin;
		addr_start = round_up(addr_start, PAGE_SIZE);
		addr_end = round_down(addr_end, PAGE_SIZE);


#ifdef SUPPORT_FULL_KERNEL_CODE_2M
		/* Try to round_down/up the boundary in 2M */
		if (kernel_code_perf && (addr_end_2m != 0) && (addr_end_2m <= addr_end))
			addr_start = addr_end_2m;
#endif
		if (addr_start == 0) {
			MKP_ERR("Cannot find the kernel rodata\n");
			goto protect_krn_fail;
		}

		nr_pages = (addr_end-addr_start)>>PAGE_SHIFT;
		phys_addr = __pa_symbol((void *)addr_start);
		policy = MKP_POLICY_KERNEL_RODATA;
		handle = mkp_create_handle(policy, (unsigned long)phys_addr, nr_pages<<12);
		if (handle == 0)
			MKP_ERR("%s:%d: Create handle fail\n", __func__, __LINE__);
		else {
			ret = mkp_set_mapping_ro(policy, handle);
			pr_info("mkp: protect krn rodata done\n");
			/* rodin b57: rodata 保护落地（覆盖 grant-ticket 槽页）——
			 * 服务端 post-grant mapping op 的 ticket 只能由"对槽页的
			 * 写入 fault"产生（GZ mkp_service produce_ticket 反汇编
			 * 实证），故此处置位、并作为 =y ESS_1 的门。 */
			mkp_krn_protect_done = true;
		}
	}

	/* rodin b57: =y 的 start granting 在此执行（krn 面 rodata 保护落地
	 * 之后）：槽 magic 经受保护写 fault→GZ produce 跳过后仍为 magic，
	 * ESS_1 换牌（ticket_key）后 post-grant ticket 纪律生效；此后内核
	 * 自身不再写 ro_after_init（mark_readonly 已过），无违规写窗口。
	 * =m 时本调用被 THIS_MODULE 恒假折叠（OEM 的 grant 在
	 * protect_mkp_self 内）。 */
	if (!THIS_MODULE && mkp_krn_protect_done)
		mkp_start_granting_hvc_call();

protect_krn_fail:
	p_stext = NULL;
	p_etext = NULL;
	p__init_begin = NULL;
}
#endif
#endif

static void probe_android_rvh_set_module_permit_before_init(void *ignore,
	const struct module *mod)
{
	if (mod == THIS_MODULE && policy_ctrl[MKP_POLICY_MKP] != 0) {
		module_enable_ro(mod, false, MKP_POLICY_MKP);
		module_enable_nx(mod, MKP_POLICY_MKP);
		module_enable_x(mod, MKP_POLICY_MKP);
		return;
	}
	if (mod != THIS_MODULE && policy_ctrl[MKP_POLICY_DRV] != 0) {
		/* rodin #159: 6.6 blob（mod->rodin_66，装载器 force-load 时置位）
		 * 停用 DRV 面 S2 保护 op。真机 #159：grant（ESS_1, 5.568s）后
		 * 首个模块保护 op（scene_swappiness @6.370s 的 module_load 钩子，
		 * grant 后唯一 mapping op 发起面）在 GZ mkp_service 内不返回：
		 * 服务端反汇编（b57 tee/mkp_service）mapping op（HVC_FUNC
		 * 0x30-0x34）post-grant 走 consume_ticket(MPIDR aff1)，票空即
		 * do_action_panic(189) 写 "mkppanic" 自陷，sync handler 判
		 * unhandled => GZ 静默死机；HVC 永不返回，CPU2 持
		 * mkp_hvc_svc_lock 困于 HVC，CPU0/4 在 task_newtask/cred 钩子
		 * 路径自旋等锁（raw spinlock IRQ 常闭），三 CPU 定时器死绝 =>
		 * RCU stall（28.0s CPU2/4）+ devfreq quiesce 挂死（10.9s 起）
		 * => 全机楔死。票据由"写票据槽页 fault"产（produce_ticket 校验
		 * 槽值 == per-boot ticket_key，槽页在 krn rodata 保护范围内
		 * S2=RO）；服务端 mapping op 自带 2M 块重整
		 * （map_for_2mb_mapping_compaction / reset_to_s2_mapping_attrs），
		 * 模块页 PFN-group 的 op 触及含槽页的 2M 块时槽页 S2 RO 被重置，
		 * 后续 poke 落地不再 fault => 无票 => 首 op 即 panic。同一内核
		 * 代码、同一 blob 在 #151 时代（10-02）可通过 = 模块页 PA 布局
		 * 随内核构建漂移，属结构性彩票而非单点回归；服务端闭源不可修。
		 * =y 面 =m 全集即 blob（源码一律 =y，内建总计划），blob 文本由
		 * 6.18 装载器 strict_module_rwx 自护，mkp S2 层对其属冗余加固
		 * 却承载全部死机风险，故停用；源码 =m 模块（rodin_66=false）
		 * 不受影响。krn code/rodata/TASK_CRED/AVC 面保留（#159 boot
		 * 5.57-6.37s cred 更新全程正常实证）。恢复：服务端票据路径
		 * 真机稳定实证后按 b54-b57 方法回归再启用。 */
		if (mod->rodin_66)
			return;
		if (drv_skip((char *)mod->name))
			return;
		module_enable_ro(mod, false, MKP_POLICY_DRV);
		module_enable_nx(mod, MKP_POLICY_DRV);
		module_enable_x(mod, MKP_POLICY_DRV);
	}
}

static void probe_android_rvh_commit_creds(void *ignore, const struct task_struct *task,
	const struct cred *new)
{
	int ret = -1;
	struct cred_sbuf_content c;

	if (g_ro_cred_handle == 0)
		return;

	if (task->pid >= DEFAULT_MAX_PID) {
		MKP_ERR("pid is overflow\n");
		handle_mkp_err_action(MKP_POLICY_TASK_CRED);
		return;
	}

	MKP_HOOK_BEGIN(__func__);

	c.csc.uid.val = new->uid.val;
	c.csc.gid.val = new->gid.val;
	c.csc.euid.val = new->euid.val;
	c.csc.egid.val = new->egid.val;
	c.csc.fsuid.val = new->fsuid.val;
	c.csc.fsgid.val = new->fsgid.val;
	c.csc.security = new->security;
	ret = mkp_update_sharebuf_4_argu(MKP_POLICY_TASK_CRED, g_ro_cred_handle,
		(unsigned long)task->pid,
		c.args[0], c.args[1], c.args[2], c.args[3]);

	MKP_HOOK_END(__func__);
}

static void probe_android_rvh_exit_creds(void *ignore, const struct task_struct *task,
	const struct cred *cred)
{
	int ret = -1;

	if (g_ro_cred_handle == 0)
		return;

	if (task->pid >= DEFAULT_MAX_PID) {
		MKP_ERR("pid is overflow\n");
		handle_mkp_err_action(MKP_POLICY_TASK_CRED);
		return;
	}

	MKP_HOOK_BEGIN(__func__);

	ret = mkp_update_sharebuf_4_argu(MKP_POLICY_TASK_CRED, g_ro_cred_handle,
		(unsigned long)task->pid, 0, 0, 0, 0);

	MKP_HOOK_END(__func__);
}

static void probe_android_rvh_override_creds(void *ignore, const struct task_struct *task,
	const struct cred *new)
{
	int ret = -1;
	struct cred_sbuf_content c;

	if (g_ro_cred_handle == 0)
		return;

	if (task->pid >= DEFAULT_MAX_PID) {
		MKP_ERR("pid is overflow\n");
		handle_mkp_err_action(MKP_POLICY_TASK_CRED);
		return;
	}

	MKP_HOOK_BEGIN(__func__);

	c.csc.uid.val = new->uid.val;
	c.csc.gid.val = new->gid.val;
	c.csc.euid.val = new->euid.val;
	c.csc.egid.val = new->egid.val;
	c.csc.fsuid.val = new->fsuid.val;
	c.csc.fsgid.val = new->fsgid.val;
	c.csc.security = new->security;
	ret = mkp_update_sharebuf_4_argu(MKP_POLICY_TASK_CRED, g_ro_cred_handle,
		(unsigned long)task->pid,
		c.args[0], c.args[1], c.args[2], c.args[3]);

	MKP_HOOK_END(__func__);
}

static void probe_android_rvh_revert_creds(void *ignore, const struct task_struct *task,
	const struct cred *old)
{
	int ret = -1;
	struct cred_sbuf_content c;

	if (g_ro_cred_handle == 0)
		return;

	if (task->pid >= DEFAULT_MAX_PID) {
		MKP_ERR("pid is overflow\n");
		handle_mkp_err_action(MKP_POLICY_TASK_CRED);
		return;
	}

	MKP_HOOK_BEGIN(__func__);

	c.csc.uid.val = old->uid.val;
	c.csc.gid.val = old->gid.val;
	c.csc.euid.val = old->euid.val;
	c.csc.egid.val = old->egid.val;
	c.csc.fsuid.val = old->fsuid.val;
	c.csc.fsgid.val = old->fsgid.val;
	c.csc.security = old->security;
	ret = mkp_update_sharebuf_4_argu(MKP_POLICY_TASK_CRED, g_ro_cred_handle,
		(unsigned long)task->pid,
		c.args[0], c.args[1], c.args[2], c.args[3]);

	MKP_HOOK_END(__func__);
}

static void __update_cpu_avc_sbuf(unsigned long key, int index)
{
	struct avc_sbuf_cache *sb;

	sb = this_cpu_ptr(&cpu_avc_sbuf);
	sb->cached[sb->pos] = key;
	sb->cached_index[sb->pos] = index;
	sb->pos = (sb->pos + 1) % MAX_CACHED_NUM;
}

static void update_cpu_avc_sbuf(unsigned long key, int index)
{
	unsigned long flags;

	local_irq_save(flags);

	__update_cpu_avc_sbuf(key, index);

	local_irq_restore(flags);
}

static int fast_avc_lookup(unsigned long key)
{
	unsigned long flags;
	int pos;
	struct avc_sbuf_cache *sb;
	int index = -1;

	local_irq_save(flags);

	sb = this_cpu_ptr(&cpu_avc_sbuf);

	pos = sb->pos;
	/* Try the 1st hit */
	pos = (pos + CACHED_NUM_MASK) & CACHED_NUM_MASK;
	if (sb->cached[pos] == key) {
		index = sb->cached_index[pos];
		goto exit;
	}

	/* Try more */
	for (pos = 0; pos < MAX_CACHED_NUM; pos++) {
		if (sb->cached[pos] == key) {
			index = sb->cached_index[pos];
			goto exit;
		}
	}

exit:
	local_irq_restore(flags);

	return index;
}

static void probe_android_rvh_selinux_avc_insert(void *ignore, const struct avc_node *node)
{
	struct mkp_avc_node *temp_node = NULL;
	int ret = -1;

	if (g_ro_avc_handle == 0)
		return;

	MKP_HOOK_BEGIN(__func__);

	temp_node = (struct mkp_avc_node *)node;
	ret = mkp_update_sharebuf_4_argu(MKP_POLICY_SELINUX_AVC, g_ro_avc_handle,
		(unsigned long)temp_node, temp_node->ae.ssid,
		temp_node->ae.tsid, temp_node->ae.tclass, temp_node->ae.avd.allowed);

	__update_cpu_avc_sbuf((unsigned long)temp_node, ret);

	MKP_HOOK_END(__func__);
}

static void probe_android_rvh_selinux_avc_node_delete(void *ignore,
	const struct avc_node *node)
{
	int ret = -1;

	if (g_ro_avc_handle == 0)
		return;

	MKP_HOOK_BEGIN(__func__);

	ret = mkp_update_sharebuf_4_argu(MKP_POLICY_SELINUX_AVC, g_ro_avc_handle,
		(unsigned long)node, 0, 0, 0, 0);

	MKP_HOOK_END(__func__);
}

static void probe_android_rvh_selinux_avc_node_replace(void *ignore,
	const struct avc_node *old, const struct avc_node *new)
{
	struct mkp_avc_node *new_node = (struct mkp_avc_node *)new;
	int ret = -1;

	if (g_ro_avc_handle == 0)
		return;

	MKP_HOOK_BEGIN(__func__);

	ret = mkp_update_sharebuf_4_argu(MKP_POLICY_SELINUX_AVC, g_ro_avc_handle,
		(unsigned long)old, 0, 0, 0, 0);

	ret = mkp_update_sharebuf_4_argu(MKP_POLICY_SELINUX_AVC, g_ro_avc_handle,
		(unsigned long)new_node, new_node->ae.ssid,
		new_node->ae.tsid, new_node->ae.tclass, new_node->ae.avd.allowed);
	__update_cpu_avc_sbuf((unsigned long)new_node, ret);

	MKP_HOOK_END(__func__);
}

static void probe_android_rvh_selinux_avc_lookup(void *ignore,
	const struct avc_node *node, u32 ssid, u32 tsid, u16 tclass)
{
	void *va;
	struct avc_sbuf_content *ro_avc_sharebuf_ptr;
	int index;
	int i = -1;
	struct mkp_avc_node *temp_node = NULL;
	bool ready = false;
	static DEFINE_RATELIMIT_STATE(rs_avc, 1*HZ, 10);
#if IS_ENABLED(CONFIG_KASAN)
	bool cached = false;
#endif

	if (!node || g_ro_avc_handle == 0)
		return;

	ratelimit_set_flags(&rs_avc, RATELIMIT_MSG_ON_RELEASE);
	if (__ratelimit(&rs_avc)) {

		MKP_HOOK_BEGIN(__func__);

		temp_node = (struct mkp_avc_node *)node;
		va = page_address(avc_pages);
		ro_avc_sharebuf_ptr = (struct avc_sbuf_content *)va;

		index = fast_avc_lookup((unsigned long)temp_node);
		if (index != -1) {
			ro_avc_sharebuf_ptr += index;
			if ((unsigned long)ro_avc_sharebuf_ptr->avc_node ==
				(unsigned long)temp_node)
				ready = true;
#if IS_ENABLED(CONFIG_KASAN)
			cached = true;
#endif
		}

		if (!ready) {
			ro_avc_sharebuf_ptr = (struct avc_sbuf_content *)va;
			for (i = 0; i < avc_array_sz; ro_avc_sharebuf_ptr++, i++) {
				if ((unsigned long)ro_avc_sharebuf_ptr->avc_node ==
					(unsigned long)temp_node) {
					ready = true;
					update_cpu_avc_sbuf((unsigned long)temp_node, i);
					break;
				}
			}
		}
		if (ready) {
			if (ro_avc_sharebuf_ptr->ssid != ssid ||
				ro_avc_sharebuf_ptr->tsid != tsid ||
				ro_avc_sharebuf_ptr->tclass != tclass ||
				ro_avc_sharebuf_ptr->ae_allowed !=
					temp_node->ae.avd.allowed) {
				MKP_ERR("avc lookup is not matched\n");
#if IS_ENABLED(CONFIG_MTK_VM_DEBUG)
				MKP_ERR("CURRENT-%16lx:%16lx:%16lx:%16lx\n",
				       (unsigned long)ssid,
				       (unsigned long)tsid,
				       (unsigned long)tclass,
				       (unsigned long)temp_node->ae.avd.allowed);
				MKP_ERR("@EXPECT-%16lx:%16lx:%16lx:%16lx\n",
				       (unsigned long)ro_avc_sharebuf_ptr->ssid,
				       (unsigned long)ro_avc_sharebuf_ptr->tsid,
				       (unsigned long)ro_avc_sharebuf_ptr->tclass,
				       (unsigned long)ro_avc_sharebuf_ptr->ae_allowed);
#endif

#if IS_ENABLED(CONFIG_KASAN)
				if (!cached)
					goto report;

				MKP_ERR("Index from fast_avc_lookup: %d\n", index);

				/* Try full iteration to find out all possible aliases */
				ro_avc_sharebuf_ptr = (struct avc_sbuf_content *)va;
				for (i = 0; i < avc_array_sz; ro_avc_sharebuf_ptr++, i++) {
					if ((unsigned long)ro_avc_sharebuf_ptr->avc_node ==
							(unsigned long)temp_node) {
						MKP_ERR("Alias found: %d\n", i);
					}
				}
report:
#endif
				handle_mkp_err_action(MKP_POLICY_SELINUX_AVC);
			}
		}
		MKP_HOOK_END(__func__);
		return; // pass
	}
}

static void avc_work_handler(struct work_struct *work)
{
	int ret = 0, ret_erri_line;

	// register avc vendor hook after selinux is initialized
	if (policy_ctrl[MKP_POLICY_SELINUX_AVC] != 0 ||
		g_ro_avc_handle != 0) {
		// register avc vendor hook
		ret = register_trace_android_rvh_selinux_avc_insert(
				probe_android_rvh_selinux_avc_insert, NULL);
		if (ret) {
			ret_erri_line = __LINE__;
			goto avc_failed;
		}
		ret = register_trace_android_rvh_selinux_avc_node_delete(
				probe_android_rvh_selinux_avc_node_delete, NULL);
		if (ret) {
			ret_erri_line = __LINE__;
			goto avc_failed;
		}
		ret = register_trace_android_rvh_selinux_avc_node_replace(
				probe_android_rvh_selinux_avc_node_replace, NULL);
		if (ret) {
			ret_erri_line = __LINE__;
			goto avc_failed;
		}
		ret = register_trace_android_rvh_selinux_avc_lookup(
				probe_android_rvh_selinux_avc_lookup, NULL);
		if (ret) {
			ret_erri_line = __LINE__;
			goto avc_failed;
		}
	}
avc_failed:
	if (ret)
		MKP_ERR("register avc hooks failed, ret %d line %d\n", ret, ret_erri_line);
}
static void probe_android_rvh_selinux_is_initialized(void *ignore,
	const struct selinux_state *state)
{
	g_initialized = state->initialized;
	g_avc = state->avc;
	g_policy = state->policy;
	g_selinux_state = state;

	if (policy_ctrl[MKP_POLICY_SELINUX_AVC]) {
		if (!avc_work) {
			MKP_ERR("avc work create fail\n");
			return;
		}
		INIT_WORK(avc_work, avc_work_handler);
		schedule_work(avc_work);
	}
}

static void check_selinux_state(struct ratelimit_state *rs)
{
	if (!policy_ctrl[MKP_POLICY_SELINUX_STATE])
		return;

	ratelimit_set_flags(rs, RATELIMIT_MSG_ON_RELEASE);
	if (!__ratelimit(rs))
		return;
	if (g_selinux_state &&
		(g_selinux_state->initialized != g_initialized ||
		g_selinux_state->avc != g_avc ||
		g_selinux_state->policy != g_policy)) {
		MKP_ERR("%s:%d: selinux_state is not matched\n", __func__, __LINE__);
#if IS_ENABLED(CONFIG_MTK_VM_DEBUG)
		MKP_ERR("CURRENT-%16lx:%16lx:%16lx\n",
				(unsigned long)g_selinux_state->initialized,
				(unsigned long)g_selinux_state->avc,
				(unsigned long)g_selinux_state->policy);
		MKP_ERR("@EXPECT-%16lx:%16lx:%16lx\n",
				(unsigned long)g_initialized,
				(unsigned long)g_avc,
				(unsigned long)g_policy);
#endif
		handle_mkp_err_action(MKP_POLICY_SELINUX_STATE);
	}
}

static bool cred_is_not_matched(const struct cred *curr, pid_t index)
{
	struct cred_sbuf_content *ro_cred_sharebuf_ptr = NULL;
	struct cred_sbuf_content *target = NULL;

	ro_cred_sharebuf_ptr = (struct cred_sbuf_content *)page_address(cred_pages);

	/* pid max */
	if (index >= DEFAULT_MAX_PID) {
		MKP_ERR("pid is overflow\n");
		handle_mkp_err_action(MKP_POLICY_TASK_CRED);
		return false;
	}

	/* Target for comparison */
	target = ro_cred_sharebuf_ptr + index;

	/* No valid cred or cleared */
	if (target->csc.security == NULL) {
		MKP_WARN("%s:%d: target security point to NULL\n", __func__, __LINE__);
		return false;
	}

	/* Do comparison */
	if (target->csc.uid.val != curr->uid.val ||
		target->csc.gid.val != curr->gid.val ||
		target->csc.euid.val != curr->euid.val ||
		target->csc.egid.val != curr->egid.val ||
		target->csc.fsuid.val != curr->fsuid.val ||
		target->csc.fsgid.val != curr->fsgid.val ||
		target->csc.security != curr->security) {

#if IS_ENABLED(CONFIG_MTK_VM_DEBUG)
		MKP_ERR("CURRENT-(%u)-%16lx:%16lx:%16lx:%16lx:%16lx:%16lx:%16lx\n",
				current->pid,
				(unsigned long)curr->uid.val,
				(unsigned long)curr->gid.val,
				(unsigned long)curr->euid.val,
				(unsigned long)curr->egid.val,
				(unsigned long)curr->fsuid.val,
				(unsigned long)curr->fsgid.val,
				(unsigned long)curr->security);
		MKP_ERR("@EXPECT-(%u)-%16lx:%16lx:%16lx:%16lx:%16lx:%16lx:%16lx\n",
				index,
				(unsigned long)target->csc.uid.val,
				(unsigned long)target->csc.gid.val,
				(unsigned long)target->csc.euid.val,
				(unsigned long)target->csc.egid.val,
				(unsigned long)target->csc.fsuid.val,
				(unsigned long)target->csc.fsgid.val,
				(unsigned long)target->csc.security);
#endif

		return true;
	}

	return false;
}

static void check_cred(struct ratelimit_state *rs)
{
	struct task_struct *cur = NULL;

	if (!policy_ctrl[MKP_POLICY_TASK_CRED])
		return;

	ratelimit_set_flags(rs, RATELIMIT_MSG_ON_RELEASE);
	if (!__ratelimit(rs) || (g_ro_cred_handle == 0))
		return;

	cur = get_current();

	/* Start matching */
	if (cred_is_not_matched(cur->cred, cur->pid)) {
		MKP_ERR("%s:%d: cred is not matched\n", __func__, __LINE__);
		handle_mkp_err_action(MKP_POLICY_TASK_CRED);
	}
}

static void probe_android_vh_check_mmap_file(void *ignore,
	const struct file *file, unsigned long prot, unsigned long flag, unsigned long ret)
{
	static DEFINE_RATELIMIT_STATE(rs_mmap, 1*HZ, 10);

	MKP_HOOK_BEGIN(__func__);

	check_cred(&rs_mmap);
	check_selinux_state(&rs_mmap);

	MKP_HOOK_END(__func__);
}

static void probe_android_vh_check_file_open(void *ignore, const struct file *file)
{
	static DEFINE_RATELIMIT_STATE(rs_open, 1*HZ, 10);

	MKP_HOOK_BEGIN(__func__);

	check_cred(&rs_open);
	check_selinux_state(&rs_open);

	MKP_HOOK_END(__func__);
}

static void probe_android_vh_check_bpf_syscall(void *ignore,
	int cmd, const union bpf_attr *attr, unsigned int size)
{
	static DEFINE_RATELIMIT_STATE(rs_bpf, 1*HZ, 10);

	MKP_HOOK_BEGIN(__func__);

	check_cred(&rs_bpf);
	check_selinux_state(&rs_bpf);

	MKP_HOOK_END(__func__);
}

static int __init protect_mkp_self(void)
{
	/* rodin b53: 内建态 THIS_MODULE 恒为 NULL（6.6 =m 时才指向自身
	 * module 结构）；内建代码的 text/rodata 属内核镜像，已由
	 * mkp_protect_kernel_work_fn（protect krn code/rodata）统一覆盖，
	 * 故仅 =m 形态保留模块面保护。 */
	if (THIS_MODULE) {
		module_enable_ro(THIS_MODULE, false, MKP_POLICY_MKP);
		module_enable_nx(THIS_MODULE, MKP_POLICY_MKP);
		module_enable_x(THIS_MODULE, MKP_POLICY_MKP);
	}
	/* rodin b55: ESS_1（start granting）恢复全形态执行——它是 grant 协议
	 * 的使能步，OEM 时序恒在 sharebuf 创建之前；b54 随三连调 =m-only 属
	 * 过度矫正：#105 实证 =y 缺失时首个 AVC sharebuf 的 cookie 写被
	 * secure 侧判违规并按其设计注入 dabt 打死内核（FAR=0xfedcba9876543210、
	 * FSC=0x3f；pkvm_mkp/hyp/mkp_handler.c 同款"inject a dabt to EL1"）。
	 * =y 无自面 handle 不影响本事件；b54 mkp_hvc_svc_lock 已保证单
	 * secure-op 串行，#104 的并发窗已闭。
	 * rodin b57: 服务端（GZ mkp_service 反汇编）ESS_1 = 校验槽 magic 并
	 * 以 per-boot ticket_key 换牌、置 start_granting；其门是"槽=magic"
	 * 与"rodata 保护已落地"（post-grant ticket 纪律），不是 b56 猜的
	 * owner 态。=m 维持 OEM 无条件时序；=y 以 mkp_krn_protect_done 为门
	 * （#106：ESS_1 先于 rodata 保护 => post-grant 首 mapping op 无
	 * ticket => do_action_panic(189) => GZ 静默死机）。 */
	if (THIS_MODULE) {
		/* rodin b57: =m 保持 OEM 时序：三连调后立即 start granting
		 * （OEM 的 krn 面 work 由模块装载时序保证远晚于内核
		 * mark_readonly）。 */
		mkp_start_granting_hvc_call();
		return 0;
	}
	/* rodin b57: =y 的 ESS_1 推迟到 mkp_pk_work 内（SYSTEM_RUNNING 且
	 * krn 面 rodata 保护落地后）执行——见 mkp_protect_kernel_work_fn。
	 * 此处不 grant：initcall 期 rodata 保护未落地，grant 后的
	 * post-grant ticket 纪律无 ticket 可产（#106 机理）；sharebuf 等
	 * 全部 op 停在 pre-grant 旁路态直到 work 完成 grant。 */
	return 0;
}

int mkp_reboot_notifier_event(struct notifier_block *nb, unsigned long event, void *v)
{
	MKP_DEBUG("mkp reboot notifier\n");
	return NOTIFY_DONE;
}
static struct notifier_block mkp_reboot_notifier = {
	.notifier_call = mkp_reboot_notifier_event,
};

/* For probing interesting tracepoints */
struct tracepoints_table {
	const char *name;
	void *func;
	struct tracepoint *tp;
	int policy;
};

static void mkp_task_newtask(void *ignore, struct task_struct *task, u64 clone_flags)
{
	int ret = -1;
	struct cred_sbuf_content c;

	if (g_ro_cred_handle == 0)
		return;

	MKP_HOOK_BEGIN(__func__);

	c.csc.uid.val = task->cred->uid.val;
	c.csc.gid.val = task->cred->gid.val;
	c.csc.euid.val = task->cred->euid.val;
	c.csc.egid.val = task->cred->egid.val;
	c.csc.fsuid.val = task->cred->fsuid.val;
	c.csc.fsgid.val = task->cred->fsgid.val;
	c.csc.security = task->cred->security;
	ret = mkp_update_sharebuf_4_argu(MKP_POLICY_TASK_CRED, g_ro_cred_handle,
			(unsigned long)task->pid,
			c.args[0], c.args[1], c.args[2], c.args[3]);

	MKP_HOOK_END(__func__);
}

static void mkp_module_load(void *ignore, struct module *mod)
{
	probe_android_rvh_set_module_permit_before_init(NULL, mod);
}

static void mkp_module_free(void *ignore, struct module *mod)
{
	if (policy_ctrl[MKP_POLICY_DRV] != 0 || policy_ctrl[MKP_POLICY_KERNEL_PAGES] != 0 ||
		policy_ctrl[MKP_POLICY_MKP] != 0) {
		probe_android_rvh_set_module_core_rw_nx(NULL, mod);
	}
}

static struct tracepoints_table mkp_tracepoints[] = {
{.name = "task_newtask", .func = mkp_task_newtask, .tp = NULL, .policy = MKP_POLICY_TASK_CRED},
{.name = "module_load", .func = mkp_module_load, .tp = NULL, .policy = MKP_POLICY_DRV},
{.name = "module_free", .func = mkp_module_free, .tp = NULL, .policy = MKP_POLICY_DRV},
};

#define FOR_EACH_INTEREST(i) \
	for (i = 0; i < sizeof(mkp_tracepoints) / sizeof(struct tracepoints_table); i++)

static void lookup_tracepoints(struct tracepoint *tp, void *ignore)
{
	int i;

	FOR_EACH_INTEREST(i) {
		if (strcmp(mkp_tracepoints[i].name, tp->name) == 0)
			mkp_tracepoints[i].tp = tp;
	}
}

/*
 * Find out interesting tracepoints and try to register them.
 * Update policy_ctrl if needed.
 */
static void __init mkp_hookup_tracepoints(void)
{
	int i;
	int ret;
	enum mkp_policy_id policy;

	/* Find out interesting tracepoints */
	for_each_kernel_tracepoint(lookup_tracepoints, NULL);

	/* Update policy control if needed */
	FOR_EACH_INTEREST(i) {
		policy = mkp_tracepoints[i].policy;
		if (policy_ctrl[policy] != 0 && mkp_tracepoints[i].tp == NULL) {
			MKP_ERR("%s not found for policy %d\n",
				mkp_tracepoints[i].name, policy);
			policy_ctrl[policy] = 0;
		}
	}

	/* Probing found tracepoints */
	FOR_EACH_INTEREST(i) {
		policy = mkp_tracepoints[i].policy;
		if (policy_ctrl[policy] != 0 && mkp_tracepoints[i].tp != NULL) {
			ret = tracepoint_probe_register(mkp_tracepoints[i].tp,
							mkp_tracepoints[i].func,  NULL);
			if (ret) {
				MKP_ERR("Failed to register %s for policy %d\n",
					mkp_tracepoints[i].name, policy);
				policy_ctrl[policy] = 0;
			}
		}
	}
}

/* Map full kernel text in the granularity of 2MB */
static const struct of_device_id mkp_of_match[] = {
	{ .compatible = "mediatek,mkp-drv", },
	{ }
};
MODULE_DEVICE_TABLE(of, mkp_of_match);

#ifndef SUPPORT_FULL_KERNEL_CODE_2M
static void free_reserved_memory(phys_addr_t start_phys, phys_addr_t end_phys)
{
	phys_addr_t pos;
	unsigned long nr_pages = 0;

	if (end_phys <= start_phys) {
		pr_info("%s: end_phys is smaller than start_phys start_phys:0x%pa end_phys:0x%pa\n",
				__func__, &start_phys, &end_phys);
		return;
	}
	for (pos = start_phys; pos < end_phys; pos += PAGE_SIZE, nr_pages++)
		free_reserved_page(phys_to_page(pos));

	if (nr_pages) {
		pr_info("freeing mkp %ldK reserved memory\n",
				nr_pages << (PAGE_SHIFT - 10));
	}
}
#endif

static int get_reserved_memory(struct device *dev)
{
	struct device_node *np;
	struct reserved_mem *rmem;

	np = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!np) {
		dev_info(dev, "no memory-region\n");
		return -EINVAL;
	}

	rmem = of_reserved_mem_lookup(np);
	of_node_put(np);

	if (!rmem) {
		dev_info(dev, "no memory-region\n");
		return -EINVAL;
	}

#ifdef SUPPORT_FULL_KERNEL_CODE_2M
	/* Enable the support of full kernel code with 2M mapping */
	full_kernel_code_2m = true;
	pr_info("resource base=%pa, size=%pa\n", &rmem->base, &rmem->size);
	MKP_INFO("Support FULL_KERNEL_CODE_2M\n");
#else
	free_reserved_memory(rmem->base, rmem->base + rmem->size);
	MKP_INFO("Not Support FULL_KERNEL_CODE_2M\n");
#endif /* SUPPORT_FULL_KERNEL_CODE_2M */

	return 0;
}

static int mkp_probe(struct platform_device *pdev)
{
	get_reserved_memory(&pdev->dev);

	return 0;
}

struct platform_driver mkp_driver = {
	.probe = mkp_probe,
	.remove = NULL,
	.driver = {
		.name = "mkp-drv",
		.owner = THIS_MODULE,
		.of_match_table = mkp_of_match,
	},
};

int __init mkp_demo_init(void)
{
	int ret = 0, ret_erri_line;
	unsigned long size = 0x100000;
	struct device_node *node;
	u32 mkp_policy_default = 0x0001fffb; // disable selinux_state policy as default
	u32 mkp_policy = 0x0001ffff;
	const char *mkp_panic;

	ret = platform_driver_register(&mkp_driver);
	if (ret)
		MKP_WARN("Failed to support FULL_KERNEL_CODE_2M\n");

	node = of_find_node_by_path("/chosen");
	if (node) {
		if (of_property_read_u32(node, "mkp,policy", &mkp_policy) == 0)
			MKP_DEBUG("mkp_policy: %x\n", mkp_policy);
		else
			MKP_WARN("mkp,policy cannot be found, use default\n");

		if (of_property_read_string(node, "mkp_panic", &mkp_panic) == 0)
			if (strcmp(mkp_panic, "on") == 0)
				enable_action_panic();
			else
				pr_info("%s: mkp_panic=off\n", __func__);
		else
			pr_info("%s: no mkp_panic node\n", __func__);

		if (mkp_policy & BIT(MKP_POLICY_DRV))
			update_drv_skip_by_dts(node);
	} else
		MKP_WARN("chosen node cannot be found, use default\n");

	if (sizeof(phys_addr_t) != sizeof(unsigned long)) {
		MKP_ERR("init mkp failed, sizeof(phys_addr_t) != sizeof(unsigned long)\n");
		return 0;
	}

	/* Set policy control */
	mkp_set_policy(mkp_policy & mkp_policy_default);

	/* Hook up interesting tracepoints and update corresponding policy_ctrl */
	mkp_hookup_tracepoints();

	/* Protect kernel code & rodata */
	if (policy_ctrl[MKP_POLICY_KERNEL_CODE] != 0 ||
		policy_ctrl[MKP_POLICY_KERNEL_RODATA] != 0) {

#if !IS_ENABLED(CONFIG_KASAN_GENERIC) && !IS_ENABLED(CONFIG_KASAN_SW_TAGS)
#if !IS_ENABLED(CONFIG_GCOV_KERNEL)
	/* rodin b57: krn 面保护与 ESS_1（start granting）都以 mkp_pk_work
	 * 执行；=y 内建态在 work 内再等 system_state == SYSTEM_RUNNING（内核
	 * mark_readonly 之后）——__ro_after_init 全内核在 mark_readonly 前持续
	 * 被写，rodata 保护早于它将制造 OEM 不存在的 fault 窗口（#105 实证：
	 * g_ro_avc_handle 写被 GZ 判违规注入 dabt）。=m 的 OEM 时序（模块装载
	 * 期）天然满足该门，不受影响。post-grant mapping op 的 ticket 只能由
	 * 对受保护槽页的写入 fault 产生（GZ mkp_service produce_ticket 反汇编
	 * 实证），故 rodata 保护必须先于 ESS_1——#106 即 ESS_1 先行 =>
	 * 首 post-grant op 无 ticket => do_action_panic(189) => GZ 静默死机
	 * （0.412s 起双 CPU 困 EL3、零回栈）。 */
	schedule_delayed_work(&mkp_pk_work, 0);
#endif
#endif
	}

	/* Protect MKP itself */
	if (policy_ctrl[MKP_POLICY_MKP] != 0)
		ret = protect_mkp_self();

	if (policy_ctrl[MKP_POLICY_SELINUX_AVC] != 0) {
		// Create selinux avc sharebuf
		g_ro_avc_handle = mkp_create_ro_sharebuf(MKP_POLICY_SELINUX_AVC, size, &avc_pages);
		if (g_ro_avc_handle != 0) {
			ret = mkp_configure_sharebuf(MKP_POLICY_SELINUX_AVC, g_ro_avc_handle,
				0, 8192 /* avc_sbuf_content */, sizeof(struct avc_sbuf_content)-8);
			rem = do_div(size, sizeof(struct avc_sbuf_content));
			avc_array_sz = size;
		} else {
			MKP_ERR("Create avc ro sharebuf fail\n");
		}
	}

	if (policy_ctrl[MKP_POLICY_SELINUX_AVC])
		avc_work = kmalloc(sizeof(struct work_struct), GFP_KERNEL);

	if (policy_ctrl[MKP_POLICY_TASK_CRED] != 0) {
		// Create task cred sharebuf
		size = 0x100000;
		g_ro_cred_handle = mkp_create_ro_sharebuf(MKP_POLICY_TASK_CRED, size, &cred_pages);
		if (g_ro_cred_handle != 0) {
			ret = mkp_configure_sharebuf(MKP_POLICY_TASK_CRED, g_ro_cred_handle,
				0, DEFAULT_MAX_PID, sizeof(struct cred_sbuf_content));
			rem = do_div(size, sizeof(struct cred_sbuf_content));
			cred_array_sz = size;
		} else {
			MKP_ERR("Create cred sharebuf fail\n");
		}
		// register creds vendor hook
		ret = register_trace_android_rvh_commit_creds(
				probe_android_rvh_commit_creds, NULL);
		if (ret) {
			ret_erri_line = __LINE__;
			goto failed;
		}
		ret = register_trace_android_rvh_exit_creds(
				probe_android_rvh_exit_creds, NULL);
		if (ret) {
			ret_erri_line = __LINE__;
			goto failed;
		}
		ret = register_trace_android_rvh_override_creds(
				probe_android_rvh_override_creds, NULL);
		if (ret) {
			ret_erri_line = __LINE__;
			goto failed;
		}
		ret = register_trace_android_rvh_revert_creds(
				probe_android_rvh_revert_creds, NULL);
		if (ret) {
			ret_erri_line = __LINE__;
			goto failed;
		}
	}

	if (policy_ctrl[MKP_POLICY_SELINUX_STATE] != 0) {
		// register selinux_state
		ret = register_trace_android_rvh_selinux_is_initialized(
				probe_android_rvh_selinux_is_initialized, NULL);
		if (ret) {
			ret_erri_line = __LINE__;
			goto failed;
		}
	}

	if (policy_ctrl[MKP_POLICY_TASK_CRED] ||
		policy_ctrl[MKP_POLICY_SELINUX_STATE]) {
		ret = register_trace_android_vh_check_mmap_file(
				probe_android_vh_check_mmap_file, NULL);
		if (ret) {
			ret_erri_line = __LINE__;
			goto failed;
		}
		ret = register_trace_android_vh_check_bpf_syscall(
				probe_android_vh_check_bpf_syscall, NULL);
		if (ret) {
			ret_erri_line = __LINE__;
			goto failed;
		}
		ret = register_trace_android_vh_check_file_open(
				probe_android_vh_check_file_open, NULL);
		if (ret) {
			ret_erri_line = __LINE__;
			goto failed;
		}
	}
	register_reboot_notifier(&mkp_reboot_notifier);


#if mkp_debug
	mod_timer(&mkp_trace_event_timer, jiffies
		+ MKP_TRACE_EVENT_TIME * HZ);
#endif

failed:
	if (ret)
		MKP_ERR("register hooks failed, ret %d line %d\n", ret, ret_erri_line);

	return 0;
}
