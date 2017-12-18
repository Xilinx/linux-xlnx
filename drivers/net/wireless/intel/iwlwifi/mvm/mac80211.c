/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/if_arp.h>
#include <linux/time.h>
#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>
#include <net/tcp.h>

#include "iwl-op-mode.h"
#include "iwl-io.h"
#include "mvm.h"
#include "sta.h"
#include "time-event.h"
#include "iwl-eeprom-parse.h"
#include "iwl-phy-db.h"
#include "testmode.h"
#include "iwl-fw-error-dump.h"
#include "iwl-prph.h"
#include "iwl-nvm-parse.h"
#include "fw-dbg.h"

static const struct ieee80211_iface_limit iwl_mvm_limits[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_STATION),
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_AP) |
			BIT(NL80211_IFTYPE_P2P_CLIENT) |
			BIT(NL80211_IFTYPE_P2P_GO),
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_P2P_DEVICE),
	},
};

static const struct ieee80211_iface_combination iwl_mvm_iface_combinations[] = {
	{
		.num_different_channels = 2,
		.max_interfaces = 3,
		.limits = iwl_mvm_limits,
		.n_limits = ARRAY_SIZE(iwl_mvm_limits),
	},
};

#ifdef CONFIG_PM_SLEEP
static const struct nl80211_wowlan_tcp_data_token_feature
iwl_mvm_wowlan_tcp_token_feature = {
	.min_len = 0,
	.max_len = 255,
	.bufsize = IWL_WOWLAN_REMOTE_WAKE_MAX_TOKENS,
};

static const struct wiphy_wowlan_tcp_support iwl_mvm_wowlan_tcp_support = {
	.tok = &iwl_mvm_wowlan_tcp_token_feature,
	.data_payload_max = IWL_WOWLAN_TCP_MAX_PACKET_LEN -
			    sizeof(struct ethhdr) -
			    sizeof(struct iphdr) -
			    sizeof(struct tcphdr),
	.data_interval_max = 65535, /* __le16 in API */
	.wake_payload_max = IWL_WOWLAN_REMOTE_WAKE_MAX_PACKET_LEN -
			    sizeof(struct ethhdr) -
			    sizeof(struct iphdr) -
			    sizeof(struct tcphdr),
	.seq = true,
};
#endif

#ifdef CONFIG_IWLWIFI_BCAST_FILTERING
/*
 * Use the reserved field to indicate magic values.
 * these values will only be used internally by the driver,
 * and won't make it to the fw (reserved will be 0).
 * BC_FILTER_MAGIC_IP - configure the val of this attribute to
 *	be the vif's ip address. in case there is not a single
 *	ip address (0, or more than 1), this attribute will
 *	be skipped.
 * BC_FILTER_MAGIC_MAC - set the val of this attribute to
 *	the LSB bytes of the vif's mac address
 */
enum {
	BC_FILTER_MAGIC_NONE = 0,
	BC_FILTER_MAGIC_IP,
	BC_FILTER_MAGIC_MAC,
};

static const struct iwl_fw_bcast_filter iwl_mvm_default_bcast_filters[] = {
	{
		/* arp */
		.discard = 0,
		.frame_type = BCAST_FILTER_FRAME_TYPE_ALL,
		.attrs = {
			{
				/* frame type - arp, hw type - ethernet */
				.offset_type =
					BCAST_FILTER_OFFSET_PAYLOAD_START,
				.offset = sizeof(rfc1042_header),
				.val = cpu_to_be32(0x08060001),
				.mask = cpu_to_be32(0xffffffff),
			},
			{
				/* arp dest ip */
				.offset_type =
					BCAST_FILTER_OFFSET_PAYLOAD_START,
				.offset = sizeof(rfc1042_header) + 2 +
					  sizeof(struct arphdr) +
					  ETH_ALEN + sizeof(__be32) +
					  ETH_ALEN,
				.mask = cpu_to_be32(0xffffffff),
				/* mark it as special field */
				.reserved1 = cpu_to_le16(BC_FILTER_MAGIC_IP),
			},
		},
	},
	{
		/* dhcp offer bcast */
		.discard = 0,
		.frame_type = BCAST_FILTER_FRAME_TYPE_IPV4,
		.attrs = {
			{
				/* udp dest port - 68 (bootp client)*/
				.offset_type = BCAST_FILTER_OFFSET_IP_END,
				.offset = offsetof(struct udphdr, dest),
				.val = cpu_to_be32(0x00440000),
				.mask = cpu_to_be32(0xffff0000),
			},
			{
				/* dhcp - lsb bytes of client hw address */
				.offset_type = BCAST_FILTER_OFFSET_IP_END,
				.offset = 38,
				.mask = cpu_to_be32(0xffffffff),
				/* mark it as special field */
				.reserved1 = cpu_to_le16(BC_FILTER_MAGIC_MAC),
			},
		},
	},
	/* last filter must be empty */
	{},
};
#endif

void iwl_mvm_ref(struct iwl_mvm *mvm, enum iwl_mvm_ref_type ref_type)
{
	if (!iwl_mvm_is_d0i3_supported(mvm))
		return;

	IWL_DEBUG_RPM(mvm, "Take mvm reference - type %d\n", ref_type);
	spin_lock_bh(&mvm->refs_lock);
	mvm->refs[ref_type]++;
	spin_unlock_bh(&mvm->refs_lock);
	iwl_trans_ref(mvm->trans);
}

void iwl_mvm_unref(struct iwl_mvm *mvm, enum iwl_mvm_ref_type ref_type)
{
	if (!iwl_mvm_is_d0i3_supported(mvm))
		return;

	IWL_DEBUG_RPM(mvm, "Leave mvm reference - type %d\n", ref_type);
	spin_lock_bh(&mvm->refs_lock);
	if (WARN_ON(!mvm->refs[ref_type])) {
		spin_unlock_bh(&mvm->refs_lock);
		return;
	}
	mvm->refs[ref_type]--;
	spin_unlock_bh(&mvm->refs_lock);
	iwl_trans_unref(mvm->trans);
}

static void iwl_mvm_unref_all_except(struct iwl_mvm *mvm,
				     enum iwl_mvm_ref_type except_ref)
{
	int i, j;

	if (!iwl_mvm_is_d0i3_supported(mvm))
		return;

	spin_lock_bh(&mvm->refs_lock);
	for (i = 0; i < IWL_MVM_REF_COUNT; i++) {
		if (except_ref == i || !mvm->refs[i])
			continue;

		IWL_DEBUG_RPM(mvm, "Cleanup: remove mvm ref type %d (%d)\n",
			      i, mvm->refs[i]);
		for (j = 0; j < mvm->refs[i]; j++)
			iwl_trans_unref(mvm->trans);
		mvm->refs[i] = 0;
	}
	spin_unlock_bh(&mvm->refs_lock);
}

bool iwl_mvm_ref_taken(struct iwl_mvm *mvm)
{
	int i;
	bool taken = false;

	if (!iwl_mvm_is_d0i3_supported(mvm))
		return true;

	spin_lock_bh(&mvm->refs_lock);
	for (i = 0; i < IWL_MVM_REF_COUNT; i++) {
		if (mvm->refs[i]) {
			taken = true;
			break;
		}
	}
	spin_unlock_bh(&mvm->refs_lock);

	return taken;
}

int iwl_mvm_ref_sync(struct iwl_mvm *mvm, enum iwl_mvm_ref_type ref_type)
{
	iwl_mvm_ref(mvm, ref_type);

	if (!wait_event_timeout(mvm->d0i3_exit_waitq,
				!test_bit(IWL_MVM_STATUS_IN_D0I3, &mvm->status),
				HZ)) {
		WARN_ON_ONCE(1);
		iwl_mvm_unref(mvm, ref_type);
		return -EIO;
	}

	return 0;
}

static void iwl_mvm_reset_phy_ctxts(struct iwl_mvm *mvm)
{
	int i;

	memset(mvm->phy_ctxts, 0, sizeof(mvm->phy_ctxts));
	for (i = 0; i < NUM_PHY_CTX; i++) {
		mvm->phy_ctxts[i].id = i;
		mvm->phy_ctxts[i].ref = 0;
	}
}

struct ieee80211_regdomain *iwl_mvm_get_regdomain(struct wiphy *wiphy,
						  const char *alpha2,
						  enum iwl_mcc_source src_id,
						  bool *changed)
{
	struct ieee80211_regdomain *regd = NULL;
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mcc_update_resp *resp;

	IWL_DEBUG_LAR(mvm, "Getting regdomain data for %s from FW\n", alpha2);

	lockdep_assert_held(&mvm->mutex);

	resp = iwl_mvm_update_mcc(mvm, alpha2, src_id);
	if (IS_ERR_OR_NULL(resp)) {
		IWL_DEBUG_LAR(mvm, "Could not get update from FW %d\n",
			      PTR_ERR_OR_ZERO(resp));
		goto out;
	}

	if (changed)
		*changed = (resp->status == MCC_RESP_NEW_CHAN_PROFILE);

	regd = iwl_parse_nvm_mcc_info(mvm->trans->dev, mvm->cfg,
				      __le32_to_cpu(resp->n_channels),
				      resp->channels,
				      __le16_to_cpu(resp->mcc));
	/* Store the return source id */
	src_id = resp->source_id;
	kfree(resp);
	if (IS_ERR_OR_NULL(regd)) {
		IWL_DEBUG_LAR(mvm, "Could not get parse update from FW %d\n",
			      PTR_ERR_OR_ZERO(regd));
		goto out;
	}

	IWL_DEBUG_LAR(mvm, "setting alpha2 from FW to %s (0x%x, 0x%x) src=%d\n",
		      regd->alpha2, regd->alpha2[0], regd->alpha2[1], src_id);
	mvm->lar_regdom_set = true;
	mvm->mcc_src = src_id;

out:
	return regd;
}

void iwl_mvm_update_changed_regdom(struct iwl_mvm *mvm)
{
	bool changed;
	struct ieee80211_regdomain *regd;

	if (!iwl_mvm_is_lar_supported(mvm))
		return;

	regd = iwl_mvm_get_current_regdomain(mvm, &changed);
	if (!IS_ERR_OR_NULL(regd)) {
		/* only update the regulatory core if changed */
		if (changed)
			regulatory_set_wiphy_regd(mvm->hw->wiphy, regd);

		kfree(regd);
	}
}

struct ieee80211_regdomain *iwl_mvm_get_current_regdomain(struct iwl_mvm *mvm,
							  bool *changed)
{
	return iwl_mvm_get_regdomain(mvm->hw->wiphy, "ZZ",
				     iwl_mvm_is_wifi_mcc_supported(mvm) ?
				     MCC_SOURCE_GET_CURRENT :
				     MCC_SOURCE_OLD_FW, changed);
}

int iwl_mvm_init_fw_regd(struct iwl_mvm *mvm)
{
	enum iwl_mcc_source used_src;
	struct ieee80211_regdomain *regd;
	int ret;
	bool changed;
	const struct ieee80211_regdomain *r =
			rtnl_dereference(mvm->hw->wiphy->regd);

	if (!r)
		return -ENOENT;

	/* save the last source in case we overwrite it below */
	used_src = mvm->mcc_src;
	if (iwl_mvm_is_wifi_mcc_supported(mvm)) {
		/* Notify the firmware we support wifi location updates */
		regd = iwl_mvm_get_current_regdomain(mvm, NULL);
		if (!IS_ERR_OR_NULL(regd))
			kfree(regd);
	}

	/* Now set our last stored MCC and source */
	regd = iwl_mvm_get_regdomain(mvm->hw->wiphy, r->alpha2, used_src,
				     &changed);
	if (IS_ERR_OR_NULL(regd))
		return -EIO;

	/* update cfg80211 if the regdomain was changed */
	if (changed)
		ret = regulatory_set_wiphy_regd_sync_rtnl(mvm->hw->wiphy, regd);
	else
		ret = 0;

	kfree(regd);
	return ret;
}

int iwl_mvm_mac_setup_register(struct iwl_mvm *mvm)
{
	struct ieee80211_hw *hw = mvm->hw;
	int num_mac, ret, i;
	static const u32 mvm_ciphers[] = {
		WLAN_CIPHER_SUITE_WEP40,
		WLAN_CIPHER_SUITE_WEP104,
		WLAN_CIPHER_SUITE_TKIP,
		WLAN_CIPHER_SUITE_CCMP,
	};

	/* Tell mac80211 our characteristics */
	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, SPECTRUM_MGMT);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(hw, QUEUE_CONTROL);
	ieee80211_hw_set(hw, WANT_MONITOR_VIF);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, TIMING_BEACON_ONLY);
	ieee80211_hw_set(hw, CONNECTION_MONITOR);
	ieee80211_hw_set(hw, CHANCTX_STA_CSA);
	ieee80211_hw_set(hw, SUPPORT_FAST_XMIT);
	ieee80211_hw_set(hw, SUPPORTS_CLONED_SKBS);
	ieee80211_hw_set(hw, SUPPORTS_AMSDU_IN_AMPDU);
	ieee80211_hw_set(hw, NEEDS_UNIQUE_STA_ADDR);
	if (iwl_mvm_has_new_rx_api(mvm))
		ieee80211_hw_set(hw, SUPPORTS_REORDERING_BUFFER);

	if (mvm->trans->num_rx_queues > 1)
		ieee80211_hw_set(hw, USES_RSS);

	if (mvm->trans->max_skb_frags)
		hw->netdev_features = NETIF_F_HIGHDMA | NETIF_F_SG;

	if (!iwl_mvm_is_dqa_supported(mvm))
		hw->queues = mvm->first_agg_queue;
	else
		hw->queues = IEEE80211_MAX_QUEUES;
	hw->offchannel_tx_hw_queue = IWL_MVM_OFFCHANNEL_QUEUE;
	hw->radiotap_mcs_details |= IEEE80211_RADIOTAP_MCS_HAVE_FEC |
				    IEEE80211_RADIOTAP_MCS_HAVE_STBC;
	hw->radiotap_vht_details |= IEEE80211_RADIOTAP_VHT_KNOWN_STBC |
		IEEE80211_RADIOTAP_VHT_KNOWN_BEAMFORMED;
	hw->rate_control_algorithm = "iwl-mvm-rs";
	hw->uapsd_queues = IWL_MVM_UAPSD_QUEUES;
	hw->uapsd_max_sp_len = IWL_UAPSD_MAX_SP;

	BUILD_BUG_ON(ARRAY_SIZE(mvm->ciphers) < ARRAY_SIZE(mvm_ciphers) + 6);
	memcpy(mvm->ciphers, mvm_ciphers, sizeof(mvm_ciphers));
	hw->wiphy->n_cipher_suites = ARRAY_SIZE(mvm_ciphers);
	hw->wiphy->cipher_suites = mvm->ciphers;

	if (iwl_mvm_has_new_rx_api(mvm)) {
		mvm->ciphers[hw->wiphy->n_cipher_suites] =
			WLAN_CIPHER_SUITE_GCMP;
		hw->wiphy->n_cipher_suites++;
		mvm->ciphers[hw->wiphy->n_cipher_suites] =
			WLAN_CIPHER_SUITE_GCMP_256;
		hw->wiphy->n_cipher_suites++;
	}

	/* Enable 11w if software crypto is not enabled (as the
	 * firmware will interpret some mgmt packets, so enabling it
	 * with software crypto isn't safe).
	 */
	if (!iwlwifi_mod_params.sw_crypto) {
		ieee80211_hw_set(hw, MFP_CAPABLE);
		mvm->ciphers[hw->wiphy->n_cipher_suites] =
			WLAN_CIPHER_SUITE_AES_CMAC;
		hw->wiphy->n_cipher_suites++;
		if (iwl_mvm_has_new_rx_api(mvm)) {
			mvm->ciphers[hw->wiphy->n_cipher_suites] =
				WLAN_CIPHER_SUITE_BIP_GMAC_128;
			hw->wiphy->n_cipher_suites++;
			mvm->ciphers[hw->wiphy->n_cipher_suites] =
				WLAN_CIPHER_SUITE_BIP_GMAC_256;
			hw->wiphy->n_cipher_suites++;
		}
	}

	/* currently FW API supports only one optional cipher scheme */
	if (mvm->fw->cs[0].cipher) {
		const struct iwl_fw_cipher_scheme *fwcs = &mvm->fw->cs[0];
		struct ieee80211_cipher_scheme *cs = &mvm->cs[0];

		mvm->hw->n_cipher_schemes = 1;

		cs->cipher = le32_to_cpu(fwcs->cipher);
		cs->iftype = BIT(NL80211_IFTYPE_STATION);
		cs->hdr_len = fwcs->hdr_len;
		cs->pn_len = fwcs->pn_len;
		cs->pn_off = fwcs->pn_off;
		cs->key_idx_off = fwcs->key_idx_off;
		cs->key_idx_mask = fwcs->key_idx_mask;
		cs->key_idx_shift = fwcs->key_idx_shift;
		cs->mic_len = fwcs->mic_len;

		mvm->hw->cipher_schemes = mvm->cs;
		mvm->ciphers[hw->wiphy->n_cipher_suites] = cs->cipher;
		hw->wiphy->n_cipher_suites++;
	}

	ieee80211_hw_set(hw, SINGLE_SCAN_ON_ALL_BANDS);
	hw->wiphy->features |=
		NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR |
		NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR |
		NL80211_FEATURE_ND_RANDOM_MAC_ADDR;

	hw->sta_data_size = sizeof(struct iwl_mvm_sta);
	hw->vif_data_size = sizeof(struct iwl_mvm_vif);
	hw->chanctx_data_size = sizeof(u16);

	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_P2P_CLIENT) |
		BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_P2P_GO) |
		BIT(NL80211_IFTYPE_P2P_DEVICE) |
		BIT(NL80211_IFTYPE_ADHOC);

	hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN;
	hw->wiphy->regulatory_flags |= REGULATORY_ENABLE_RELAX_NO_IR;
	if (iwl_mvm_is_lar_supported(mvm))
		hw->wiphy->regulatory_flags |= REGULATORY_WIPHY_SELF_MANAGED;
	else
		hw->wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG |
					       REGULATORY_DISABLE_BEACON_HINTS;

	hw->wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
	hw->wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;

	hw->wiphy->iface_combinations = iwl_mvm_iface_combinations;
	hw->wiphy->n_iface_combinations =
		ARRAY_SIZE(iwl_mvm_iface_combinations);

	hw->wiphy->max_remain_on_channel_duration = 10000;
	hw->max_listen_interval = IWL_CONN_MAX_LISTEN_INTERVAL;
	/* we can compensate an offset of up to 3 channels = 15 MHz */
	hw->wiphy->max_adj_channel_rssi_comp = 3 * 5;

	/* Extract MAC address */
	memcpy(mvm->addresses[0].addr, mvm->nvm_data->hw_addr, ETH_ALEN);
	hw->wiphy->addresses = mvm->addresses;
	hw->wiphy->n_addresses = 1;

	/* Extract additional MAC addresses if available */
	num_mac = (mvm->nvm_data->n_hw_addrs > 1) ?
		min(IWL_MVM_MAX_ADDRESSES, mvm->nvm_data->n_hw_addrs) : 1;

	for (i = 1; i < num_mac; i++) {
		memcpy(mvm->addresses[i].addr, mvm->addresses[i-1].addr,
		       ETH_ALEN);
		mvm->addresses[i].addr[5]++;
		hw->wiphy->n_addresses++;
	}

	iwl_mvm_reset_phy_ctxts(mvm);

	hw->wiphy->max_scan_ie_len = iwl_mvm_max_scan_ie_len(mvm);

	hw->wiphy->max_scan_ssids = PROBE_OPTION_MAX;

	BUILD_BUG_ON(IWL_MVM_SCAN_STOPPING_MASK & IWL_MVM_SCAN_MASK);
	BUILD_BUG_ON(IWL_MVM_MAX_UMAC_SCANS > HWEIGHT32(IWL_MVM_SCAN_MASK) ||
		     IWL_MVM_MAX_LMAC_SCANS > HWEIGHT32(IWL_MVM_SCAN_MASK));

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN))
		mvm->max_scans = IWL_MVM_MAX_UMAC_SCANS;
	else
		mvm->max_scans = IWL_MVM_MAX_LMAC_SCANS;

	if (mvm->nvm_data->bands[NL80211_BAND_2GHZ].n_channels)
		hw->wiphy->bands[NL80211_BAND_2GHZ] =
			&mvm->nvm_data->bands[NL80211_BAND_2GHZ];
	if (mvm->nvm_data->bands[NL80211_BAND_5GHZ].n_channels) {
		hw->wiphy->bands[NL80211_BAND_5GHZ] =
			&mvm->nvm_data->bands[NL80211_BAND_5GHZ];

		if (fw_has_capa(&mvm->fw->ucode_capa,
				IWL_UCODE_TLV_CAPA_BEAMFORMER) &&
		    fw_has_api(&mvm->fw->ucode_capa,
			       IWL_UCODE_TLV_API_LQ_SS_PARAMS))
			hw->wiphy->bands[NL80211_BAND_5GHZ]->vht_cap.cap |=
				IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE;
	}

	hw->wiphy->hw_version = mvm->trans->hw_id;

	if (iwlmvm_mod_params.power_scheme != IWL_POWER_SCHEME_CAM)
		hw->wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
	else
		hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_SCHED_SCAN;
	hw->wiphy->max_sched_scan_ssids = PROBE_OPTION_MAX;
	hw->wiphy->max_match_sets = IWL_SCAN_MAX_PROFILES;
	/* we create the 802.11 header and zero length SSID IE. */
	hw->wiphy->max_sched_scan_ie_len =
		SCAN_OFFLOAD_PROBE_REQ_SIZE - 24 - 2;
	hw->wiphy->max_sched_scan_plans = IWL_MAX_SCHED_SCAN_PLANS;
	hw->wiphy->max_sched_scan_plan_interval = U16_MAX;

	/*
	 * the firmware uses u8 for num of iterations, but 0xff is saved for
	 * infinite loop, so the maximum number of iterations is actually 254.
	 */
	hw->wiphy->max_sched_scan_plan_iterations = 254;

	hw->wiphy->features |= NL80211_FEATURE_P2P_GO_CTWIN |
			       NL80211_FEATURE_LOW_PRIORITY_SCAN |
			       NL80211_FEATURE_P2P_GO_OPPPS |
			       NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE |
			       NL80211_FEATURE_DYNAMIC_SMPS |
			       NL80211_FEATURE_STATIC_SMPS |
			       NL80211_FEATURE_SUPPORTS_WMM_ADMISSION;

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_TXPOWER_INSERTION_SUPPORT))
		hw->wiphy->features |= NL80211_FEATURE_TX_POWER_INSERTION;
	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_QUIET_PERIOD_SUPPORT))
		hw->wiphy->features |= NL80211_FEATURE_QUIET;

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT))
		hw->wiphy->features |=
			NL80211_FEATURE_DS_PARAM_SET_IE_IN_PROBES;

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT))
		hw->wiphy->features |= NL80211_FEATURE_WFA_TPC_IE_IN_PROBES;

	if (fw_has_api(&mvm->fw->ucode_capa,
		       IWL_UCODE_TLV_API_SCAN_TSF_REPORT)) {
		wiphy_ext_feature_set(hw->wiphy,
				      NL80211_EXT_FEATURE_SCAN_START_TIME);
		wiphy_ext_feature_set(hw->wiphy,
				      NL80211_EXT_FEATURE_BSS_PARENT_TSF);
		wiphy_ext_feature_set(hw->wiphy,
				      NL80211_EXT_FEATURE_SET_SCAN_DWELL);
	}

	mvm->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;

