/*
 * Linux Socket Filter - Kernel level socket filtering
 *
 * Based on the design of the Berkeley Packet Filter. The new
 * internal format has been designed by PLUMgrid:
 *
 *	Copyright (c) 2011 - 2014 PLUMgrid, http://plumgrid.com
 *
 * Authors:
 *
 *	Jay Schulist <jschlst@samba.org>
 *	Alexei Starovoitov <ast@plumgrid.com>
 *	Daniel Borkmann <dborkman@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Andi Kleen - Fix a few bad bugs and races.
 * Kris Katterjohn - Added many additional checks in bpf_check_classic()
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_packet.h>
#include <linux/gfp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/netlink.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/flow_dissector.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <linux/filter.h>
#include <linux/ratelimit.h>
#include <linux/seccomp.h>
#include <linux/if_vlan.h>
#include <linux/bpf.h>
#include <net/sch_generic.h>
#include <net/cls_cgroup.h>
#include <net/dst_metadata.h>
#include <net/dst.h>
#include <net/sock_reuseport.h>

/**
 *	sk_filter_trim_cap - run a packet through a socket filter
 *	@sk: sock associated with &sk_buff
 *	@skb: buffer to filter
 *	@cap: limit on how short the eBPF program may trim the packet
 *
 * Run the eBPF program and then cut skb->data to correct size returned by
 * the program. If pkt_len is 0 we toss packet. If skb->len is smaller
 * than pkt_len we keep whole skb->data. This is the socket level
 * wrapper to BPF_PROG_RUN. It returns 0 if the packet should
 * be accepted or -EPERM if the packet should be tossed.
 *
 */
int sk_filter_trim_cap(struct sock *sk, struct sk_buff *skb, unsigned int cap)
{
	int err;
	struct sk_filter *filter;

	/*
	 * If the skb was allocated from pfmemalloc reserves, only
	 * allow SOCK_MEMALLOC sockets to use it as this socket is
	 * helping free memory
	 */
	if (skb_pfmemalloc(skb) && !sock_flag(sk, SOCK_MEMALLOC))
		return -ENOMEM;

	err = security_sock_rcv_skb(sk, skb);
	if (err)
		return err;

	rcu_read_lock();
	filter = rcu_dereference(sk->sk_filter);
	if (filter) {
		unsigned int pkt_len = bpf_prog_run_save_cb(filter->prog, skb);
		err = pkt_len ? pskb_trim(skb, max(cap, pkt_len)) : -EPERM;
	}
	rcu_read_unlock();

	return err;
}
EXPORT_SYMBOL(sk_filter_trim_cap);

BPF_CALL_1(__skb_get_pay_offset, struct sk_buff *, skb)
{
	return skb_get_poff(skb);
}

BPF_CALL_3(__skb_get_nlattr, struct sk_buff *, skb, u32, a, u32, x)
{
	struct nlattr *nla;

	if (skb_is_nonlinear(skb))
		return 0;

	if (skb->len < sizeof(struct nlattr))
		return 0;

	if (a > skb->len - sizeof(struct nlattr))
		return 0;

	nla = nla_find((struct nlattr *) &skb->data[a], skb->len - a, x);
	if (nla)
		return (void *) nla - (void *) skb->data;

	return 0;
}

BPF_CALL_3(__skb_get_nlattr_nest, struct sk_buff *, skb, u32, a, u32, x)
{
	struct nlattr *nla;

	if (skb_is_nonlinear(skb))
		return 0;

	if (skb->len < sizeof(struct nlattr))
		return 0;

	if (a > skb->len - sizeof(struct nlattr))
		return 0;

	nla = (struct nlattr *) &skb->data[a];
	if (nla->nla_len > skb->len - a)
		return 0;

	nla = nla_find_nested(nla, x);
	if (nla)
		return (void *) nla - (void *) skb->data;

	return 0;
}

BPF_CALL_0(__get_raw_cpu_id)
{
	return raw_smp_processor_id();
}

static const struct bpf_func_proto bpf_get_raw_smp_processor_id_proto = {
	.func		= __get_raw_cpu_id,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

static u32 convert_skb_access(int skb_field, int dst_reg, int src_reg,
			      struct bpf_insn *insn_buf)
{
	struct bpf_insn *insn = insn_buf;

	switch (skb_field) {
	case SKF_AD_MARK:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, mark) != 4);

		*insn++ = BPF_LDX_MEM(BPF_W, dst_reg, src_reg,
				      offsetof(struct sk_buff, mark));
		break;

	case SKF_AD_PKTTYPE:
		*insn++ = BPF_LDX_MEM(BPF_B, dst_reg, src_reg, PKT_TYPE_OFFSET());
		*insn++ = BPF_ALU32_IMM(BPF_AND, dst_reg, PKT_TYPE_MAX);
#ifdef __BIG_ENDIAN_BITFIELD
		*insn++ = BPF_ALU32_IMM(BPF_RSH, dst_reg, 5);
#endif
		break;

	case SKF_AD_QUEUE:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, queue_mapping) != 2);

		*insn++ = BPF_LDX_MEM(BPF_H, dst_reg, src_reg,
				      offsetof(struct sk_buff, queue_mapping));
		break;

	case SKF_AD_VLAN_TAG:
	case SKF_AD_VLAN_TAG_PRESENT:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, vlan_tci) != 2);
		BUILD_BUG_ON(VLAN_TAG_PRESENT != 0x1000);

		/* dst_reg = *(u16 *) (src_reg + offsetof(vlan_tci)) */
		*insn++ = BPF_LDX_MEM(BPF_H, dst_reg, src_reg,
				      offsetof(struct sk_buff, vlan_tci));
		if (skb_field == SKF_AD_VLAN_TAG) {
			*insn++ = BPF_ALU32_IMM(BPF_AND, dst_reg,
						~VLAN_TAG_PRESENT);
		} else {
			/* dst_reg >>= 12 */
			*insn++ = BPF_ALU32_IMM(BPF_RSH, dst_reg, 12);
			/* dst_reg &= 1 */
			*insn++ = BPF_ALU32_IMM(BPF_AND, dst_reg, 1);
		}
		break;
	}

	return insn - insn_buf;
}

static bool convert_bpf_extensions(struct sock_filter *fp,
				   struct bpf_insn **insnp)
{
	struct bpf_insn *insn = *insnp;
	u32 cnt;

	switch (fp->k) {
	case SKF_AD_OFF + SKF_AD_PROTOCOL:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, protocol) != 2);

		/* A = *(u16 *) (CTX + offsetof(protocol)) */
		*insn++ = BPF_LDX_MEM(BPF_H, BPF_REG_A, BPF_REG_CTX,
				      offsetof(struct sk_buff, protocol));
		/* A = ntohs(A) [emitting a nop or swap16] */
		*insn = BPF_ENDIAN(BPF_FROM_BE, BPF_REG_A, 16);
		break;

	case SKF_AD_OFF + SKF_AD_PKTTYPE:
		cnt = convert_skb_access(SKF_AD_PKTTYPE, BPF_REG_A, BPF_REG_CTX, insn);
		insn += cnt - 1;
		break;

	case SKF_AD_OFF + SKF_AD_IFINDEX:
	case SKF_AD_OFF + SKF_AD_HATYPE:
		BUILD_BUG_ON(FIELD_SIZEOF(struct net_device, ifindex) != 4);
		BUILD_BUG_ON(FIELD_SIZEOF(struct net_device, type) != 2);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, dev),
				      BPF_REG_TMP, BPF_REG_CTX,
				      offsetof(struct sk_buff, dev));
		/* if (tmp != 0) goto pc + 1 */
		*insn++ = BPF_JMP_IMM(BPF_JNE, BPF_REG_TMP, 0, 1);
		*insn++ = BPF_EXIT_INSN();
		if (fp->k == SKF_AD_OFF + SKF_AD_IFINDEX)
			*insn = BPF_LDX_MEM(BPF_W, BPF_REG_A, BPF_REG_TMP,
					    offsetof(struct net_device, ifindex));
		else
			*insn = BPF_LDX_MEM(BPF_H, BPF_REG_A, BPF_REG_TMP,
					    offsetof(struct net_device, type));
		break;

	case SKF_AD_OFF + SKF_AD_MARK:
		cnt = convert_skb_access(SKF_AD_MARK, BPF_REG_A, BPF_REG_CTX, insn);
		insn += cnt - 1;
		break;

	case SKF_AD_OFF + SKF_AD_RXHASH:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, hash) != 4);

		*insn = BPF_LDX_MEM(BPF_W, BPF_REG_A, BPF_REG_CTX,
				    offsetof(struct sk_buff, hash));
		break;

	case SKF_AD_OFF + SKF_AD_QUEUE:
		cnt = convert_skb_access(SKF_AD_QUEUE, BPF_REG_A, BPF_REG_CTX, insn);
		insn += cnt - 1;
		break;

	case SKF_AD_OFF + SKF_AD_VLAN_TAG:
		cnt = convert_skb_access(SKF_AD_VLAN_TAG,
					 BPF_REG_A, BPF_REG_CTX, insn);
		insn += cnt - 1;
		break;

	case SKF_AD_OFF + SKF_AD_VLAN_TAG_PRESENT:
		cnt = convert_skb_access(SKF_AD_VLAN_TAG_PRESENT,
					 BPF_REG_A, BPF_REG_CTX, insn);
		insn += cnt - 1;
		break;

	case SKF_AD_OFF + SKF_AD_VLAN_TPID:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, vlan_proto) != 2);

		/* A = *(u16 *) (CTX + offsetof(vlan_proto)) */
		*insn++ = BPF_LDX_MEM(BPF_H, BPF_REG_A, BPF_REG_CTX,
				      offsetof(struct sk_buff, vlan_proto));
		/* A = ntohs(A) [emitting a nop or swap16] */
		*insn = BPF_ENDIAN(BPF_FROM_BE, BPF_REG_A, 16);
		break;

	case SKF_AD_OFF + SKF_AD_PAY_OFFSET:
	case SKF_AD_OFF + SKF_AD_NLATTR:
	case SKF_AD_OFF + SKF_AD_NLATTR_NEST:
	case SKF_AD_OFF + SKF_AD_CPU:
	case SKF_AD_OFF + SKF_AD_RANDOM:
		/* arg1 = CTX */
		*insn++ = BPF_MOV64_REG(BPF_REG_ARG1, BPF_REG_CTX);
		/* arg2 = A */
		*insn++ = BPF_MOV64_REG(BPF_REG_ARG2, BPF_REG_A);
		/* arg3 = X */
		*insn++ = BPF_MOV64_REG(BPF_REG_ARG3, BPF_REG_X);
		/* Emit call(arg1=CTX, arg2=A, arg3=X) */
		switch (fp->k) {
		case SKF_AD_OFF + SKF_AD_PAY_OFFSET:
			*insn = BPF_EMIT_CALL(__skb_get_pay_offset);
			break;
		case SKF_AD_OFF + SKF_AD_NLATTR:
			*insn = BPF_EMIT_CALL(__skb_get_nlattr);
			break;
		case SKF_AD_OFF + SKF_AD_NLATTR_NEST:
			*insn = BPF_EMIT_CALL(__skb_get_nlattr_nest);
			break;
		case SKF_AD_OFF + SKF_AD_CPU:
			*insn = BPF_EMIT_CALL(__get_raw_cpu_id);
			break;
		case SKF_AD_OFF + SKF_AD_RANDOM:
			*insn = BPF_EMIT_CALL(bpf_user_rnd_u32);
			bpf_user_rnd_init_once();
			break;
		}
		break;

	case SKF_AD_OFF + SKF_AD_ALU_XOR_X:
		/* A ^= X */
		*insn = BPF_ALU32_REG(BPF_XOR, BPF_REG_A, BPF_REG_X);
		break;

	default:
		/* This is just a dummy call to avoid letting the compiler
		 * evict __bpf_call_base() as an optimization. Placed here
		 * where no-one bothers.
		 */
		BUG_ON(__bpf_call_base(0, 0, 0, 0, 0) != 0);
		return false;
	}

	*insnp = insn;
	return true;
}

/**
 *	bpf_convert_filter - convert filter program
 *	@prog: the user passed filter program
 *	@len: the length of the user passed filter program
 *	@new_prog: buffer where converted program will be stored
 *	@new_len: pointer to store length of converted program
 *
 * Remap 'sock_filter' style BPF instruction set to 'sock_filter_ext' style.
 * Conversion workflow:
 *
 * 1) First pass for calculating the new program length:
 *   bpf_convert_filter(old_prog, old_len, NULL, &new_len)
 *
 * 2) 2nd pass to remap in two passes: 1st pass finds new
 *    jump offsets, 2nd pass remapping:
 *   new_prog = kmalloc(sizeof(struct bpf_insn) * new_len);
 *   bpf_convert_filter(old_prog, old_len, new_prog, &new_len);
 */
