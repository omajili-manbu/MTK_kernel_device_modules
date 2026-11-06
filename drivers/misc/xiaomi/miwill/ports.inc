/* SPDX-License-Identifier: GPL-2.0-only */
/* Port management and the private framing helpers.  The framing helpers
 * (add/remove head, dev_queue_xmit) are deliberately compiled privately into
 * each stock module: miwill.ko and miwill_mode_redudancy.ko each carry their
 * own copies. */
#include <linux/unaligned.h>
#include <linux/jiffies.h>

static struct sk_buff *miwill_add_head(struct sk_buff *skb)
{
	int extra = skb_headroom(skb) < 9 ? 8 - skb_headroom(skb) : 0;
	u8 *data;
	__be16 protocol;
	if ((extra || skb_header_cloned(skb)) &&
		pskb_expand_head(skb, (extra + 63) & 64, 0, GFP_ATOMIC)) {
		consume_skb(skb);
		return NULL;
	}
	data = skb_push(skb, MIWILL_HLEN);
	skb_reset_mac_header(skb);
	skb->network_header -= MIWILL_HLEN;
	protocol = get_unaligned((__be16 *)(data + 20));
	memmove(data, data + 8, 12);
	put_unaligned(protocol, (__be16 *)(data + 20));
	put_unaligned_be16(MIWILL_ETH_P, data + 12);
	data[14] = (data[14] & 15) | 0x10;
	skb->protocol = htons(MIWILL_ETH_P);
	skb->priority = 7;
	return skb;
}
static void miwill_remove_head(struct sk_buff *skb)
{
	u8 *data = skb->data;
	skb->protocol = get_unaligned((__be16 *)(data + 6));
	memmove(data - 6, data - ETH_HLEN, 12);
	skb_pull(skb, MIWILL_HLEN);
	skb->mac_header += MIWILL_HLEN;
	skb->network_header += MIWILL_HLEN;
	skb->transport_header += MIWILL_HLEN;
	skb->mac_len = skb->network_header - skb->mac_header;
}
static int miwill_dev_queue_xmit(struct miwill *mi,
		struct miwill_port *port, struct sk_buff *skb)
{
	if (!port->dev) {
		printk(KERN_ERR "miwill-e: %s port->dev is NULL",
		       "miwill_dev_queue_xmit");
		consume_skb(skb);
		return -1;
	}
	skb->dev = port->dev;
	memcpy(skb->data, mi->gateway, 6);
	memcpy(skb->data + 6, port->dev->dev_addr, 6);
	memcpy(skb->data + 18, mi->dev->dev_addr + 4, 2);
	return dev_queue_xmit(skb);
}