#ifdef CONFIG_PM_SLEEP
	if (iwl_mvm_is_d0i3_supported(mvm) &&
	    device_can_wakeup(mvm->trans->dev)) {
		mvm->wowlan.flags = WIPHY_WOWLAN_ANY;
		hw->wiphy->wowlan = &mvm->wowlan;
	}

	if (mvm->fw->img[IWL_UCODE_WOWLAN].sec[0].len &&
	    mvm->trans->ops->d3_suspend &&
	    mvm->trans->ops->d3_resume &&
	    device_can_wakeup(mvm->trans->dev)) {
		mvm->wowlan.flags |= WIPHY_WOWLAN_MAGIC_PKT |
				     WIPHY_WOWLAN_DISCONNECT |
				     WIPHY_WOWLAN_EAP_IDENTITY_REQ |
				     WIPHY_WOWLAN_RFKILL_RELEASE |
				     WIPHY_WOWLAN_NET_DETECT;
		if (!iwlwifi_mod_params.sw_crypto)
			mvm->wowlan.flags |= WIPHY_WOWLAN_SUPPORTS_GTK_REKEY |
					     WIPHY_WOWLAN_GTK_REKEY_FAILURE |
					     WIPHY_WOWLAN_4WAY_HANDSHAKE;

		mvm->wowlan.n_patterns = IWL_WOWLAN_MAX_PATTERNS;
		mvm->wowlan.pattern_min_len = IWL_WOWLAN_MIN_PATTERN_LEN;
		mvm->wowlan.pattern_max_len = IWL_WOWLAN_MAX_PATTERN_LEN;
		mvm->wowlan.max_nd_match_sets = IWL_SCAN_MAX_PROFILES;
		mvm->wowlan.tcp = &iwl_mvm_wowlan_tcp_support;
		hw->wiphy->wowlan = &mvm->wowlan;
	}
#endif

#ifdef CONFIG_IWLWIFI_BCAST_FILTERING
	/* assign default bcast filtering configuration */
	mvm->bcast_filters = iwl_mvm_default_bcast_filters;
#endif

	ret = iwl_mvm_leds_init(mvm);
	if (ret)
		return ret;

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_TDLS_SUPPORT)) {
		IWL_DEBUG_TDLS(mvm, "TDLS supported\n");
		hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
		ieee80211_hw_set(hw, TDLS_WIDER_BW);
	}

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_TDLS_CHANNEL_SWITCH)) {
		IWL_DEBUG_TDLS(mvm, "TDLS channel switch supported\n");
		hw->wiphy->features |= NL80211_FEATURE_TDLS_CHANNEL_SWITCH;
	}

	hw->netdev_features |= mvm->cfg->features;
	if (!iwl_mvm_is_csum_supported(mvm)) {
		hw->netdev_features &= ~(IWL_TX_CSUM_NETIF_FLAGS |
					 NETIF_F_RXCSUM);
		/* We may support SW TX CSUM */
		if (IWL_MVM_SW_TX_CSUM_OFFLOAD)
			hw->netdev_features |= IWL_TX_CSUM_NETIF_FLAGS;
	}

	ret = ieee80211_register_hw(mvm->hw);
	if (ret)
		iwl_mvm_leds_exit(mvm);

	if (mvm->cfg->vht_mu_mimo_supported)
		wiphy_ext_feature_set(hw->wiphy,
				      NL80211_EXT_FEATURE_MU_MIMO_AIR_SNIFFER);

	return ret;
}

static bool iwl_mvm_defer_tx(struct iwl_mvm *mvm,
			     struct ieee80211_sta *sta,
			     struct sk_buff *skb)
{
	struct iwl_mvm_sta *mvmsta;
	bool defer = false;

	/*
	 * double check the IN_D0I3 flag both before and after
	 * taking the spinlock, in order to prevent taking
	 * the spinlock when not needed.
	 */
	if (likely(!test_bit(IWL_MVM_STATUS_IN_D0I3, &mvm->status)))
		return false;

	spin_lock(&mvm->d0i3_tx_lock);
	/*
	 * testing the flag again ensures the skb dequeue
	 * loop (on d0i3 exit) hasn't run yet.
	 */
	if (!test_bit(IWL_MVM_STATUS_IN_D0I3, &mvm->status))
		goto out;

	mvmsta = iwl_mvm_sta_from_mac80211(sta);
	if (mvmsta->sta_id == IWL_MVM_STATION_COUNT ||
	    mvmsta->sta_id != mvm->d0i3_ap_sta_id)
		goto out;

	__skb_queue_tail(&mvm->d0i3_tx, skb);
	ieee80211_stop_queues(mvm->hw);

	/* trigger wakeup */
	iwl_mvm_ref(mvm, IWL_MVM_REF_TX);
	iwl_mvm_unref(mvm, IWL_MVM_REF_TX);

	defer = true;
out:
	spin_unlock(&mvm->d0i3_tx_lock);
	return defer;
}

static void iwl_mvm_mac_tx(struct ieee80211_hw *hw,
			   struct ieee80211_tx_control *control,
			   struct sk_buff *skb)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct ieee80211_sta *sta = control->sta;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (void *)skb->data;

	if (iwl_mvm_is_radio_killed(mvm)) {
		IWL_DEBUG_DROP(mvm, "Dropping - RF/CT KILL\n");
		goto drop;
	}

	if (IEEE80211_SKB_CB(skb)->hw_queue == IWL_MVM_OFFCHANNEL_QUEUE &&
	    !test_bit(IWL_MVM_STATUS_ROC_RUNNING, &mvm->status) &&
	    !test_bit(IWL_MVM_STATUS_ROC_AUX_RUNNING, &mvm->status))
		goto drop;

	/* treat non-bufferable MMPDUs as broadcast if sta is sleeping */
	if (unlikely(info->flags & IEEE80211_TX_CTL_NO_PS_BUFFER &&
		     ieee80211_is_mgmt(hdr->frame_control) &&
		     !ieee80211_is_deauth(hdr->frame_control) &&
		     !ieee80211_is_disassoc(hdr->frame_control) &&
		     !ieee80211_is_action(hdr->frame_control)))
		sta = NULL;

	if (sta) {
		if (iwl_mvm_defer_tx(mvm, sta, skb))
			return;
		if (iwl_mvm_tx_skb(mvm, skb, sta))
			goto drop;
		return;
	}

	if (iwl_mvm_tx_skb_non_sta(mvm, skb))
		goto drop;
	return;
 drop:
	ieee80211_free_txskb(hw, skb);
}

static inline bool iwl_enable_rx_ampdu(const struct iwl_cfg *cfg)
{
	if (iwlwifi_mod_params.disable_11n & IWL_DISABLE_HT_RXAGG)
		return false;
	return true;
}

static inline bool iwl_enable_tx_ampdu(const struct iwl_cfg *cfg)
{
	if (iwlwifi_mod_params.disable_11n & IWL_DISABLE_HT_TXAGG)
		return false;
	if (iwlwifi_mod_params.disable_11n & IWL_ENABLE_HT_TXAGG)
		return true;

	/* enabled by default */
	return true;
}

#define CHECK_BA_TRIGGER(_mvm, _trig, _tid_bm, _tid, _fmt...)	\
	do {							\
		if (!(le16_to_cpu(_tid_bm) & BIT(_tid)))	\
			break;					\
		iwl_mvm_fw_dbg_collect_trig(_mvm, _trig, _fmt);	\
	} while (0)

static void
iwl_mvm_ampdu_check_trigger(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, u16 tid, u16 rx_ba_ssn,
			    enum ieee80211_ampdu_mlme_action action)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_ba *ba_trig;

	if (!iwl_fw_dbg_trigger_enabled(mvm->fw, FW_DBG_TRIGGER_BA))
		return;

	trig = iwl_fw_dbg_get_trigger(mvm->fw, FW_DBG_TRIGGER_BA);
	ba_trig = (void *)trig->data;

	if (!iwl_fw_dbg_trigger_check_stop(mvm, vif, trig))
		return;

	switch (action) {
	case IEEE80211_AMPDU_TX_OPERATIONAL: {
		struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
		struct iwl_mvm_tid_data *tid_data = &mvmsta->tid_data[tid];

		CHECK_BA_TRIGGER(mvm, trig, ba_trig->tx_ba_start, tid,
				 "TX AGG START: MAC %pM tid %d ssn %d\n",
				 sta->addr, tid, tid_data->ssn);
		break;
		}
	case IEEE80211_AMPDU_TX_STOP_CONT:
		CHECK_BA_TRIGGER(mvm, trig, ba_trig->tx_ba_stop, tid,
				 "TX AGG STOP: MAC %pM tid %d\n",
				 sta->addr, tid);
		break;
	case IEEE80211_AMPDU_RX_START:
		CHECK_BA_TRIGGER(mvm, trig, ba_trig->rx_ba_start, tid,
				 "RX AGG START: MAC %pM tid %d ssn %d\n",
				 sta->addr, tid, rx_ba_ssn);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		CHECK_BA_TRIGGER(mvm, trig, ba_trig->rx_ba_stop, tid,
				 "RX AGG STOP: MAC %pM tid %d\n",
				 sta->addr, tid);
		break;
	default:
		break;
	}
}

static int iwl_mvm_mac_ampdu_action(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_ampdu_params *params)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;
	bool tx_agg_ref = false;
	struct ieee80211_sta *sta = params->sta;
	enum ieee80211_ampdu_mlme_action action = params->action;
	u16 tid = params->tid;
	u16 *ssn = &params->ssn;
	u8 buf_size = params->buf_size;
	bool amsdu = params->amsdu;
	u16 timeout = params->timeout;

	IWL_DEBUG_HT(mvm, "A-MPDU action on addr %pM tid %d: action %d\n",
		     sta->addr, tid, action);

	if (!(mvm->nvm_data->sku_cap_11n_enable))
		return -EACCES;

	/* return from D0i3 before starting a new Tx aggregation */
	switch (action) {
	case IEEE80211_AMPDU_TX_START:
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		/*
		 * for tx start, wait synchronously until D0i3 exit to
		 * get the correct sequence number for the tid.
		 * additionally, some other ampdu actions use direct
		 * target access, which is not handled automatically
		 * by the trans layer (unlike commands), so wait for
		 * d0i3 exit in these cases as well.
		 */
		ret = iwl_mvm_ref_sync(mvm, IWL_MVM_REF_TX_AGG);
		if (ret)
			return ret;

		tx_agg_ref = true;
		break;
	default:
		break;
	}

	mutex_lock(&mvm->mutex);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		if (!iwl_enable_rx_ampdu(mvm->cfg)) {
			ret = -EINVAL;
			break;
		}
		ret = iwl_mvm_sta_rx_agg(mvm, sta, tid, *ssn, true, buf_size,
					 timeout);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		ret = iwl_mvm_sta_rx_agg(mvm, sta, tid, 0, false, buf_size,
					 timeout);
		break;
	case IEEE80211_AMPDU_TX_START:
		if (!iwl_enable_tx_ampdu(mvm->cfg)) {
			ret = -EINVAL;
			break;
		}
		ret = iwl_mvm_sta_tx_agg_start(mvm, vif, sta, tid, ssn);
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
		ret = iwl_mvm_sta_tx_agg_stop(mvm, vif, sta, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		ret = iwl_mvm_sta_tx_agg_flush(mvm, vif, sta, tid);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		ret = iwl_mvm_sta_tx_agg_oper(mvm, vif, sta, tid,
					      buf_size, amsdu);
		break;
	default:
		WARN_ON_ONCE(1);
		ret = -EINVAL;
		break;
	}

	if (!ret) {
		u16 rx_ba_ssn = 0;

		if (action == IEEE80211_AMPDU_RX_START)
			rx_ba_ssn = *ssn;

		iwl_mvm_ampdu_check_trigger(mvm, vif, sta, tid,
					    rx_ba_ssn, action);
	}
	mutex_unlock(&mvm->mutex);

	/*
	 * If the tid is marked as started, we won't use it for offloaded
	 * traffic on the next D0i3 entry. It's safe to unref.
	 */
	if (tx_agg_ref)
		iwl_mvm_unref(mvm, IWL_MVM_REF_TX_AGG);

	return ret;
}

static void iwl_mvm_cleanup_iterator(void *data, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	mvmvif->uploaded = false;
	mvmvif->ap_sta_id = IWL_MVM_STATION_COUNT;

	spin_lock_bh(&mvm->time_event_lock);
	iwl_mvm_te_clear_data(mvm, &mvmvif->time_event_data);
	spin_unlock_bh(&mvm->time_event_lock);

	mvmvif->phy_ctxt = NULL;
	memset(&mvmvif->bf_data, 0, sizeof(mvmvif->bf_data));
}

