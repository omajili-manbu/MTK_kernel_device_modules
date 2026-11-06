/* SPDX-License-Identifier: GPL-2.0-only */
/* Embedded MiWiLL minet: recovered against miwill.ko, not minet.ko.
 * Log face follows the blob generation (miwill-minet-e/-d families,
 * no trailing newline); blob names its init/cleanup literals
 * minet_init_module / minet_cleanup_module. */
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/kthread.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <net/sock.h>
#include <net/inet_sock.h>
#define MINET_CPUS 32
struct minet_tuple {
	u8 protocol;
	u8 reserved;
	u16 family;
	u16 peer_port;
	u16 host_port;
	struct in6_addr peer_addr;
	struct in6_addr host_addr;
};

struct minet_counters {
	u32 rx_packets;
	u32 rx_bytes;
	u32 tx_packets;
	u32 tx_bytes;
};

struct minet_flow {
	struct minet_tuple tuple;
	struct minet_counters counters;
	struct list_head entry;
};

struct minet_list {
	u32 count;
	spinlock_t lock;
	struct list_head head;
};

struct minet_app {
	u32 reserved;
	u32 uid;
	struct minet_list flows;
	struct list_head entry;
};

struct minet_context {
	u64 reserved;
	struct task_struct *thread;
	struct minet_list apps[MINET_CPUS];
};

struct minet_app_request {
	u16 version;
	u16 operation;
	u32 count;
	u32 uid[];
};

struct minet_flow_report {
	u16 version;
	u16 reserved0;
	u32 uid;
	u16 count;
	u16 reserved1;
	u8 flows[];
};


static struct minet_context *g_minet;
static void minet_nl_notify(const void *, int);

static struct minet_flow *add_new_flow(struct minet_list *list)
{
	struct minet_flow *flow;

	if (!list) {
		printk(KERN_ERR "miwill-minet-e: %s NULL", "add_new_flow");
		return NULL;
	}
	flow = kmalloc(sizeof(*flow), GFP_ATOMIC);
	if (flow) {
		memset(flow, 0, sizeof(*flow));
		list_add_tail(&flow->entry, &list->head);
		list->count++;
	}
	return flow;
}

static struct minet_flow *match_flow(struct minet_list *list,
				    const struct minet_tuple *tuple)
{
	struct minet_flow *flow;

	list_for_each_entry(flow, &list->head, entry) {
		if (flow->tuple.family != tuple->family ||
		    flow->tuple.protocol != tuple->protocol ||
		    flow->tuple.peer_port != tuple->peer_port)
			continue;
		if (tuple->family == NFPROTO_IPV4) {
			if (flow->tuple.peer_addr.s6_addr32[0] !=
			    tuple->peer_addr.s6_addr32[0])
				continue;
		} else if (memcmp(&flow->tuple.peer_addr, &tuple->peer_addr, 16)) {
			continue;
		}
		return flow;
	}
	return NULL;
}

static struct minet_app *add_new_app(struct minet_list *list)
{
	struct minet_app *app;

	if (!list) {
		printk(KERN_ERR "miwill-minet-e: %s NULL", "add_new_app");
		return NULL;
	}
	app = kmalloc(sizeof(*app), GFP_ATOMIC);
	if (app) {
		memset(app, 0, sizeof(*app));
		INIT_LIST_HEAD(&app->flows.head);
		list_add_tail(&app->entry, &list->head);
		list->count++;
	}
	return app;
}

static struct minet_app *match_app(struct minet_list *list, u32 uid)
{
	struct minet_app *app;

	list_for_each_entry(app, &list->head, entry)
		if (app->uid == uid)
			return app;
	return NULL;
}

/* miwill.ko .text+0x442c: doubled counts and unchecked app allocation. */
static int merge_one_app_flow(u32 uid, const struct minet_tuple *tuple,
			      const struct minet_counters *stats,
			      struct minet_list *list)
{
	struct minet_app *app;
	struct minet_flow *flow;

	spin_lock_bh(&list->lock);
	app = match_app(list, uid);
	if (!app) {
		app = add_new_app(list);
		if (app) {
			app->uid = uid;
			list->count++;
		}
	}
	spin_unlock_bh(&list->lock);
	/* Stock dereferences the newly allocated app even after allocation fails. */
	spin_lock_bh(&app->flows.lock);
	flow = match_flow(&app->flows, tuple);
	if (flow) {
		flow->counters.rx_packets += stats->rx_packets;
		flow->counters.rx_bytes += stats->rx_bytes;
		flow->counters.tx_packets += stats->tx_packets;
		flow->counters.tx_bytes += stats->tx_bytes;
	} else {
		flow = add_new_flow(&app->flows);
		if (flow) {
			flow->tuple = *tuple;
			flow->counters = *stats;
			app->flows.count++;
		}
	}
	spin_unlock_bh(&app->flows.lock);
	return 0;
}

