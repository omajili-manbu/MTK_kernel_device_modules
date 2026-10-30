// SPDX-License-Identifier: GPL-2.0-only
/* rodin 6.18（2026-10-27）：metis_carrier_audit —— 载体机器级开机对账件。
 * 位置 = late_initcall（晚于 vseq DEVICE 波与四件 init，boot 一次）；
 * 输出 = dmesg 单行 summary（对齐 rodin_vendor_fw 门闩风格），异常 WARN。
 *
 * 2026-10-27 定案后的对账面：
 *  1. BTF 对账（主防线）：内核真身 rwsem_waiter（rwsem.c 私有）的
 *     sizeof/task@/timeout@ 三值 vs 副本三断言（metis-final-link.c
 *     sizeof=48/task@16/timeout@32）。LTS 追新上游改私有结构时此处
 *     开机即红——副本断言只锁副本自身，够不着真身，本件补足机器级
 *     对账（tripwire 未采纳，本件为唯一防线）。BTF 缺失降级
 *     SKIP（不误报）。
 *  2. rwsem 16B 原生槽不变量：mi_rwsem_init 复位语义（magic 模 +
 *     deadline 清零）+ 幂等二次应用。活体接力载体，stock 布局保真。
 *  3. 同类微回归：do_rwsem_list_insert 在链守卫（重入置 already
 *     不再插）、priority==5 门（already 不动）、空链首插。
 *  4. futex alter 钩子 already 出参契约（直通不触链）+ 空链 smoke。
 *     NULL-task 遍历在 boot 期不可确定性驱动（钩子前置
 *     VIP/qos 门对 init 任务恒关），留作 LTS 重验项。
 *  5. 退役面提示：mutex 侧表/payload 已结构性消失，
 *     无运行时可查面，summary 行带注记。
 */
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/plist.h>
#include <linux/rwsem.h>
#include <linux/string.h>
#include "metis_common.h"
#include "metis_classify.h"
#include "metis-final-link.h"

#define METIS_AUDIT_RWSEM_MAGIC	0xab9f19ab00000000ULL

struct metis_audit_fake_waiter {
	struct list_head list;
	u8 pad[32];	/* 对位真身 48B：list(0..15)+task/type/timeout/... */
};

static bool metis_audit_btf(void)
{
	const struct btf *btf;
	const struct btf_type *t;
	const struct btf_member *m;
	const char *mname;
	s32 id;
	u16 vlen, i;
	u32 got_size = 0, got_task = 0, got_timeout = 0;
	int err = 0;

	btf = bpf_get_btf_vmlinux();
	if (IS_ERR_OR_NULL(btf)) {
		pr_info("metis-audit: btf: vmlinux BTF unavailable, SKIP\n");
		return true;	/* 降级不误报 */
	}
	id = btf_find_by_name_kind(btf, "rwsem_waiter", BTF_KIND_STRUCT);
	if (id < 0) {
		pr_warn("metis-audit: btf: rwsem_waiter not found (%d)\n", id);
		return false;
	}
	t = btf_type_by_id(btf, id);
	got_size = t->size;
	vlen = BTF_INFO_VLEN(t->info);
	m = btf_type_member(t);
	for (i = 0; i < vlen; i++) {
		/* rodin 6.18（2026-10-31）：BTF member offset 恒为 bit 单位
		 * （kflag 只控 bitfield_size 域）；旧版 kflag=false 分支裸用
		 * bit 值致 task@128/timeout@256 假失配——恒除 8。 */
		u32 bytes = BTF_MEMBER_BIT_OFFSET(m[i].offset) / 8;

		mname = btf_name_by_offset(btf, m[i].name_off);
		if (!mname)
			continue;
		if (!strcmp(mname, "task"))
			got_task = bytes;
		else if (!strcmp(mname, "timeout"))
			got_timeout = bytes;
	}
	if (got_size != 48)
		err |= 1;
	if (got_task != 16)
		err |= 2;
	if (got_timeout != 32)
		err |= 4;
	pr_info("metis-audit: btf: rwsem_waiter size=%u task@%u timeout@%u (copy: 48/16/32) -> %s\n",
		got_size, got_task, got_timeout, err ? "MISMATCH" : "ok");
	return err == 0;
}

static bool metis_audit_rwsem_reset(void)
{
	struct rw_semaphore sem;
	u64 expect;

	memset(&sem, 0, sizeof(sem));
	sem.android_oem_data1[0] = 0xdeadbeefcafebabeULL;
	sem.android_oem_data1[1] = 0x1234ULL;
	mi_rwsem_init(NULL, &sem);
	/* protect_cs=0 时钩子早退（脏值原样保留 = stock 无钩子形态）。 */
	expect = protect_cs ? METIS_AUDIT_RWSEM_MAGIC : 0xdeadbeefcafebabeULL;
	if (sem.android_oem_data1[0] != expect || sem.android_oem_data1[1] != 0)
		goto fail;
	mi_rwsem_init(NULL, &sem);
	if (sem.android_oem_data1[0] != expect || sem.android_oem_data1[1] != 0)
		goto fail;
	pr_info("metis-audit: rwsem-slot reset: ok (magic=%#llx, idempotent)\n",
		sem.android_oem_data1[0]);
	return true;
fail:
	pr_warn("metis-audit: rwsem-slot reset: MISMATCH (word0=%#llx word1=%#llx expect0=%#llx)\n",
		sem.android_oem_data1[0], sem.android_oem_data1[1], expect);
	return false;
}