static void iwl_mvm_restart_cleanup(struct iwl_mvm *mvm)
{
	/* clear the D3 reconfig, we only need it to avoid dumping a
	 * firmware coredump on reconfiguration, we shouldn't do that
	 * on D3->D0 transition
	 */
	if (!test_and_clear_bit(IWL_MVM_STATUS_D3_RECONFIG, &mvm->status)) {
		mvm->fw_dump_desc = &iwl_mvm_dump_desc_assert;
		iwl_mvm_fw_error_dump(mvm);
	}

	/* cleanup all stale references (scan, roc), but keep the
	 * ucode_down ref until reconfig is complete
	 */
	iwl_mvm_unref_all_except(mvm, IWL_MVM_REF_UCODE_DOWN);

	iwl_mvm_stop_device(mvm);

	mvm->scan_status = 0;
	mvm->ps_disabled = false;
	mvm->calibrating = false;

	/* just in case one was running */
	iwl_mvm_cleanup_roc_te(mvm);
	ieee80211_remain_on_channel_expired(mvm->hw);

	/*
	 * cleanup all interfaces, even inactive ones, as some might have
	 * gone down during the HW restart
	 */
	ieee80211_iterate_interfaces(mvm->hw, 0, iwl_mvm_cleanup_iterator, mvm);

	mvm->p2p_device_vif = NULL;
	mvm->d0i3_ap_sta_id = IWL_MVM_STATION_COUNT;

	iwl_mvm_reset_phy_ctxts(mvm);
	memset(mvm->fw_key_table, 0, sizeof(mvm->fw_key_table));
	memset(mvm->sta_drained, 0, sizeof(mvm->sta_drained));
	memset(mvm->sta_deferred_frames, 0, sizeof(mvm->sta_deferred_frames));
	memset(mvm->tfd_drained, 0, sizeof(mvm->tfd_drained));
	memset(&mvm->last_bt_notif, 0, sizeof(mvm->last_bt_notif));
	memset(&mvm->last_bt_ci_cmd, 0, sizeof(mvm->last_bt_ci_cmd));

	ieee80211_wake_queues(mvm->hw);

	/* clear any stale d0i3 state */
	clear_bit(IWL_MVM_STATUS_IN_D0I3, &mvm->status);

	mvm->vif_count = 0;
	mvm->rx_ba_sessions = 0;
	mvm->fw_dbg_conf = FW_DBG_INVALID;

	/* keep statistics ticking */
	iwl_mvm_accu_radio_stats(mvm);
}

int __iwl_mvm_mac_start(struct iwl_mvm *mvm)
{
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		/* Clean up some internal and mac80211 state on restart */
		iwl_mvm_restart_cleanup(mvm);
	} else {
		/* Hold the reference to prevent runtime suspend while
		 * the start procedure runs.  It's a bit confusing
		 * that the UCODE_DOWN reference is taken, but it just
		 * means "UCODE is not UP yet". ( TODO: rename this
		 * reference).
		 */
		iwl_mvm_ref(mvm, IWL_MVM_REF_UCODE_DOWN);
	}
	ret = iwl_mvm_up(mvm);

	if (ret && test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		/* Something went wrong - we need to finish some cleanup
		 * that normally iwl_mvm_mac_restart_complete() below
		 * would do.
		 */
		clear_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status);
		iwl_mvm_d0i3_enable_tx(mvm, NULL);
	}

	return ret;
}

static int iwl_mvm_mac_start(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	/* Some hw restart cleanups must not hold the mutex */
	if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		/*
		 * Make sure we are out of d0i3. This is needed
		 * to make sure the reference accounting is correct
		 * (and there is no stale d0i3_exit_work).
		 */
		wait_event_timeout(mvm->d0i3_exit_waitq,
				   !test_bit(IWL_MVM_STATUS_IN_D0I3,
					     &mvm->status),
				   HZ);
	}

	mutex_lock(&mvm->mutex);
	ret = __iwl_mvm_mac_start(mvm);
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void iwl_mvm_restart_complete(struct iwl_mvm *mvm)
{
	int ret;

	mutex_lock(&mvm->mutex);

	clear_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status);
	iwl_mvm_d0i3_enable_tx(mvm, NULL);
	ret = iwl_mvm_update_quotas(mvm, true, NULL);
	if (ret)
		IWL_ERR(mvm, "Failed to update quotas after restart (%d)\n",
			ret);

	/* allow transport/FW low power modes */
	iwl_mvm_unref(mvm, IWL_MVM_REF_UCODE_DOWN);

	/*
	 * If we have TDLS peers, remove them. We don't know the last seqno/PN
	 * of packets the FW sent out, so we must reconnect.
	 */
	iwl_mvm_teardown_tdls_peers(mvm);

	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_resume_complete(struct iwl_mvm *mvm)
{
	if (iwl_mvm_is_d0i3_supported(mvm) &&
	    iwl_mvm_enter_d0i3_on_suspend(mvm))
		WARN_ONCE(!wait_event_timeout(mvm->d0i3_exit_waitq,
					      !test_bit(IWL_MVM_STATUS_IN_D0I3,
							&mvm->status),
					      HZ),
			  "D0i3 exit on resume timed out\n");
}

static void
iwl_mvm_mac_reconfig_complete(struct ieee80211_hw *hw,
			      enum ieee80211_reconfig_type reconfig_type)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	switch (reconfig_type) {
	case IEEE80211_RECONFIG_TYPE_RESTART:
		iwl_mvm_restart_complete(mvm);
		break;
	case IEEE80211_RECONFIG_TYPE_SUSPEND:
		iwl_mvm_resume_complete(mvm);
		break;
	}
}

void __iwl_mvm_mac_stop(struct iwl_mvm *mvm)
{
	lockdep_assert_held(&mvm->mutex);

	/* firmware counters are obviously reset now, but we shouldn't
	 * partially track so also clear the fw_reset_accu counters.
	 */
	memset(&mvm->accu_radio_stats, 0, sizeof(mvm->accu_radio_stats));

	/* async_handlers_wk is now blocked */

	/*
	 * The work item could be running or queued if the
	 * ROC time event stops just as we get here.
	 */
	flush_work(&mvm->roc_done_wk);

	iwl_mvm_stop_device(mvm);

	iwl_mvm_async_handlers_purge(mvm);
	/* async_handlers_list is empty and will stay empty: HW is stopped */

	/* the fw is stopped, the aux sta is dead: clean up driver state */
	iwl_mvm_del_aux_sta(mvm);

	iwl_free_fw_paging(mvm);

	/*
	 * Clear IN_HW_RESTART flag when stopping the hw (as restart_complete()
	 * won't be called in this case).
	 * But make sure to cleanup interfaces that have gone down before/during
	 * HW restart was requested.
	 */
	if (test_and_clear_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
		ieee80211_iterate_interfaces(mvm->hw, 0,
					     iwl_mvm_cleanup_iterator, mvm);

	/* We shouldn't have any UIDs still set.  Loop over all the UIDs to
	 * make sure there's nothing left there and warn if any is found.
	 */
	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN)) {
		int i;

		for (i = 0; i < mvm->max_scans; i++) {
			if (WARN_ONCE(mvm->scan_uid_status[i],
				      "UMAC scan UID %d status was not cleaned\n",
				      i))
				mvm->scan_uid_status[i] = 0;
		}
	}
}

static void iwl_mvm_mac_stop(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	flush_work(&mvm->d0i3_exit_work);
	flush_work(&mvm->async_handlers_wk);
	flush_work(&mvm->add_stream_wk);
	cancel_delayed_work_sync(&mvm->fw_dump_wk);
	cancel_delayed_work_sync(&mvm->cs_tx_unblock_dwork);
	cancel_delayed_work_sync(&mvm->scan_timeout_dwork);
	iwl_mvm_free_fw_dump_desc(mvm);

	mutex_lock(&mvm->mutex);
	__iwl_mvm_mac_stop(mvm);
	mutex_unlock(&mvm->mutex);

	/*
	 * The worker might have been waiting for the mutex, let it run and
	 * discover that its list is now empty.
	 */
	cancel_work_sync(&mvm->async_handlers_wk);
}

static struct iwl_mvm_phy_ctxt *iwl_mvm_get_free_phy_ctxt(struct iwl_mvm *mvm)
{
	u16 i;

	lockdep_assert_held(&mvm->mutex);

	for (i = 0; i < NUM_PHY_CTX; i++)
		if (!mvm->phy_ctxts[i].ref)
			return &mvm->phy_ctxts[i];

	IWL_ERR(mvm, "No available PHY context\n");
	return NULL;
}

static int iwl_mvm_set_tx_power(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				s16 tx_power)
{
	struct iwl_dev_tx_power_cmd cmd = {
		.v3.set_mode = cpu_to_le32(IWL_TX_POWER_MODE_SET_MAC),
		.v3.mac_context_id =
			cpu_to_le32(iwl_mvm_vif_from_mac80211(vif)->id),
		.v3.pwr_restriction = cpu_to_le16(8 * tx_power),
	};
	int len = sizeof(cmd);

	if (tx_power == IWL_DEFAULT_MAX_TX_POWER)
		cmd.v3.pwr_restriction = cpu_to_le16(IWL_DEV_MAX_TX_POWER);

	if (!fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_TX_POWER_ACK))
		len = sizeof(cmd.v3);

	return iwl_mvm_send_cmd_pdu(mvm, REDUCE_TX_POWER_CMD, 0, len, &cmd);
}

static int iwl_mvm_mac_add_interface(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	mvmvif->mvm = mvm;

	/*
	 * make sure D0i3 exit is completed, otherwise a target access
	 * during tx queue configuration could be done when still in
	 * D0i3 state.
	 */
	ret = iwl_mvm_ref_sync(mvm, IWL_MVM_REF_ADD_IF);
	if (ret)
		return ret;

	/*
	 * Not much to do here. The stack will not allow interface
	 * types or combinations that we didn't advertise, so we
	 * don't really have to check the types.
	 */

	mutex_lock(&mvm->mutex);

	/* make sure that beacon statistics don't go backwards with FW reset */
	if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
		mvmvif->beacon_stats.accu_num_beacons +=
			mvmvif->beacon_stats.num_beacons;

	/* Allocate resources for the MAC context, and add it to the fw  */
	ret = iwl_mvm_mac_ctxt_init(mvm, vif);
	if (ret)
		goto out_unlock;

	/* Counting number of interfaces is needed for legacy PM */
	if (vif->type != NL80211_IFTYPE_P2P_DEVICE)
		mvm->vif_count++;

	/*
	 * The AP binding flow can be done only after the beacon
	 * template is configured (which happens only in the mac80211
	 * start_ap() flow), and adding the broadcast station can happen
	 * only after the binding.
	 * In addition, since modifying the MAC before adding a bcast
	 * station is not allowed by the FW, delay the adding of MAC context to
	 * the point where we can also add the bcast station.
	 * In short: there's not much we can do at this point, other than
	 * allocating resources :)
	 */
	if (vif->type == NL80211_IFTYPE_AP ||
	    vif->type == NL80211_IFTYPE_ADHOC) {
		ret = iwl_mvm_alloc_bcast_sta(mvm, vif);
		if (ret) {
			IWL_ERR(mvm, "Failed to allocate bcast sta\n");
			goto out_release;
		}

		iwl_mvm_vif_dbgfs_register(mvm, vif);
		goto out_unlock;
	}

	mvmvif->features |= hw->netdev_features;

	ret = iwl_mvm_mac_ctxt_add(mvm, vif);
	if (ret)
		goto out_release;

	ret = iwl_mvm_power_update_mac(mvm);
	if (ret)
		goto out_remove_mac;

	/* beacon filtering */
	ret = iwl_mvm_disable_beacon_filter(mvm, vif, 0);
	if (ret)
		goto out_remove_mac;

	if (!mvm->bf_allowed_vif &&
	    vif->type == NL80211_IFTYPE_STATION && !vif->p2p) {
		mvm->bf_allowed_vif = mvmvif;
		vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER |
				     IEEE80211_VIF_SUPPORTS_CQM_RSSI;
	}

	/*
	 * P2P_DEVICE interface does not have a channel context assigned to it,
	 * so a dedicated PHY context is allocated to it and the corresponding
	 * MAC context is bound to it at this stage.
	 */
	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {

		mvmvif->phy_ctxt = iwl_mvm_get_free_phy_ctxt(mvm);
		if (!mvmvif->phy_ctxt) {
			ret = -ENOSPC;
			goto out_free_bf;
		}

		iwl_mvm_phy_ctxt_ref(mvm, mvmvif->phy_ctxt);
		ret = iwl_mvm_binding_add_vif(mvm, vif);
		if (ret)
			goto out_unref_phy;

		ret = iwl_mvm_add_bcast_sta(mvm, vif);
		if (ret)
			goto out_unbind;

		/* Save a pointer to p2p device vif, so it can later be used to
		 * update the p2p device MAC when a GO is started/stopped */
		mvm->p2p_device_vif = vif;
	}

	iwl_mvm_vif_dbgfs_register(mvm, vif);
	goto out_unlock;

 out_unbind:
	iwl_mvm_binding_remove_vif(mvm, vif);
 out_unref_phy:
	iwl_mvm_phy_ctxt_unref(mvm, mvmvif->phy_ctxt);
 out_free_bf:
	if (mvm->bf_allowed_vif == mvmvif) {
		mvm->bf_allowed_vif = NULL;
		vif->driver_flags &= ~(IEEE80211_VIF_BEACON_FILTER |
				       IEEE80211_VIF_SUPPORTS_CQM_RSSI);
	}
 out_remove_mac:
	mvmvif->phy_ctxt = NULL;
	iwl_mvm_mac_ctxt_remove(mvm, vif);
 out_release:
	if (vif->type != NL80211_IFTYPE_P2P_DEVICE)
		mvm->vif_count--;

	iwl_mvm_mac_ctxt_release(mvm, vif);
 out_unlock:
	mutex_unlock(&mvm->mutex);

	iwl_mvm_unref(mvm, IWL_MVM_REF_ADD_IF);

	return ret;
}

static void iwl_mvm_prepare_mac_removal(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif)
{
	u32 tfd_msk = iwl_mvm_mac_get_queues_mask(vif);

	if (tfd_msk) {
		/*
		 * mac80211 first removes all the stations of the vif and
		 * then removes the vif. When it removes a station it also
		 * flushes the AMPDU session. So by now, all the AMPDU sessions
		 * of all the stations of this vif are closed, and the queues
		 * of these AMPDU sessions are properly closed.
		 * We still need to take care of the shared queues of the vif.
		 * Flush them here.
		 */
		mutex_lock(&mvm->mutex);
		iwl_mvm_flush_tx_path(mvm, tfd_msk, 0);
		mutex_unlock(&mvm->mutex);

		/*
		 * There are transports that buffer a few frames in the host.
		 * For these, the flush above isn't enough since while we were
		 * flushing, the transport might have sent more frames to the
		 * device. To solve this, wait here until the transport is
		 * empty. Technically, this could have replaced the flush
		 * above, but flush is much faster than draining. So flush
		 * first, and drain to make sure we have no frames in the
		 * transport anymore.
		 * If a station still had frames on the shared queues, it is
		 * already marked as draining, so to complete the draining, we
		 * just need to wait until the transport is empty.
		 */
		iwl_trans_wait_tx_queue_empty(mvm->trans, tfd_msk);
	}

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		/*
		 * Flush the ROC worker which will flush the OFFCHANNEL queue.
		 * We assume here that all the packets sent to the OFFCHANNEL
		 * queue are sent in ROC session.
		 */
		flush_work(&mvm->roc_done_wk);
	} else {
		/*
		 * By now, all the AC queues are empty. The AGG queues are
		 * empty too. We already got all the Tx responses for all the
		 * packets in the queues. The drain work can have been
		 * triggered. Flush it.
		 */
		flush_work(&mvm->sta_drained_wk);
	}
}

static void iwl_mvm_mac_remove_interface(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	iwl_mvm_prepare_mac_removal(mvm, vif);

	mutex_lock(&mvm->mutex);

	if (mvm->bf_allowed_vif == mvmvif) {
		mvm->bf_allowed_vif = NULL;
		vif->driver_flags &= ~(IEEE80211_VIF_BEACON_FILTER |
				       IEEE80211_VIF_SUPPORTS_CQM_RSSI);
	}

	iwl_mvm_vif_dbgfs_clean(mvm, vif);

	/*
	 * For AP/GO interface, the tear down of the resources allocated to the
	 * interface is be handled as part of the stop_ap flow.
	 */
	if (vif->type == NL80211_IFTYPE_AP ||
	    vif->type == NL80211_IFTYPE_ADHOC) {
#ifdef CONFIG_NL80211_TESTMODE
		if (vif == mvm->noa_vif) {
			mvm->noa_vif = NULL;
			mvm->noa_duration = 0;
		}
#endif
		iwl_mvm_dealloc_bcast_sta(mvm, vif);
		goto out_release;
	}

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		mvm->p2p_device_vif = NULL;
		iwl_mvm_rm_bcast_sta(mvm, vif);
		iwl_mvm_binding_remove_vif(mvm, vif);
		iwl_mvm_phy_ctxt_unref(mvm, mvmvif->phy_ctxt);
		mvmvif->phy_ctxt = NULL;
	}

	if (mvm->vif_count && vif->type != NL80211_IFTYPE_P2P_DEVICE)
		mvm->vif_count--;

	iwl_mvm_power_update_mac(mvm);
	iwl_mvm_mac_ctxt_remove(mvm, vif);

out_release:
	iwl_mvm_mac_ctxt_release(mvm, vif);
	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_mac_config(struct ieee80211_hw *hw, u32 changed)
{
	return 0;
}

struct iwl_mvm_mc_iter_data {
	struct iwl_mvm *mvm;
	int port_id;
};

static void iwl_mvm_mc_iface_iterator(void *_data, u8 *mac,
				      struct ieee80211_vif *vif)
{
	struct iwl_mvm_mc_iter_data *data = _data;
	struct iwl_mvm *mvm = data->mvm;
	struct iwl_mcast_filter_cmd *cmd = mvm->mcast_filter_cmd;
	int ret, len;

