/* SPDX-License-Identifier: GPL-2.0-only */
/* Deliberately compiled privately into each stock module. */
#include <linux/ctype.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <net/net_namespace.h>
#include "miwill.h"

static LIST_HEAD(mode_list);
static DEFINE_SPINLOCK(mode_list_lock);
static rx_handler_result_t miwill_dummy_receive(struct miwill *mi,
		struct miwill_port *port, struct sk_buff *skb)
{
	return RX_HANDLER_ANOTHER;
}
static bool miwill_dummy_transmit(struct miwill *mi, struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
	return false;
}
static const struct miwill_mode_ops dummy_ops = {
	.receive = miwill_dummy_receive, .transmit = miwill_dummy_transmit,
};
static struct miwill_mode dummy_mode = {
	.kind = "dummy", .owner = THIS_MODULE, .ops = &dummy_ops,
};
struct mode_item { struct list_head list; struct miwill_mode *mode; };

static struct miwill_mode *mode_find(const char *kind)
{
	struct mode_item *item;
	list_for_each_entry(item, &mode_list, list)
		if (!strcmp(item->mode->kind, kind))
			return item->mode;
	return NULL;
}
int miwill_mode_register(struct miwill_mode *mode)
{
	struct mode_item *item;
	const unsigned char *p;
	for (p = (const unsigned char *)mode->kind; *p; ++p)
		if (!isalpha(*p) && !isdigit(*p) && *p != '_')
			return -EINVAL;
	item = kmalloc(sizeof(*item), GFP_KERNEL);
	if (!item)
		return -ENOMEM;
	spin_lock(&mode_list_lock);
	if (mode_find(mode->kind)) {
		kfree(item);
		spin_unlock(&mode_list_lock);
		return -EEXIST;
	}
	item->mode = mode;
	list_add_tail(&item->list, &mode_list);
	spin_unlock(&mode_list_lock);
	printk(KERN_DEBUG "miwill-d: miwill_mode_register %s", mode->kind);
	return 0;
}
void miwill_mode_unregister(struct miwill_mode *mode)
{
	struct mode_item *item;
	spin_lock(&mode_list_lock);
	list_for_each_entry(item, &mode_list, list) {
		if (!strcmp(item->mode->kind, mode->kind)) {
			list_del_init(&item->list);
			kfree(item);
			break;
		}
	}
	spin_unlock(&mode_list_lock);
	printk(KERN_DEBUG "miwill-d: miwill %s-mode module cleanup", mode->kind);
}
int miwill_change_mode(struct miwill *mi, const char *kind)
{
	struct miwill_mode *mode;
	int err = -EINVAL;
	if (mi->mode && mi->mode != &dummy_mode && !strcmp(mi->mode->kind, kind)) {
		printk(KERN_ERR "miwill-e: %s %s, do nothing", "miwill_change_mode", kind);
		return err;
	}
	if (!try_module_get(THIS_MODULE)) {
		printk(KERN_ERR "miwill-e: %s %s, not found", "miwill_change_mode", kind);
		return err;
	}
	spin_lock(&mode_list_lock);
	mode = mode_find(kind);
	if (!mode) {
		spin_unlock(&mode_list_lock);
		request_module("miwill-mode-%s", kind);
		spin_lock(&mode_list_lock);
		mode = mode_find(kind);
	}
	if (mode && !try_module_get(mode->owner))
		mode = NULL;
	spin_unlock(&mode_list_lock);
	module_put(THIS_MODULE);
	if (!mode) {
		printk(KERN_ERR "miwill-e: %s %s, not found", "miwill_change_mode", kind);
		return err;
	}
	printk(KERN_DEBUG "miwill-d: %s", "miwill_set_no_mode");
	if (mi->mode && mi->mode != &dummy_mode) {
		if (mi->mode->ops->exit)
			mi->mode->ops->exit(mi);
		module_put(mi->mode->owner);
	}
	mi->mode = &dummy_mode;
	err = 0;
	if (mode->ops->init) {
		err = mode->ops->init(mi);
		if (err) {
			module_put(mode->owner);
			printk(KERN_ERR "miwill-e: %s %s, err", "miwill_change_mode", kind);
			return err;
		}
	}
	mi->mode = mode;
	printk(KERN_DEBUG "miwill-d: %s %s, ok", "miwill_change_mode", kind);
	return 0;
}
int miwill_modeop_port_enter(struct miwill *mi, struct miwill_port *port)
{
	if (!mi->dev || !mi->dev->dev_addr) {
		printk(KERN_DEBUG "miwill-d: failed to set src_id of port_dev %s.",
		       port->dev ? port->dev->name : "");
		return -1;
	}
	memcpy(&port->sender_tail, mi->dev->dev_addr + 4, 2);
	return 0;
}
struct net_device *miwill_name_to_dev(const char *name)
{
	struct net_device *dev = dev_get_by_name(&init_net, name);
	if (!dev)
		printk(KERN_DEBUG "miwill-d: get %s dev err.", name);
	return dev;
}
static struct miwill_option *option_find(struct miwill *mi, const char *name)
{
	struct miwill_option *opt;
	list_for_each_entry(opt, &mi->options, list)
		if (!strcmp(opt->name, name))
			return opt;
	return NULL;
}
static void instances_delete(struct miwill *mi, struct miwill_option *opt)
{
	struct miwill_option_instance *inst, *next;
	list_for_each_entry_safe(inst, next, &mi->instances, list)
		if (inst->option == opt) {
			list_del(&inst->list);
			kfree(inst);
		}
}
int miwill_options_register(struct miwill *mi,
		const struct miwill_option *options, size_t count)
{
	struct miwill_option **copy;
	struct miwill_option_instance *inst;
	size_t i, j;
	unsigned int index, n;
	int err = -ENOMEM;
	copy = kcalloc(count, sizeof(*copy), GFP_KERNEL);
	if (!copy)
		return err;
	for (i = 0; i < count; ++i) {
		if (option_find(mi, options[i].name)) {
			err = -EEXIST;
			goto free_copies;
		}
		copy[i] = kmemdup(&options[i], sizeof(*copy[i]), GFP_KERNEL);
		if (!copy[i])
			goto free_copies;
	}
	for (i = 0; i < count; ++i) {
		if (!copy[i]->per_port) {
			n = max(copy[i]->array_size, 1U);
			for (index = 0; index < n; ++index) {
				inst = kmalloc(sizeof(*inst), GFP_KERNEL);
				if (!inst) {
					err = -ENOMEM;
					goto remove_instances;
				}
				inst->option = copy[i];
				inst->info.array_index = index;
				inst->info.port = NULL;
				list_add_tail(&inst->list, &mi->instances);
				if (copy[i]->init) {
					err = copy[i]->init(mi, &inst->info);
					if (err)
						goto remove_instances;
				}
			}
		}
		list_add_tail(&copy[i]->list, &mi->options);
	}
	kfree(copy);
	return 0;
remove_instances:
	for (j = 0; j <= i; ++j)
		instances_delete(mi, copy[j]);
	/* Stock frees prior list-linked options without unlinking them. */
	i = count;
free_copies:
	while (i)
		kfree(copy[--i]);
	kfree(copy);
	return err;
}
void miwill_options_unregister(struct miwill *mi,
		const struct miwill_option *options, size_t count)
{
	struct miwill_option *opt;
	size_t i;
	for (i = 0; i < count; ++i) {
		opt = option_find(mi, options[i].name);
		if (!opt)
			continue;
		instances_delete(mi, opt);
		list_del(&opt->list);
		kfree(opt);
	}
}
int miwill_options_set(struct miwill *mi, s8 enable, const char *mode,
		const u8 *gateway, const char *port5, const char *port24)
{
	struct miwill_option_instance *inst;
	struct miwill_option_value value = {};
	struct miwill_option *opt;
	int err = 0;
	if (enable != -1) {
		list_for_each_entry(inst, &mi->instances, list) {
			opt = inst->option;
			if (strcmp(opt->name, "enable"))
				continue;
			value.boolean = enable != 0;
			err = opt->set ? opt->set(mi, &value) : -EOPNOTSUPP;
		}
		if (!enable)
			return err;
	}
	list_for_each_entry(inst, &mi->instances, list) {
		opt = inst->option;
		if (mode && !strcmp(opt->name, "mode"))
			value.ptr = mode;
		else if (port5 && !strcmp(opt->name, "gw_addr")) {
			value.ptr = gateway;
			value.data_len = 6;
		} else if (port5 && !strcmp(opt->name, "port_5g"))
			value.ptr = port5;
		else if (port24 && !strcmp(opt->name, "port_24g"))
			value.ptr = port24;
		else
			continue;
		err = opt->set ? opt->set(mi, &value) : -EOPNOTSUPP;
	}
	return err;
}