static int bpf_convert_filter(struct sock_filter *prog, int len,
			      struct bpf_insn *new_prog, int *new_len)
{
	int new_flen = 0, pass = 0, target, i;
	struct bpf_insn *new_insn;
	struct sock_filter *fp;
	int *addrs = NULL;
	u8 bpf_src;

	BUILD_BUG_ON(BPF_MEMWORDS * sizeof(u32) > MAX_BPF_STACK);
	BUILD_BUG_ON(BPF_REG_FP + 1 != MAX_BPF_REG);

	if (len <= 0 || len > BPF_MAXINSNS)
		return -EINVAL;

	if (new_prog) {
		addrs = kcalloc(len, sizeof(*addrs),
				GFP_KERNEL | __GFP_NOWARN);
		if (!addrs)
			return -ENOMEM;
	}

do_pass:
	new_insn = new_prog;
	fp = prog;

	/* Classic BPF related prologue emission. */
	if (new_insn) {
		/* Classic BPF expects A and X to be reset first. These need
		 * to be guaranteed to be the first two instructions.
		 */
		*new_insn++ = BPF_ALU64_REG(BPF_XOR, BPF_REG_A, BPF_REG_A);
		*new_insn++ = BPF_ALU64_REG(BPF_XOR, BPF_REG_X, BPF_REG_X);

		/* All programs must keep CTX in callee saved BPF_REG_CTX.
		 * In eBPF case it's done by the compiler, here we need to
		 * do this ourself. Initial CTX is present in BPF_REG_ARG1.
		 */
		*new_insn++ = BPF_MOV64_REG(BPF_REG_CTX, BPF_REG_ARG1);
	} else {
		new_insn += 3;
	}

	for (i = 0; i < len; fp++, i++) {
		struct bpf_insn tmp_insns[6] = { };
		struct bpf_insn *insn = tmp_insns;

		if (addrs)
			addrs[i] = new_insn - new_prog;

		switch (fp->code) {
		/* All arithmetic insns and skb loads map as-is. */
		case BPF_ALU | BPF_ADD | BPF_X:
		case BPF_ALU | BPF_ADD | BPF_K:
		case BPF_ALU | BPF_SUB | BPF_X:
		case BPF_ALU | BPF_SUB | BPF_K:
		case BPF_ALU | BPF_AND | BPF_X:
		case BPF_ALU | BPF_AND | BPF_K:
		case BPF_ALU | BPF_OR | BPF_X:
		case BPF_ALU | BPF_OR | BPF_K:
		case BPF_ALU | BPF_LSH | BPF_X:
		case BPF_ALU | BPF_LSH | BPF_K:
		case BPF_ALU | BPF_RSH | BPF_X:
		case BPF_ALU | BPF_RSH | BPF_K:
		case BPF_ALU | BPF_XOR | BPF_X:
		case BPF_ALU | BPF_XOR | BPF_K:
		case BPF_ALU | BPF_MUL | BPF_X:
		case BPF_ALU | BPF_MUL | BPF_K:
		case BPF_ALU | BPF_DIV | BPF_X:
		case BPF_ALU | BPF_DIV | BPF_K:
		case BPF_ALU | BPF_MOD | BPF_X:
		case BPF_ALU | BPF_MOD | BPF_K:
		case BPF_ALU | BPF_NEG:
		case BPF_LD | BPF_ABS | BPF_W:
		case BPF_LD | BPF_ABS | BPF_H:
		case BPF_LD | BPF_ABS | BPF_B:
		case BPF_LD | BPF_IND | BPF_W:
		case BPF_LD | BPF_IND | BPF_H:
		case BPF_LD | BPF_IND | BPF_B:
			/* Check for overloaded BPF extension and
			 * directly convert it if found, otherwise
			 * just move on with mapping.
			 */
			if (BPF_CLASS(fp->code) == BPF_LD &&
			    BPF_MODE(fp->code) == BPF_ABS &&
			    convert_bpf_extensions(fp, &insn))
				break;

			*insn = BPF_RAW_INSN(fp->code, BPF_REG_A, BPF_REG_X, 0, fp->k);
			break;

		/* Jump transformation cannot use BPF block macros
		 * everywhere as offset calculation and target updates
		 * require a bit more work than the rest, i.e. jump
		 * opcodes map as-is, but offsets need adjustment.
		 */

#define BPF_EMIT_JMP							\
	do {								\
		if (target >= len || target < 0)			\
			goto err;					\
		insn->off = addrs ? addrs[target] - addrs[i] - 1 : 0;	\
		/* Adjust pc relative offset for 2nd or 3rd insn. */	\
		insn->off -= insn - tmp_insns;				\
	} while (0)

		case BPF_JMP | BPF_JA:
			target = i + fp->k + 1;
			insn->code = fp->code;
			BPF_EMIT_JMP;
			break;

		case BPF_JMP | BPF_JEQ | BPF_K:
		case BPF_JMP | BPF_JEQ | BPF_X:
		case BPF_JMP | BPF_JSET | BPF_K:
		case BPF_JMP | BPF_JSET | BPF_X:
		case BPF_JMP | BPF_JGT | BPF_K:
		case BPF_JMP | BPF_JGT | BPF_X:
		case BPF_JMP | BPF_JGE | BPF_K:
		case BPF_JMP | BPF_JGE | BPF_X:
			if (BPF_SRC(fp->code) == BPF_K && (int) fp->k < 0) {
				/* BPF immediates are signed, zero extend
				 * immediate into tmp register and use it
				 * in compare insn.
				 */
				*insn++ = BPF_MOV32_IMM(BPF_REG_TMP, fp->k);

				insn->dst_reg = BPF_REG_A;
				insn->src_reg = BPF_REG_TMP;
				bpf_src = BPF_X;
			} else {
				insn->dst_reg = BPF_REG_A;
				insn->imm = fp->k;
				bpf_src = BPF_SRC(fp->code);
				insn->src_reg = bpf_src == BPF_X ? BPF_REG_X : 0;
			}

			/* Common case where 'jump_false' is next insn. */
			if (fp->jf == 0) {
				insn->code = BPF_JMP | BPF_OP(fp->code) | bpf_src;
				target = i + fp->jt + 1;
				BPF_EMIT_JMP;
				break;
			}

			/* Convert JEQ into JNE when 'jump_true' is next insn. */
			if (fp->jt == 0 && BPF_OP(fp->code) == BPF_JEQ) {
				insn->code = BPF_JMP | BPF_JNE | bpf_src;
				target = i + fp->jf + 1;
				BPF_EMIT_JMP;
				break;
			}

			/* Other jumps are mapped into two insns: Jxx and JA. */
			target = i + fp->jt + 1;
			insn->code = BPF_JMP | BPF_OP(fp->code) | bpf_src;
			BPF_EMIT_JMP;
			insn++;

			insn->code = BPF_JMP | BPF_JA;
			target = i + fp->jf + 1;
			BPF_EMIT_JMP;
			break;

		/* ldxb 4 * ([14] & 0xf) is remaped into 6 insns. */
		case BPF_LDX | BPF_MSH | BPF_B:
			/* tmp = A */
			*insn++ = BPF_MOV64_REG(BPF_REG_TMP, BPF_REG_A);
			/* A = BPF_R0 = *(u8 *) (skb->data + K) */
			*insn++ = BPF_LD_ABS(BPF_B, fp->k);
			/* A &= 0xf */
			*insn++ = BPF_ALU32_IMM(BPF_AND, BPF_REG_A, 0xf);
			/* A <<= 2 */
			*insn++ = BPF_ALU32_IMM(BPF_LSH, BPF_REG_A, 2);
			/* X = A */
			*insn++ = BPF_MOV64_REG(BPF_REG_X, BPF_REG_A);
			/* A = tmp */
			*insn = BPF_MOV64_REG(BPF_REG_A, BPF_REG_TMP);
			break;

		/* RET_K is remaped into 2 insns. RET_A case doesn't need an
		 * extra mov as BPF_REG_0 is already mapped into BPF_REG_A.
		 */
		case BPF_RET | BPF_A:
		case BPF_RET | BPF_K:
			if (BPF_RVAL(fp->code) == BPF_K)
				*insn++ = BPF_MOV32_RAW(BPF_K, BPF_REG_0,
							0, fp->k);
			*insn = BPF_EXIT_INSN();
			break;

		/* Store to stack. */
		case BPF_ST:
		case BPF_STX:
			*insn = BPF_STX_MEM(BPF_W, BPF_REG_FP, BPF_CLASS(fp->code) ==
					    BPF_ST ? BPF_REG_A : BPF_REG_X,
					    -(BPF_MEMWORDS - fp->k) * 4);
			break;

		/* Load from stack. */
		case BPF_LD | BPF_MEM:
		case BPF_LDX | BPF_MEM:
			*insn = BPF_LDX_MEM(BPF_W, BPF_CLASS(fp->code) == BPF_LD  ?
					    BPF_REG_A : BPF_REG_X, BPF_REG_FP,
					    -(BPF_MEMWORDS - fp->k) * 4);
			break;

		/* A = K or X = K */
		case BPF_LD | BPF_IMM:
		case BPF_LDX | BPF_IMM:
			*insn = BPF_MOV32_IMM(BPF_CLASS(fp->code) == BPF_LD ?
					      BPF_REG_A : BPF_REG_X, fp->k);
			break;

		/* X = A */
		case BPF_MISC | BPF_TAX:
			*insn = BPF_MOV64_REG(BPF_REG_X, BPF_REG_A);
			break;

		/* A = X */
		case BPF_MISC | BPF_TXA:
			*insn = BPF_MOV64_REG(BPF_REG_A, BPF_REG_X);
			break;

		/* A = skb->len or X = skb->len */
		case BPF_LD | BPF_W | BPF_LEN:
		case BPF_LDX | BPF_W | BPF_LEN:
			*insn = BPF_LDX_MEM(BPF_W, BPF_CLASS(fp->code) == BPF_LD ?
					    BPF_REG_A : BPF_REG_X, BPF_REG_CTX,
					    offsetof(struct sk_buff, len));
			break;

		/* Access seccomp_data fields. */
		case BPF_LDX | BPF_ABS | BPF_W:
			/* A = *(u32 *) (ctx + K) */
			*insn = BPF_LDX_MEM(BPF_W, BPF_REG_A, BPF_REG_CTX, fp->k);
			break;

		/* Unknown instruction. */
		default:
			goto err;
		}

		insn++;
		if (new_prog)
			memcpy(new_insn, tmp_insns,
			       sizeof(*insn) * (insn - tmp_insns));
		new_insn += insn - tmp_insns;
	}

	if (!new_prog) {
		/* Only calculating new length. */
		*new_len = new_insn - new_prog;
		return 0;
	}

	pass++;
	if (new_flen != new_insn - new_prog) {
		new_flen = new_insn - new_prog;
		if (pass > 2)
			goto err;
		goto do_pass;
	}

	kfree(addrs);
	BUG_ON(*new_len != new_flen);
	return 0;
err:
	kfree(addrs);
	return -EINVAL;
}

/* Security:
 *
 * As we dont want to clear mem[] array for each packet going through
 * __bpf_prog_run(), we check that filter loaded by user never try to read
 * a cell if not previously written, and we check all branches to be sure
 * a malicious user doesn't try to abuse us.
 */
static int check_load_and_stores(const struct sock_filter *filter, int flen)
{
	u16 *masks, memvalid = 0; /* One bit per cell, 16 cells */
	int pc, ret = 0;

	BUILD_BUG_ON(BPF_MEMWORDS > 16);

	masks = kmalloc_array(flen, sizeof(*masks), GFP_KERNEL);
	if (!masks)
		return -ENOMEM;

	memset(masks, 0xff, flen * sizeof(*masks));

	for (pc = 0; pc < flen; pc++) {
		memvalid &= masks[pc];

		switch (filter[pc].code) {
		case BPF_ST:
		case BPF_STX:
			memvalid |= (1 << filter[pc].k);
			break;
		case BPF_LD | BPF_MEM:
		case BPF_LDX | BPF_MEM:
			if (!(memvalid & (1 << filter[pc].k))) {
				ret = -EINVAL;
				goto error;
			}
			break;
		case BPF_JMP | BPF_JA:
			/* A jump must set masks on target */
			masks[pc + 1 + filter[pc].k] &= memvalid;
			memvalid = ~0;
			break;
		case BPF_JMP | BPF_JEQ | BPF_K:
		case BPF_JMP | BPF_JEQ | BPF_X:
		case BPF_JMP | BPF_JGE | BPF_K:
		case BPF_JMP | BPF_JGE | BPF_X:
		case BPF_JMP | BPF_JGT | BPF_K:
		case BPF_JMP | BPF_JGT | BPF_X:
		case BPF_JMP | BPF_JSET | BPF_K:
		case BPF_JMP | BPF_JSET | BPF_X:
			/* A jump must set masks on targets */
			masks[pc + 1 + filter[pc].jt] &= memvalid;
			masks[pc + 1 + filter[pc].jf] &= memvalid;
			memvalid = ~0;
			break;
		}
	}
error:
	kfree(masks);
	return ret;
}