	/* if we don't have free ports, mcast frames will be dropped */
	if (WARN_ON_ONCE(data->port_id >= MAX_PORT_ID_NUM))
		return;

	if (vif->type != NL80211_IFTYPE_STATION ||
	    !vif->bss_conf.assoc)
		return;

	cmd->port_id = data->port_id++;
	memcpy(cmd->bssid, vif->bss_conf.bssid, ETH_ALEN);
	len = roundup(sizeof(*cmd) + cmd->count * ETH_ALEN, 4);

	ret = iwl_mvm_send_cmd_pdu(mvm, MCAST_FILTER_CMD, CMD_ASYNC, len, cmd);
	if (ret)
		IWL_ERR(mvm, "mcast filter cmd error. ret=%d\n", ret);
}

static void iwl_mvm_recalc_multicast(struct iwl_mvm *mvm)
{
	struct iwl_mvm_mc_iter_data iter_data = {
		.mvm = mvm,
	};

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON_ONCE(!mvm->mcast_filter_cmd))
		return;

	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_mc_iface_iterator, &iter_data);
}

static u64 iwl_mvm_prepare_multicast(struct ieee80211_hw *hw,
				     struct netdev_hw_addr_list *mc_list)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mcast_filter_cmd *cmd;
	struct netdev_hw_addr *addr;
	int addr_count;
	bool pass_all;
	int len;

	addr_count = netdev_hw_addr_list_count(mc_list);
	pass_all = addr_count > MAX_MCAST_FILTERING_ADDRESSES ||
		   IWL_MVM_FW_MCAST_FILTER_PASS_ALL;
	if (pass_all)
		addr_count = 0;

	len = roundup(sizeof(*cmd) + addr_count * ETH_ALEN, 4);
	cmd = kzalloc(len, GFP_ATOMIC);
	if (!cmd)
		return 0;

	if (pass_all) {
		cmd->pass_all = 1;
		return (u64)(unsigned long)cmd;
	}

	netdev_hw_addr_list_for_each(addr, mc_list) {
		IWL_DEBUG_MAC80211(mvm, "mcast addr (%d): %pM\n",
				   cmd->count, addr->addr);
		memcpy(&cmd->addr_list[cmd->count * ETH_ALEN],
		       addr->addr, ETH_ALEN);
		cmd->count++;
	}

	return (u64)(unsigned long)cmd;
}

static void iwl_mvm_configure_filter(struct ieee80211_hw *hw,
				     unsigned int changed_flags,
				     unsigned int *total_flags,
				     u64 multicast)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mcast_filter_cmd *cmd = (void *)(unsigned long)multicast;

	mutex_lock(&mvm->mutex);

	/* replace previous configuration */
	kfree(mvm->mcast_filter_cmd);
	mvm->mcast_filter_cmd = cmd;

	if (!cmd)
		goto out;

	iwl_mvm_recalc_multicast(mvm);
out:
	mutex_unlock(&mvm->mutex);
	*total_flags = 0;
}

static void iwl_mvm_config_iface_filter(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					unsigned int filter_flags,
					unsigned int changed_flags)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	/* We support only filter for probe requests */
	if (!(changed_flags & FIF_PROBE_REQ))
		return;

	/* Supported only for p2p client interfaces */
	if (vif->type != NL80211_IFTYPE_STATION || !vif->bss_conf.assoc ||
	    !vif->p2p)
		return;

	mutex_lock(&mvm->mutex);
	iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
	mutex_unlock(&mvm->mutex);
}

#ifdef CONFIG_IWLWIFI_BCAST_FILTERING
struct iwl_bcast_iter_data {
	struct iwl_mvm *mvm;
	struct iwl_bcast_filter_cmd *cmd;
	u8 current_filter;
};

static void
iwl_mvm_set_bcast_filter(struct ieee80211_vif *vif,
			 const struct iwl_fw_bcast_filter *in_filter,
			 struct iwl_fw_bcast_filter *out_filter)
{
	struct iwl_fw_bcast_filter_attr *attr;
	int i;

	memcpy(out_filter, in_filter, sizeof(*out_filter));

	for (i = 0; i < ARRAY_SIZE(out_filter->attrs); i++) {
		attr = &out_filter->attrs[i];

		if (!attr->mask)
			break;

		switch (attr->reserved1) {
		case cpu_to_le16(BC_FILTER_MAGIC_IP):
			if (vif->bss_conf.arp_addr_cnt != 1) {
				attr->mask = 0;
				continue;
			}

			attr->val = vif->bss_conf.arp_addr_list[0];
			break;
		case cpu_to_le16(BC_FILTER_MAGIC_MAC):
			attr->val = *(__be32 *)&vif->addr[2];
			break;
		default:
			break;
		}
		attr->reserved1 = 0;
		out_filter->num_attrs++;
	}
}

static void iwl_mvm_bcast_filter_iterator(void *_data, u8 *mac,
					  struct ieee80211_vif *vif)
{
	struct iwl_bcast_iter_data *data = _data;
	struct iwl_mvm *mvm = data->mvm;
	struct iwl_bcast_filter_cmd *cmd = data->cmd;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_fw_bcast_mac *bcast_mac;
	int i;

	if (WARN_ON(mvmvif->id >= ARRAY_SIZE(cmd->macs)))
		return;

	bcast_mac = &cmd->macs[mvmvif->id];

	/*
	 * enable filtering only for associated stations, but not for P2P
	 * Clients
	 */
	if (vif->type != NL80211_IFTYPE_STATION || vif->p2p ||
	    !vif->bss_conf.assoc)
		return;

	bcast_mac->default_discard = 1;

	/* copy all configured filters */
	for (i = 0; mvm->bcast_filters[i].attrs[0].mask; i++) {
		/*
		 * Make sure we don't exceed our filters limit.
		 * if there is still a valid filter to be configured,
		 * be on the safe side and just allow bcast for this mac.
		 */
		if (WARN_ON_ONCE(data->current_filter >=
				 ARRAY_SIZE(cmd->filters))) {
			bcast_mac->default_discard = 0;
			bcast_mac->attached_filters = 0;
			break;
		}

		iwl_mvm_set_bcast_filter(vif,
					 &mvm->bcast_filters[i],
					 &cmd->filters[data->current_filter]);

		/* skip current filter if it contains no attributes */
		if (!cmd->filters[data->current_filter].num_attrs)
			continue;

		/* attach the filter to current mac */
		bcast_mac->attached_filters |=
				cpu_to_le16(BIT(data->current_filter));

		data->current_filter++;
	}
}

bool iwl_mvm_bcast_filter_build_cmd(struct iwl_mvm *mvm,
				    struct iwl_bcast_filter_cmd *cmd)
{
	struct iwl_bcast_iter_data iter_data = {
		.mvm = mvm,
		.cmd = cmd,
	};

	if (IWL_MVM_FW_BCAST_FILTER_PASS_ALL)
		return false;

	memset(cmd, 0, sizeof(*cmd));
	cmd->max_bcast_filters = ARRAY_SIZE(cmd->filters);
	cmd->max_macs = ARRAY_SIZE(cmd->macs);

#ifdef CONFIG_IWLWIFI_DEBUGFS
	/* use debugfs filters/macs if override is configured */
	if (mvm->dbgfs_bcast_filtering.override) {
		memcpy(cmd->filters, &mvm->dbgfs_bcast_filtering.cmd.filters,
		       sizeof(cmd->filters));
		memcpy(cmd->macs, &mvm->dbgfs_bcast_filtering.cmd.macs,
		       sizeof(cmd->macs));
		return true;
	}
#endif

	/* if no filters are configured, do nothing */
	if (!mvm->bcast_filters)
		return false;

	/* configure and attach these filters for each associated sta vif */
	ieee80211_iterate_active_interfaces(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_bcast_filter_iterator, &iter_data);

	return true;
}

static int iwl_mvm_configure_bcast_filter(struct iwl_mvm *mvm)
{
	struct iwl_bcast_filter_cmd cmd;

	if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_BCAST_FILTERING))
		return 0;

	if (!iwl_mvm_bcast_filter_build_cmd(mvm, &cmd))
		return 0;

	return iwl_mvm_send_cmd_pdu(mvm, BCAST_FILTER_CMD, 0,
				    sizeof(cmd), &cmd);
}
#else
static inline int iwl_mvm_configure_bcast_filter(struct iwl_mvm *mvm)
{
	return 0;
}
#endif

static int iwl_mvm_update_mu_groups(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif)
{
	struct iwl_mu_group_mgmt_cmd cmd = {};

	memcpy(cmd.membership_status, vif->bss_conf.mu_group.membership,
	       WLAN_MEMBERSHIP_LEN);
	memcpy(cmd.user_position, vif->bss_conf.mu_group.position,
	       WLAN_USER_POSITION_LEN);

	return iwl_mvm_send_cmd_pdu(mvm,
				    WIDE_ID(DATA_PATH_GROUP,
					    UPDATE_MU_GROUPS_CMD),
				    0, sizeof(cmd), &cmd);
}

static void iwl_mvm_mu_mimo_iface_iterator(void *_data, u8 *mac,
					   struct ieee80211_vif *vif)
{
	if (vif->mu_mimo_owner) {
		struct iwl_mu_group_mgmt_notif *notif = _data;

		/*
		 * MU-MIMO Group Id action frame is little endian. We treat
		 * the data received from firmware as if it came from the
		 * action frame, so no conversion is needed.
		 */
		ieee80211_update_mu_groups(vif,
					   (u8 *)&notif->membership_status,
					   (u8 *)&notif->user_position);
	}
}

void iwl_mvm_mu_mimo_grp_notif(struct iwl_mvm *mvm,
			       struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mu_group_mgmt_notif *notif = (void *)pkt->data;

	ieee80211_iterate_active_interfaces_atomic(
			mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
			iwl_mvm_mu_mimo_iface_iterator, notif);
}

static void iwl_mvm_bss_info_changed_station(struct iwl_mvm *mvm,
					     struct ieee80211_vif *vif,
					     struct ieee80211_bss_conf *bss_conf,
					     u32 changes)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	/*
	 * Re-calculate the tsf id, as the master-slave relations depend on the
	 * beacon interval, which was not known when the station interface was
	 * added.
	 */
	if (changes & BSS_CHANGED_ASSOC && bss_conf->assoc)
		iwl_mvm_mac_ctxt_recalc_tsf_id(mvm, vif);

	if (changes & BSS_CHANGED_ASSOC && !bss_conf->assoc &&
	    mvmvif->lqm_active)
		iwl_mvm_send_lqm_cmd(vif, LQM_CMD_OPERATION_STOP_MEASUREMENT,
				     0, 0);

	/*
	 * If we're not associated yet, take the (new) BSSID before associating
	 * so the firmware knows. If we're already associated, then use the old
	 * BSSID here, and we'll send a cleared one later in the CHANGED_ASSOC
	 * branch for disassociation below.
	 */
	if (changes & BSS_CHANGED_BSSID && !mvmvif->associated)
		memcpy(mvmvif->bssid, bss_conf->bssid, ETH_ALEN);

	ret = iwl_mvm_mac_ctxt_changed(mvm, vif, false, mvmvif->bssid);
	if (ret)
		IWL_ERR(mvm, "failed to update MAC %pM\n", vif->addr);

	/* after sending it once, adopt mac80211 data */
	memcpy(mvmvif->bssid, bss_conf->bssid, ETH_ALEN);
	mvmvif->associated = bss_conf->assoc;

	if (changes & BSS_CHANGED_ASSOC) {
		if (bss_conf->assoc) {
			/* clear statistics to get clean beacon counter */
			iwl_mvm_request_statistics(mvm, true);
			memset(&mvmvif->beacon_stats, 0,
			       sizeof(mvmvif->beacon_stats));

			/* add quota for this interface */
			ret = iwl_mvm_update_quotas(mvm, true, NULL);
			if (ret) {
				IWL_ERR(mvm, "failed to update quotas\n");
				return;
			}

			if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART,
				     &mvm->status)) {
				/*
				 * If we're restarting then the firmware will
				 * obviously have lost synchronisation with
				 * the AP. It will attempt to synchronise by
				 * itself, but we can make it more reliable by
				 * scheduling a session protection time event.
				 *
				 * The firmware needs to receive a beacon to
				 * catch up with synchronisation, use 110% of
				 * the beacon interval.
				 *
				 * Set a large maximum delay to allow for more
				 * than a single interface.
				 */
				u32 dur = (11 * vif->bss_conf.beacon_int) / 10;
				iwl_mvm_protect_session(mvm, vif, dur, dur,
							5 * dur, false);
			}

			iwl_mvm_sf_update(mvm, vif, false);
			iwl_mvm_power_vif_assoc(mvm, vif);
			if (vif->p2p) {
				iwl_mvm_ref(mvm, IWL_MVM_REF_P2P_CLIENT);
				iwl_mvm_update_smps(mvm, vif,
						    IWL_MVM_SMPS_REQ_PROT,
						    IEEE80211_SMPS_DYNAMIC);
			}
		} else if (mvmvif->ap_sta_id != IWL_MVM_STATION_COUNT) {
			/*
			 * If update fails - SF might be running in associated
			 * mode while disassociated - which is forbidden.
			 */
			WARN_ONCE(iwl_mvm_sf_update(mvm, vif, false),
				  "Failed to update SF upon disassociation\n");

			/* remove AP station now that the MAC is unassoc */
			ret = iwl_mvm_rm_sta_id(mvm, vif, mvmvif->ap_sta_id);
			if (ret)
				IWL_ERR(mvm, "failed to remove AP station\n");

			if (mvm->d0i3_ap_sta_id == mvmvif->ap_sta_id)
				mvm->d0i3_ap_sta_id = IWL_MVM_STATION_COUNT;
			mvmvif->ap_sta_id = IWL_MVM_STATION_COUNT;
			/* remove quota for this interface */
			ret = iwl_mvm_update_quotas(mvm, false, NULL);
			if (ret)
				IWL_ERR(mvm, "failed to update quotas\n");

			if (vif->p2p)
				iwl_mvm_unref(mvm, IWL_MVM_REF_P2P_CLIENT);

			/* this will take the cleared BSSID from bss_conf */
			ret = iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
			if (ret)
				IWL_ERR(mvm,
					"failed to update MAC %pM (clear after unassoc)\n",
					vif->addr);
		}

		/*
		 * The firmware tracks the MU-MIMO group on its own.
		 * However, on HW restart we should restore this data.
		 */
		if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) &&
		    (changes & BSS_CHANGED_MU_GROUPS) && vif->mu_mimo_owner) {
			ret = iwl_mvm_update_mu_groups(mvm, vif);
			if (ret)
				IWL_ERR(mvm,
					"failed to update VHT MU_MIMO groups\n");
		}

		iwl_mvm_recalc_multicast(mvm);
		iwl_mvm_configure_bcast_filter(mvm);

		/* reset rssi values */
		mvmvif->bf_data.ave_beacon_signal = 0;

		iwl_mvm_bt_coex_vif_change(mvm);
		iwl_mvm_update_smps(mvm, vif, IWL_MVM_SMPS_REQ_TT,
				    IEEE80211_SMPS_AUTOMATIC);
		if (fw_has_capa(&mvm->fw->ucode_capa,
				IWL_UCODE_TLV_CAPA_UMAC_SCAN))
			iwl_mvm_config_scan(mvm);
	} else if (changes & BSS_CHANGED_BEACON_INFO) {
		/*
		 * We received a beacon _after_ association so
		 * remove the session protection.
		 */
		iwl_mvm_remove_time_event(mvm, mvmvif,
					  &mvmvif->time_event_data);
	}

	if (changes & BSS_CHANGED_BEACON_INFO) {
		iwl_mvm_sf_update(mvm, vif, false);
		WARN_ON(iwl_mvm_enable_beacon_filter(mvm, vif, 0));
	}

	if (changes & (BSS_CHANGED_PS | BSS_CHANGED_P2P_PS | BSS_CHANGED_QOS |
		       /*
			* Send power command on every beacon change,
			* because we may have not enabled beacon abort yet.
			*/
		       BSS_CHANGED_BEACON_INFO)) {
		ret = iwl_mvm_power_update_mac(mvm);
		if (ret)
			IWL_ERR(mvm, "failed to update power mode\n");
	}

	if (changes & BSS_CHANGED_TXPOWER) {
		IWL_DEBUG_CALIB(mvm, "Changing TX Power to %d\n",
				bss_conf->txpower);
		iwl_mvm_set_tx_power(mvm, vif, bss_conf->txpower);
	}

	if (changes & BSS_CHANGED_CQM) {
		IWL_DEBUG_MAC80211(mvm, "cqm info_changed\n");
		/* reset cqm events tracking */
		mvmvif->bf_data.last_cqm_event = 0;
		if (mvmvif->bf_data.bf_enabled) {
			ret = iwl_mvm_enable_beacon_filter(mvm, vif, 0);
			if (ret)
				IWL_ERR(mvm,
					"failed to update CQM thresholds\n");
		}
	}

	if (changes & BSS_CHANGED_ARP_FILTER) {
		IWL_DEBUG_MAC80211(mvm, "arp filter changed\n");
		iwl_mvm_configure_bcast_filter(mvm);
	}
}