static void miwill_queue_override_update(struct miwill *mi)
{
	struct miwill_port *port;
	mi->queue_override = false;
	list_for_each_entry(port, &mi->ports, list)
		if (port->queue_id) {
			mi->queue_override = true;
			break;
		}
}
static void miwill_port_enable(struct miwill_port *port)
{
	struct miwill *mi = port->miwill;
	struct miwill_port *other;
	struct list_head *head, *before;
	if (port->index != -1)
		return;
	port->index = mi->enabled_ports++;
	hlist_add_head(&port->index_node, &mi->index_hash[port->index & 3]);
	if (port->queue_id) {
		head = &mi->queues[port->queue_id - 1];
		before = head;
		list_for_each_entry(other, head, queue_list)
			if (other->priority > port->priority ||
				(other->priority == port->priority && other->index > port->index)) {
				before = &other->queue_list;
				break;
			}
		list_add_tail(&port->queue_list, before);
	}
	miwill_queue_override_update(mi);
	if (mi->mode && mi->mode->ops->port_enabled)
		mi->mode->ops->port_enabled(mi, port);
	miwill_lower_changed(port);
}
static struct miwill_port *miwill_port_by_index(struct miwill *mi, int index)
{
	struct miwill_port *port;
	hlist_for_each_entry(port, &mi->index_hash[index & 3], index_node)
		if (port->index == index)
			return port;
	return NULL;
}
static void miwill_port_disable(struct miwill_port *port)
{
	struct miwill *mi = port->miwill;
	struct miwill_port *other;
	int i;
	if (port->index == -1)
		return;
	if (mi->mode && mi->mode->ops->port_disabled)
		mi->mode->ops->port_disabled(mi, port);
	hlist_del(&port->index_node);
	for (i = port->index + 1; i < mi->enabled_ports; ++i) {
		other = miwill_port_by_index(mi, i);
		/* Stock dereferences a failed lookup rather than skipping it. */
		hlist_del(&other->index_node);
		--other->index;
		hlist_add_head(&other->index_node, &mi->index_hash[other->index & 3]);
	}
	port->index = -1;
	--mi->enabled_ports;
	if (port->queue_id)
		list_del(&port->queue_list);
	miwill_queue_override_update(mi);
	miwill_lower_changed(port);
}
static void miwill_features_after_remove(struct miwill *mi)
{
	struct miwill_port *port;
	netdev_features_t features;
	u32 release = 0x20020;
	u16 hard_header_len = ETH_HLEN;
	bool dst_release;
	if (list_empty(&mi->ports)) {
		features = 0x21020fdd0089ULL;
		dst_release = true;
	} else {
		features = 0x100001d0009ULL;
		list_for_each_entry(port, &mi->ports, list) {
			features = netdev_increment_features(features,
				port->dev->hw_enc_features, 0x100001d0009ULL);
			release &= port->dev->priv_flags;
			hard_header_len = max(hard_header_len, port->dev->hard_header_len);
		}
		dst_release = release == 0x20020;
		features |= 0x20020fc00080ULL;
	}
	mi->dev->hw_enc_features = features;
	mi->dev->hard_header_len = hard_header_len;
	mi->dev->priv_flags &= ~IFF_XMIT_DST_RELEASE;
	if (dst_release)
		mi->dev->priv_flags |= IFF_XMIT_DST_RELEASE;
}
static void miwill_port_instances_delete(struct miwill *mi, struct miwill_port *port)
{
	struct miwill_option_instance *inst, *next;
	list_for_each_entry_safe(inst, next, &mi->instances, list)
		if (inst->option->per_port && inst->info.port == port) {
			list_del(&inst->list);
			kfree(inst);
		}
}
static int miwill_port_instances_add(struct miwill *mi, struct miwill_port *port)
{
	struct miwill_option *opt;
	struct miwill_option_instance *inst;
	unsigned int index, n;
	int err;
	list_for_each_entry(opt, &mi->options, list) {
		if (!opt->per_port)
			continue;
		n = max(opt->array_size, 1U);
		for (index = 0; index < n; ++index) {
			inst = kmalloc(sizeof(*inst), GFP_KERNEL);
			if (!inst) {
				err = -ENOMEM;
				goto unwind;
			}
			inst->option = opt;
			inst->info.array_index = index;
			inst->info.port = port;
			list_add_tail(&inst->list, &mi->instances);
			if (opt->init) {
				err = opt->init(mi, &inst->info);
				if (err)
					goto unwind;
			}
		}
	}
	return 0;
unwind:
	miwill_port_instances_delete(mi, port);
	return err;
}
static int miwill_dev_type_check_change(struct miwill *mi, struct net_device *lower)
{
	struct net_device *dev = mi->dev;
	int err;
	if (dev->type == lower->type)
		return 0;
	if (!list_empty(&mi->ports)) {
		printk(KERN_ERR "miwill-e: %s %s, not first port",
		       "miwill_port_add_by_name", lower->name);
		return -EBUSY;
	}
	err = notifier_to_errno(call_netdevice_notifiers(NETDEV_PRE_TYPE_CHANGE, dev));
	if (err) {
		printk(KERN_ERR "miwill-e: %s %s, change device type",
		       "miwill_port_add_by_name", lower->name);
		return err;
	}
	dev->header_ops = lower->header_ops;
	dev->type = lower->type;
	dev->hard_header_len = lower->hard_header_len;
	dev->needed_headroom = lower->needed_headroom;
	dev->addr_len = lower->addr_len;
	dev->mtu = lower->mtu;
	memcpy(dev->broadcast, lower->broadcast, lower->addr_len);
	dev->addr_assign_type = lower->addr_assign_type;
	dev_addr_mod(dev, 0, lower->dev_addr, ETH_ALEN);
	call_netdevice_notifiers(NETDEV_POST_TYPE_CHANGE, dev);
	return 0;
}
static int miwill_port_add_by_name(struct miwill *mi, const char *name, int band)
{
	struct net_device *lower = dev_get_by_name(&init_net, name);
	struct miwill_port *port;
	int err;
	printk(KERN_DEBUG "miwill-d: %s %s %d",
	       "miwill_port_add_by_name", name, band);
	if (!lower) {
		printk(KERN_DEBUG "miwill-d: get %s dev err.", name);
		return -ENXIO;
	}
	rtnl_lock();
	if (lower->flags & IFF_LOOPBACK) {
		err = -EINVAL;
		do_trace_netlink_extack("Loopback device can't be added as a miwill port");
		printk(KERN_ERR "miwill-e: %s %s, loopback device",
		       "miwill_port_add_by_name", lower->name);
		goto unlock;
	}
	if (lower->priv_flags & IFF_TEAM_PORT) {
		err = -EBUSY;
		do_trace_netlink_extack("Device is already a port of a miwill device");
		printk(KERN_ERR "miwill-e: %s %s, added already",
		       "miwill_port_add_by_name", lower->name);
		goto unlock;
	}
	if (mi->dev == lower) {
		err = -EINVAL;
		do_trace_netlink_extack("cannot enslave miwill device to itself");
		printk(KERN_ERR "miwill-e: %s %s, don't add itself",
		       "miwill_port_add_by_name", lower->name);
		goto unlock;
	}
	err = miwill_dev_type_check_change(mi, lower);
	if (err)
		goto unlock;
	/* kzalloc in place of stock's bare kmalloc: the untouched port fields
	 * would otherwise leak stack garbage into the fixed features path. */
	port = kzalloc(sizeof(*port) + mi->mode->port_priv_size, GFP_KERNEL);
	if (!port) {
		printk(KERN_ERR "miwill-e: %s %s",
		       "miwill_port_add_by_name", lower->name);
		err = -ENOMEM;
		goto unlock;
	}
	port->dev = lower;
	port->miwill = mi;
	port->band = band;
	INIT_LIST_HEAD(&port->queue_list);
	port->original_mtu = lower->mtu;
	err = dev_set_mtu(lower, mi->dev->mtu);
	if (err) {
		printk(KERN_ERR "miwill-e: %s %s, dev_set_mtu",
		       "miwill_port_add_by_name", lower->name);
		goto free_port;
	}
	if (mi->dev)
		dev_hold(mi->dev);
	if (mi->mode && mi->mode->ops->port_enter) {
		err = mi->mode->ops->port_enter(mi, port);
		if (err) {
			printk(KERN_ERR "miwill-e: %s %s, miwill_port_enter",
			       "miwill_port_add_by_name", lower->name);
			goto put_master;
		}
	}
	lower->priv_flags |= IFF_TEAM_PORT;
	if (lower->features & NETIF_F_LRO)
		printk(KERN_ERR "miwill-e: %s need to disable lro",
		       "miwill_port_add_by_name");
	if (lower->flags & IFF_ALLMULTI)
		printk(KERN_ERR "miwill-e: %s need to set allmulti level",
		       "miwill_port_add_by_name");
	err = netdev_rx_handler_register(lower, miwill_handle_frame, port);
	if (err) {
		printk(KERN_ERR "miwill-e: %s %s, netdev_rx_handler_register",
		       "miwill_port_add_by_name", lower->name);
		goto leave_port;
	}
	err = miwill_port_instances_add(mi, port);
	if (err) {
		printk(KERN_ERR "miwill-e: %s %s, __miwill_option_inst_add_port",
		       "miwill_port_add_by_name", lower->name);
		goto unregister_handler;
	}
	netif_addr_lock_bh(mi->dev);
	dev_uc_sync_multiple(lower, mi->dev);
	dev_mc_sync_multiple(lower, mi->dev);
	netif_addr_unlock_bh(mi->dev);
	port->index = -1;
	spin_lock_bh(&mi->lock);
	list_add_tail(&port->list, &mi->ports);
	miwill_port_enable(port);
	__miwill_port_change(port,
		lower->operstate == IF_OPER_UP || lower->operstate == IF_OPER_UNKNOWN);
	spin_unlock_bh(&mi->lock);
	netdev_change_features(mi->dev);
	rtnl_unlock();
	if (lower->dev_addr) {
		memcpy(band == 1 ? mi->local_addr : mi->peer_addr, lower->dev_addr, 6);
		printk(KERN_DEBUG "miwill-d: %s addr:%02x:%02x:%02x:%02x:%02x:%02x",
		       "miwill_port_add_by_name",
		       lower->dev_addr[0], lower->dev_addr[1], lower->dev_addr[2],
		       lower->dev_addr[3], lower->dev_addr[4], lower->dev_addr[5]);
	}
	printk(KERN_INFO "miwill-i: %s %s, ok",
	       "miwill_port_add_by_name", lower->name);
	dev_put(lower);
	return 0;
unregister_handler:
	netdev_rx_handler_unregister(lower);
leave_port:
	lower->priv_flags &= ~IFF_TEAM_PORT;
	if (mi->mode && mi->mode->ops->port_leave)
		mi->mode->ops->port_leave(mi, port);
put_master:
	if (mi->dev)
		dev_put(mi->dev);
	dev_set_mtu(lower, port->original_mtu);
free_port:
	kfree(port);
unlock:
	rtnl_unlock();
	dev_put(lower);
	return err;
}
static void miwill_port_del(struct net_device *dev, struct net_device *lower)
{
	struct miwill *mi = netdev_priv(dev);
	struct miwill_port *port, *other;
	bool found = false;
	if (!(lower->priv_flags & IFF_TEAM_PORT)) {
		printk(KERN_ERR "miwill-e: %s %s, not a port",
		       "__miwill_port_del", lower->name);
		return;
	}
	port = rtnl_dereference(lower->rx_handler_data);
	if (!port) {
		printk(KERN_ERR "miwill-e: %s %s, not a port",
		       "__miwill_port_del", lower->name);
		return;
	}
	list_for_each_entry(other, &mi->ports, list)
		if (other == port) {
			found = true;
			break;
		}
	if (!found) {
		printk(KERN_ERR "miwill-e: %s %s, not a port",
		       "__miwill_port_del", lower->name);
		return;
	}
	spin_lock_bh(&mi->lock);
	miwill_port_disable(port);
	list_del(&port->list);
	spin_unlock_bh(&mi->lock);
	lower->priv_flags &= ~IFF_TEAM_PORT;
	netdev_rx_handler_unregister(lower);
	dev_uc_unsync(lower, mi->dev);
	dev_mc_unsync(lower, mi->dev);
	if (mi->mode && mi->mode->ops->port_leave)
		mi->mode->ops->port_leave(mi, port);
	if (mi->dev)
		dev_put(mi->dev);
	miwill_port_instances_delete(mi, port);
	spin_lock_bh(&mi->lock);
	__miwill_port_change(port, false);
	spin_unlock_bh(&mi->lock);
	dev_set_mtu(lower, port->original_mtu);
	kfree_rcu(port, rcu);
	miwill_features_after_remove(mi);
	netdev_change_features(dev);
	printk(KERN_INFO "miwill-i: %s %s, ok",
	       "__miwill_port_del", lower->name);
}