static bool chk_code_allowed(u16 code_to_probe)
{
	static const bool codes[] = {
		/* 32 bit ALU operations */
		[BPF_ALU | BPF_ADD | BPF_K] = true,
		[BPF_ALU | BPF_ADD | BPF_X] = true,
		[BPF_ALU | BPF_SUB | BPF_K] = true,
		[BPF_ALU | BPF_SUB | BPF_X] = true,
		[BPF_ALU | BPF_MUL | BPF_K] = true,
		[BPF_ALU | BPF_MUL | BPF_X] = true,
		[BPF_ALU | BPF_DIV | BPF_K] = true,
		[BPF_ALU | BPF_DIV | BPF_X] = true,
		[BPF_ALU | BPF_MOD | BPF_K] = true,
		[BPF_ALU | BPF_MOD | BPF_X] = true,
		[BPF_ALU | BPF_AND | BPF_K] = true,
		[BPF_ALU | BPF_AND | BPF_X] = true,
		[BPF_ALU | BPF_OR | BPF_K] = true,
		[BPF_ALU | BPF_OR | BPF_X] = true,
		[BPF_ALU | BPF_XOR | BPF_K] = true,
		[BPF_ALU | BPF_XOR | BPF_X] = true,
		[BPF_ALU | BPF_LSH | BPF_K] = true,
		[BPF_ALU | BPF_LSH | BPF_X] = true,
		[BPF_ALU | BPF_RSH | BPF_K] = true,
		[BPF_ALU | BPF_RSH | BPF_X] = true,
		[BPF_ALU | BPF_NEG] = true,
		/* Load instructions */
		[BPF_LD | BPF_W | BPF_ABS] = true,
		[BPF_LD | BPF_H | BPF_ABS] = true,
		[BPF_LD | BPF_B | BPF_ABS] = true,
		[BPF_LD | BPF_W | BPF_LEN] = true,
		[BPF_LD | BPF_W | BPF_IND] = true,
		[BPF_LD | BPF_H | BPF_IND] = true,
		[BPF_LD | BPF_B | BPF_IND] = true,
		[BPF_LD | BPF_IMM] = true,
		[BPF_LD | BPF_MEM] = true,
		[BPF_LDX | BPF_W | BPF_LEN] = true,
		[BPF_LDX | BPF_B | BPF_MSH] = true,
		[BPF_LDX | BPF_IMM] = true,
		[BPF_LDX | BPF_MEM] = true,
		/* Store instructions */
		[BPF_ST] = true,
		[BPF_STX] = true,
		/* Misc instructions */
		[BPF_MISC | BPF_TAX] = true,
		[BPF_MISC | BPF_TXA] = true,
		/* Return instructions */
		[BPF_RET | BPF_K] = true,
		[BPF_RET | BPF_A] = true,
		/* Jump instructions */
		[BPF_JMP | BPF_JA] = true,
		[BPF_JMP | BPF_JEQ | BPF_K] = true,
		[BPF_JMP | BPF_JEQ | BPF_X] = true,
		[BPF_JMP | BPF_JGE | BPF_K] = true,
		[BPF_JMP | BPF_JGE | BPF_X] = true,
		[BPF_JMP | BPF_JGT | BPF_K] = true,
		[BPF_JMP | BPF_JGT | BPF_X] = true,
		[BPF_JMP | BPF_JSET | BPF_K] = true,
		[BPF_JMP | BPF_JSET | BPF_X] = true,
	};

	if (code_to_probe >= ARRAY_SIZE(codes))
		return false;

	return codes[code_to_probe];
}

static bool bpf_check_basics_ok(const struct sock_filter *filter,
				unsigned int flen)
{
	if (filter == NULL)
		return false;
	if (flen == 0 || flen > BPF_MAXINSNS)
		return false;

	return true;
}

/**
 *	bpf_check_classic - verify socket filter code
 *	@filter: filter to verify
 *	@flen: length of filter
 *
 * Check the user's filter code. If we let some ugly
 * filter code slip through kaboom! The filter must contain
 * no references or jumps that are out of range, no illegal
 * instructions, and must end with a RET instruction.
 *
 * All jumps are forward as they are not signed.
 *
 * Returns 0 if the rule set is legal or -EINVAL if not.
 */
static int bpf_check_classic(const struct sock_filter *filter,
			     unsigned int flen)
{
	bool anc_found;
	int pc;

	/* Check the filter code now */
	for (pc = 0; pc < flen; pc++) {
		const struct sock_filter *ftest = &filter[pc];

		/* May we actually operate on this code? */
		if (!chk_code_allowed(ftest->code))
			return -EINVAL;

		/* Some instructions need special checks */
		switch (ftest->code) {
		case BPF_ALU | BPF_DIV | BPF_K:
		case BPF_ALU | BPF_MOD | BPF_K:
			/* Check for division by zero */
			if (ftest->k == 0)
				return -EINVAL;
			break;
		case BPF_ALU | BPF_LSH | BPF_K:
		case BPF_ALU | BPF_RSH | BPF_K:
			if (ftest->k >= 32)
				return -EINVAL;
			break;
		case BPF_LD | BPF_MEM:
		case BPF_LDX | BPF_MEM:
		case BPF_ST:
		case BPF_STX:
			/* Check for invalid memory addresses */
			if (ftest->k >= BPF_MEMWORDS)
				return -EINVAL;
			break;
		case BPF_JMP | BPF_JA:
			/* Note, the large ftest->k might cause loops.
			 * Compare this with conditional jumps below,
			 * where offsets are limited. --ANK (981016)
			 */
			if (ftest->k >= (unsigned int)(flen - pc - 1))
				return -EINVAL;
			break;
		case BPF_JMP | BPF_JEQ | BPF_K:
		case BPF_JMP | BPF_JEQ | BPF_X:
		case BPF_JMP | BPF_JGE | BPF_K:
		case BPF_JMP | BPF_JGE | BPF_X:
		case BPF_JMP | BPF_JGT | BPF_K:
		case BPF_JMP | BPF_JGT | BPF_X:
		case BPF_JMP | BPF_JSET | BPF_K:
		case BPF_JMP | BPF_JSET | BPF_X:
			/* Both conditionals must be safe */
			if (pc + ftest->jt + 1 >= flen ||
			    pc + ftest->jf + 1 >= flen)
				return -EINVAL;
			break;
		case BPF_LD | BPF_W | BPF_ABS:
		case BPF_LD | BPF_H | BPF_ABS:
		case BPF_LD | BPF_B | BPF_ABS:
			anc_found = false;
			if (bpf_anc_helper(ftest) & BPF_ANC)
				anc_found = true;
			/* Ancillary operation unknown or unsupported */
			if (anc_found == false && ftest->k >= SKF_AD_OFF)
				return -EINVAL;
		}
	}

	/* Last instruction must be a RET code */
	switch (filter[flen - 1].code) {
	case BPF_RET | BPF_K:
	case BPF_RET | BPF_A:
		return check_load_and_stores(filter, flen);
	}

	return -EINVAL;
}

static int bpf_prog_store_orig_filter(struct bpf_prog *fp,
				      const struct sock_fprog *fprog)
{
	unsigned int fsize = bpf_classic_proglen(fprog);
	struct sock_fprog_kern *fkprog;

	fp->orig_prog = kmalloc(sizeof(*fkprog), GFP_KERNEL);
	if (!fp->orig_prog)
		return -ENOMEM;

	fkprog = fp->orig_prog;
	fkprog->len = fprog->len;

	fkprog->filter = kmemdup(fp->insns, fsize,
				 GFP_KERNEL | __GFP_NOWARN);
	if (!fkprog->filter) {
		kfree(fp->orig_prog);
		return -ENOMEM;
	}

	return 0;
}

static void bpf_release_orig_filter(struct bpf_prog *fp)
{
	struct sock_fprog_kern *fprog = fp->orig_prog;

	if (fprog) {
		kfree(fprog->filter);
		kfree(fprog);
	}
}

static void __bpf_prog_release(struct bpf_prog *prog)
{
	if (prog->type == BPF_PROG_TYPE_SOCKET_FILTER) {
		bpf_prog_put(prog);
	} else {
		bpf_release_orig_filter(prog);
		bpf_prog_free(prog);
	}
}

static void __sk_filter_release(struct sk_filter *fp)
{
	__bpf_prog_release(fp->prog);
	kfree(fp);
}

/**
 * 	sk_filter_release_rcu - Release a socket filter by rcu_head
 *	@rcu: rcu_head that contains the sk_filter to free
 */
static void sk_filter_release_rcu(struct rcu_head *rcu)
{
	struct sk_filter *fp = container_of(rcu, struct sk_filter, rcu);

	__sk_filter_release(fp);
}

/**
 *	sk_filter_release - release a socket filter
 *	@fp: filter to remove
 *
 *	Remove a filter from a socket and release its resources.
 */
static void sk_filter_release(struct sk_filter *fp)
{
	if (atomic_dec_and_test(&fp->refcnt))
		call_rcu(&fp->rcu, sk_filter_release_rcu);
}

void sk_filter_uncharge(struct sock *sk, struct sk_filter *fp)
{
	u32 filter_size = bpf_prog_size(fp->prog->len);

	atomic_sub(filter_size, &sk->sk_omem_alloc);
	sk_filter_release(fp);
}

/* try to charge the socket memory if there is space available
 * return true on success
 */
bool sk_filter_charge(struct sock *sk, struct sk_filter *fp)
{
	u32 filter_size = bpf_prog_size(fp->prog->len);

	/* same check as in sock_kmalloc() */
	if (filter_size <= sysctl_optmem_max &&
	    atomic_read(&sk->sk_omem_alloc) + filter_size < sysctl_optmem_max) {
		atomic_inc(&fp->refcnt);
		atomic_add(filter_size, &sk->sk_omem_alloc);
		return true;
	}
	return false;
}

static struct bpf_prog *bpf_migrate_filter(struct bpf_prog *fp)
{
	struct sock_filter *old_prog;
	struct bpf_prog *old_fp;
	int err, new_len, old_len = fp->len;

	/* We are free to overwrite insns et al right here as it
	 * won't be used at this point in time anymore internally
	 * after the migration to the internal BPF instruction
	 * representation.
	 */
	BUILD_BUG_ON(sizeof(struct sock_filter) !=
		     sizeof(struct bpf_insn));

	/* Conversion cannot happen on overlapping memory areas,
	 * so we need to keep the user BPF around until the 2nd
	 * pass. At this time, the user BPF is stored in fp->insns.
	 */
	old_prog = kmemdup(fp->insns, old_len * sizeof(struct sock_filter),
			   GFP_KERNEL | __GFP_NOWARN);
	if (!old_prog) {
		err = -ENOMEM;
		goto out_err;
	}

	/* 1st pass: calculate the new program length. */
	err = bpf_convert_filter(old_prog, old_len, NULL, &new_len);
	if (err)
		goto out_err_free;

	/* Expand fp for appending the new filter representation. */
	old_fp = fp;
	fp = bpf_prog_realloc(old_fp, bpf_prog_size(new_len), 0);
	if (!fp) {
		/* The old_fp is still around in case we couldn't
		 * allocate new memory, so uncharge on that one.
		 */
		fp = old_fp;
		err = -ENOMEM;
		goto out_err_free;
	}

	fp->len = new_len;

	/* 2nd pass: remap sock_filter insns into bpf_insn insns. */
	err = bpf_convert_filter(old_prog, old_len, fp->insnsi, &new_len);
	if (err)
		/* 2nd bpf_convert_filter() can fail only if it fails
		 * to allocate memory, remapping must succeed. Note,
		 * that at this time old_fp has already been released
		 * by krealloc().
		 */
		goto out_err_free;

	/* We are guaranteed to never error here with cBPF to eBPF
	 * transitions, since there's no issue with type compatibility
	 * checks on program arrays.
	 */
	fp = bpf_prog_select_runtime(fp, &err);

	kfree(old_prog);
	return fp;

out_err_free:
	kfree(old_prog);
out_err:
	__bpf_prog_release(fp);
	return ERR_PTR(err);
}