static int iwl_mvm_start_ap_ibss(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	/*
	 * iwl_mvm_mac_ctxt_add() might read directly from the device
	 * (the system time), so make sure it is available.
	 */
	ret = iwl_mvm_ref_sync(mvm, IWL_MVM_REF_START_AP);
	if (ret)
		return ret;

	mutex_lock(&mvm->mutex);

	/* Send the beacon template */
	ret = iwl_mvm_mac_ctxt_beacon_changed(mvm, vif);
	if (ret)
		goto out_unlock;

	/*
	 * Re-calculate the tsf id, as the master-slave relations depend on the
	 * beacon interval, which was not known when the AP interface was added.
	 */
	if (vif->type == NL80211_IFTYPE_AP)
		iwl_mvm_mac_ctxt_recalc_tsf_id(mvm, vif);

	mvmvif->ap_assoc_sta_count = 0;

	/* Add the mac context */
	ret = iwl_mvm_mac_ctxt_add(mvm, vif);
	if (ret)
		goto out_unlock;

	/* Perform the binding */
	ret = iwl_mvm_binding_add_vif(mvm, vif);
	if (ret)
		goto out_remove;

	/* Send the bcast station. At this stage the TBTT and DTIM time events
	 * are added and applied to the scheduler */
	ret = iwl_mvm_send_add_bcast_sta(mvm, vif);
	if (ret)
		goto out_unbind;

	/* must be set before quota calculations */
	mvmvif->ap_ibss_active = true;

	/* power updated needs to be done before quotas */
	iwl_mvm_power_update_mac(mvm);

	ret = iwl_mvm_update_quotas(mvm, false, NULL);
	if (ret)
		goto out_quota_failed;

	/* Need to update the P2P Device MAC (only GO, IBSS is single vif) */
	if (vif->p2p && mvm->p2p_device_vif)
		iwl_mvm_mac_ctxt_changed(mvm, mvm->p2p_device_vif, false, NULL);

	iwl_mvm_ref(mvm, IWL_MVM_REF_AP_IBSS);

	iwl_mvm_bt_coex_vif_change(mvm);

	/* we don't support TDLS during DCM */
	if (iwl_mvm_phy_ctx_count(mvm) > 1)
		iwl_mvm_teardown_tdls_peers(mvm);

	goto out_unlock;

out_quota_failed:
	iwl_mvm_power_update_mac(mvm);
	mvmvif->ap_ibss_active = false;
	iwl_mvm_send_rm_bcast_sta(mvm, vif);
out_unbind:
	iwl_mvm_binding_remove_vif(mvm, vif);
out_remove:
	iwl_mvm_mac_ctxt_remove(mvm, vif);
out_unlock:
	mutex_unlock(&mvm->mutex);
	iwl_mvm_unref(mvm, IWL_MVM_REF_START_AP);
	return ret;
}

static void iwl_mvm_stop_ap_ibss(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	iwl_mvm_prepare_mac_removal(mvm, vif);

	mutex_lock(&mvm->mutex);

	/* Handle AP stop while in CSA */
	if (rcu_access_pointer(mvm->csa_vif) == vif) {
		iwl_mvm_remove_time_event(mvm, mvmvif,
					  &mvmvif->time_event_data);
		RCU_INIT_POINTER(mvm->csa_vif, NULL);
		mvmvif->csa_countdown = false;
	}

	if (rcu_access_pointer(mvm->csa_tx_blocked_vif) == vif) {
		RCU_INIT_POINTER(mvm->csa_tx_blocked_vif, NULL);
		mvm->csa_tx_block_bcn_timeout = 0;
	}

	mvmvif->ap_ibss_active = false;
	mvm->ap_last_beacon_gp2 = 0;

	iwl_mvm_bt_coex_vif_change(mvm);

	iwl_mvm_unref(mvm, IWL_MVM_REF_AP_IBSS);

	/* Need to update the P2P Device MAC (only GO, IBSS is single vif) */
	if (vif->p2p && mvm->p2p_device_vif)
		iwl_mvm_mac_ctxt_changed(mvm, mvm->p2p_device_vif, false, NULL);

	iwl_mvm_update_quotas(mvm, false, NULL);
	iwl_mvm_send_rm_bcast_sta(mvm, vif);
	iwl_mvm_binding_remove_vif(mvm, vif);

	iwl_mvm_power_update_mac(mvm);

	iwl_mvm_mac_ctxt_remove(mvm, vif);

	mutex_unlock(&mvm->mutex);
}

static void
iwl_mvm_bss_info_changed_ap_ibss(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *bss_conf,
				 u32 changes)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	/* Changes will be applied when the AP/IBSS is started */
	if (!mvmvif->ap_ibss_active)
		return;

	if (changes & (BSS_CHANGED_ERP_CTS_PROT | BSS_CHANGED_HT |
		       BSS_CHANGED_BANDWIDTH | BSS_CHANGED_QOS) &&
	    iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL))
		IWL_ERR(mvm, "failed to update MAC %pM\n", vif->addr);

	/* Need to send a new beacon template to the FW */
	if (changes & BSS_CHANGED_BEACON &&
	    iwl_mvm_mac_ctxt_beacon_changed(mvm, vif))
		IWL_WARN(mvm, "Failed updating beacon data\n");

	if (changes & BSS_CHANGED_TXPOWER) {
		IWL_DEBUG_CALIB(mvm, "Changing TX Power to %d\n",
				bss_conf->txpower);
		iwl_mvm_set_tx_power(mvm, vif, bss_conf->txpower);
	}
}

static void iwl_mvm_bss_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *bss_conf,
				     u32 changes)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	/*
	 * iwl_mvm_bss_info_changed_station() might call
	 * iwl_mvm_protect_session(), which reads directly from
	 * the device (the system time), so make sure it is available.
	 */
	if (iwl_mvm_ref_sync(mvm, IWL_MVM_REF_BSS_CHANGED))
		return;

	mutex_lock(&mvm->mutex);

	if (changes & BSS_CHANGED_IDLE && !bss_conf->idle)
		iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_SCHED, true);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		iwl_mvm_bss_info_changed_station(mvm, vif, bss_conf, changes);
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
		iwl_mvm_bss_info_changed_ap_ibss(mvm, vif, bss_conf, changes);
		break;
	case NL80211_IFTYPE_MONITOR:
		if (changes & BSS_CHANGED_MU_GROUPS)
			iwl_mvm_update_mu_groups(mvm, vif);
		break;
	default:
		/* shouldn't happen */
		WARN_ON_ONCE(1);
	}

	mutex_unlock(&mvm->mutex);
	iwl_mvm_unref(mvm, IWL_MVM_REF_BSS_CHANGED);
}

static int iwl_mvm_mac_hw_scan(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_scan_request *hw_req)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	if (hw_req->req.n_channels == 0 ||
	    hw_req->req.n_channels > mvm->fw->ucode_capa.n_scan_channels)
		return -EINVAL;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_reg_scan_start(mvm, vif, &hw_req->req, &hw_req->ies);
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void iwl_mvm_mac_cancel_hw_scan(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);

	/* Due to a race condition, it's possible that mac80211 asks
	 * us to stop a hw_scan when it's already stopped.  This can
	 * happen, for instance, if we stopped the scan ourselves,
	 * called ieee80211_scan_completed() and the userspace called
	 * cancel scan scan before ieee80211_scan_work() could run.
	 * To handle that, simply return if the scan is not running.
	*/
	if (mvm->scan_status & IWL_MVM_SCAN_REGULAR)
		iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_REGULAR, true);

	mutex_unlock(&mvm->mutex);
}

static void
iwl_mvm_mac_allow_buffered_frames(struct ieee80211_hw *hw,
				  struct ieee80211_sta *sta, u16 tids,
				  int num_frames,
				  enum ieee80211_frame_release_type reason,
				  bool more_data)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	/* Called when we need to transmit (a) frame(s) from mac80211 */

	iwl_mvm_sta_modify_sleep_tx_count(mvm, sta, reason, num_frames,
					  tids, more_data, false);
}

static void
iwl_mvm_mac_release_buffered_frames(struct ieee80211_hw *hw,
				    struct ieee80211_sta *sta, u16 tids,
				    int num_frames,
				    enum ieee80211_frame_release_type reason,
				    bool more_data)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	/* Called when we need to transmit (a) frame(s) from agg queue */

	iwl_mvm_sta_modify_sleep_tx_count(mvm, sta, reason, num_frames,
					  tids, more_data, true);
}

static void iwl_mvm_mac_sta_notify(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   enum sta_notify_cmd cmd,
				   struct ieee80211_sta *sta)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	unsigned long txqs = 0, tids = 0;
	int tid;

	spin_lock_bh(&mvmsta->lock);
	for (tid = 0; tid < IWL_MAX_TID_COUNT; tid++) {
		struct iwl_mvm_tid_data *tid_data = &mvmsta->tid_data[tid];

		if (tid_data->state != IWL_AGG_ON &&
		    tid_data->state != IWL_EMPTYING_HW_QUEUE_DELBA)
			continue;

		__set_bit(tid_data->txq_id, &txqs);

		if (iwl_mvm_tid_queued(tid_data) == 0)
			continue;

		__set_bit(tid, &tids);
	}

	switch (cmd) {
	case STA_NOTIFY_SLEEP:
		if (atomic_read(&mvm->pending_frames[mvmsta->sta_id]) > 0)
			ieee80211_sta_block_awake(hw, sta, true);

		for_each_set_bit(tid, &tids, IWL_MAX_TID_COUNT)
			ieee80211_sta_set_buffered(sta, tid, true);

		if (txqs)
			iwl_trans_freeze_txq_timer(mvm->trans, txqs, true);
		/*
		 * The fw updates the STA to be asleep. Tx packets on the Tx
		 * queues to this station will not be transmitted. The fw will
		 * send a Tx response with TX_STATUS_FAIL_DEST_PS.
		 */
		break;
	case STA_NOTIFY_AWAKE:
		if (WARN_ON(mvmsta->sta_id == IWL_MVM_STATION_COUNT))
			break;

		if (txqs)
			iwl_trans_freeze_txq_timer(mvm->trans, txqs, false);
		iwl_mvm_sta_modify_ps_wake(mvm, sta);
		break;
	default:
		break;
	}
	spin_unlock_bh(&mvmsta->lock);
}

static void iwl_mvm_sta_pre_rcu_remove(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);

	/*
	 * This is called before mac80211 does RCU synchronisation,
	 * so here we already invalidate our internal RCU-protected
	 * station pointer. The rest of the code will thus no longer
	 * be able to find the station this way, and we don't rely
	 * on further RCU synchronisation after the sta_state()
	 * callback deleted the station.
	 */
	mutex_lock(&mvm->mutex);
	if (sta == rcu_access_pointer(mvm->fw_id_to_mac_id[mvm_sta->sta_id]))
		rcu_assign_pointer(mvm->fw_id_to_mac_id[mvm_sta->sta_id],
				   ERR_PTR(-ENOENT));

	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_check_uapsd(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				const u8 *bssid)
{
	if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_UAPSD_SUPPORT))
		return;

	if (vif->p2p && !iwl_mvm_is_p2p_scm_uapsd_supported(mvm)) {
		vif->driver_flags &= ~IEEE80211_VIF_SUPPORTS_UAPSD;
		return;
	}

	if (!vif->p2p &&
	    (iwlwifi_mod_params.uapsd_disable & IWL_DISABLE_UAPSD_BSS)) {
		vif->driver_flags &= ~IEEE80211_VIF_SUPPORTS_UAPSD;
		return;
	}

	vif->driver_flags |= IEEE80211_VIF_SUPPORTS_UAPSD;
}

static void
iwl_mvm_tdls_check_trigger(struct iwl_mvm *mvm,
			   struct ieee80211_vif *vif, u8 *peer_addr,
			   enum nl80211_tdls_operation action)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_tdls *tdls_trig;

	if (!iwl_fw_dbg_trigger_enabled(mvm->fw, FW_DBG_TRIGGER_TDLS))
		return;

	trig = iwl_fw_dbg_get_trigger(mvm->fw, FW_DBG_TRIGGER_TDLS);
	tdls_trig = (void *)trig->data;
	if (!iwl_fw_dbg_trigger_check_stop(mvm, vif, trig))
		return;

	if (!(tdls_trig->action_bitmap & BIT(action)))
		return;

	if (tdls_trig->peer_mode &&
	    memcmp(tdls_trig->peer, peer_addr, ETH_ALEN) != 0)
		return;

	iwl_mvm_fw_dbg_collect_trig(mvm, trig,
				    "TDLS event occurred, peer %pM, action %d",
				    peer_addr, action);
}

static void iwl_mvm_purge_deferred_tx_frames(struct iwl_mvm *mvm,
					     struct iwl_mvm_sta *mvm_sta)
{
	struct iwl_mvm_tid_data *tid_data;
	struct sk_buff *skb;
	int i;

	spin_lock_bh(&mvm_sta->lock);
	for (i = 0; i <= IWL_MAX_TID_COUNT; i++) {
		tid_data = &mvm_sta->tid_data[i];
		while ((skb = __skb_dequeue(&tid_data->deferred_tx_frames)))
			ieee80211_free_txskb(mvm->hw, skb);
	}
	spin_unlock_bh(&mvm_sta->lock);
}

static int iwl_mvm_mac_sta_state(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 enum ieee80211_sta_state old_state,
				 enum ieee80211_sta_state new_state)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	IWL_DEBUG_MAC80211(mvm, "station %pM state change %d->%d\n",
			   sta->addr, old_state, new_state);

	/* this would be a mac80211 bug ... but don't crash */
	if (WARN_ON_ONCE(!mvmvif->phy_ctxt))
		return -EINVAL;

	/* if a STA is being removed, reuse its ID */
	flush_work(&mvm->sta_drained_wk);

	/*
	 * If we are in a STA removal flow and in DQA mode:
	 *
	 * This is after the sync_rcu part, so the queues have already been
	 * flushed. No more TXs on their way in mac80211's path, and no more in
	 * the queues.
	 * Also, we won't be getting any new TX frames for this station.
	 * What we might have are deferred TX frames that need to be taken care
	 * of.
	 *
	 * Drop any still-queued deferred-frame before removing the STA, and
	 * make sure the worker is no longer handling frames for this STA.
	 */
	if (old_state == IEEE80211_STA_NONE &&
	    new_state == IEEE80211_STA_NOTEXIST &&
	    iwl_mvm_is_dqa_supported(mvm)) {
		struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);

		iwl_mvm_purge_deferred_tx_frames(mvm, mvm_sta);
		flush_work(&mvm->add_stream_wk);

		/*
		 * No need to make sure deferred TX indication is off since the
		 * worker will already remove it if it was on
		 */
	}

	mutex_lock(&mvm->mutex);
	if (old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE) {
		/*
		 * Firmware bug - it'll crash if the beacon interval is less
		 * than 16. We can't avoid connecting at all, so refuse the
		 * station state change, this will cause mac80211 to abandon
		 * attempts to connect to this AP, and eventually wpa_s will
		 * blacklist the AP...
		 */
		if (vif->type == NL80211_IFTYPE_STATION &&
		    vif->bss_conf.beacon_int < 16) {
			IWL_ERR(mvm,
				"AP %pM beacon interval is %d, refusing due to firmware bug!\n",
				sta->addr, vif->bss_conf.beacon_int);
			ret = -EINVAL;
			goto out_unlock;
		}

		if (sta->tdls &&
		    (vif->p2p ||
		     iwl_mvm_tdls_sta_count(mvm, NULL) ==
						IWL_MVM_TDLS_STA_COUNT ||
		     iwl_mvm_phy_ctx_count(mvm) > 1)) {
			IWL_DEBUG_MAC80211(mvm, "refusing TDLS sta\n");
			ret = -EBUSY;
			goto out_unlock;
		}

		ret = iwl_mvm_add_sta(mvm, vif, sta);
		if (sta->tdls && ret == 0) {
			iwl_mvm_recalc_tdls_state(mvm, vif, true);
			iwl_mvm_tdls_check_trigger(mvm, vif, sta->addr,
						   NL80211_TDLS_SETUP);
		}
	} else if (old_state == IEEE80211_STA_NONE &&
		   new_state == IEEE80211_STA_AUTH) {
		/*
		 * EBS may be disabled due to previous failures reported by FW.
		 * Reset EBS status here assuming environment has been changed.
		 */
		mvm->last_ebs_successful = true;
		iwl_mvm_check_uapsd(mvm, vif, sta->addr);
		ret = 0;
	} else if (old_state == IEEE80211_STA_AUTH &&
		   new_state == IEEE80211_STA_ASSOC) {
		if (vif->type == NL80211_IFTYPE_AP) {
			mvmvif->ap_assoc_sta_count++;
			iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
		}
		ret = iwl_mvm_update_sta(mvm, vif, sta);
		if (ret == 0)
			iwl_mvm_rs_rate_init(mvm, sta,
					     mvmvif->phy_ctxt->channel->band,
					     true);
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTHORIZED) {

		/* we don't support TDLS during DCM */
		if (iwl_mvm_phy_ctx_count(mvm) > 1)
			iwl_mvm_teardown_tdls_peers(mvm);

		if (sta->tdls)
			iwl_mvm_tdls_check_trigger(mvm, vif, sta->addr,
						   NL80211_TDLS_ENABLE_LINK);

		/* enable beacon filtering */
		WARN_ON(iwl_mvm_enable_beacon_filter(mvm, vif, 0));
		ret = 0;
	} else if (old_state == IEEE80211_STA_AUTHORIZED &&
		   new_state == IEEE80211_STA_ASSOC) {
		/* disable beacon filtering */
		WARN_ON(iwl_mvm_disable_beacon_filter(mvm, vif, 0));
		ret = 0;
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTH) {
		if (vif->type == NL80211_IFTYPE_AP) {
			mvmvif->ap_assoc_sta_count--;
			iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
		}
		ret = 0;
	} else if (old_state == IEEE80211_STA_AUTH &&
		   new_state == IEEE80211_STA_NONE) {
		ret = 0;
	} else if (old_state == IEEE80211_STA_NONE &&
		   new_state == IEEE80211_STA_NOTEXIST) {
		ret = iwl_mvm_rm_sta(mvm, vif, sta);
		if (sta->tdls) {
			iwl_mvm_recalc_tdls_state(mvm, vif, false);
			iwl_mvm_tdls_check_trigger(mvm, vif, sta->addr,
						   NL80211_TDLS_DISABLE_LINK);
		}
	} else {
		ret = -EIO;
	}
 out_unlock:
	mutex_unlock(&mvm->mutex);

	if (sta->tdls && ret == 0) {
		if (old_state == IEEE80211_STA_NOTEXIST &&
		    new_state == IEEE80211_STA_NONE)
			ieee80211_reserve_tid(sta, IWL_MVM_TDLS_FW_TID);
		else if (old_state == IEEE80211_STA_NONE &&
			 new_state == IEEE80211_STA_NOTEXIST)
			ieee80211_unreserve_tid(sta, IWL_MVM_TDLS_FW_TID);
	}

	return ret;
}

