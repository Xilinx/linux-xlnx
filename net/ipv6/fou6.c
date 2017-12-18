#include <linux/module.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <net/fou.h>
#include <net/ip.h>
#include <net/ip6_tunnel.h>
#include <net/ip6_checksum.h>
#include <net/protocol.h>
#include <net/udp.h>
#include <net/udp_tunnel.h>

static void fou6_build_udp(struct sk_buff *skb, struct ip_tunnel_encap *e,
			   struct flowi6 *fl6, u8 *protocol, __be16 sport)
{
	struct udphdr *uh;

	skb_push(skb, sizeof(struct udphdr));
	skb_reset_transport_header(skb);

	uh = udp_hdr(skb);

	uh->dest = e->dport;
	uh->source = sport;
	uh->len = htons(skb->len);
	udp6_set_csum(!(e->flags & TUNNEL_ENCAP_FLAG_CSUM6), skb,
		      &fl6->saddr, &fl6->daddr, skb->len);

	*protocol = IPPROTO_UDP;
}

int fou6_build_header(struct sk_buff *skb, struct ip_tunnel_encap *e,
		      u8 *protocol, struct flowi6 *fl6)
{
	__be16 sport;
	int err;
	int type = e->flags & TUNNEL_ENCAP_FLAG_CSUM6 ?
		SKB_GSO_UDP_TUNNEL_CSUM : SKB_GSO_UDP_TUNNEL;

	err = __fou_build_header(skb, e, protocol, &sport, type);
	if (err)
		return err;

	fou6_build_udp(skb, e, fl6, protocol, sport);

	return 0;
}
EXPORT_SYMBOL(fou6_build_header);

int gue6_build_header(struct sk_buff *skb, struct ip_tunnel_encap *e,
		      u8 *protocol, struct flowi6 *fl6)
{
	__be16 sport;
	int err;
	int type = e->flags & TUNNEL_ENCAP_FLAG_CSUM6 ?
		SKB_GSO_UDP_TUNNEL_CSUM : SKB_GSO_UDP_TUNNEL;

	err = __gue_build_header(skb, e, protocol, &sport, type);
	if (err)
		return err;

	fou6_build_udp(skb, e, fl6, protocol, sport);

	return 0;
}
EXPORT_SYMBOL(gue6_build_header);

#if IS_ENABLED(CONFIG_IPV6_FOU_TUNNEL)

static const struct ip6_tnl_encap_ops fou_ip6tun_ops = {
	.encap_hlen = fou_encap_hlen,
	.build_header = fou6_build_header,
};

static const struct ip6_tnl_encap_ops gue_ip6tun_ops = {
	.encap_hlen = gue_encap_hlen,
	.build_header = gue6_build_header,
};

static int ip6_tnl_encap_add_fou_ops(void)
{
	int ret;

	ret = ip6_tnl_encap_add_ops(&fou_ip6tun_ops, TUNNEL_ENCAP_FOU);
	if (ret < 0) {
		pr_err("can't add fou6 ops\n");
		return ret;
	}

	ret = ip6_tnl_encap_add_ops(&gue_ip6tun_ops, TUNNEL_ENCAP_GUE);
	if (ret < 0) {
		pr_err("can't add gue6 ops\n");
		ip6_tnl_encap_del_ops(&fou_ip6tun_ops, TUNNEL_ENCAP_FOU);
		return ret;
	}

	return 0;
}

static void ip6_tnl_encap_del_fou_ops(void)
{
	ip6_tnl_encap_del_ops(&fou_ip6tun_ops, TUNNEL_ENCAP_FOU);
	ip6_tnl_encap_del_ops(&gue_ip6tun_ops, TUNNEL_ENCAP_GUE);
}

#else

static int ip6_tnl_encap_add_fou_ops(void)
{
	return 0;
}

static void ip6_tnl_encap_del_fou_ops(void)
{
}

#endif

static int __init fou6_init(void)
{
	int ret;

	ret = ip6_tnl_encap_add_fou_ops();

	return ret;
}

static void __exit fou6_fini(void)
{
	ip6_tnl_encap_del_fou_ops();
}

module_init(fou6_init);
module_exit(fou6_fini);
MODULE_AUTHOR("Tom Herbert <therbert@google.com>");
MODULE_LICENSE("GPL");