static struct bpf_prog *bpf_prepare_filter(struct bpf_prog *fp,
					   bpf_aux_classic_check_t trans)
{
	int err;

	fp->bpf_func = NULL;
	fp->jited = 0;

	err = bpf_check_classic(fp->insns, fp->len);
	if (err) {
		__bpf_prog_release(fp);
		return ERR_PTR(err);
	}

	/* There might be additional checks and transformations
	 * needed on classic filters, f.e. in case of seccomp.
	 */
	if (trans) {
		err = trans(fp->insns, fp->len);
		if (err) {
			__bpf_prog_release(fp);
			return ERR_PTR(err);
		}
	}

	/* Probe if we can JIT compile the filter and if so, do
	 * the compilation of the filter.
	 */
	bpf_jit_compile(fp);

	/* JIT compiler couldn't process this filter, so do the
	 * internal BPF translation for the optimized interpreter.
	 */
	if (!fp->jited)
		fp = bpf_migrate_filter(fp);

	return fp;
}

/**
 *	bpf_prog_create - create an unattached filter
 *	@pfp: the unattached filter that is created
 *	@fprog: the filter program
 *
 * Create a filter independent of any socket. We first run some
 * sanity checks on it to make sure it does not explode on us later.
 * If an error occurs or there is insufficient memory for the filter
 * a negative errno code is returned. On success the return is zero.
 */
int bpf_prog_create(struct bpf_prog **pfp, struct sock_fprog_kern *fprog)
{
	unsigned int fsize = bpf_classic_proglen(fprog);
	struct bpf_prog *fp;

	/* Make sure new filter is there and in the right amounts. */
	if (!bpf_check_basics_ok(fprog->filter, fprog->len))
		return -EINVAL;

	fp = bpf_prog_alloc(bpf_prog_size(fprog->len), 0);
	if (!fp)
		return -ENOMEM;

	memcpy(fp->insns, fprog->filter, fsize);

	fp->len = fprog->len;
	/* Since unattached filters are not copied back to user
	 * space through sk_get_filter(), we do not need to hold
	 * a copy here, and can spare us the work.
	 */
	fp->orig_prog = NULL;

	/* bpf_prepare_filter() already takes care of freeing
	 * memory in case something goes wrong.
	 */
	fp = bpf_prepare_filter(fp, NULL);
	if (IS_ERR(fp))
		return PTR_ERR(fp);

	*pfp = fp;
	return 0;
}
EXPORT_SYMBOL_GPL(bpf_prog_create);

/**
 *	bpf_prog_create_from_user - create an unattached filter from user buffer
 *	@pfp: the unattached filter that is created
 *	@fprog: the filter program
 *	@trans: post-classic verifier transformation handler
 *	@save_orig: save classic BPF program
 *
 * This function effectively does the same as bpf_prog_create(), only
 * that it builds up its insns buffer from user space provided buffer.
 * It also allows for passing a bpf_aux_classic_check_t handler.
 */
int bpf_prog_create_from_user(struct bpf_prog **pfp, struct sock_fprog *fprog,
			      bpf_aux_classic_check_t trans, bool save_orig)
{
	unsigned int fsize = bpf_classic_proglen(fprog);
	struct bpf_prog *fp;
	int err;

	/* Make sure new filter is there and in the right amounts. */
	if (!bpf_check_basics_ok(fprog->filter, fprog->len))
		return -EINVAL;

	fp = bpf_prog_alloc(bpf_prog_size(fprog->len), 0);
	if (!fp)
		return -ENOMEM;

	if (copy_from_user(fp->insns, fprog->filter, fsize)) {
		__bpf_prog_free(fp);
		return -EFAULT;
	}

	fp->len = fprog->len;
	fp->orig_prog = NULL;

	if (save_orig) {
		err = bpf_prog_store_orig_filter(fp, fprog);
		if (err) {
			__bpf_prog_free(fp);
			return -ENOMEM;
		}
	}

	/* bpf_prepare_filter() already takes care of freeing
	 * memory in case something goes wrong.
	 */
	fp = bpf_prepare_filter(fp, trans);
	if (IS_ERR(fp))
		return PTR_ERR(fp);

	*pfp = fp;
	return 0;
}
EXPORT_SYMBOL_GPL(bpf_prog_create_from_user);

void bpf_prog_destroy(struct bpf_prog *fp)
{
	__bpf_prog_release(fp);
}
EXPORT_SYMBOL_GPL(bpf_prog_destroy);

static int __sk_attach_prog(struct bpf_prog *prog, struct sock *sk)
{
	struct sk_filter *fp, *old_fp;

	fp = kmalloc(sizeof(*fp), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	fp->prog = prog;
	atomic_set(&fp->refcnt, 0);

	if (!sk_filter_charge(sk, fp)) {
		kfree(fp);
		return -ENOMEM;
	}

	old_fp = rcu_dereference_protected(sk->sk_filter,
					   lockdep_sock_is_held(sk));
	rcu_assign_pointer(sk->sk_filter, fp);

	if (old_fp)
		sk_filter_uncharge(sk, old_fp);

	return 0;
}

static int __reuseport_attach_prog(struct bpf_prog *prog, struct sock *sk)
{
	struct bpf_prog *old_prog;
	int err;

	if (bpf_prog_size(prog->len) > sysctl_optmem_max)
		return -ENOMEM;

	if (sk_unhashed(sk) && sk->sk_reuseport) {
		err = reuseport_alloc(sk);
		if (err)
			return err;
	} else if (!rcu_access_pointer(sk->sk_reuseport_cb)) {
		/* The socket wasn't bound with SO_REUSEPORT */
		return -EINVAL;
	}

	old_prog = reuseport_attach_prog(sk, prog);
	if (old_prog)
		bpf_prog_destroy(old_prog);

	return 0;
}

static
struct bpf_prog *__get_filter(struct sock_fprog *fprog, struct sock *sk)
{
	unsigned int fsize = bpf_classic_proglen(fprog);
	struct bpf_prog *prog;
	int err;

	if (sock_flag(sk, SOCK_FILTER_LOCKED))
		return ERR_PTR(-EPERM);

	/* Make sure new filter is there and in the right amounts. */
	if (!bpf_check_basics_ok(fprog->filter, fprog->len))
		return ERR_PTR(-EINVAL);

	prog = bpf_prog_alloc(bpf_prog_size(fprog->len), 0);
	if (!prog)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(prog->insns, fprog->filter, fsize)) {
		__bpf_prog_free(prog);
		return ERR_PTR(-EFAULT);
	}

	prog->len = fprog->len;

	err = bpf_prog_store_orig_filter(prog, fprog);
	if (err) {
		__bpf_prog_free(prog);
		return ERR_PTR(-ENOMEM);
	}

	/* bpf_prepare_filter() already takes care of freeing
	 * memory in case something goes wrong.
	 */
	return bpf_prepare_filter(prog, NULL);
}

/**
 *	sk_attach_filter - attach a socket filter
 *	@fprog: the filter program
 *	@sk: the socket to use
 *
 * Attach the user's filter code. We first run some sanity checks on
 * it to make sure it does not explode on us later. If an error
 * occurs or there is insufficient memory for the filter a negative
 * errno code is returned. On success the return is zero.
 */