static struct minet_list *merge_app_flow(void)
{
	struct minet_list *merged, *list;
	struct minet_app *app;
	struct minet_flow *flow, *next;
	int cpu;

	merged = kmalloc(sizeof(*merged), GFP_KERNEL);
	if (!merged) {
		printk(KERN_DEBUG "miwill-minet-d: %s err.", "merge_app_flow");
		return NULL;
	}
	merged->count = 0;
	spin_lock_init(&merged->lock);
	INIT_LIST_HEAD(&merged->head);
	for (cpu = 0; cpu < MINET_CPUS; cpu++) {
		list = &g_minet->apps[cpu];
		if (list_empty(&list->head))
			continue;
		spin_lock_bh(&list->lock);
		list_for_each_entry(app, &list->head, entry) {
			if (list_empty(&app->flows.head))
				continue;
			spin_lock_bh(&app->flows.lock);
			list_for_each_entry_safe(flow, next, &app->flows.head, entry) {
				list_del_init(&flow->entry);
				app->flows.count--;
				merge_one_app_flow(app->uid, &flow->tuple,
						   &flow->counters, merged);
				kfree(flow);
			}
			spin_unlock_bh(&app->flows.lock);
		}
		spin_unlock_bh(&list->lock);
	}
	return merged;
}

static void minet_report_flow(const struct minet_tuple *tuple,
			      const struct minet_counters *counters)
{
	if (tuple->family == NFPROTO_IPV6) {
		const u16 *p = (const u16 *)tuple->peer_addr.s6_addr;
		const u16 *h = (const u16 *)tuple->host_addr.s6_addr;
		printk(KERN_DEBUG "miwill-minet-d: flow: p:%d "
		       "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x:%d "
		       "<--> %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x:%d",
		       tuple->protocol,
		       ntohs(p[0]), ntohs(p[1]), ntohs(p[2]), ntohs(p[3]),
		       ntohs(p[4]), ntohs(p[5]), ntohs(p[6]), ntohs(p[7]),
		       ntohs(tuple->peer_port),
		       ntohs(h[0]), ntohs(h[1]), ntohs(h[2]), ntohs(h[3]),
		       ntohs(h[4]), ntohs(h[5]), ntohs(h[6]), ntohs(h[7]),
		       ntohs(tuple->host_port));
	} else {
		const u8 *pa = (const u8 *)&tuple->peer_addr.s6_addr32[0];
		const u8 *ha = (const u8 *)&tuple->host_addr.s6_addr32[0];
		printk(KERN_DEBUG "miwill-minet-d: flow: p:%d %d.%d.%d.%d:%d <--> %d.%d.%d.%d:%d",
		       tuple->protocol,
		       pa[0], pa[1], pa[2], pa[3], ntohs(tuple->peer_port),
		       ha[0], ha[1], ha[2], ha[3], ntohs(tuple->host_port));
	}
	printk(KERN_DEBUG "miwill-minet-d:       in:%d(%d) out:%d(%d)",
	       counters->rx_packets, counters->rx_bytes,
	       counters->tx_packets, counters->tx_bytes);
}

static int minet_notify_app_flow(struct minet_list *list)
{
	struct minet_app *app, *next_app;
	struct minet_flow *flow, *next_flow;
	struct minet_flow_report *report;

	if (!list) {
		printk(KERN_ERR "miwill-minet-e: %s NULL", "minet_notify_app_flow");
		return -1;
	}
	spin_lock_bh(&list->lock);
	list_for_each_entry_safe(app, next_app, &list->head, entry) {
		list_del_init(&app->entry);
		printk(KERN_DEBUG "miwill-minet-d: %s uid:%d, cnt:%d",
		       "minet_notify_app_flow", app->uid, app->flows.count);
		report = kmalloc((size_t)app->flows.count * 56 + 12, GFP_ATOMIC);
		if (report) {
			/* Stock leaves both reserved halfwords uninitialized. */
			report->version = 1;
			report->uid = app->uid;
			report->count = 0;
		} else {
			printk(KERN_ERR "miwill-minet-e: %s nf NULL",
			       "minet_notify_app_flow");
		}
		spin_lock_bh(&app->flows.lock);
		list_for_each_entry_safe(flow, next_flow, &app->flows.head, entry) {
			list_del_init(&flow->entry);
			minet_report_flow(&flow->tuple, &flow->counters);
			if (report) {
				memcpy(report->flows + (size_t)report->count * 56,
				       flow, 56);
				report->count++;
			}
			kfree(flow);
		}
		spin_unlock_bh(&app->flows.lock);
		kfree(app);
		if (report) {
			minet_nl_notify(report, (u32)report->count * 56 + 12);
			kfree(report);
		}
	}
	spin_unlock_bh(&list->lock);
	kfree(list);
	return 0;
}