static bool metis_audit_rwsem_reentry(void)
{
	struct rw_semaphore sem;
	struct metis_audit_fake_waiter w_onsrc, w_prio, w_fresh;
	LIST_HEAD(onsrc);
	bool already;
	bool ok = true;

	memset(&sem, 0, sizeof(sem));
	INIT_LIST_HEAD(&sem.wait_list);

	/* 在链守卫：waiter.list 非空才视为在链（list_empty 语义：自指 = 空 =
	 * 不在链）。挂上宿主链制造非空 → 守卫置 already 且不插。旧版误用
	 * INIT_LIST_HEAD 造出空链，守卫不触发反而真插，并把脏链状态带进
	 * 后续用例——2026-10-31 修正。 */
	memset(&w_onsrc, 0, sizeof(w_onsrc));
	list_add(&w_onsrc.list, &onsrc);
	already = false;
	do_rwsem_list_insert((struct metis618_rwsem_waiter *)&w_onsrc, &sem,
			     &already, 0);
	if (!already || sem.wait_list.next != &sem.wait_list)
		ok = false;
	pr_info("metis-audit: rwsem reentry guard: %s (already=%d)\n",
		(already && sem.wait_list.next == &sem.wait_list) ? "ok" :
							  "MISMATCH",
		already);
	list_del(&w_onsrc.list);

	/* priority==5 门：already 原样、不插。 */
	memset(&w_prio, 0, sizeof(w_prio));
	INIT_LIST_HEAD(&w_prio.list);
	already = false;
	do_rwsem_list_insert((struct metis618_rwsem_waiter *)&w_prio, &sem,
			     &already, 5);
	if (already || sem.wait_list.next != &sem.wait_list)
		ok = false;
	pr_info("metis-audit: rwsem prio5 gate: %s\n",
		!already ? "ok" : "MISMATCH");

	/* 空链首插：fresh waiter（list 空）→ 挂头节点 + already=true
	 * （stock "already on list" 语义）。 */
	memset(&w_fresh, 0, sizeof(w_fresh));
	INIT_LIST_HEAD(&w_fresh.list);
	already = false;
	do_rwsem_list_insert((struct metis618_rwsem_waiter *)&w_fresh, &sem,
			     &already, 0);
	if (!already || sem.wait_list.next != &w_fresh.list)
		ok = false;
	pr_info("metis-audit: rwsem fresh insert: %s\n",
		(already && sem.wait_list.next == &w_fresh.list) ? "ok" :
							  "MISMATCH");
	return ok;
}

static bool metis_audit_futex_contract(void)
{
	struct plist_head head;
	struct plist_node node;
	bool already;
	bool ok = true;

	plist_head_init(&head);
	plist_node_init(&node, 100);

	/* already 出参直通：置位即返回，不触链。 */
	already = true;
	mi_alter_futex_plist_add(NULL, &node, &head, &already);
	if (head.node_list.next != &head.node_list)
		ok = false;
	pr_info("metis-audit: futex already passthrough: %s\n",
		head.node_list.next == &head.node_list ? "ok" : "MISMATCH");

	/* 空链 smoke（boot 期钩子前置门对 init 任务恒关，仅验不崩不触链）。 */
	already = false;
	mi_alter_futex_plist_add(NULL, &node, &head, &already);
	if (head.node_list.next != &head.node_list)
		ok = false;
	pr_info("metis-audit: futex smoke: %s\n",
		head.node_list.next == &head.node_list ? "ok" : "MISMATCH");
	return ok;
}

static int __init metis_carrier_audit_init(void)
{
	bool btf, reset, reentry, futex;

	btf = metis_audit_btf();
	reset = metis_audit_rwsem_reset();
	reentry = metis_audit_rwsem_reentry();
	futex = metis_audit_futex_contract();

	pr_info("metis-audit: RESULT=%s btf=%s rwsem_reset=%s rwsem_reentry=%s futex=%s (mutex payload/side-table retired: surfaces structurally gone)\n",
		(btf && reset && reentry && futex) ? "GREEN" : "RED",
		btf ? "ok" : "FAIL", reset ? "ok" : "FAIL",
		reentry ? "ok" : "FAIL", futex ? "ok" : "FAIL");
	WARN_ON(!(btf && reset && reentry && futex));
	return 0;
}
late_initcall(metis_carrier_audit_init);