int sk_attach_filter(struct sock_fprog *fprog, struct sock *sk)
{
	struct bpf_prog *prog = __get_filter(fprog, sk);
	int err;

	if (IS_ERR(prog))
		return PTR_ERR(prog);

	err = __sk_attach_prog(prog, sk);
	if (err < 0) {
		__bpf_prog_release(prog);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sk_attach_filter);

int sk_reuseport_attach_filter(struct sock_fprog *fprog, struct sock *sk)
{
	struct bpf_prog *prog = __get_filter(fprog, sk);
	int err;

	if (IS_ERR(prog))
		return PTR_ERR(prog);

	err = __reuseport_attach_prog(prog, sk);
	if (err < 0) {
		__bpf_prog_release(prog);
		return err;
	}

	return 0;
}

static struct bpf_prog *__get_bpf(u32 ufd, struct sock *sk)
{
	if (sock_flag(sk, SOCK_FILTER_LOCKED))
		return ERR_PTR(-EPERM);

	return bpf_prog_get_type(ufd, BPF_PROG_TYPE_SOCKET_FILTER);
}

int sk_attach_bpf(u32 ufd, struct sock *sk)
{
	struct bpf_prog *prog = __get_bpf(ufd, sk);
	int err;

	if (IS_ERR(prog))
		return PTR_ERR(prog);

	err = __sk_attach_prog(prog, sk);
	if (err < 0) {
		bpf_prog_put(prog);
		return err;
	}

	return 0;
}

int sk_reuseport_attach_bpf(u32 ufd, struct sock *sk)
{
	struct bpf_prog *prog = __get_bpf(ufd, sk);
	int err;

	if (IS_ERR(prog))
		return PTR_ERR(prog);

	err = __reuseport_attach_prog(prog, sk);
	if (err < 0) {
		bpf_prog_put(prog);
		return err;
	}

	return 0;
}

struct bpf_scratchpad {
	union {
		__be32 diff[MAX_BPF_STACK / sizeof(__be32)];
		u8     buff[MAX_BPF_STACK];
	};
};

static DEFINE_PER_CPU(struct bpf_scratchpad, bpf_sp);

static inline int __bpf_try_make_writable(struct sk_buff *skb,
					  unsigned int write_len)
{
	return skb_ensure_writable(skb, write_len);
}

static inline int bpf_try_make_writable(struct sk_buff *skb,
					unsigned int write_len)
{
	int err = __bpf_try_make_writable(skb, write_len);

	bpf_compute_data_end(skb);
	return err;
}

static int bpf_try_make_head_writable(struct sk_buff *skb)
{
	return bpf_try_make_writable(skb, skb_headlen(skb));
}

static inline void bpf_push_mac_rcsum(struct sk_buff *skb)
{
	if (skb_at_tc_ingress(skb))
		skb_postpush_rcsum(skb, skb_mac_header(skb), skb->mac_len);
}

static inline void bpf_pull_mac_rcsum(struct sk_buff *skb)
{
	if (skb_at_tc_ingress(skb))
		skb_postpull_rcsum(skb, skb_mac_header(skb), skb->mac_len);
}

BPF_CALL_5(bpf_skb_store_bytes, struct sk_buff *, skb, u32, offset,
	   const void *, from, u32, len, u64, flags)
{
	void *ptr;

	if (unlikely(flags & ~(BPF_F_RECOMPUTE_CSUM | BPF_F_INVALIDATE_HASH)))
		return -EINVAL;
	if (unlikely(offset > 0xffff))
		return -EFAULT;
	if (unlikely(bpf_try_make_writable(skb, offset + len)))
		return -EFAULT;

	ptr = skb->data + offset;
	if (flags & BPF_F_RECOMPUTE_CSUM)
		__skb_postpull_rcsum(skb, ptr, len, offset);

	memcpy(ptr, from, len);

	if (flags & BPF_F_RECOMPUTE_CSUM)
		__skb_postpush_rcsum(skb, ptr, len, offset);
	if (flags & BPF_F_INVALIDATE_HASH)
		skb_clear_hash(skb);

	return 0;
}

static const struct bpf_func_proto bpf_skb_store_bytes_proto = {
	.func		= bpf_skb_store_bytes,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_STACK,
	.arg4_type	= ARG_CONST_STACK_SIZE,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_skb_load_bytes, const struct sk_buff *, skb, u32, offset,
	   void *, to, u32, len)
{
	void *ptr;

	if (unlikely(offset > 0xffff))
		goto err_clear;

	ptr = skb_header_pointer(skb, offset, len, to);
	if (unlikely(!ptr))
		goto err_clear;
	if (ptr != to)
		memcpy(to, ptr, len);

	return 0;
err_clear:
	memset(to, 0, len);
	return -EFAULT;
}

static const struct bpf_func_proto bpf_skb_load_bytes_proto = {
	.func		= bpf_skb_load_bytes,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_RAW_STACK,
	.arg4_type	= ARG_CONST_STACK_SIZE,
};

BPF_CALL_2(bpf_skb_pull_data, struct sk_buff *, skb, u32, len)
{
	/* Idea is the following: should the needed direct read/write
	 * test fail during runtime, we can pull in more data and redo
	 * again, since implicitly, we invalidate previous checks here.
	 *
	 * Or, since we know how much we need to make read/writeable,
	 * this can be done once at the program beginning for direct
	 * access case. By this we overcome limitations of only current
	 * headroom being accessible.
	 */
	return bpf_try_make_writable(skb, len ? : skb_headlen(skb));
}

static const struct bpf_func_proto bpf_skb_pull_data_proto = {
	.func		= bpf_skb_pull_data,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_5(bpf_l3_csum_replace, struct sk_buff *, skb, u32, offset,
	   u64, from, u64, to, u64, flags)
{
	__sum16 *ptr;

	if (unlikely(flags & ~(BPF_F_HDR_FIELD_MASK)))
		return -EINVAL;
	if (unlikely(offset > 0xffff || offset & 1))
		return -EFAULT;
	if (unlikely(bpf_try_make_writable(skb, offset + sizeof(*ptr))))
		return -EFAULT;

	ptr = (__sum16 *)(skb->data + offset);
	switch (flags & BPF_F_HDR_FIELD_MASK) {
	case 0:
		if (unlikely(from != 0))
			return -EINVAL;

		csum_replace_by_diff(ptr, to);
		break;
	case 2:
		csum_replace2(ptr, from, to);
		break;
	case 4:
		csum_replace4(ptr, from, to);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct bpf_func_proto bpf_l3_csum_replace_proto = {
	.func		= bpf_l3_csum_replace,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_ANYTHING,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_5(bpf_l4_csum_replace, struct sk_buff *, skb, u32, offset,
	   u64, from, u64, to, u64, flags)
{
	bool is_pseudo = flags & BPF_F_PSEUDO_HDR;
	bool is_mmzero = flags & BPF_F_MARK_MANGLED_0;
	__sum16 *ptr;

	if (unlikely(flags & ~(BPF_F_MARK_MANGLED_0 | BPF_F_PSEUDO_HDR |
			       BPF_F_HDR_FIELD_MASK)))
		return -EINVAL;
	if (unlikely(offset > 0xffff || offset & 1))
		return -EFAULT;
	if (unlikely(bpf_try_make_writable(skb, offset + sizeof(*ptr))))
		return -EFAULT;

	ptr = (__sum16 *)(skb->data + offset);
	if (is_mmzero && !*ptr)
		return 0;

	switch (flags & BPF_F_HDR_FIELD_MASK) {
	case 0:
		if (unlikely(from != 0))
			return -EINVAL;

		inet_proto_csum_replace_by_diff(ptr, skb, to, is_pseudo);
		break;
	case 2:
		inet_proto_csum_replace2(ptr, skb, from, to, is_pseudo);
		break;
	case 4:
		inet_proto_csum_replace4(ptr, skb, from, to, is_pseudo);
		break;
	default:
		return -EINVAL;
	}

	if (is_mmzero && !*ptr)
		*ptr = CSUM_MANGLED_0;
	return 0;
}

static const struct bpf_func_proto bpf_l4_csum_replace_proto = {
	.func		= bpf_l4_csum_replace,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_ANYTHING,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_5(bpf_csum_diff, __be32 *, from, u32, from_size,
	   __be32 *, to, u32, to_size, __wsum, seed)
{
	struct bpf_scratchpad *sp = this_cpu_ptr(&bpf_sp);
	u32 diff_size = from_size + to_size;
	int i, j = 0;

	/* This is quite flexible, some examples:
	 *
	 * from_size == 0, to_size > 0,  seed := csum --> pushing data
	 * from_size > 0,  to_size == 0, seed := csum --> pulling data
	 * from_size > 0,  to_size > 0,  seed := 0    --> diffing data
	 *
	 * Even for diffing, from_size and to_size don't need to be equal.
	 */
	if (unlikely(((from_size | to_size) & (sizeof(__be32) - 1)) ||
		     diff_size > sizeof(sp->diff)))
		return -EINVAL;

	for (i = 0; i < from_size / sizeof(__be32); i++, j++)
		sp->diff[j] = ~from[i];
	for (i = 0; i <   to_size / sizeof(__be32); i++, j++)
		sp->diff[j] = to[i];

	return csum_partial(sp->diff, diff_size, seed);
}

static const struct bpf_func_proto bpf_csum_diff_proto = {
	.func		= bpf_csum_diff,
	.gpl_only	= false,
	.pkt_access	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_STACK,
	.arg2_type	= ARG_CONST_STACK_SIZE_OR_ZERO,
	.arg3_type	= ARG_PTR_TO_STACK,
	.arg4_type	= ARG_CONST_STACK_SIZE_OR_ZERO,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_2(bpf_csum_update, struct sk_buff *, skb, __wsum, csum)
{
	/* The interface is to be used in combination with bpf_csum_diff()
	 * for direct packet writes. csum rotation for alignment as well
	 * as emulating csum_sub() can be done from the eBPF program.
	 */
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		return (skb->csum = csum_add(skb->csum, csum));

	return -ENOTSUPP;
}

static const struct bpf_func_proto bpf_csum_update_proto = {
	.func		= bpf_csum_update,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
};

static inline int __bpf_rx_skb(struct net_device *dev, struct sk_buff *skb)
{
	return dev_forward_skb(dev, skb);
}

static inline int __bpf_rx_skb_no_mac(struct net_device *dev,
				      struct sk_buff *skb)
{
	int ret = ____dev_forward_skb(dev, skb);

	if (likely(!ret)) {
		skb->dev = dev;
		ret = netif_rx(skb);
	}

	return ret;
}

static inline int __bpf_tx_skb(struct net_device *dev, struct sk_buff *skb)
{
	int ret;

	if (unlikely(__this_cpu_read(xmit_recursion) > XMIT_RECURSION_LIMIT)) {
		net_crit_ratelimited("bpf: recursion limit reached on datapath, buggy bpf program?\n");
		kfree_skb(skb);
		return -ENETDOWN;
	}

	skb->dev = dev;

	__this_cpu_inc(xmit_recursion);
	ret = dev_queue_xmit(skb);
	__this_cpu_dec(xmit_recursion);

	return ret;
}

static int __bpf_redirect_no_mac(struct sk_buff *skb, struct net_device *dev,
				 u32 flags)
{
	/* skb->mac_len is not set on normal egress */
	unsigned int mlen = skb->network_header - skb->mac_header;

	__skb_pull(skb, mlen);

	/* At ingress, the mac header has already been pulled once.
	 * At egress, skb_pospull_rcsum has to be done in case that
	 * the skb is originated from ingress (i.e. a forwarded skb)
	 * to ensure that rcsum starts at net header.
	 */
	if (!skb_at_tc_ingress(skb))
		skb_postpull_rcsum(skb, skb_mac_header(skb), mlen);
	skb_pop_mac_header(skb);
	skb_reset_mac_len(skb);
	return flags & BPF_F_INGRESS ?
	       __bpf_rx_skb_no_mac(dev, skb) : __bpf_tx_skb(dev, skb);
}

static int __bpf_redirect_common(struct sk_buff *skb, struct net_device *dev,
				 u32 flags)
{
	bpf_push_mac_rcsum(skb);
	return flags & BPF_F_INGRESS ?
	       __bpf_rx_skb(dev, skb) : __bpf_tx_skb(dev, skb);
}

static int __bpf_redirect(struct sk_buff *skb, struct net_device *dev,
			  u32 flags)
{
	switch (dev->type) {
	case ARPHRD_TUNNEL:
	case ARPHRD_TUNNEL6:
	case ARPHRD_SIT:
	case ARPHRD_IPGRE:
	case ARPHRD_VOID:
	case ARPHRD_NONE:
		return __bpf_redirect_no_mac(skb, dev, flags);
	default:
		return __bpf_redirect_common(skb, dev, flags);
	}
}

BPF_CALL_3(bpf_clone_redirect, struct sk_buff *, skb, u32, ifindex, u64, flags)
{
	struct net_device *dev;
	struct sk_buff *clone;
	int ret;

	if (unlikely(flags & ~(BPF_F_INGRESS)))
		return -EINVAL;

	dev = dev_get_by_index_rcu(dev_net(skb->dev), ifindex);
	if (unlikely(!dev))
		return -EINVAL;

	clone = skb_clone(skb, GFP_ATOMIC);
	if (unlikely(!clone))
		return -ENOMEM;

	/* For direct write, we need to keep the invariant that the skbs
	 * we're dealing with need to be uncloned. Should uncloning fail
	 * here, we need to free the just generated clone to unclone once
	 * again.
	 */
	ret = bpf_try_make_head_writable(skb);
	if (unlikely(ret)) {
		kfree_skb(clone);
		return -ENOMEM;
	}

	return __bpf_redirect(clone, dev, flags);
}

static const struct bpf_func_proto bpf_clone_redirect_proto = {
	.func           = bpf_clone_redirect,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
	.arg2_type      = ARG_ANYTHING,
	.arg3_type      = ARG_ANYTHING,
};

struct redirect_info {
	u32 ifindex;
	u32 flags;
};

static DEFINE_PER_CPU(struct redirect_info, redirect_info);

BPF_CALL_2(bpf_redirect, u32, ifindex, u64, flags)
{
	struct redirect_info *ri = this_cpu_ptr(&redirect_info);

	if (unlikely(flags & ~(BPF_F_INGRESS)))
		return TC_ACT_SHOT;

	ri->ifindex = ifindex;
	ri->flags = flags;

	return TC_ACT_REDIRECT;
}

int skb_do_redirect(struct sk_buff *skb)
{
	struct redirect_info *ri = this_cpu_ptr(&redirect_info);
	struct net_device *dev;

	dev = dev_get_by_index_rcu(dev_net(skb->dev), ri->ifindex);
	ri->ifindex = 0;
	if (unlikely(!dev)) {
		kfree_skb(skb);
		return -EINVAL;
	}

	return __bpf_redirect(skb, dev, ri->flags);
}

static const struct bpf_func_proto bpf_redirect_proto = {
	.func           = bpf_redirect,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_ANYTHING,
	.arg2_type      = ARG_ANYTHING,
};

BPF_CALL_1(bpf_get_cgroup_classid, const struct sk_buff *, skb)
{
	return task_get_classid(skb);
}

static const struct bpf_func_proto bpf_get_cgroup_classid_proto = {
	.func           = bpf_get_cgroup_classid,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_get_route_realm, const struct sk_buff *, skb)
{
	return dst_tclassid(skb);
}

static const struct bpf_func_proto bpf_get_route_realm_proto = {
	.func           = bpf_get_route_realm,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_get_hash_recalc, struct sk_buff *, skb)
{
	/* If skb_clear_hash() was called due to mangling, we can
	 * trigger SW recalculation here. Later access to hash
	 * can then use the inline skb->hash via context directly
	 * instead of calling this helper again.
	 */
	return skb_get_hash(skb);
}

static const struct bpf_func_proto bpf_get_hash_recalc_proto = {
	.func		= bpf_get_hash_recalc,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_set_hash_invalid, struct sk_buff *, skb)
{
	/* After all direct packet write, this can be used once for
	 * triggering a lazy recalc on next skb_get_hash() invocation.
	 */
	skb_clear_hash(skb);
	return 0;
}

static const struct bpf_func_proto bpf_set_hash_invalid_proto = {
	.func		= bpf_set_hash_invalid,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

BPF_CALL_3(bpf_skb_vlan_push, struct sk_buff *, skb, __be16, vlan_proto,
	   u16, vlan_tci)
{
	int ret;

	if (unlikely(vlan_proto != htons(ETH_P_8021Q) &&
		     vlan_proto != htons(ETH_P_8021AD)))
		vlan_proto = htons(ETH_P_8021Q);

	bpf_push_mac_rcsum(skb);
	ret = skb_vlan_push(skb, vlan_proto, vlan_tci);
	bpf_pull_mac_rcsum(skb);

	bpf_compute_data_end(skb);
	return ret;
}

const struct bpf_func_proto bpf_skb_vlan_push_proto = {
	.func           = bpf_skb_vlan_push,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
	.arg2_type      = ARG_ANYTHING,
	.arg3_type      = ARG_ANYTHING,
};
EXPORT_SYMBOL_GPL(bpf_skb_vlan_push_proto);

BPF_CALL_1(bpf_skb_vlan_pop, struct sk_buff *, skb)
{
	int ret;

	bpf_push_mac_rcsum(skb);
	ret = skb_vlan_pop(skb);
	bpf_pull_mac_rcsum(skb);

	bpf_compute_data_end(skb);
	return ret;
}

const struct bpf_func_proto bpf_skb_vlan_pop_proto = {
	.func           = bpf_skb_vlan_pop,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
};
EXPORT_SYMBOL_GPL(bpf_skb_vlan_pop_proto);

static int bpf_skb_generic_push(struct sk_buff *skb, u32 off, u32 len)
{
	/* Caller already did skb_cow() with len as headroom,
	 * so no need to do it here.
	 */
	skb_push(skb, len);
	memmove(skb->data, skb->data + len, off);
	memset(skb->data + off, 0, len);

	/* No skb_postpush_rcsum(skb, skb->data + off, len)
	 * needed here as it does not change the skb->csum
	 * result for checksum complete when summing over
	 * zeroed blocks.
	 */
	return 0;
}

static int bpf_skb_generic_pop(struct sk_buff *skb, u32 off, u32 len)
{
	/* skb_ensure_writable() is not needed here, as we're
	 * already working on an uncloned skb.
	 */
	if (unlikely(!pskb_may_pull(skb, off + len)))
		return -ENOMEM;

	skb_postpull_rcsum(skb, skb->data + off, len);
	memmove(skb->data + len, skb->data, off);
	__skb_pull(skb, len);

	return 0;
}

static int bpf_skb_net_hdr_push(struct sk_buff *skb, u32 off, u32 len)
{
	bool trans_same = skb->transport_header == skb->network_header;
	int ret;

	/* There's no need for __skb_push()/__skb_pull() pair to
	 * get to the start of the mac header as we're guaranteed
	 * to always start from here under eBPF.
	 */
	ret = bpf_skb_generic_push(skb, off, len);
	if (likely(!ret)) {
		skb->mac_header -= len;
		skb->network_header -= len;
		if (trans_same)
			skb->transport_header = skb->network_header;
	}

	return ret;
}

static int bpf_skb_net_hdr_pop(struct sk_buff *skb, u32 off, u32 len)
{
	bool trans_same = skb->transport_header == skb->network_header;
	int ret;

	/* Same here, __skb_push()/__skb_pull() pair not needed. */
	ret = bpf_skb_generic_pop(skb, off, len);
	if (likely(!ret)) {
		skb->mac_header += len;
		skb->network_header += len;
		if (trans_same)
			skb->transport_header = skb->network_header;
	}

	return ret;
}

static int bpf_skb_proto_4_to_6(struct sk_buff *skb)
{
	const u32 len_diff = sizeof(struct ipv6hdr) - sizeof(struct iphdr);
	u32 off = skb->network_header - skb->mac_header;
	int ret;

	ret = skb_cow(skb, len_diff);
	if (unlikely(ret < 0))
		return ret;

	ret = bpf_skb_net_hdr_push(skb, off, len_diff);
	if (unlikely(ret < 0))
		return ret;

	if (skb_is_gso(skb)) {
		/* SKB_GSO_UDP stays as is. SKB_GSO_TCPV4 needs to
		 * be changed into SKB_GSO_TCPV6.
		 */
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4) {
			skb_shinfo(skb)->gso_type &= ~SKB_GSO_TCPV4;
			skb_shinfo(skb)->gso_type |=  SKB_GSO_TCPV6;
		}

		/* Due to IPv6 header, MSS needs to be downgraded. */
		skb_shinfo(skb)->gso_size -= len_diff;
		/* Header must be checked, and gso_segs recomputed. */
		skb_shinfo(skb)->gso_type |= SKB_GSO_DODGY;
		skb_shinfo(skb)->gso_segs = 0;
	}

	skb->protocol = htons(ETH_P_IPV6);
	skb_clear_hash(skb);

	return 0;
}

static int bpf_skb_proto_6_to_4(struct sk_buff *skb)
{
	const u32 len_diff = sizeof(struct ipv6hdr) - sizeof(struct iphdr);
	u32 off = skb->network_header - skb->mac_header;
	int ret;

	ret = skb_unclone(skb, GFP_ATOMIC);
	if (unlikely(ret < 0))
		return ret;

	ret = bpf_skb_net_hdr_pop(skb, off, len_diff);
	if (unlikely(ret < 0))
		return ret;

	if (skb_is_gso(skb)) {
		/* SKB_GSO_UDP stays as is. SKB_GSO_TCPV6 needs to
		 * be changed into SKB_GSO_TCPV4.
		 */
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6) {
			skb_shinfo(skb)->gso_type &= ~SKB_GSO_TCPV6;
			skb_shinfo(skb)->gso_type |=  SKB_GSO_TCPV4;
		}

		/* Due to IPv4 header, MSS can be upgraded. */
		skb_shinfo(skb)->gso_size += len_diff;
		/* Header must be checked, and gso_segs recomputed. */
		skb_shinfo(skb)->gso_type |= SKB_GSO_DODGY;
		skb_shinfo(skb)->gso_segs = 0;
	}

	skb->protocol = htons(ETH_P_IP);
	skb_clear_hash(skb);

	return 0;
}

static int bpf_skb_proto_xlat(struct sk_buff *skb, __be16 to_proto)
{
	__be16 from_proto = skb->protocol;

	if (from_proto == htons(ETH_P_IP) &&
	      to_proto == htons(ETH_P_IPV6))
		return bpf_skb_proto_4_to_6(skb);

	if (from_proto == htons(ETH_P_IPV6) &&
	      to_proto == htons(ETH_P_IP))
		return bpf_skb_proto_6_to_4(skb);

	return -ENOTSUPP;
}

BPF_CALL_3(bpf_skb_change_proto, struct sk_buff *, skb, __be16, proto,
	   u64, flags)
{
	int ret;

	if (unlikely(flags))
		return -EINVAL;

	/* General idea is that this helper does the basic groundwork
	 * needed for changing the protocol, and eBPF program fills the
	 * rest through bpf_skb_store_bytes(), bpf_lX_csum_replace()
	 * and other helpers, rather than passing a raw buffer here.
	 *
	 * The rationale is to keep this minimal and without a need to
	 * deal with raw packet data. F.e. even if we would pass buffers
	 * here, the program still needs to call the bpf_lX_csum_replace()
	 * helpers anyway. Plus, this way we keep also separation of
	 * concerns, since f.e. bpf_skb_store_bytes() should only take
	 * care of stores.
	 *
	 * Currently, additional options and extension header space are
	 * not supported, but flags register is reserved so we can adapt
	 * that. For offloads, we mark packet as dodgy, so that headers
	 * need to be verified first.
	 */
	ret = bpf_skb_proto_xlat(skb, proto);
	bpf_compute_data_end(skb);
	return ret;
}

static const struct bpf_func_proto bpf_skb_change_proto_proto = {
	.func		= bpf_skb_change_proto,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
};

BPF_CALL_2(bpf_skb_change_type, struct sk_buff *, skb, u32, pkt_type)
{
	/* We only allow a restricted subset to be changed for now. */
	if (unlikely(!skb_pkt_type_ok(skb->pkt_type) ||
		     !skb_pkt_type_ok(pkt_type)))
		return -EINVAL;

	skb->pkt_type = pkt_type;
	return 0;
}

static const struct bpf_func_proto bpf_skb_change_type_proto = {
	.func		= bpf_skb_change_type,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
};

static u32 __bpf_skb_min_len(const struct sk_buff *skb)
{
	u32 min_len = skb_network_offset(skb);

	if (skb_transport_header_was_set(skb))
		min_len = skb_transport_offset(skb);
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		min_len = skb_checksum_start_offset(skb) +
			  skb->csum_offset + sizeof(__sum16);
	return min_len;
}

static u32 __bpf_skb_max_len(const struct sk_buff *skb)
{
	return skb->dev->mtu + skb->dev->hard_header_len;
}

static int bpf_skb_grow_rcsum(struct sk_buff *skb, unsigned int new_len)
{
	unsigned int old_len = skb->len;
	int ret;

	ret = __skb_grow_rcsum(skb, new_len);
	if (!ret)
		memset(skb->data + old_len, 0, new_len - old_len);
	return ret;
}

static int bpf_skb_trim_rcsum(struct sk_buff *skb, unsigned int new_len)
{
	return __skb_trim_rcsum(skb, new_len);
}

BPF_CALL_3(bpf_skb_change_tail, struct sk_buff *, skb, u32, new_len,
	   u64, flags)
{
	u32 max_len = __bpf_skb_max_len(skb);
	u32 min_len = __bpf_skb_min_len(skb);
	int ret;

	if (unlikely(flags || new_len > max_len || new_len < min_len))
		return -EINVAL;
	if (skb->encapsulation)
		return -ENOTSUPP;

	/* The basic idea of this helper is that it's performing the
	 * needed work to either grow or trim an skb, and eBPF program
	 * rewrites the rest via helpers like bpf_skb_store_bytes(),
	 * bpf_lX_csum_replace() and others rather than passing a raw
	 * buffer here. This one is a slow path helper and intended
	 * for replies with control messages.
	 *
	 * Like in bpf_skb_change_proto(), we want to keep this rather
	 * minimal and without protocol specifics so that we are able
	 * to separate concerns as in bpf_skb_store_bytes() should only
	 * be the one responsible for writing buffers.
	 *
	 * It's really expected to be a slow path operation here for
	 * control message replies, so we're implicitly linearizing,
	 * uncloning and drop offloads from the skb by this.
	 */
	ret = __bpf_try_make_writable(skb, skb->len);
	if (!ret) {
		if (new_len > skb->len)
			ret = bpf_skb_grow_rcsum(skb, new_len);
		else if (new_len < skb->len)
			ret = bpf_skb_trim_rcsum(skb, new_len);
		if (!ret && skb_is_gso(skb))
			skb_gso_reset(skb);
	}

	bpf_compute_data_end(skb);
	return ret;
}

static const struct bpf_func_proto bpf_skb_change_tail_proto = {
	.func		= bpf_skb_change_tail,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
};

bool bpf_helper_changes_skb_data(void *func)
{
	if (func == bpf_skb_vlan_push ||
	    func == bpf_skb_vlan_pop ||
	    func == bpf_skb_store_bytes ||
	    func == bpf_skb_change_proto ||
	    func == bpf_skb_change_tail ||
	    func == bpf_skb_pull_data ||
	    func == bpf_l3_csum_replace ||
	    func == bpf_l4_csum_replace)
		return true;

	return false;
}

static unsigned long bpf_skb_copy(void *dst_buff, const void *skb,
				  unsigned long off, unsigned long len)
{
	void *ptr = skb_header_pointer(skb, off, len, dst_buff);

	if (unlikely(!ptr))
		return len;
	if (ptr != dst_buff)
		memcpy(dst_buff, ptr, len);

	return 0;
}

BPF_CALL_5(bpf_skb_event_output, struct sk_buff *, skb, struct bpf_map *, map,
	   u64, flags, void *, meta, u64, meta_size)
{
	u64 skb_size = (flags & BPF_F_CTXLEN_MASK) >> 32;

	if (unlikely(flags & ~(BPF_F_CTXLEN_MASK | BPF_F_INDEX_MASK)))
		return -EINVAL;
	if (unlikely(skb_size > skb->len))
		return -EFAULT;

	return bpf_event_output(map, flags, meta, meta_size, skb, skb_size,
				bpf_skb_copy);
}

static const struct bpf_func_proto bpf_skb_event_output_proto = {
	.func		= bpf_skb_event_output,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_STACK,
	.arg5_type	= ARG_CONST_STACK_SIZE,
};

static unsigned short bpf_tunnel_key_af(u64 flags)
{
	return flags & BPF_F_TUNINFO_IPV6 ? AF_INET6 : AF_INET;
}

BPF_CALL_4(bpf_skb_get_tunnel_key, struct sk_buff *, skb, struct bpf_tunnel_key *, to,
	   u32, size, u64, flags)
{
	const struct ip_tunnel_info *info = skb_tunnel_info(skb);
	u8 compat[sizeof(struct bpf_tunnel_key)];
	void *to_orig = to;
	int err;

	if (unlikely(!info || (flags & ~(BPF_F_TUNINFO_IPV6)))) {
		err = -EINVAL;
		goto err_clear;
	}
	if (ip_tunnel_info_af(info) != bpf_tunnel_key_af(flags)) {
		err = -EPROTO;
		goto err_clear;
	}
	if (unlikely(size != sizeof(struct bpf_tunnel_key))) {
		err = -EINVAL;
		switch (size) {
		case offsetof(struct bpf_tunnel_key, tunnel_label):
		case offsetof(struct bpf_tunnel_key, tunnel_ext):
			goto set_compat;
		case offsetof(struct bpf_tunnel_key, remote_ipv6[1]):
			/* Fixup deprecated structure layouts here, so we have
			 * a common path later on.
			 */
			if (ip_tunnel_info_af(info) != AF_INET)
				goto err_clear;
set_compat:
			to = (struct bpf_tunnel_key *)compat;
			break;
		default:
			goto err_clear;
		}
	}

	to->tunnel_id = be64_to_cpu(info->key.tun_id);
	to->tunnel_tos = info->key.tos;
	to->tunnel_ttl = info->key.ttl;

	if (flags & BPF_F_TUNINFO_IPV6) {
		memcpy(to->remote_ipv6, &info->key.u.ipv6.src,
		       sizeof(to->remote_ipv6));
		to->tunnel_label = be32_to_cpu(info->key.label);
	} else {
		to->remote_ipv4 = be32_to_cpu(info->key.u.ipv4.src);
	}

	if (unlikely(size != sizeof(struct bpf_tunnel_key)))
		memcpy(to_orig, to, size);

	return 0;
err_clear:
	memset(to_orig, 0, size);
	return err;
}

static const struct bpf_func_proto bpf_skb_get_tunnel_key_proto = {
	.func		= bpf_skb_get_tunnel_key,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_RAW_STACK,
	.arg3_type	= ARG_CONST_STACK_SIZE,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_skb_get_tunnel_opt, struct sk_buff *, skb, u8 *, to, u32, size)
{
	const struct ip_tunnel_info *info = skb_tunnel_info(skb);
	int err;

	if (unlikely(!info ||
		     !(info->key.tun_flags & TUNNEL_OPTIONS_PRESENT))) {
		err = -ENOENT;
		goto err_clear;
	}
	if (unlikely(size < info->options_len)) {
		err = -ENOMEM;
		goto err_clear;
	}

	ip_tunnel_info_opts_get(to, info);
	if (size > info->options_len)
		memset(to + info->options_len, 0, size - info->options_len);

	return info->options_len;
err_clear:
	memset(to, 0, size);
	return err;
}

static const struct bpf_func_proto bpf_skb_get_tunnel_opt_proto = {
	.func		= bpf_skb_get_tunnel_opt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_RAW_STACK,
	.arg3_type	= ARG_CONST_STACK_SIZE,
};

static struct metadata_dst __percpu *md_dst;

BPF_CALL_4(bpf_skb_set_tunnel_key, struct sk_buff *, skb,
	   const struct bpf_tunnel_key *, from, u32, size, u64, flags)
{
	struct metadata_dst *md = this_cpu_ptr(md_dst);
	u8 compat[sizeof(struct bpf_tunnel_key)];
	struct ip_tunnel_info *info;

	if (unlikely(flags & ~(BPF_F_TUNINFO_IPV6 | BPF_F_ZERO_CSUM_TX |
			       BPF_F_DONT_FRAGMENT)))
		return -EINVAL;
	if (unlikely(size != sizeof(struct bpf_tunnel_key))) {
		switch (size) {
		case offsetof(struct bpf_tunnel_key, tunnel_label):
		case offsetof(struct bpf_tunnel_key, tunnel_ext):
		case offsetof(struct bpf_tunnel_key, remote_ipv6[1]):
			/* Fixup deprecated structure layouts here, so we have
			 * a common path later on.
			 */
			memcpy(compat, from, size);
			memset(compat + size, 0, sizeof(compat) - size);
			from = (const struct bpf_tunnel_key *) compat;
			break;
		default:
			return -EINVAL;
		}
	}
	if (unlikely((!(flags & BPF_F_TUNINFO_IPV6) && from->tunnel_label) ||
		     from->tunnel_ext))
		return -EINVAL;

	skb_dst_drop(skb);
	dst_hold((struct dst_entry *) md);
	skb_dst_set(skb, (struct dst_entry *) md);

	info = &md->u.tun_info;
	info->mode = IP_TUNNEL_INFO_TX;

	info->key.tun_flags = TUNNEL_KEY | TUNNEL_CSUM | TUNNEL_NOCACHE;
	if (flags & BPF_F_DONT_FRAGMENT)
		info->key.tun_flags |= TUNNEL_DONT_FRAGMENT;

	info->key.tun_id = cpu_to_be64(from->tunnel_id);
	info->key.tos = from->tunnel_tos;
	info->key.ttl = from->tunnel_ttl;

	if (flags & BPF_F_TUNINFO_IPV6) {
		info->mode |= IP_TUNNEL_INFO_IPV6;
		memcpy(&info->key.u.ipv6.dst, from->remote_ipv6,
		       sizeof(from->remote_ipv6));
		info->key.label = cpu_to_be32(from->tunnel_label) &
				  IPV6_FLOWLABEL_MASK;
	} else {
		info->key.u.ipv4.dst = cpu_to_be32(from->remote_ipv4);
		if (flags & BPF_F_ZERO_CSUM_TX)
			info->key.tun_flags &= ~TUNNEL_CSUM;
	}

	return 0;
}

static const struct bpf_func_proto bpf_skb_set_tunnel_key_proto = {
	.func		= bpf_skb_set_tunnel_key,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_STACK,
	.arg3_type	= ARG_CONST_STACK_SIZE,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_skb_set_tunnel_opt, struct sk_buff *, skb,
	   const u8 *, from, u32, size)
{
	struct ip_tunnel_info *info = skb_tunnel_info(skb);
	const struct metadata_dst *md = this_cpu_ptr(md_dst);

	if (unlikely(info != &md->u.tun_info || (size & (sizeof(u32) - 1))))
		return -EINVAL;
	if (unlikely(size > IP_TUNNEL_OPTS_MAX))
		return -ENOMEM;

	ip_tunnel_info_opts_set(info, from, size);

	return 0;
}

static const struct bpf_func_proto bpf_skb_set_tunnel_opt_proto = {
	.func		= bpf_skb_set_tunnel_opt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_STACK,
	.arg3_type	= ARG_CONST_STACK_SIZE,
};

static const struct bpf_func_proto *
bpf_get_skb_set_tunnel_proto(enum bpf_func_id which)
{
	if (!md_dst) {
		/* Race is not possible, since it's called from verifier
		 * that is holding verifier mutex.
		 */
		md_dst = metadata_dst_alloc_percpu(IP_TUNNEL_OPTS_MAX,
						   GFP_KERNEL);
		if (!md_dst)
			return NULL;
	}

	switch (which) {
	case BPF_FUNC_skb_set_tunnel_key:
		return &bpf_skb_set_tunnel_key_proto;
	case BPF_FUNC_skb_set_tunnel_opt:
		return &bpf_skb_set_tunnel_opt_proto;
	default:
		return NULL;
	}
}

BPF_CALL_3(bpf_skb_under_cgroup, struct sk_buff *, skb, struct bpf_map *, map,
	   u32, idx)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	struct cgroup *cgrp;
	struct sock *sk;

	sk = skb_to_full_sk(skb);
	if (!sk || !sk_fullsock(sk))
		return -ENOENT;
	if (unlikely(idx >= array->map.max_entries))
		return -E2BIG;

	cgrp = READ_ONCE(array->ptrs[idx]);
	if (unlikely(!cgrp))
		return -EAGAIN;

	return sk_under_cgroup_hierarchy(sk, cgrp);
}

static const struct bpf_func_proto bpf_skb_under_cgroup_proto = {
	.func		= bpf_skb_under_cgroup,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
};

static unsigned long bpf_xdp_copy(void *dst_buff, const void *src_buff,
				  unsigned long off, unsigned long len)
{
	memcpy(dst_buff, src_buff + off, len);
	return 0;
}

BPF_CALL_5(bpf_xdp_event_output, struct xdp_buff *, xdp, struct bpf_map *, map,
	   u64, flags, void *, meta, u64, meta_size)
{
	u64 xdp_size = (flags & BPF_F_CTXLEN_MASK) >> 32;

	if (unlikely(flags & ~(BPF_F_CTXLEN_MASK | BPF_F_INDEX_MASK)))
		return -EINVAL;
	if (unlikely(xdp_size > (unsigned long)(xdp->data_end - xdp->data)))
		return -EFAULT;

	return bpf_event_output(map, flags, meta, meta_size, xdp, xdp_size,
				bpf_xdp_copy);
}

static const struct bpf_func_proto bpf_xdp_event_output_proto = {
	.func		= bpf_xdp_event_output,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_STACK,
	.arg5_type	= ARG_CONST_STACK_SIZE,
};

static const struct bpf_func_proto *
sk_filter_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;
	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;
	case BPF_FUNC_map_delete_elem:
		return &bpf_map_delete_elem_proto;
	case BPF_FUNC_get_prandom_u32:
		return &bpf_get_prandom_u32_proto;
	case BPF_FUNC_get_smp_processor_id:
		return &bpf_get_raw_smp_processor_id_proto;
	case BPF_FUNC_tail_call:
		return &bpf_tail_call_proto;
	case BPF_FUNC_ktime_get_ns:
		return &bpf_ktime_get_ns_proto;
	case BPF_FUNC_trace_printk:
		if (capable(CAP_SYS_ADMIN))
			return bpf_get_trace_printk_proto();
	default:
		return NULL;
	}
}

static const struct bpf_func_proto *
tc_cls_act_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
	case BPF_FUNC_skb_store_bytes:
		return &bpf_skb_store_bytes_proto;
	case BPF_FUNC_skb_load_bytes:
		return &bpf_skb_load_bytes_proto;
	case BPF_FUNC_skb_pull_data:
		return &bpf_skb_pull_data_proto;
	case BPF_FUNC_csum_diff:
		return &bpf_csum_diff_proto;
	case BPF_FUNC_csum_update:
		return &bpf_csum_update_proto;
	case BPF_FUNC_l3_csum_replace:
		return &bpf_l3_csum_replace_proto;
	case BPF_FUNC_l4_csum_replace:
		return &bpf_l4_csum_replace_proto;
	case BPF_FUNC_clone_redirect:
		return &bpf_clone_redirect_proto;
	case BPF_FUNC_get_cgroup_classid:
		return &bpf_get_cgroup_classid_proto;
	case BPF_FUNC_skb_vlan_push:
		return &bpf_skb_vlan_push_proto;
	case BPF_FUNC_skb_vlan_pop:
		return &bpf_skb_vlan_pop_proto;
	case BPF_FUNC_skb_change_proto:
		return &bpf_skb_change_proto_proto;
	case BPF_FUNC_skb_change_type:
		return &bpf_skb_change_type_proto;
	case BPF_FUNC_skb_change_tail:
		return &bpf_skb_change_tail_proto;
	case BPF_FUNC_skb_get_tunnel_key:
		return &bpf_skb_get_tunnel_key_proto;
	case BPF_FUNC_skb_set_tunnel_key:
		return bpf_get_skb_set_tunnel_proto(func_id);
	case BPF_FUNC_skb_get_tunnel_opt:
		return &bpf_skb_get_tunnel_opt_proto;
	case BPF_FUNC_skb_set_tunnel_opt:
		return bpf_get_skb_set_tunnel_proto(func_id);
	case BPF_FUNC_redirect:
		return &bpf_redirect_proto;
	case BPF_FUNC_get_route_realm:
		return &bpf_get_route_realm_proto;
	case BPF_FUNC_get_hash_recalc:
		return &bpf_get_hash_recalc_proto;
	case BPF_FUNC_set_hash_invalid:
		return &bpf_set_hash_invalid_proto;
	case BPF_FUNC_perf_event_output:
		return &bpf_skb_event_output_proto;
	case BPF_FUNC_get_smp_processor_id:
		return &bpf_get_smp_processor_id_proto;
	case BPF_FUNC_skb_under_cgroup:
		return &bpf_skb_under_cgroup_proto;
	default:
		return sk_filter_func_proto(func_id);
	}
}

static const struct bpf_func_proto *
xdp_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
	case BPF_FUNC_perf_event_output:
		return &bpf_xdp_event_output_proto;
	case BPF_FUNC_get_smp_processor_id:
		return &bpf_get_smp_processor_id_proto;
	default:
		return sk_filter_func_proto(func_id);
	}
}