static int add_app(u32 uid)
{
	struct minet_app *app;
	struct minet_list *list;
	int cpu;

	for (cpu = 0; cpu < MINET_CPUS; cpu++) {
		list = &g_minet->apps[cpu];
		spin_lock_bh(&list->lock);
		app = add_new_app(list);
		if (app)
			app->uid = uid;
		spin_unlock_bh(&list->lock);
	}
	return 0;
}

static void destroy_app(struct minet_app *app)
{
	struct minet_flow *flow, *next;

	if (!list_empty(&app->flows.head)) {
		spin_lock_bh(&app->flows.lock);
		list_for_each_entry_safe(flow, next, &app->flows.head, entry) {
			list_del_init(&flow->entry);
			app->flows.count--;
			kfree(flow);
		}
		spin_unlock_bh(&app->flows.lock);
		kfree(app);
	}
	/* Stock leaves the unlinked empty app allocated. */
}

static int del_app(u32 uid)
{
	struct minet_list *list;
	struct minet_app *app, *next;
	int cpu;

	for (cpu = 0; cpu < MINET_CPUS; cpu++) {
		list = &g_minet->apps[cpu];
		if (list_empty(&list->head))
			continue;
		spin_lock_bh(&list->lock);
		list_for_each_entry_safe(app, next, &list->head, entry) {
			if (app->uid != uid)
				continue;
			list_del_init(&app->entry);
			list->count--;
			destroy_app(app);
		}
		spin_unlock_bh(&list->lock);
	}
	return 0;
}

static int clear_app(void)
{
	struct minet_list *list;
	struct minet_app *app, *next;
	int cpu;

	for (cpu = 0; cpu < MINET_CPUS; cpu++) {
		list = &g_minet->apps[cpu];
		if (list_empty(&list->head))
			continue;
		spin_lock_bh(&list->lock);
		list_for_each_entry_safe(app, next, &list->head, entry) {
			list_del_init(&app->entry);
			list->count--;
			destroy_app(app);
		}
		spin_unlock_bh(&list->lock);
	}
	return 0;
}

static int minet_set_app(const struct minet_app_request *request)
{
	u32 i;

	if (!request) {
		printk(KERN_ERR "miwill-minet-e: %s NULL", "minet_set_app");
		return -1;
	}
	if (request->version != 1) {
		printk(KERN_ERR "miwill-minet-e: %s type:%d",
		       "minet_set_app", request->version);
		return -1;
	}
	/* Stock trusts the wire count, without checking the attribute length. */
	switch (request->operation) {
	case 1:
		for (i = 0; i < request->count; i++) {
			printk(KERN_DEBUG "miwill-minet-d: %s add %d",
			       "minet_set_app", request->uid[i]);
			add_app(request->uid[i]);
		}
		break;
	case 2:
		for (i = 0; i < request->count; i++) {
			printk(KERN_DEBUG "miwill-minet-d: %s del %d",
			       "minet_set_app", request->uid[i]);
			del_app(request->uid[i]);
		}
		break;
	case 3:
		printk(KERN_DEBUG "miwill-minet-d: %s clear", "minet_set_app");
		clear_app();
		break;
	default:
		printk(KERN_ERR "miwill-minet-e: %s type:%d",
		       "minet_set_app", request->operation);
		break;
	}
	return 0;
}