static int iwl_mvm_mac_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mvm->rts_threshold = value;

	return 0;
}

static void iwl_mvm_sta_rc_update(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta, u32 changed)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	if (vif->type == NL80211_IFTYPE_STATION &&
	    changed & IEEE80211_RC_NSS_CHANGED)
		iwl_mvm_sf_update(mvm, vif, false);
}

static int iwl_mvm_mac_conf_tx(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif, u16 ac,
			       const struct ieee80211_tx_queue_params *params)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	mvmvif->queue_params[ac] = *params;

	/*
	 * No need to update right away, we'll get BSS_CHANGED_QOS
	 * The exception is P2P_DEVICE interface which needs immediate update.
	 */
	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		int ret;

		mutex_lock(&mvm->mutex);
		ret = iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
		mutex_unlock(&mvm->mutex);
		return ret;
	}
	return 0;
}

static void iwl_mvm_mac_mgd_prepare_tx(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	u32 duration = IWL_MVM_TE_SESSION_PROTECTION_MAX_TIME_MS;
	u32 min_duration = IWL_MVM_TE_SESSION_PROTECTION_MIN_TIME_MS;

	if (WARN_ON_ONCE(vif->bss_conf.assoc))
		return;

	/*
	 * iwl_mvm_protect_session() reads directly from the device
	 * (the system time), so make sure it is available.
	 */
	if (iwl_mvm_ref_sync(mvm, IWL_MVM_REF_PREPARE_TX))
		return;

	mutex_lock(&mvm->mutex);
	/* Try really hard to protect the session and hear a beacon */
	iwl_mvm_protect_session(mvm, vif, duration, min_duration, 500, false);
	mutex_unlock(&mvm->mutex);

	iwl_mvm_unref(mvm, IWL_MVM_REF_PREPARE_TX);
}

static int iwl_mvm_mac_sched_scan_start(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct cfg80211_sched_scan_request *req,
					struct ieee80211_scan_ies *ies)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	int ret;

	mutex_lock(&mvm->mutex);

	if (!vif->bss_conf.idle) {
		ret = -EBUSY;
		goto out;
	}

	ret = iwl_mvm_sched_scan_start(mvm, vif, req, ies, IWL_MVM_SCAN_SCHED);

out:
	mutex_unlock(&mvm->mutex);
	return ret;
}

static int iwl_mvm_mac_sched_scan_stop(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);

	/* Due to a race condition, it's possible that mac80211 asks
	 * us to stop a sched_scan when it's already stopped.  This
	 * can happen, for instance, if we stopped the scan ourselves,
	 * called ieee80211_sched_scan_stopped() and the userspace called
	 * stop sched scan scan before ieee80211_sched_scan_stopped_work()
	 * could run.  To handle this, simply return if the scan is
	 * not running.
	*/
	if (!(mvm->scan_status & IWL_MVM_SCAN_SCHED)) {
		mutex_unlock(&mvm->mutex);
		return 0;
	}

	ret = iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_SCHED, false);
	mutex_unlock(&mvm->mutex);
	iwl_mvm_wait_for_async_handlers(mvm);

	return ret;
}

static int iwl_mvm_mac_set_key(struct ieee80211_hw *hw,
			       enum set_key_cmd cmd,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       struct ieee80211_key_conf *key)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_sta *mvmsta;
	struct iwl_mvm_key_pn *ptk_pn;
	int keyidx = key->keyidx;
	int ret;
	u8 key_offset;

	if (iwlwifi_mod_params.sw_crypto) {
		IWL_DEBUG_MAC80211(mvm, "leave - hwcrypto disabled\n");
		return -EOPNOTSUPP;
	}

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		key->flags |= IEEE80211_KEY_FLAG_PUT_IV_SPACE;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		key->flags |= IEEE80211_KEY_FLAG_PUT_IV_SPACE;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		WARN_ON_ONCE(!ieee80211_hw_check(hw, MFP_CAPABLE));
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		/* For non-client mode, only use WEP keys for TX as we probably
		 * don't have a station yet anyway and would then have to keep
		 * track of the keys, linking them to each of the clients/peers
		 * as they appear. For now, don't do that, for performance WEP
		 * offload doesn't really matter much, but we need it for some
		 * other offload features in client mode.
		 */
		if (vif->type != NL80211_IFTYPE_STATION)
			return 0;
		break;
	default:
		/* currently FW supports only one optional cipher scheme */
		if (hw->n_cipher_schemes &&
		    hw->cipher_schemes->cipher == key->cipher)
			key->flags |= IEEE80211_KEY_FLAG_PUT_IV_SPACE;
		else
			return -EOPNOTSUPP;
	}

	mutex_lock(&mvm->mutex);

	switch (cmd) {
	case SET_KEY:
		if ((vif->type == NL80211_IFTYPE_ADHOC ||
		     vif->type == NL80211_IFTYPE_AP) && !sta) {
			/*
			 * GTK on AP interface is a TX-only key, return 0;
			 * on IBSS they're per-station and because we're lazy
			 * we don't support them for RX, so do the same.
			 * CMAC/GMAC in AP/IBSS modes must be done in software.
			 */
			if (key->cipher == WLAN_CIPHER_SUITE_AES_CMAC ||
			    key->cipher == WLAN_CIPHER_SUITE_BIP_GMAC_128 ||
			    key->cipher == WLAN_CIPHER_SUITE_BIP_GMAC_256)
				ret = -EOPNOTSUPP;
			else
				ret = 0;
			key->hw_key_idx = STA_KEY_IDX_INVALID;
			break;
		}

		/* During FW restart, in order to restore the state as it was,
		 * don't try to reprogram keys we previously failed for.
		 */
		if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) &&
		    key->hw_key_idx == STA_KEY_IDX_INVALID) {
			IWL_DEBUG_MAC80211(mvm,
					   "skip invalid idx key programming during restart\n");
			ret = 0;
			break;
		}

		if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) &&
		    sta && iwl_mvm_has_new_rx_api(mvm) &&
		    key->flags & IEEE80211_KEY_FLAG_PAIRWISE &&
		    (key->cipher == WLAN_CIPHER_SUITE_CCMP ||
		     key->cipher == WLAN_CIPHER_SUITE_GCMP ||
		     key->cipher == WLAN_CIPHER_SUITE_GCMP_256)) {
			struct ieee80211_key_seq seq;
			int tid, q;

			mvmsta = iwl_mvm_sta_from_mac80211(sta);
			WARN_ON(rcu_access_pointer(mvmsta->ptk_pn[keyidx]));
			ptk_pn = kzalloc(sizeof(*ptk_pn) +
					 mvm->trans->num_rx_queues *
						sizeof(ptk_pn->q[0]),
					 GFP_KERNEL);
			if (!ptk_pn) {
				ret = -ENOMEM;
				break;
			}

			for (tid = 0; tid < IWL_MAX_TID_COUNT; tid++) {
				ieee80211_get_key_rx_seq(key, tid, &seq);
				for (q = 0; q < mvm->trans->num_rx_queues; q++)
					memcpy(ptk_pn->q[q].pn[tid],
					       seq.ccmp.pn,
					       IEEE80211_CCMP_PN_LEN);
			}

			rcu_assign_pointer(mvmsta->ptk_pn[keyidx], ptk_pn);
		}

		/* in HW restart reuse the index, otherwise request a new one */
		if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
			key_offset = key->hw_key_idx;
		else
			key_offset = STA_KEY_IDX_INVALID;

		IWL_DEBUG_MAC80211(mvm, "set hwcrypto key\n");
		ret = iwl_mvm_set_sta_key(mvm, vif, sta, key, key_offset);
		if (ret) {
			IWL_WARN(mvm, "set key failed\n");
			/*
			 * can't add key for RX, but we don't need it
			 * in the device for TX so still return 0
			 */
			key->hw_key_idx = STA_KEY_IDX_INVALID;
			ret = 0;
		}

		break;
	case DISABLE_KEY:
		if (key->hw_key_idx == STA_KEY_IDX_INVALID) {
			ret = 0;
			break;
		}

		if (sta && iwl_mvm_has_new_rx_api(mvm) &&
		    key->flags & IEEE80211_KEY_FLAG_PAIRWISE &&
		    (key->cipher == WLAN_CIPHER_SUITE_CCMP ||
		     key->cipher == WLAN_CIPHER_SUITE_GCMP ||
		     key->cipher == WLAN_CIPHER_SUITE_GCMP_256)) {
			mvmsta = iwl_mvm_sta_from_mac80211(sta);
			ptk_pn = rcu_dereference_protected(
						mvmsta->ptk_pn[keyidx],
						lockdep_is_held(&mvm->mutex));
			RCU_INIT_POINTER(mvmsta->ptk_pn[keyidx], NULL);
			if (ptk_pn)
				kfree_rcu(ptk_pn, rcu_head);
		}

		IWL_DEBUG_MAC80211(mvm, "disable hwcrypto key\n");
		ret = iwl_mvm_remove_sta_key(mvm, vif, sta, key);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&mvm->mutex);
	return ret;
}

static void iwl_mvm_mac_update_tkip_key(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_key_conf *keyconf,
					struct ieee80211_sta *sta,
					u32 iv32, u16 *phase1key)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	if (keyconf->hw_key_idx == STA_KEY_IDX_INVALID)
		return;

	iwl_mvm_update_tkip_key(mvm, vif, keyconf, sta, iv32, phase1key);
}


static bool iwl_mvm_rx_aux_roc(struct iwl_notif_wait_data *notif_wait,
			       struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_mvm *mvm =
		container_of(notif_wait, struct iwl_mvm, notif_wait);
	struct iwl_hs20_roc_res *resp;
	int resp_len = iwl_rx_packet_payload_len(pkt);
	struct iwl_mvm_time_event_data *te_data = data;

	if (WARN_ON(pkt->hdr.cmd != HOT_SPOT_CMD))
		return true;

	if (WARN_ON_ONCE(resp_len != sizeof(*resp))) {
		IWL_ERR(mvm, "Invalid HOT_SPOT_CMD response\n");
		return true;
	}

	resp = (void *)pkt->data;

	IWL_DEBUG_TE(mvm,
		     "Aux ROC: Recieved response from ucode: status=%d uid=%d\n",
		     resp->status, resp->event_unique_id);

	te_data->uid = le32_to_cpu(resp->event_unique_id);
	IWL_DEBUG_TE(mvm, "TIME_EVENT_CMD response - UID = 0x%x\n",
		     te_data->uid);

	spin_lock_bh(&mvm->time_event_lock);
	list_add_tail(&te_data->list, &mvm->aux_roc_te_list);
	spin_unlock_bh(&mvm->time_event_lock);

	return true;
}

#define AUX_ROC_MIN_DURATION MSEC_TO_TU(100)
#define AUX_ROC_MIN_DELAY MSEC_TO_TU(200)
#define AUX_ROC_MAX_DELAY MSEC_TO_TU(600)
#define AUX_ROC_SAFETY_BUFFER MSEC_TO_TU(20)
#define AUX_ROC_MIN_SAFETY_BUFFER MSEC_TO_TU(10)
static int iwl_mvm_send_aux_roc_cmd(struct iwl_mvm *mvm,
				    struct ieee80211_channel *channel,
				    struct ieee80211_vif *vif,
				    int duration)
{
	int res, time_reg = DEVICE_SYSTEM_TIME_REG;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_time_event_data *te_data = &mvmvif->hs_time_event_data;
	static const u16 time_event_response[] = { HOT_SPOT_CMD };
	struct iwl_notification_wait wait_time_event;
	u32 dtim_interval = vif->bss_conf.dtim_period *
		vif->bss_conf.beacon_int;
	u32 req_dur, delay;
	struct iwl_hs20_roc_req aux_roc_req = {
		.action = cpu_to_le32(FW_CTXT_ACTION_ADD),
		.id_and_color =
			cpu_to_le32(FW_CMD_ID_AND_COLOR(MAC_INDEX_AUX, 0)),
		.sta_id_and_color = cpu_to_le32(mvm->aux_sta.sta_id),
		/* Set the channel info data */
		.channel_info.band = (channel->band == NL80211_BAND_2GHZ) ?
			PHY_BAND_24 : PHY_BAND_5,
		.channel_info.channel = channel->hw_value,
		.channel_info.width = PHY_VHT_CHANNEL_MODE20,
		/* Set the time and duration */
		.apply_time = cpu_to_le32(iwl_read_prph(mvm->trans, time_reg)),
	 };

	delay = AUX_ROC_MIN_DELAY;
	req_dur = MSEC_TO_TU(duration);

	/*
	 * If we are associated we want the delay time to be at least one
	 * dtim interval so that the FW can wait until after the DTIM and
	 * then start the time event, this will potentially allow us to
	 * remain off-channel for the max duration.
	 * Since we want to use almost a whole dtim interval we would also
	 * like the delay to be for 2-3 dtim intervals, in case there are
	 * other time events with higher priority.
	 */
	if (vif->bss_conf.assoc) {
		delay = min_t(u32, dtim_interval * 3, AUX_ROC_MAX_DELAY);
		/* We cannot remain off-channel longer than the DTIM interval */
		if (dtim_interval <= req_dur) {
			req_dur = dtim_interval - AUX_ROC_SAFETY_BUFFER;
			if (req_dur <= AUX_ROC_MIN_DURATION)
				req_dur = dtim_interval -
					AUX_ROC_MIN_SAFETY_BUFFER;
		}
	}

	aux_roc_req.duration = cpu_to_le32(req_dur);
	aux_roc_req.apply_time_max_delay = cpu_to_le32(delay);

	IWL_DEBUG_TE(mvm,
		     "ROC: Requesting to remain on channel %u for %ums (requested = %ums, max_delay = %ums, dtim_interval = %ums)\n",
		     channel->hw_value, req_dur, duration, delay,
		     dtim_interval);
	/* Set the node address */
	memcpy(aux_roc_req.node_addr, vif->addr, ETH_ALEN);

	lockdep_assert_held(&mvm->mutex);

	spin_lock_bh(&mvm->time_event_lock);

	if (WARN_ON(te_data->id == HOT_SPOT_CMD)) {
		spin_unlock_bh(&mvm->time_event_lock);
		return -EIO;
	}

	te_data->vif = vif;
	te_data->duration = duration;
	te_data->id = HOT_SPOT_CMD;

	spin_unlock_bh(&mvm->time_event_lock);

	/*
	 * Use a notification wait, which really just processes the
	 * command response and doesn't wait for anything, in order
	 * to be able to process the response and get the UID inside
	 * the RX path. Using CMD_WANT_SKB doesn't work because it
	 * stores the buffer and then wakes up this thread, by which
	 * time another notification (that the time event started)
	 * might already be processed unsuccessfully.
	 */
	iwl_init_notification_wait(&mvm->notif_wait, &wait_time_event,
				   time_event_response,
				   ARRAY_SIZE(time_event_response),
				   iwl_mvm_rx_aux_roc, te_data);

	res = iwl_mvm_send_cmd_pdu(mvm, HOT_SPOT_CMD, 0, sizeof(aux_roc_req),
				   &aux_roc_req);

	if (res) {
		IWL_ERR(mvm, "Couldn't send HOT_SPOT_CMD: %d\n", res);
		iwl_remove_notification(&mvm->notif_wait, &wait_time_event);
		goto out_clear_te;
	}

	/* No need to wait for anything, so just pass 1 (0 isn't valid) */
	res = iwl_wait_notification(&mvm->notif_wait, &wait_time_event, 1);
	/* should never fail */
	WARN_ON_ONCE(res);

	if (res) {
 out_clear_te:
		spin_lock_bh(&mvm->time_event_lock);
		iwl_mvm_te_clear_data(mvm, te_data);
		spin_unlock_bh(&mvm->time_event_lock);
	}

	return res;
}

static int iwl_mvm_roc(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif,
		       struct ieee80211_channel *channel,
		       int duration,
		       enum ieee80211_roc_type type)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct cfg80211_chan_def chandef;
	struct iwl_mvm_phy_ctxt *phy_ctxt;
	int ret, i;

	IWL_DEBUG_MAC80211(mvm, "enter (%d, %d, %d)\n", channel->hw_value,
			   duration, type);

	flush_work(&mvm->roc_done_wk);

	mutex_lock(&mvm->mutex);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		if (fw_has_capa(&mvm->fw->ucode_capa,
				IWL_UCODE_TLV_CAPA_HOTSPOT_SUPPORT)) {
			/* Use aux roc framework (HS20) */
			ret = iwl_mvm_send_aux_roc_cmd(mvm, channel,
						       vif, duration);
			goto out_unlock;
		}
		IWL_ERR(mvm, "hotspot not supported\n");
		ret = -EINVAL;
		goto out_unlock;
	case NL80211_IFTYPE_P2P_DEVICE:
		/* handle below */
		break;
	default:
		IWL_ERR(mvm, "vif isn't P2P_DEVICE: %d\n", vif->type);
		ret = -EINVAL;
		goto out_unlock;
	}

	for (i = 0; i < NUM_PHY_CTX; i++) {
		phy_ctxt = &mvm->phy_ctxts[i];
		if (phy_ctxt->ref == 0 || mvmvif->phy_ctxt == phy_ctxt)
			continue;

		if (phy_ctxt->ref && channel == phy_ctxt->channel) {
			/*
			 * Unbind the P2P_DEVICE from the current PHY context,
			 * and if the PHY context is not used remove it.
			 */
			ret = iwl_mvm_binding_remove_vif(mvm, vif);
			if (WARN(ret, "Failed unbinding P2P_DEVICE\n"))
				goto out_unlock;

			iwl_mvm_phy_ctxt_unref(mvm, mvmvif->phy_ctxt);

			/* Bind the P2P_DEVICE to the current PHY Context */
			mvmvif->phy_ctxt = phy_ctxt;

			ret = iwl_mvm_binding_add_vif(mvm, vif);
			if (WARN(ret, "Failed binding P2P_DEVICE\n"))
				goto out_unlock;

			iwl_mvm_phy_ctxt_ref(mvm, mvmvif->phy_ctxt);
			goto schedule_time_event;
		}
	}

	/* Need to update the PHY context only if the ROC channel changed */
	if (channel == mvmvif->phy_ctxt->channel)
		goto schedule_time_event;

	cfg80211_chandef_create(&chandef, channel, NL80211_CHAN_NO_HT);

	/*
	 * Change the PHY context configuration as it is currently referenced
	 * only by the P2P Device MAC
	 */
	if (mvmvif->phy_ctxt->ref == 1) {
		ret = iwl_mvm_phy_ctxt_changed(mvm, mvmvif->phy_ctxt,
					       &chandef, 1, 1);
		if (ret)
			goto out_unlock;
	} else {
		/*
		 * The PHY context is shared with other MACs. Need to remove the
		 * P2P Device from the binding, allocate an new PHY context and
		 * create a new binding
		 */
		phy_ctxt = iwl_mvm_get_free_phy_ctxt(mvm);
		if (!phy_ctxt) {
			ret = -ENOSPC;
			goto out_unlock;
		}

		ret = iwl_mvm_phy_ctxt_changed(mvm, phy_ctxt, &chandef,
					       1, 1);
		if (ret) {
			IWL_ERR(mvm, "Failed to change PHY context\n");
			goto out_unlock;
		}

		/* Unbind the P2P_DEVICE from the current PHY context */
		ret = iwl_mvm_binding_remove_vif(mvm, vif);
		if (WARN(ret, "Failed unbinding P2P_DEVICE\n"))
			goto out_unlock;

		iwl_mvm_phy_ctxt_unref(mvm, mvmvif->phy_ctxt);

		/* Bind the P2P_DEVICE to the new allocated PHY context */
		mvmvif->phy_ctxt = phy_ctxt;

		ret = iwl_mvm_binding_add_vif(mvm, vif);
		if (WARN(ret, "Failed binding P2P_DEVICE\n"))
			goto out_unlock;

		iwl_mvm_phy_ctxt_ref(mvm, mvmvif->phy_ctxt);
	}