static bool __is_valid_access(int off, int size, enum bpf_access_type type)
{
	if (off < 0 || off >= sizeof(struct __sk_buff))
		return false;
	/* The verifier guarantees that size > 0. */
	if (off % size != 0)
		return false;
	if (size != sizeof(__u32))
		return false;

	return true;
}

static bool sk_filter_is_valid_access(int off, int size,
				      enum bpf_access_type type,
				      enum bpf_reg_type *reg_type)
{
	switch (off) {
	case offsetof(struct __sk_buff, tc_classid):
	case offsetof(struct __sk_buff, data):
	case offsetof(struct __sk_buff, data_end):
		return false;
	}

	if (type == BPF_WRITE) {
		switch (off) {
		case offsetof(struct __sk_buff, cb[0]) ...
		     offsetof(struct __sk_buff, cb[4]):
			break;
		default:
			return false;
		}
	}

	return __is_valid_access(off, size, type);
}

static int tc_cls_act_prologue(struct bpf_insn *insn_buf, bool direct_write,
			       const struct bpf_prog *prog)
{
	struct bpf_insn *insn = insn_buf;

	if (!direct_write)
		return 0;

	/* if (!skb->cloned)
	 *       goto start;
	 *
	 * (Fast-path, otherwise approximation that we might be
	 *  a clone, do the rest in helper.)
	 */
	*insn++ = BPF_LDX_MEM(BPF_B, BPF_REG_6, BPF_REG_1, CLONED_OFFSET());
	*insn++ = BPF_ALU32_IMM(BPF_AND, BPF_REG_6, CLONED_MASK);
	*insn++ = BPF_JMP_IMM(BPF_JEQ, BPF_REG_6, 0, 7);

	/* ret = bpf_skb_pull_data(skb, 0); */
	*insn++ = BPF_MOV64_REG(BPF_REG_6, BPF_REG_1);
	*insn++ = BPF_ALU64_REG(BPF_XOR, BPF_REG_2, BPF_REG_2);
	*insn++ = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
			       BPF_FUNC_skb_pull_data);
	/* if (!ret)
	 *      goto restore;
	 * return TC_ACT_SHOT;
	 */
	*insn++ = BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2);
	*insn++ = BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, TC_ACT_SHOT);
	*insn++ = BPF_EXIT_INSN();

	/* restore: */
	*insn++ = BPF_MOV64_REG(BPF_REG_1, BPF_REG_6);
	/* start: */
	*insn++ = prog->insnsi[0];

	return insn - insn_buf;
}