/* stock .text+0x1ee8, +0x2310, +0x2470, +0x2718 */
static void app_flow_stat(struct minet_app *app, struct sk_buff *skb,
			  const struct nf_hook_state *state)
{
	struct minet_tuple tuple = {};
	struct minet_flow *flow;
	__be16 *ports = (__be16 *)skb_transport_header(skb);
	bool incoming = state->hook == NF_INET_LOCAL_IN;

	if (state->pf == NFPROTO_IPV6) {
		struct ipv6hdr *ip6 = ipv6_hdr(skb);

		if (ip6 && ports) {
			tuple.protocol = ip6->nexthdr;
			tuple.peer_addr = incoming ? ip6->saddr : ip6->daddr;
			tuple.host_addr = incoming ? ip6->daddr : ip6->saddr;
			tuple.peer_port = ntohs(ports[incoming ? 0 : 1]);
			tuple.host_port = ntohs(ports[incoming ? 1 : 0]);
		}
	} else if (state->pf == NFPROTO_IPV4) {
		struct iphdr *ip4 = ip_hdr(skb);

		if (ip4 && ports) {
			tuple.protocol = ip4->protocol;
			tuple.peer_addr.s6_addr32[0] = incoming ? ip4->saddr : ip4->daddr;
			tuple.host_addr.s6_addr32[0] = incoming ? ip4->daddr : ip4->saddr;
			tuple.peer_port = ntohs(ports[incoming ? 0 : 1]);
			tuple.host_port = ntohs(ports[incoming ? 1 : 0]);
		}
	}
	tuple.family = state->pf;
	spin_lock_bh(&app->flows.lock);
	flow = match_flow(&app->flows, &tuple);
	if (!flow) {
		flow = add_new_flow(&app->flows);
		if (flow)
			flow->tuple = tuple;
	}
	if (flow) {
		if (state->hook == NF_INET_LOCAL_OUT) {
			flow->counters.tx_packets++;
			flow->counters.tx_bytes += skb->len;
		} else if (incoming) {
			flow->counters.rx_packets++;
			flow->counters.rx_bytes += skb->len;
		}
	}
	spin_unlock_bh(&app->flows.lock);
}

static u32 get_sock_uid(const struct sock *sk)
{
	return __kuid_val(sk->sk_uid);
}

static bool minet_excluded_v4(__be32 address)
{
	u32 addr = (__force u32)address;

	return (addr & 0xf0ff) == 0x10ac || (addr & 0xff) == 10 ||
		addr == U32_MAX || (addr & 0xff) == 0x7f ||
		(addr & 0xf0) == 0xe0 || (addr & 0xffff) == 0xa8c0 ||
		(addr & 0xffff) == 0xfea9;
}

