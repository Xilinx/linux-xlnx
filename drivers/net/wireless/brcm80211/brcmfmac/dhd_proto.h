/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _BRCMF_PROTO_H_
#define _BRCMF_PROTO_H_

/*
 * Exported from the brcmf protocol module (brcmf_cdc)
 */

/* Linkage, sets prot link and updates hdrlen in pub */
int brcmf_proto_attach(struct brcmf_pub *drvr);

/* Unlink, frees allocated protocol memory (including brcmf_proto) */
void brcmf_proto_detach(struct brcmf_pub *drvr);

/* Stop protocol: sync w/dongle state. */
void brcmf_proto_stop(struct brcmf_pub *drvr);

/* Add any protocol-specific data header.
 * Caller must reserve prot_hdrlen prepend space.
 */
void brcmf_proto_hdrpush(struct brcmf_pub *, int ifidx, u8 offset,
			 struct sk_buff *txp);

/* Sets dongle media info (drv_version, mac address). */
int brcmf_c_preinit_dcmds(struct brcmf_if *ifp);

#endif				/* _BRCMF_PROTO_H_ */