static bool tc_cls_act_is_valid_access(int off, int size,
				       enum bpf_access_type type,
				       enum bpf_reg_type *reg_type)
{
	if (type == BPF_WRITE) {
		switch (off) {
		case offsetof(struct __sk_buff, mark):
		case offsetof(struct __sk_buff, tc_index):
		case offsetof(struct __sk_buff, priority):
		case offsetof(struct __sk_buff, cb[0]) ...
		     offsetof(struct __sk_buff, cb[4]):
		case offsetof(struct __sk_buff, tc_classid):
			break;
		default:
			return false;
		}
	}

	switch (off) {
	case offsetof(struct __sk_buff, data):
		*reg_type = PTR_TO_PACKET;
		break;
	case offsetof(struct __sk_buff, data_end):
		*reg_type = PTR_TO_PACKET_END;
		break;
	}

	return __is_valid_access(off, size, type);
}

static bool __is_valid_xdp_access(int off, int size,
				  enum bpf_access_type type)
{
	if (off < 0 || off >= sizeof(struct xdp_md))
		return false;
	if (off % size != 0)
		return false;
	if (size != sizeof(__u32))
		return false;

	return true;
}

static bool xdp_is_valid_access(int off, int size,
				enum bpf_access_type type,
				enum bpf_reg_type *reg_type)
{
	if (type == BPF_WRITE)
		return false;