static void minet_account(struct sk_buff *skb, const struct nf_hook_state *state,
		u32 uid, bool first)
{
	struct minet_list *list;
	struct minet_app *app;
	u32 cpu;
	preempt_disable();
	cpu = smp_processor_id();
	if (cpu < MINET_CPUS) {
		list = &g_minet->apps[cpu];
		spin_lock_bh(&list->lock);
		if (first)
			app = list_empty(&list->head) ? NULL :
				list_first_entry(&list->head, struct minet_app, entry);
		else
			app = match_app(list, uid);
		if (app)
			app_flow_stat(app, skb, state);
		spin_unlock_bh(&list->lock);
	}
	preempt_enable();
}
static struct sock *minet_full_socket(struct sk_buff *skb)
{
	struct sock *sk = skb_to_full_sk(skb);
	if (!sk || !sk_fullsock(sk) || (u16)(sk->sk_type - 1) > 1)
		return NULL;
	return sk;
}
static unsigned int hook4(void *priv, struct sk_buff *skb,
		const struct nf_hook_state *state)
{
	struct iphdr *ip = ip_hdr(skb);
	struct sock *sk;
	bool first;
	if (!ip)
		return NF_ACCEPT;
	first = state->hook == NF_INET_LOCAL_IN && ip->protocol == IPPROTO_UDP;
	if (first) {
		if (!(skb->mark & 0x1000000) || minet_excluded_v4(ip->saddr))
			return NF_ACCEPT;
		minet_account(skb, state, 0, true);
		return NF_ACCEPT;
	}
	sk = minet_full_socket(skb);
	if (!sk || (ip->protocol != IPPROTO_TCP && ip->protocol != IPPROTO_UDP))
		return NF_ACCEPT;
	if ((state->hook == NF_INET_LOCAL_OUT && minet_excluded_v4(ip->daddr)) ||
		(state->hook == NF_INET_LOCAL_IN && minet_excluded_v4(ip->saddr)))
		return NF_ACCEPT;
	minet_account(skb, state, get_sock_uid(sk), false);
	return NF_ACCEPT;
}
static unsigned int hook6(void *priv, struct sk_buff *skb,
		const struct nf_hook_state *state)
{
	struct ipv6hdr *ip = ipv6_hdr(skb);
	struct sock *sk;
	if (!ip)
		return NF_ACCEPT;
	if (state->hook == NF_INET_LOCAL_IN && ip->nexthdr == IPPROTO_UDP) {
		if (skb->mark & 0x1000000)
			minet_account(skb, state, 0, true);
		return NF_ACCEPT;
	}
	sk = minet_full_socket(skb);
	if (!sk || (ip->nexthdr != IPPROTO_TCP && ip->nexthdr != IPPROTO_UDP))
		return NF_ACCEPT;
	minet_account(skb, state, get_sock_uid(sk), false);
	return NF_ACCEPT;
}
static const struct nf_hook_ops minet_hooks[] = {
	{ .hook = hook4, .pf = NFPROTO_IPV4, .hooknum = NF_INET_LOCAL_IN, .priority = 1 },
	{ .hook = hook4, .pf = NFPROTO_IPV4, .hooknum = NF_INET_LOCAL_OUT, .priority = -1 },
	{ .hook = hook6, .pf = NFPROTO_IPV6, .hooknum = NF_INET_LOCAL_IN, .priority = 1 },
	{ .hook = hook6, .pf = NFPROTO_IPV6, .hooknum = NF_INET_LOCAL_OUT, .priority = -1 },
};
static int minet_thread(void *data)
{
	printk(KERN_DEBUG "miwill-minet-d: %s", "minet_thread");
	while (!kthread_should_stop()) {
		schedule_timeout_interruptible(7500);
		minet_notify_app_flow(merge_app_flow());
	}
	return 0;
}
static int minet_init(void)
{
	struct task_struct *thread;
	int cpu, err;
	g_minet = kmalloc(sizeof(*g_minet), GFP_KERNEL);
	if (!g_minet)
		return -1;
	memset(g_minet, 0, sizeof(*g_minet));
	for (cpu = 0; cpu < MINET_CPUS; ++cpu) {
		spin_lock_init(&g_minet->apps[cpu].lock);
		INIT_LIST_HEAD(&g_minet->apps[cpu].head);
	}
	err = nf_register_net_hooks(&init_net, minet_hooks, ARRAY_SIZE(minet_hooks));
	if (err) {
		printk(KERN_ERR "miwill-minet-e: %s nf_register_net_hooks err:%d",
		       "minet_init_module", err);
		goto fail;
	}
	thread = kthread_create(minet_thread, g_minet, "minet_thread");
	if (IS_ERR(thread)) {
		g_minet->thread = thread;
		printk(KERN_ERR "miwill-minet-e: %s kthread_run err",
		       "minet_init_module");
		goto fail;
	}
	wake_up_process(thread);
	g_minet->thread = thread;
	if (!thread)
		goto fail;
	printk(KERN_DEBUG "miwill-minet-d: %s ok", "minet_init_module");
	return 0;
fail:
	/* Stock leaves hooks installed after thread creation fails. */
	kfree(g_minet);
	g_minet = NULL;
	return -1;
}
static void minet_cleanup(void)
{
	printk(KERN_DEBUG "miwill-minet-d: %s", "minet_cleanup_module");
	nf_unregister_net_hooks(&init_net, minet_hooks, ARRAY_SIZE(minet_hooks));
	if (g_minet->thread) {
		kthread_stop(g_minet->thread);
		g_minet->thread = NULL;
	}
	clear_app();
	if (g_minet) {
		kfree(g_minet);
		g_minet = NULL;
	}
	printk(KERN_DEBUG "miwill-minet-d: %s ok", "minet_cleanup_module");
}
static int minet_nl_cmd_set(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attrs[10] = {};
	struct nlmsghdr *nlh = (struct nlmsghdr *)skb->data;
	int err = __nla_parse(attrs, 9, (struct nlattr *)(skb->data + 20),
		nlh->nlmsg_len - 20, miwill_genl_policy, 0, info->extack);
	if (err)
		return err;
	return attrs[9] ? minet_set_app(nla_data(attrs[9])) : 0;
}
static void minet_nl_notify(const void *payload, int length)
{
	printk(KERN_DEBUG "miwill_genl: %s %d", "minet_nl_notify", length);
	miwill_nl_msg(payload, length, 0, 3, 9);
}
