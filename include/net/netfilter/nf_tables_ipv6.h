#ifndef _NF_TABLES_IPV6_H_
#define _NF_TABLES_IPV6_H_

#include <linux/netfilter_ipv6/ip6_tables.h>
#include <net/ipv6.h>

static inline void
nft_set_pktinfo_ipv6(struct nft_pktinfo *pkt,
		     struct sk_buff *skb,
		     const struct nf_hook_state *state)
{
	int protohdr, thoff = 0;
	unsigned short frag_off;

	nft_set_pktinfo(pkt, skb, state);

	protohdr = ipv6_find_hdr(pkt->skb, &thoff, -1, &frag_off, NULL);
	if (protohdr < 0) {
		nft_set_pktinfo_proto_unspec(pkt, skb);
		return;
	}

	pkt->tprot_set = true;
	pkt->tprot = protohdr;
	pkt->xt.thoff = thoff;
	pkt->xt.fragoff = frag_off;
}

static inline int
__nft_set_pktinfo_ipv6_validate(struct nft_pktinfo *pkt,
				struct sk_buff *skb,
				const struct nf_hook_state *state)
{
#if IS_ENABLED(CONFIG_IPV6)
	struct ipv6hdr *ip6h, _ip6h;
	unsigned int thoff = 0;
	unsigned short frag_off;
	int protohdr;
	u32 pkt_len;

	ip6h = skb_header_pointer(skb, skb_network_offset(skb), sizeof(*ip6h),
				  &_ip6h);
	if (!ip6h)
		return -1;

	if (ip6h->version != 6)
		return -1;

	pkt_len = ntohs(ip6h->payload_len);
	if (pkt_len + sizeof(*ip6h) > skb->len)
		return -1;

	protohdr = ipv6_find_hdr(pkt->skb, &thoff, -1, &frag_off, NULL);
	if (protohdr < 0)
		return -1;

	pkt->tprot_set = true;
	pkt->tprot = protohdr;
	pkt->xt.thoff = thoff;
	pkt->xt.fragoff = frag_off;

	return 0;
#else
	return -1;
#endif
}

static inline void
nft_set_pktinfo_ipv6_validate(struct nft_pktinfo *pkt,
			      struct sk_buff *skb,
			      const struct nf_hook_state *state)
{
	nft_set_pktinfo(pkt, skb, state);
	if (__nft_set_pktinfo_ipv6_validate(pkt, skb, state) < 0)
		nft_set_pktinfo_proto_unspec(pkt, skb);
}

extern struct nft_af_info nft_af_ipv6;

#endif