schedule_time_event:
	/* Schedule the time events */
	ret = iwl_mvm_start_p2p_roc(mvm, vif, duration, type);

out_unlock:
	mutex_unlock(&mvm->mutex);
	IWL_DEBUG_MAC80211(mvm, "leave\n");
	return ret;
}

static int iwl_mvm_cancel_roc(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	IWL_DEBUG_MAC80211(mvm, "enter\n");

	mutex_lock(&mvm->mutex);
	iwl_mvm_stop_roc(mvm);
	mutex_unlock(&mvm->mutex);

	IWL_DEBUG_MAC80211(mvm, "leave\n");
	return 0;
}

static int __iwl_mvm_add_chanctx(struct iwl_mvm *mvm,
				 struct ieee80211_chanctx_conf *ctx)
{
	u16 *phy_ctxt_id = (u16 *)ctx->drv_priv;
	struct iwl_mvm_phy_ctxt *phy_ctxt;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	IWL_DEBUG_MAC80211(mvm, "Add channel context\n");

	phy_ctxt = iwl_mvm_get_free_phy_ctxt(mvm);
	if (!phy_ctxt) {
		ret = -ENOSPC;
		goto out;
	}

	ret = iwl_mvm_phy_ctxt_changed(mvm, phy_ctxt, &ctx->min_def,
				       ctx->rx_chains_static,
				       ctx->rx_chains_dynamic);
	if (ret) {
		IWL_ERR(mvm, "Failed to add PHY context\n");
		goto out;
	}

	iwl_mvm_phy_ctxt_ref(mvm, phy_ctxt);
	*phy_ctxt_id = phy_ctxt->id;
out:
	return ret;
}

static int iwl_mvm_add_chanctx(struct ieee80211_hw *hw,
			       struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);
	ret = __iwl_mvm_add_chanctx(mvm, ctx);
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void __iwl_mvm_remove_chanctx(struct iwl_mvm *mvm,
				     struct ieee80211_chanctx_conf *ctx)
{
	u16 *phy_ctxt_id = (u16 *)ctx->drv_priv;
	struct iwl_mvm_phy_ctxt *phy_ctxt = &mvm->phy_ctxts[*phy_ctxt_id];

	lockdep_assert_held(&mvm->mutex);

	iwl_mvm_phy_ctxt_unref(mvm, phy_ctxt);
}

static void iwl_mvm_remove_chanctx(struct ieee80211_hw *hw,
				   struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);
	__iwl_mvm_remove_chanctx(mvm, ctx);
	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_change_chanctx(struct ieee80211_hw *hw,
				   struct ieee80211_chanctx_conf *ctx,
				   u32 changed)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	u16 *phy_ctxt_id = (u16 *)ctx->drv_priv;
	struct iwl_mvm_phy_ctxt *phy_ctxt = &mvm->phy_ctxts[*phy_ctxt_id];

	if (WARN_ONCE((phy_ctxt->ref > 1) &&
		      (changed & ~(IEEE80211_CHANCTX_CHANGE_WIDTH |
				   IEEE80211_CHANCTX_CHANGE_RX_CHAINS |
				   IEEE80211_CHANCTX_CHANGE_RADAR |
				   IEEE80211_CHANCTX_CHANGE_MIN_WIDTH)),
		      "Cannot change PHY. Ref=%d, changed=0x%X\n",
		      phy_ctxt->ref, changed))
		return;

	mutex_lock(&mvm->mutex);
	iwl_mvm_bt_coex_vif_change(mvm);
	iwl_mvm_phy_ctxt_changed(mvm, phy_ctxt, &ctx->min_def,
				 ctx->rx_chains_static,
				 ctx->rx_chains_dynamic);
	mutex_unlock(&mvm->mutex);
}

static int __iwl_mvm_assign_vif_chanctx(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					struct ieee80211_chanctx_conf *ctx,
					bool switching_chanctx)
{
	u16 *phy_ctxt_id = (u16 *)ctx->drv_priv;
	struct iwl_mvm_phy_ctxt *phy_ctxt = &mvm->phy_ctxts[*phy_ctxt_id];
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	lockdep_assert_held(&mvm->mutex);

	mvmvif->phy_ctxt = phy_ctxt;

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
		/* only needed if we're switching chanctx (i.e. during CSA) */
		if (switching_chanctx) {
			mvmvif->ap_ibss_active = true;
			break;
		}
	case NL80211_IFTYPE_ADHOC:
		/*
		 * The AP binding flow is handled as part of the start_ap flow
		 * (in bss_info_changed), similarly for IBSS.
		 */
		ret = 0;
		goto out;
	case NL80211_IFTYPE_STATION:
		break;
	case NL80211_IFTYPE_MONITOR:
		/* always disable PS when a monitor interface is active */
		mvmvif->ps_disabled = true;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	ret = iwl_mvm_binding_add_vif(mvm, vif);
	if (ret)
		goto out;

	/*
	 * Power state must be updated before quotas,
	 * otherwise fw will complain.
	 */
	iwl_mvm_power_update_mac(mvm);

	/* Setting the quota at this stage is only required for monitor
	 * interfaces. For the other types, the bss_info changed flow
	 * will handle quota settings.
	 */
	if (vif->type == NL80211_IFTYPE_MONITOR) {
		mvmvif->monitor_active = true;
		ret = iwl_mvm_update_quotas(mvm, false, NULL);
		if (ret)
			goto out_remove_binding;

		ret = iwl_mvm_add_snif_sta(mvm, vif);
		if (ret)
			goto out_remove_binding;

	}

	/* Handle binding during CSA */
	if (vif->type == NL80211_IFTYPE_AP) {
		iwl_mvm_update_quotas(mvm, false, NULL);
		iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
	}

	if (switching_chanctx && vif->type == NL80211_IFTYPE_STATION) {
		u32 duration = 2 * vif->bss_conf.beacon_int;

		/* iwl_mvm_protect_session() reads directly from the
		 * device (the system time), so make sure it is
		 * available.
		 */
		ret = iwl_mvm_ref_sync(mvm, IWL_MVM_REF_PROTECT_CSA);
		if (ret)
			goto out_remove_binding;

		/* Protect the session to make sure we hear the first
		 * beacon on the new channel.
		 */
		iwl_mvm_protect_session(mvm, vif, duration, duration,
					vif->bss_conf.beacon_int / 2,
					true);

		iwl_mvm_unref(mvm, IWL_MVM_REF_PROTECT_CSA);

		iwl_mvm_update_quotas(mvm, false, NULL);
	}

	goto out;

out_remove_binding:
	iwl_mvm_binding_remove_vif(mvm, vif);
	iwl_mvm_power_update_mac(mvm);
out:
	if (ret)
		mvmvif->phy_ctxt = NULL;
	return ret;
}
static int iwl_mvm_assign_vif_chanctx(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);
	ret = __iwl_mvm_assign_vif_chanctx(mvm, vif, ctx, false);
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void __iwl_mvm_unassign_vif_chanctx(struct iwl_mvm *mvm,
					   struct ieee80211_vif *vif,
					   struct ieee80211_chanctx_conf *ctx,
					   bool switching_chanctx)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct ieee80211_vif *disabled_vif = NULL;

	lockdep_assert_held(&mvm->mutex);

	iwl_mvm_remove_time_event(mvm, mvmvif, &mvmvif->time_event_data);

	switch (vif->type) {
	case NL80211_IFTYPE_ADHOC:
		goto out;
	case NL80211_IFTYPE_MONITOR:
		mvmvif->monitor_active = false;
		mvmvif->ps_disabled = false;
		iwl_mvm_rm_snif_sta(mvm, vif);
		break;
	case NL80211_IFTYPE_AP:
		/* This part is triggered only during CSA */
		if (!switching_chanctx || !mvmvif->ap_ibss_active)
			goto out;

		mvmvif->csa_countdown = false;

		/* Set CS bit on all the stations */
		iwl_mvm_modify_all_sta_disable_tx(mvm, mvmvif, true);

		/* Save blocked iface, the timeout is set on the next beacon */
		rcu_assign_pointer(mvm->csa_tx_blocked_vif, vif);

		mvmvif->ap_ibss_active = false;
		break;
	case NL80211_IFTYPE_STATION:
		if (!switching_chanctx)
			break;

		disabled_vif = vif;

		iwl_mvm_mac_ctxt_changed(mvm, vif, true, NULL);
		break;
	default:
		break;
	}

	iwl_mvm_update_quotas(mvm, false, disabled_vif);
	iwl_mvm_binding_remove_vif(mvm, vif);

out:
	mvmvif->phy_ctxt = NULL;
	iwl_mvm_power_update_mac(mvm);
}

static void iwl_mvm_unassign_vif_chanctx(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);
	__iwl_mvm_unassign_vif_chanctx(mvm, vif, ctx, false);
	mutex_unlock(&mvm->mutex);
}

static int
iwl_mvm_switch_vif_chanctx_swap(struct iwl_mvm *mvm,
				struct ieee80211_vif_chanctx_switch *vifs)
{
	int ret;

	mutex_lock(&mvm->mutex);
	__iwl_mvm_unassign_vif_chanctx(mvm, vifs[0].vif, vifs[0].old_ctx, true);
	__iwl_mvm_remove_chanctx(mvm, vifs[0].old_ctx);

	ret = __iwl_mvm_add_chanctx(mvm, vifs[0].new_ctx);
	if (ret) {
		IWL_ERR(mvm, "failed to add new_ctx during channel switch\n");
		goto out_reassign;
	}

	ret = __iwl_mvm_assign_vif_chanctx(mvm, vifs[0].vif, vifs[0].new_ctx,
					   true);
	if (ret) {
		IWL_ERR(mvm,
			"failed to assign new_ctx during channel switch\n");
		goto out_remove;
	}

	/* we don't support TDLS during DCM - can be caused by channel switch */
	if (iwl_mvm_phy_ctx_count(mvm) > 1)
		iwl_mvm_teardown_tdls_peers(mvm);

	goto out;

out_remove:
	__iwl_mvm_remove_chanctx(mvm, vifs[0].new_ctx);

out_reassign:
	if (__iwl_mvm_add_chanctx(mvm, vifs[0].old_ctx)) {
		IWL_ERR(mvm, "failed to add old_ctx back after failure.\n");
		goto out_restart;
	}

	if (__iwl_mvm_assign_vif_chanctx(mvm, vifs[0].vif, vifs[0].old_ctx,
					 true)) {
		IWL_ERR(mvm, "failed to reassign old_ctx after failure.\n");
		goto out_restart;
	}

	goto out;

out_restart:
	/* things keep failing, better restart the hw */
	iwl_mvm_nic_restart(mvm, false);

out:
	mutex_unlock(&mvm->mutex);

	return ret;
}

static int
iwl_mvm_switch_vif_chanctx_reassign(struct iwl_mvm *mvm,
				    struct ieee80211_vif_chanctx_switch *vifs)
{
	int ret;

	mutex_lock(&mvm->mutex);
	__iwl_mvm_unassign_vif_chanctx(mvm, vifs[0].vif, vifs[0].old_ctx, true);

	ret = __iwl_mvm_assign_vif_chanctx(mvm, vifs[0].vif, vifs[0].new_ctx,
					   true);
	if (ret) {
		IWL_ERR(mvm,
			"failed to assign new_ctx during channel switch\n");
		goto out_reassign;
	}

	goto out;

out_reassign:
	if (__iwl_mvm_assign_vif_chanctx(mvm, vifs[0].vif, vifs[0].old_ctx,
					 true)) {
		IWL_ERR(mvm, "failed to reassign old_ctx after failure.\n");
		goto out_restart;
	}

	goto out;

out_restart:
	/* things keep failing, better restart the hw */
	iwl_mvm_nic_restart(mvm, false);

out:
	mutex_unlock(&mvm->mutex);

	return ret;
}

static int iwl_mvm_switch_vif_chanctx(struct ieee80211_hw *hw,
				      struct ieee80211_vif_chanctx_switch *vifs,
				      int n_vifs,
				      enum ieee80211_chanctx_switch_mode mode)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	/* we only support a single-vif right now */
	if (n_vifs > 1)
		return -EOPNOTSUPP;

	switch (mode) {
	case CHANCTX_SWMODE_SWAP_CONTEXTS:
		ret = iwl_mvm_switch_vif_chanctx_swap(mvm, vifs);
		break;
	case CHANCTX_SWMODE_REASSIGN_VIF:
		ret = iwl_mvm_switch_vif_chanctx_reassign(mvm, vifs);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int iwl_mvm_set_tim(struct ieee80211_hw *hw,
			   struct ieee80211_sta *sta,
			   bool set)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);

	if (!mvm_sta || !mvm_sta->vif) {
		IWL_ERR(mvm, "Station is not associated to a vif\n");
		return -EINVAL;
	}

	return iwl_mvm_mac_ctxt_beacon_changed(mvm, mvm_sta->vif);
}

#ifdef CONFIG_NL80211_TESTMODE
static const struct nla_policy iwl_mvm_tm_policy[IWL_MVM_TM_ATTR_MAX + 1] = {
	[IWL_MVM_TM_ATTR_CMD] = { .type = NLA_U32 },
	[IWL_MVM_TM_ATTR_NOA_DURATION] = { .type = NLA_U32 },
	[IWL_MVM_TM_ATTR_BEACON_FILTER_STATE] = { .type = NLA_U32 },
};

static int __iwl_mvm_mac_testmode_cmd(struct iwl_mvm *mvm,
				      struct ieee80211_vif *vif,
				      void *data, int len)
{
	struct nlattr *tb[IWL_MVM_TM_ATTR_MAX + 1];
	int err;
	u32 noa_duration;

	err = nla_parse(tb, IWL_MVM_TM_ATTR_MAX, data, len, iwl_mvm_tm_policy);
	if (err)
		return err;

	if (!tb[IWL_MVM_TM_ATTR_CMD])
		return -EINVAL;

	switch (nla_get_u32(tb[IWL_MVM_TM_ATTR_CMD])) {
	case IWL_MVM_TM_CMD_SET_NOA:
		if (!vif || vif->type != NL80211_IFTYPE_AP || !vif->p2p ||
		    !vif->bss_conf.enable_beacon ||
		    !tb[IWL_MVM_TM_ATTR_NOA_DURATION])
			return -EINVAL;

		noa_duration = nla_get_u32(tb[IWL_MVM_TM_ATTR_NOA_DURATION]);
		if (noa_duration >= vif->bss_conf.beacon_int)
			return -EINVAL;

		mvm->noa_duration = noa_duration;
		mvm->noa_vif = vif;

		return iwl_mvm_update_quotas(mvm, false, NULL);
	case IWL_MVM_TM_CMD_SET_BEACON_FILTER:
		/* must be associated client vif - ignore authorized */
		if (!vif || vif->type != NL80211_IFTYPE_STATION ||
		    !vif->bss_conf.assoc || !vif->bss_conf.dtim_period ||
		    !tb[IWL_MVM_TM_ATTR_BEACON_FILTER_STATE])
			return -EINVAL;

		if (nla_get_u32(tb[IWL_MVM_TM_ATTR_BEACON_FILTER_STATE]))
			return iwl_mvm_enable_beacon_filter(mvm, vif, 0);
		return iwl_mvm_disable_beacon_filter(mvm, vif, 0);
	}

	return -EOPNOTSUPP;
}

static int iwl_mvm_mac_testmode_cmd(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    void *data, int len)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int err;

	mutex_lock(&mvm->mutex);
	err = __iwl_mvm_mac_testmode_cmd(mvm, vif, data, len);
	mutex_unlock(&mvm->mutex);

	return err;
}
#endif

static void iwl_mvm_channel_switch(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_channel_switch *chsw)
{
	/* By implementing this operation, we prevent mac80211 from
	 * starting its own channel switch timer, so that we can call
	 * ieee80211_chswitch_done() ourselves at the right time
	 * (which is when the absence time event starts).
	 */

	IWL_DEBUG_MAC80211(IWL_MAC80211_GET_MVM(hw),
			   "dummy channel switch op\n");
}