	switch (off) {
	case offsetof(struct xdp_md, data):
		*reg_type = PTR_TO_PACKET;
		break;
	case offsetof(struct xdp_md, data_end):
		*reg_type = PTR_TO_PACKET_END;
		break;
	}

	return __is_valid_xdp_access(off, size, type);
}

void bpf_warn_invalid_xdp_action(u32 act)
{
	WARN_ONCE(1, "Illegal XDP return value %u, expect packet loss\n", act);
}
EXPORT_SYMBOL_GPL(bpf_warn_invalid_xdp_action);

static u32 sk_filter_convert_ctx_access(enum bpf_access_type type, int dst_reg,
					int src_reg, int ctx_off,
					struct bpf_insn *insn_buf,
					struct bpf_prog *prog)
{
	struct bpf_insn *insn = insn_buf;

	switch (ctx_off) {
	case offsetof(struct __sk_buff, len):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, len) != 4);

		*insn++ = BPF_LDX_MEM(BPF_W, dst_reg, src_reg,
				      offsetof(struct sk_buff, len));
		break;

	case offsetof(struct __sk_buff, protocol):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, protocol) != 2);

		*insn++ = BPF_LDX_MEM(BPF_H, dst_reg, src_reg,
				      offsetof(struct sk_buff, protocol));
		break;

	case offsetof(struct __sk_buff, vlan_proto):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, vlan_proto) != 2);

		*insn++ = BPF_LDX_MEM(BPF_H, dst_reg, src_reg,
				      offsetof(struct sk_buff, vlan_proto));
		break;

	case offsetof(struct __sk_buff, priority):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, priority) != 4);

		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_W, dst_reg, src_reg,
					      offsetof(struct sk_buff, priority));
		else
			*insn++ = BPF_LDX_MEM(BPF_W, dst_reg, src_reg,
					      offsetof(struct sk_buff, priority));
		break;

	case offsetof(struct __sk_buff, ingress_ifindex):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, skb_iif) != 4);

		*insn++ = BPF_LDX_MEM(BPF_W, dst_reg, src_reg,
				      offsetof(struct sk_buff, skb_iif));
		break;

	case offsetof(struct __sk_buff, ifindex):
		BUILD_BUG_ON(FIELD_SIZEOF(struct net_device, ifindex) != 4);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, dev),
				      dst_reg, src_reg,
				      offsetof(struct sk_buff, dev));
		*insn++ = BPF_JMP_IMM(BPF_JEQ, dst_reg, 0, 1);
		*insn++ = BPF_LDX_MEM(BPF_W, dst_reg, dst_reg,
				      offsetof(struct net_device, ifindex));
		break;

	case offsetof(struct __sk_buff, hash):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, hash) != 4);

		*insn++ = BPF_LDX_MEM(BPF_W, dst_reg, src_reg,
				      offsetof(struct sk_buff, hash));
		break;

	case offsetof(struct __sk_buff, mark):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, mark) != 4);

		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_W, dst_reg, src_reg,
					      offsetof(struct sk_buff, mark));
		else
			*insn++ = BPF_LDX_MEM(BPF_W, dst_reg, src_reg,
					      offsetof(struct sk_buff, mark));
		break;

	case offsetof(struct __sk_buff, pkt_type):
		return convert_skb_access(SKF_AD_PKTTYPE, dst_reg, src_reg, insn);

	case offsetof(struct __sk_buff, queue_mapping):
		return convert_skb_access(SKF_AD_QUEUE, dst_reg, src_reg, insn);

	case offsetof(struct __sk_buff, vlan_present):
		return convert_skb_access(SKF_AD_VLAN_TAG_PRESENT,
					  dst_reg, src_reg, insn);

	case offsetof(struct __sk_buff, vlan_tci):
		return convert_skb_access(SKF_AD_VLAN_TAG,
					  dst_reg, src_reg, insn);

	case offsetof(struct __sk_buff, cb[0]) ...
	     offsetof(struct __sk_buff, cb[4]):
		BUILD_BUG_ON(FIELD_SIZEOF(struct qdisc_skb_cb, data) < 20);

		prog->cb_access = 1;
		ctx_off -= offsetof(struct __sk_buff, cb[0]);
		ctx_off += offsetof(struct sk_buff, cb);
		ctx_off += offsetof(struct qdisc_skb_cb, data);
		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_W, dst_reg, src_reg, ctx_off);
		else
			*insn++ = BPF_LDX_MEM(BPF_W, dst_reg, src_reg, ctx_off);
		break;

	case offsetof(struct __sk_buff, tc_classid):
		ctx_off -= offsetof(struct __sk_buff, tc_classid);
		ctx_off += offsetof(struct sk_buff, cb);
		ctx_off += offsetof(struct qdisc_skb_cb, tc_classid);
		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_H, dst_reg, src_reg, ctx_off);
		else
			*insn++ = BPF_LDX_MEM(BPF_H, dst_reg, src_reg, ctx_off);
		break;

	case offsetof(struct __sk_buff, data):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, data),
				      dst_reg, src_reg,
				      offsetof(struct sk_buff, data));
		break;

	case offsetof(struct __sk_buff, data_end):
		ctx_off -= offsetof(struct __sk_buff, data_end);
		ctx_off += offsetof(struct sk_buff, cb);
		ctx_off += offsetof(struct bpf_skb_data_end, data_end);
		*insn++ = BPF_LDX_MEM(BPF_SIZEOF(void *), dst_reg, src_reg,
				      ctx_off);
		break;

	case offsetof(struct __sk_buff, tc_index):
#ifdef CONFIG_NET_SCHED
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, tc_index) != 2);

		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_H, dst_reg, src_reg,
					      offsetof(struct sk_buff, tc_index));
		else
			*insn++ = BPF_LDX_MEM(BPF_H, dst_reg, src_reg,
					      offsetof(struct sk_buff, tc_index));
		break;
#else
		if (type == BPF_WRITE)
			*insn++ = BPF_MOV64_REG(dst_reg, dst_reg);
		else
			*insn++ = BPF_MOV64_IMM(dst_reg, 0);
		break;
#endif
	}

	return insn - insn_buf;
}

static u32 tc_cls_act_convert_ctx_access(enum bpf_access_type type, int dst_reg,
					 int src_reg, int ctx_off,
					 struct bpf_insn *insn_buf,
					 struct bpf_prog *prog)
{
	struct bpf_insn *insn = insn_buf;

	switch (ctx_off) {
	case offsetof(struct __sk_buff, ifindex):
		BUILD_BUG_ON(FIELD_SIZEOF(struct net_device, ifindex) != 4);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, dev),
				      dst_reg, src_reg,
				      offsetof(struct sk_buff, dev));
		*insn++ = BPF_LDX_MEM(BPF_W, dst_reg, dst_reg,
				      offsetof(struct net_device, ifindex));
		break;
	default:
		return sk_filter_convert_ctx_access(type, dst_reg, src_reg,
						    ctx_off, insn_buf, prog);
	}

	return insn - insn_buf;
}

static u32 xdp_convert_ctx_access(enum bpf_access_type type, int dst_reg,
				  int src_reg, int ctx_off,
				  struct bpf_insn *insn_buf,
				  struct bpf_prog *prog)
{
	struct bpf_insn *insn = insn_buf;

	switch (ctx_off) {
	case offsetof(struct xdp_md, data):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct xdp_buff, data),
				      dst_reg, src_reg,
				      offsetof(struct xdp_buff, data));
		break;
	case offsetof(struct xdp_md, data_end):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct xdp_buff, data_end),
				      dst_reg, src_reg,
				      offsetof(struct xdp_buff, data_end));
		break;
	}

	return insn - insn_buf;
}

static const struct bpf_verifier_ops sk_filter_ops = {
	.get_func_proto		= sk_filter_func_proto,
	.is_valid_access	= sk_filter_is_valid_access,
	.convert_ctx_access	= sk_filter_convert_ctx_access,
};

static const struct bpf_verifier_ops tc_cls_act_ops = {
	.get_func_proto		= tc_cls_act_func_proto,
	.is_valid_access	= tc_cls_act_is_valid_access,
	.convert_ctx_access	= tc_cls_act_convert_ctx_access,
	.gen_prologue		= tc_cls_act_prologue,
};

static const struct bpf_verifier_ops xdp_ops = {
	.get_func_proto		= xdp_func_proto,
	.is_valid_access	= xdp_is_valid_access,
	.convert_ctx_access	= xdp_convert_ctx_access,
};

static struct bpf_prog_type_list sk_filter_type __read_mostly = {
	.ops	= &sk_filter_ops,
	.type	= BPF_PROG_TYPE_SOCKET_FILTER,
};

static struct bpf_prog_type_list sched_cls_type __read_mostly = {
	.ops	= &tc_cls_act_ops,
	.type	= BPF_PROG_TYPE_SCHED_CLS,
};

static struct bpf_prog_type_list sched_act_type __read_mostly = {
	.ops	= &tc_cls_act_ops,
	.type	= BPF_PROG_TYPE_SCHED_ACT,
};

static struct bpf_prog_type_list xdp_type __read_mostly = {
	.ops	= &xdp_ops,
	.type	= BPF_PROG_TYPE_XDP,
};

static int __init register_sk_filter_ops(void)
{
	bpf_register_prog_type(&sk_filter_type);
	bpf_register_prog_type(&sched_cls_type);
	bpf_register_prog_type(&sched_act_type);
	bpf_register_prog_type(&xdp_type);

	return 0;
}
late_initcall(register_sk_filter_ops);

int sk_detach_filter(struct sock *sk)
{
	int ret = -ENOENT;
	struct sk_filter *filter;

	if (sock_flag(sk, SOCK_FILTER_LOCKED))
		return -EPERM;

	filter = rcu_dereference_protected(sk->sk_filter,
					   lockdep_sock_is_held(sk));
	if (filter) {
		RCU_INIT_POINTER(sk->sk_filter, NULL);
		sk_filter_uncharge(sk, filter);
		ret = 0;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sk_detach_filter);

int sk_get_filter(struct sock *sk, struct sock_filter __user *ubuf,
		  unsigned int len)
{
	struct sock_fprog_kern *fprog;
	struct sk_filter *filter;
	int ret = 0;

	lock_sock(sk);
	filter = rcu_dereference_protected(sk->sk_filter,
					   lockdep_sock_is_held(sk));
	if (!filter)
		goto out;

	/* We're copying the filter that has been originally attached,
	 * so no conversion/decode needed anymore. eBPF programs that
	 * have no original program cannot be dumped through this.
	 */
	ret = -EACCES;
	fprog = filter->prog->orig_prog;
	if (!fprog)
		goto out;

	ret = fprog->len;
	if (!len)
		/* User space only enquires number of filter blocks. */
		goto out;

	ret = -EINVAL;
	if (len < fprog->len)
		goto out;

	ret = -EFAULT;
	if (copy_to_user(ubuf, fprog->filter, bpf_classic_proglen(fprog)))
		goto out;

	/* Instead of bytes, the API requests to return the number
	 * of filter blocks.
	 */
	ret = fprog->len;
out:
	release_sock(sk);
	return ret;
}
