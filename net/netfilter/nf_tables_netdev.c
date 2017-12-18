/*
 * Copyright (c) 2015 Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/netfilter/nf_tables.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/netfilter/nf_tables_ipv4.h>
#include <net/netfilter/nf_tables_ipv6.h>

static unsigned int
nft_do_chain_netdev(void *priv, struct sk_buff *skb,
		    const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		nft_set_pktinfo_ipv4_validate(&pkt, skb, state);
		break;
	case htons(ETH_P_IPV6):
		nft_set_pktinfo_ipv6_validate(&pkt, skb, state);
		break;
	default:
		nft_set_pktinfo_unspec(&pkt, skb, state);
		break;
	}

	return nft_do_chain(&pkt, priv);
}

static struct nft_af_info nft_af_netdev __read_mostly = {
	.family		= NFPROTO_NETDEV,
	.nhooks		= NF_NETDEV_NUMHOOKS,
	.owner		= THIS_MODULE,
	.flags		= NFT_AF_NEEDS_DEV,
	.nops		= 1,
	.hooks		= {
		[NF_NETDEV_INGRESS]	= nft_do_chain_netdev,
	},
};

static int nf_tables_netdev_init_net(struct net *net)
{
	net->nft.netdev = kmalloc(sizeof(struct nft_af_info), GFP_KERNEL);
	if (net->nft.netdev == NULL)
		return -ENOMEM;

	memcpy(net->nft.netdev, &nft_af_netdev, sizeof(nft_af_netdev));

	if (nft_register_afinfo(net, net->nft.netdev) < 0)
		goto err;

	return 0;
err:
	kfree(net->nft.netdev);
	return -ENOMEM;
}

static void nf_tables_netdev_exit_net(struct net *net)
{
	nft_unregister_afinfo(net, net->nft.netdev);
	kfree(net->nft.netdev);
}

static struct pernet_operations nf_tables_netdev_net_ops = {
	.init	= nf_tables_netdev_init_net,
	.exit	= nf_tables_netdev_exit_net,
};

static const struct nf_chain_type nft_filter_chain_netdev = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_NETDEV,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_NETDEV_INGRESS),
};

static void nft_netdev_event(unsigned long event, struct net_device *dev,
			     struct nft_ctx *ctx)
{
	struct nft_base_chain *basechain = nft_base_chain(ctx->chain);

	switch (event) {
	case NETDEV_UNREGISTER:
		if (strcmp(basechain->dev_name, dev->name) != 0)
			return;

		__nft_release_basechain(ctx);
		break;
	case NETDEV_CHANGENAME:
		if (dev->ifindex != basechain->ops[0].dev->ifindex)
			return;

		strncpy(basechain->dev_name, dev->name, IFNAMSIZ);
		break;
	}
}

static int nf_tables_netdev_event(struct notifier_block *this,
				  unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct nft_af_info *afi;
	struct nft_table *table;
	struct nft_chain *chain, *nr;
	struct nft_ctx ctx = {
		.net	= dev_net(dev),
	};

	if (event != NETDEV_UNREGISTER &&
	    event != NETDEV_CHANGENAME)
		return NOTIFY_DONE;

	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_for_each_entry(afi, &dev_net(dev)->nft.af_info, list) {
		ctx.afi = afi;
		if (afi->family != NFPROTO_NETDEV)
			continue;

		list_for_each_entry(table, &afi->tables, list) {
			ctx.table = table;
			list_for_each_entry_safe(chain, nr, &table->chains, list) {
				if (!(chain->flags & NFT_BASE_CHAIN))
					continue;

				ctx.chain = chain;
				nft_netdev_event(event, dev, &ctx);
			}
		}
	}
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);

	return NOTIFY_DONE;
}

static struct notifier_block nf_tables_netdev_notifier = {
	.notifier_call	= nf_tables_netdev_event,
};

static int __init nf_tables_netdev_init(void)
{
	int ret;

	ret = nft_register_chain_type(&nft_filter_chain_netdev);
	if (ret)
		return ret;

	ret = register_pernet_subsys(&nf_tables_netdev_net_ops);
	if (ret)
		goto err1;

	ret = register_netdevice_notifier(&nf_tables_netdev_notifier);
	if (ret)
		goto err2;

	return 0;

err2:
	unregister_pernet_subsys(&nf_tables_netdev_net_ops);
err1:
	nft_unregister_chain_type(&nft_filter_chain_netdev);
	return ret;
}

static void __exit nf_tables_netdev_exit(void)
{
	unregister_netdevice_notifier(&nf_tables_netdev_notifier);
	unregister_pernet_subsys(&nf_tables_netdev_net_ops);
	nft_unregister_chain_type(&nft_filter_chain_netdev);
}

module_init(nf_tables_netdev_init);
module_exit(nf_tables_netdev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NFT_FAMILY(5); /* NFPROTO_NETDEV */
