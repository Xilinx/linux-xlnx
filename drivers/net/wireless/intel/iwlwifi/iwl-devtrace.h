/******************************************************************************
 *
 * Copyright(c) 2009 - 2014 Intel Corporation. All rights reserved.
 * Copyright(C) 2016 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#ifndef __IWLWIFI_DEVICE_TRACE
#include <linux/skbuff.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include "iwl-trans.h"
#if !defined(__IWLWIFI_DEVICE_TRACE)
static inline bool iwl_trace_data(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	__le16 fc = hdr->frame_control;
	int offs = 24; /* start with normal header length */

	if (!ieee80211_is_data(fc))
		return false;

	/* Try to determine if the frame is EAPOL. This might have false
	 * positives (if there's no RFC 1042 header and we compare to some
	 * payload instead) but since we're only doing tracing that's not
	 * a problem.
	 */

	if (ieee80211_has_a4(fc))
		offs += 6;
	if (ieee80211_is_data_qos(fc))
		offs += 2;
	/* don't account for crypto - these are unencrypted */

	/* also account for the RFC 1042 header, of course */
	offs += 6;

	return skb->len > offs + 2 &&
	       *(__be16 *)(skb->data + offs) == cpu_to_be16(ETH_P_PAE);
}

static inline size_t iwl_rx_trace_len(const struct iwl_trans *trans,
				      void *rxbuf, size_t len)
{
	struct iwl_cmd_header *cmd = (void *)((u8 *)rxbuf + sizeof(__le32));
	struct ieee80211_hdr *hdr;

	if (cmd->cmd != trans->rx_mpdu_cmd)
		return len;

	hdr = (void *)((u8 *)cmd + sizeof(struct iwl_cmd_header) +
			trans->rx_mpdu_cmd_hdr_size);
	if (!ieee80211_is_data(hdr->frame_control))
		return len;
	/* maybe try to identify EAPOL frames? */
	return sizeof(__le32) + sizeof(*cmd) + trans->rx_mpdu_cmd_hdr_size +
		ieee80211_hdrlen(hdr->frame_control);
}
#endif

#define __IWLWIFI_DEVICE_TRACE

#include <linux/tracepoint.h>
#include <linux/device.h>
#include "iwl-trans.h"


#if !defined(CONFIG_IWLWIFI_DEVICE_TRACING) || defined(__CHECKER__)
#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) \
static inline void trace_ ## name(proto) {}
#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(...)
#undef DEFINE_EVENT
#define DEFINE_EVENT(evt_class, name, proto, ...) \
static inline void trace_ ## name(proto) {}
#endif

#define DEV_ENTRY	__string(dev, dev_name(dev))
#define DEV_ASSIGN	__assign_str(dev, dev_name(dev))

#include "iwl-devtrace-io.h"
#include "iwl-devtrace-ucode.h"
#include "iwl-devtrace-msg.h"
#include "iwl-devtrace-data.h"
#include "iwl-devtrace-iwlwifi.h"

#endif /* __IWLWIFI_DEVICE_TRACE */