static int iwl_mvm_pre_channel_switch(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_channel_switch *chsw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct ieee80211_vif *csa_vif;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	u32 apply_time;
	int ret;

	mutex_lock(&mvm->mutex);

	mvmvif->csa_failed = false;

	IWL_DEBUG_MAC80211(mvm, "pre CSA to freq %d\n",
			   chsw->chandef.center_freq1);

	iwl_fw_dbg_trigger_simple_stop(mvm, vif, FW_DBG_TRIGGER_CHANNEL_SWITCH);

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
		csa_vif =
			rcu_dereference_protected(mvm->csa_vif,
						  lockdep_is_held(&mvm->mutex));
		if (WARN_ONCE(csa_vif && csa_vif->csa_active,
			      "Another CSA is already in progress")) {
			ret = -EBUSY;
			goto out_unlock;
		}

		/* we still didn't unblock tx. prevent new CS meanwhile */
		if (rcu_dereference_protected(mvm->csa_tx_blocked_vif,
					      lockdep_is_held(&mvm->mutex))) {
			ret = -EBUSY;
			goto out_unlock;
		}

		rcu_assign_pointer(mvm->csa_vif, vif);

		if (WARN_ONCE(mvmvif->csa_countdown,
			      "Previous CSA countdown didn't complete")) {
			ret = -EBUSY;
			goto out_unlock;
		}

		mvmvif->csa_target_freq = chsw->chandef.chan->center_freq;

		break;
	case NL80211_IFTYPE_STATION:
		if (mvmvif->lqm_active)
			iwl_mvm_send_lqm_cmd(vif,
					     LQM_CMD_OPERATION_STOP_MEASUREMENT,
					     0, 0);

		/* Schedule the time event to a bit before beacon 1,
		 * to make sure we're in the new channel when the
		 * GO/AP arrives.
		 */
		apply_time = chsw->device_timestamp +
			((vif->bss_conf.beacon_int * (chsw->count - 1) -
			  IWL_MVM_CHANNEL_SWITCH_TIME_CLIENT) * 1024);

		if (chsw->block_tx)
			iwl_mvm_csa_client_absent(mvm, vif);

		iwl_mvm_schedule_csa_period(mvm, vif, vif->bss_conf.beacon_int,
					    apply_time);
		if (mvmvif->bf_data.bf_enabled) {
			ret = iwl_mvm_disable_beacon_filter(mvm, vif, 0);
			if (ret)
				goto out_unlock;
		}

		break;
	default:
		break;
	}

	mvmvif->ps_disabled = true;

	ret = iwl_mvm_power_update_ps(mvm);
	if (ret)
		goto out_unlock;

	/* we won't be on this channel any longer */
	iwl_mvm_teardown_tdls_peers(mvm);

out_unlock:
	mutex_unlock(&mvm->mutex);

	return ret;
}

static int iwl_mvm_post_channel_switch(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);

	if (mvmvif->csa_failed) {
		mvmvif->csa_failed = false;
		ret = -EIO;
		goto out_unlock;
	}

	if (vif->type == NL80211_IFTYPE_STATION) {
		struct iwl_mvm_sta *mvmsta;

		mvmsta = iwl_mvm_sta_from_staid_protected(mvm,
							  mvmvif->ap_sta_id);

		if (WARN_ON(!mvmsta)) {
			ret = -EIO;
			goto out_unlock;
		}

		iwl_mvm_sta_modify_disable_tx(mvm, mvmsta, false);

		iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);

		ret = iwl_mvm_enable_beacon_filter(mvm, vif, 0);
		if (ret)
			goto out_unlock;

		iwl_mvm_stop_session_protection(mvm, vif);
	}

	mvmvif->ps_disabled = false;

	ret = iwl_mvm_power_update_ps(mvm);

out_unlock:
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void iwl_mvm_mac_flush(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif, u32 queues, bool drop)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif;
	struct iwl_mvm_sta *mvmsta;
	struct ieee80211_sta *sta;
	int i;
	u32 msk = 0;

	if (!vif || vif->type != NL80211_IFTYPE_STATION)
		return;

	/* Make sure we're done with the deferred traffic before flushing */
	if (iwl_mvm_is_dqa_supported(mvm))
		flush_work(&mvm->add_stream_wk);

	mutex_lock(&mvm->mutex);
	mvmvif = iwl_mvm_vif_from_mac80211(vif);

	/* flush the AP-station and all TDLS peers */
	for (i = 0; i < IWL_MVM_STATION_COUNT; i++) {
		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[i],
						lockdep_is_held(&mvm->mutex));
		if (IS_ERR_OR_NULL(sta))
			continue;

		mvmsta = iwl_mvm_sta_from_mac80211(sta);
		if (mvmsta->vif != vif)
			continue;

		/* make sure only TDLS peers or the AP are flushed */
		WARN_ON(i != mvmvif->ap_sta_id && !sta->tdls);

		msk |= mvmsta->tfd_queue_msk;
	}

	if (drop) {
		if (iwl_mvm_flush_tx_path(mvm, msk, 0))
			IWL_ERR(mvm, "flush request fail\n");
		mutex_unlock(&mvm->mutex);
	} else {
		mutex_unlock(&mvm->mutex);

		/* this can take a while, and we may need/want other operations
		 * to succeed while doing this, so do it without the mutex held
		 */
		iwl_trans_wait_tx_queue_empty(mvm->trans, msk);
	}
}

static int iwl_mvm_mac_get_survey(struct ieee80211_hw *hw, int idx,
				  struct survey_info *survey)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	memset(survey, 0, sizeof(*survey));

	/* only support global statistics right now */
	if (idx != 0)
		return -ENOENT;

	if (!fw_has_capa(&mvm->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_RADIO_BEACON_STATS))
		return -ENOENT;

	mutex_lock(&mvm->mutex);

	if (mvm->ucode_loaded) {
		ret = iwl_mvm_request_statistics(mvm, false);
		if (ret)
			goto out;
	}

	survey->filled = SURVEY_INFO_TIME |
			 SURVEY_INFO_TIME_RX |
			 SURVEY_INFO_TIME_TX |
			 SURVEY_INFO_TIME_SCAN;
	survey->time = mvm->accu_radio_stats.on_time_rf +
		       mvm->radio_stats.on_time_rf;
	do_div(survey->time, USEC_PER_MSEC);

	survey->time_rx = mvm->accu_radio_stats.rx_time +
			  mvm->radio_stats.rx_time;
	do_div(survey->time_rx, USEC_PER_MSEC);

	survey->time_tx = mvm->accu_radio_stats.tx_time +
			  mvm->radio_stats.tx_time;
	do_div(survey->time_tx, USEC_PER_MSEC);

	survey->time_scan = mvm->accu_radio_stats.on_time_scan +
			    mvm->radio_stats.on_time_scan;
	do_div(survey->time_scan, USEC_PER_MSEC);

	ret = 0;
 out:
	mutex_unlock(&mvm->mutex);
	return ret;
}

static void iwl_mvm_mac_sta_statistics(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta,
				       struct station_info *sinfo)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);

	if (mvmsta->avg_energy) {
		sinfo->signal_avg = mvmsta->avg_energy;
		sinfo->filled |= BIT(NL80211_STA_INFO_SIGNAL_AVG);
	}

	if (!fw_has_capa(&mvm->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_RADIO_BEACON_STATS))
		return;

	/* if beacon filtering isn't on mac80211 does it anyway */
	if (!(vif->driver_flags & IEEE80211_VIF_BEACON_FILTER))
		return;

	if (!vif->bss_conf.assoc)
		return;

	mutex_lock(&mvm->mutex);

	if (mvmvif->ap_sta_id != mvmsta->sta_id)
		goto unlock;

	if (iwl_mvm_request_statistics(mvm, false))
		goto unlock;

	sinfo->rx_beacon = mvmvif->beacon_stats.num_beacons +
			   mvmvif->beacon_stats.accu_num_beacons;
	sinfo->filled |= BIT(NL80211_STA_INFO_BEACON_RX);
	if (mvmvif->beacon_stats.avg_signal) {
		/* firmware only reports a value after RXing a few beacons */
		sinfo->rx_beacon_signal_avg = mvmvif->beacon_stats.avg_signal;
		sinfo->filled |= BIT(NL80211_STA_INFO_BEACON_SIGNAL_AVG);
	}
 unlock:
	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_event_mlme_callback(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					const struct ieee80211_event *event)
{
#define CHECK_MLME_TRIGGER(_mvm, _trig, _buf, _cnt, _fmt...)	\
	do {							\
		if ((_cnt) && --(_cnt))				\
			break;					\
		iwl_mvm_fw_dbg_collect_trig(_mvm, _trig, _fmt);\
	} while (0)

	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_mlme *trig_mlme;

	if (!iwl_fw_dbg_trigger_enabled(mvm->fw, FW_DBG_TRIGGER_MLME))
		return;

	trig = iwl_fw_dbg_get_trigger(mvm->fw, FW_DBG_TRIGGER_MLME);
	trig_mlme = (void *)trig->data;
	if (!iwl_fw_dbg_trigger_check_stop(mvm, vif, trig))
		return;

	if (event->u.mlme.data == ASSOC_EVENT) {
		if (event->u.mlme.status == MLME_DENIED)
			CHECK_MLME_TRIGGER(mvm, trig, buf,
					   trig_mlme->stop_assoc_denied,
					   "DENIED ASSOC: reason %d",
					    event->u.mlme.reason);
		else if (event->u.mlme.status == MLME_TIMEOUT)
			CHECK_MLME_TRIGGER(mvm, trig, buf,
					   trig_mlme->stop_assoc_timeout,
					   "ASSOC TIMEOUT");
	} else if (event->u.mlme.data == AUTH_EVENT) {
		if (event->u.mlme.status == MLME_DENIED)
			CHECK_MLME_TRIGGER(mvm, trig, buf,
					   trig_mlme->stop_auth_denied,
					   "DENIED AUTH: reason %d",
					   event->u.mlme.reason);
		else if (event->u.mlme.status == MLME_TIMEOUT)
			CHECK_MLME_TRIGGER(mvm, trig, buf,
					   trig_mlme->stop_auth_timeout,
					   "AUTH TIMEOUT");
	} else if (event->u.mlme.data == DEAUTH_RX_EVENT) {
		CHECK_MLME_TRIGGER(mvm, trig, buf,
				   trig_mlme->stop_rx_deauth,
				   "DEAUTH RX %d", event->u.mlme.reason);
	} else if (event->u.mlme.data == DEAUTH_TX_EVENT) {
		CHECK_MLME_TRIGGER(mvm, trig, buf,
				   trig_mlme->stop_tx_deauth,
				   "DEAUTH TX %d", event->u.mlme.reason);
	}
#undef CHECK_MLME_TRIGGER
}

static void iwl_mvm_event_bar_rx_callback(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  const struct ieee80211_event *event)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_ba *ba_trig;

	if (!iwl_fw_dbg_trigger_enabled(mvm->fw, FW_DBG_TRIGGER_BA))
		return;

	trig = iwl_fw_dbg_get_trigger(mvm->fw, FW_DBG_TRIGGER_BA);
	ba_trig = (void *)trig->data;
	if (!iwl_fw_dbg_trigger_check_stop(mvm, vif, trig))
		return;

	if (!(le16_to_cpu(ba_trig->rx_bar) & BIT(event->u.ba.tid)))
		return;

	iwl_mvm_fw_dbg_collect_trig(mvm, trig,
				    "BAR received from %pM, tid %d, ssn %d",
				    event->u.ba.sta->addr, event->u.ba.tid,
				    event->u.ba.ssn);
}

static void
iwl_mvm_event_frame_timeout_callback(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif,
				     const struct ieee80211_event *event)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_ba *ba_trig;

	if (!iwl_fw_dbg_trigger_enabled(mvm->fw, FW_DBG_TRIGGER_BA))
		return;

	trig = iwl_fw_dbg_get_trigger(mvm->fw, FW_DBG_TRIGGER_BA);
	ba_trig = (void *)trig->data;
	if (!iwl_fw_dbg_trigger_check_stop(mvm, vif, trig))
		return;

	if (!(le16_to_cpu(ba_trig->frame_timeout) & BIT(event->u.ba.tid)))
		return;

	iwl_mvm_fw_dbg_collect_trig(mvm, trig,
				    "Frame from %pM timed out, tid %d",
				    event->u.ba.sta->addr, event->u.ba.tid);
}

static void iwl_mvm_mac_event_callback(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       const struct ieee80211_event *event)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	switch (event->type) {
	case MLME_EVENT:
		iwl_mvm_event_mlme_callback(mvm, vif, event);
		break;
	case BAR_RX_EVENT:
		iwl_mvm_event_bar_rx_callback(mvm, vif, event);
		break;
	case BA_FRAME_TIMEOUT:
		iwl_mvm_event_frame_timeout_callback(mvm, vif, event);
		break;
	default:
		break;
	}
}

void iwl_mvm_sync_rx_queues_internal(struct iwl_mvm *mvm,
				     struct iwl_mvm_internal_rxq_notif *notif,
				     u32 size)
{
	u32 qmask = BIT(mvm->trans->num_rx_queues) - 1;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (!iwl_mvm_has_new_rx_api(mvm))
		return;

	notif->cookie = mvm->queue_sync_cookie;

	if (notif->sync)
		atomic_set(&mvm->queue_sync_counter,
			   mvm->trans->num_rx_queues);

	ret = iwl_mvm_notify_rx_queue(mvm, qmask, (u8 *)notif, size);
	if (ret) {
		IWL_ERR(mvm, "Failed to trigger RX queues sync (%d)\n", ret);
		goto out;
	}

	if (notif->sync)
		ret = wait_event_timeout(mvm->rx_sync_waitq,
					 atomic_read(&mvm->queue_sync_counter) == 0,
					 HZ);
	WARN_ON_ONCE(!ret);

out:
	atomic_set(&mvm->queue_sync_counter, 0);
	mvm->queue_sync_cookie++;
}

static void iwl_mvm_sync_rx_queues(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_internal_rxq_notif data = {
		.type = IWL_MVM_RXQ_EMPTY,
		.sync = 1,
	};

	mutex_lock(&mvm->mutex);
	iwl_mvm_sync_rx_queues_internal(mvm, &data, sizeof(data));
	mutex_unlock(&mvm->mutex);
}

const struct ieee80211_ops iwl_mvm_hw_ops = {
	.tx = iwl_mvm_mac_tx,
	.ampdu_action = iwl_mvm_mac_ampdu_action,
	.start = iwl_mvm_mac_start,
	.reconfig_complete = iwl_mvm_mac_reconfig_complete,
	.stop = iwl_mvm_mac_stop,
	.add_interface = iwl_mvm_mac_add_interface,
	.remove_interface = iwl_mvm_mac_remove_interface,
	.config = iwl_mvm_mac_config,
	.prepare_multicast = iwl_mvm_prepare_multicast,
	.configure_filter = iwl_mvm_configure_filter,
	.config_iface_filter = iwl_mvm_config_iface_filter,
	.bss_info_changed = iwl_mvm_bss_info_changed,
	.hw_scan = iwl_mvm_mac_hw_scan,
	.cancel_hw_scan = iwl_mvm_mac_cancel_hw_scan,
	.sta_pre_rcu_remove = iwl_mvm_sta_pre_rcu_remove,
	.sta_state = iwl_mvm_mac_sta_state,
	.sta_notify = iwl_mvm_mac_sta_notify,
	.allow_buffered_frames = iwl_mvm_mac_allow_buffered_frames,
	.release_buffered_frames = iwl_mvm_mac_release_buffered_frames,
	.set_rts_threshold = iwl_mvm_mac_set_rts_threshold,
	.sta_rc_update = iwl_mvm_sta_rc_update,
	.conf_tx = iwl_mvm_mac_conf_tx,
	.mgd_prepare_tx = iwl_mvm_mac_mgd_prepare_tx,
	.mgd_protect_tdls_discover = iwl_mvm_mac_mgd_protect_tdls_discover,
	.flush = iwl_mvm_mac_flush,
	.sched_scan_start = iwl_mvm_mac_sched_scan_start,
	.sched_scan_stop = iwl_mvm_mac_sched_scan_stop,
	.set_key = iwl_mvm_mac_set_key,
	.update_tkip_key = iwl_mvm_mac_update_tkip_key,
	.remain_on_channel = iwl_mvm_roc,
	.cancel_remain_on_channel = iwl_mvm_cancel_roc,
	.add_chanctx = iwl_mvm_add_chanctx,
	.remove_chanctx = iwl_mvm_remove_chanctx,
	.change_chanctx = iwl_mvm_change_chanctx,
	.assign_vif_chanctx = iwl_mvm_assign_vif_chanctx,
	.unassign_vif_chanctx = iwl_mvm_unassign_vif_chanctx,
	.switch_vif_chanctx = iwl_mvm_switch_vif_chanctx,

	.start_ap = iwl_mvm_start_ap_ibss,
	.stop_ap = iwl_mvm_stop_ap_ibss,
	.join_ibss = iwl_mvm_start_ap_ibss,
	.leave_ibss = iwl_mvm_stop_ap_ibss,

	.set_tim = iwl_mvm_set_tim,

	.channel_switch = iwl_mvm_channel_switch,
	.pre_channel_switch = iwl_mvm_pre_channel_switch,
	.post_channel_switch = iwl_mvm_post_channel_switch,

	.tdls_channel_switch = iwl_mvm_tdls_channel_switch,
	.tdls_cancel_channel_switch = iwl_mvm_tdls_cancel_channel_switch,
	.tdls_recv_channel_switch = iwl_mvm_tdls_recv_channel_switch,

	.event_callback = iwl_mvm_mac_event_callback,

	.sync_rx_queues = iwl_mvm_sync_rx_queues,

	CFG80211_TESTMODE_CMD(iwl_mvm_mac_testmode_cmd)

#ifdef CONFIG_PM_SLEEP
	/* look at d3.c */
	.suspend = iwl_mvm_suspend,
	.resume = iwl_mvm_resume,
	.set_wakeup = iwl_mvm_set_wakeup,
	.set_rekey_data = iwl_mvm_set_rekey_data,
#if IS_ENABLED(CONFIG_IPV6)
	.ipv6_addr_change = iwl_mvm_ipv6_addr_change,
#endif
	.set_default_unicast_key = iwl_mvm_set_default_unicast_key,
#endif
	.get_survey = iwl_mvm_mac_get_survey,
	.sta_statistics = iwl_mvm_mac_sta_statistics,
};
