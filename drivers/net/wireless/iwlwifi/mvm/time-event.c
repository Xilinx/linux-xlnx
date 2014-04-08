/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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

#include <linux/jiffies.h>
#include <net/mac80211.h>

#include "iwl-notif-wait.h"
#include "iwl-trans.h"
#include "fw-api.h"
#include "time-event.h"
#include "mvm.h"
#include "iwl-io.h"
#include "iwl-prph.h"

/* A TimeUnit is 1024 microsecond */
#define MSEC_TO_TU(_msec)	(_msec*1000/1024)

/*
 * For the high priority TE use a time event type that has similar priority to
 * the FW's action scan priority.
 */
#define IWL_MVM_ROC_TE_TYPE_NORMAL TE_P2P_DEVICE_DISCOVERABLE
#define IWL_MVM_ROC_TE_TYPE_MGMT_TX TE_P2P_CLIENT_ASSOC

void iwl_mvm_te_clear_data(struct iwl_mvm *mvm,
			   struct iwl_mvm_time_event_data *te_data)
{
	lockdep_assert_held(&mvm->time_event_lock);

	if (te_data->id == TE_MAX)
		return;

	list_del(&te_data->list);
	te_data->running = false;
	te_data->uid = 0;
	te_data->id = TE_MAX;
	te_data->vif = NULL;
}

void iwl_mvm_roc_done_wk(struct work_struct *wk)
{
	struct iwl_mvm *mvm = container_of(wk, struct iwl_mvm, roc_done_wk);

	synchronize_net();

	/*
	 * Flush the offchannel queue -- this is called when the time
	 * event finishes or is cancelled, so that frames queued for it
	 * won't get stuck on the queue and be transmitted in the next
	 * time event.
	 * We have to send the command asynchronously since this cannot
	 * be under the mutex for locking reasons, but that's not an
	 * issue as it will have to complete before the next command is
	 * executed, and a new time event means a new command.
	 */
	iwl_mvm_flush_tx_path(mvm, BIT(IWL_MVM_OFFCHANNEL_QUEUE), false);
}

static void iwl_mvm_roc_finished(struct iwl_mvm *mvm)
{
	/*
	 * First, clear the ROC_RUNNING status bit. This will cause the TX
	 * path to drop offchannel transmissions. That would also be done
	 * by mac80211, but it is racy, in particular in the case that the
	 * time event actually completed in the firmware (which is handled
	 * in iwl_mvm_te_handle_notif).
	 */
	clear_bit(IWL_MVM_STATUS_ROC_RUNNING, &mvm->status);

	/*
	 * Of course, our status bit is just as racy as mac80211, so in
	 * addition, fire off the work struct which will drop all frames
	 * from the hardware queues that made it through the race. First
	 * it will of course synchronize the TX path to make sure that
	 * any *new* TX will be rejected.
	 */
	schedule_work(&mvm->roc_done_wk);
}

static bool iwl_mvm_te_check_disconnect(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					const char *errmsg)
{
	if (vif->type != NL80211_IFTYPE_STATION)
		return false;
	if (vif->bss_conf.assoc && vif->bss_conf.dtim_period)
		return false;
	if (errmsg)
		IWL_ERR(mvm, "%s\n", errmsg);
	ieee80211_connection_loss(vif);
	return true;
}

/*
 * Handles a FW notification for an event that is known to the driver.
 *
 * @mvm: the mvm component
 * @te_data: the time event data
 * @notif: the notification data corresponding the time event data.
 */
static void iwl_mvm_te_handle_notif(struct iwl_mvm *mvm,
				    struct iwl_mvm_time_event_data *te_data,
				    struct iwl_time_event_notif *notif)
{
	lockdep_assert_held(&mvm->time_event_lock);

	IWL_DEBUG_TE(mvm, "Handle time event notif - UID = 0x%x action %d\n",
		     le32_to_cpu(notif->unique_id),
		     le32_to_cpu(notif->action));

	/*
	 * The FW sends the start/end time event notifications even for events
	 * that it fails to schedule. This is indicated in the status field of
	 * the notification. This happens in cases that the scheduler cannot
	 * find a schedule that can handle the event (for example requesting a
	 * P2P Device discoveribility, while there are other higher priority
	 * events in the system).
	 */
	if (!le32_to_cpu(notif->status)) {
		bool start = le32_to_cpu(notif->action) &
				TE_V2_NOTIF_HOST_EVENT_START;
		IWL_WARN(mvm, "Time Event %s notification failure\n",
			 start ? "start" : "end");
		if (iwl_mvm_te_check_disconnect(mvm, te_data->vif, NULL)) {
			iwl_mvm_te_clear_data(mvm, te_data);
			return;
		}
	}

	if (le32_to_cpu(notif->action) & TE_V2_NOTIF_HOST_EVENT_END) {
		IWL_DEBUG_TE(mvm,
			     "TE ended - current time %lu, estimated end %lu\n",
			     jiffies, te_data->end_jiffies);

		if (te_data->vif->type == NL80211_IFTYPE_P2P_DEVICE) {
			ieee80211_remain_on_channel_expired(mvm->hw);
			iwl_mvm_roc_finished(mvm);
		}

		/*
		 * By now, we should have finished association
		 * and know the dtim period.
		 */
		iwl_mvm_te_check_disconnect(mvm, te_data->vif,
			"No association and the time event is over already...");
		iwl_mvm_te_clear_data(mvm, te_data);
	} else if (le32_to_cpu(notif->action) & TE_V2_NOTIF_HOST_EVENT_START) {
		te_data->running = true;
		te_data->end_jiffies = TU_TO_EXP_TIME(te_data->duration);

		if (te_data->vif->type == NL80211_IFTYPE_P2P_DEVICE) {
			set_bit(IWL_MVM_STATUS_ROC_RUNNING, &mvm->status);
			ieee80211_ready_on_channel(mvm->hw);
		}
	} else {
		IWL_WARN(mvm, "Got TE with unknown action\n");
	}
}

/*
 * The Rx handler for time event notifications
 */
int iwl_mvm_rx_time_event_notif(struct iwl_mvm *mvm,
				struct iwl_rx_cmd_buffer *rxb,
				struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_time_event_notif *notif = (void *)pkt->data;
	struct iwl_mvm_time_event_data *te_data, *tmp;

	IWL_DEBUG_TE(mvm, "Time event notification - UID = 0x%x action %d\n",
		     le32_to_cpu(notif->unique_id),
		     le32_to_cpu(notif->action));

	spin_lock_bh(&mvm->time_event_lock);
	list_for_each_entry_safe(te_data, tmp, &mvm->time_event_list, list) {
		if (le32_to_cpu(notif->unique_id) == te_data->uid)
			iwl_mvm_te_handle_notif(mvm, te_data, notif);
	}
	spin_unlock_bh(&mvm->time_event_lock);

	return 0;
}

static bool iwl_mvm_time_event_response(struct iwl_notif_wait_data *notif_wait,
					struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_mvm *mvm =
		container_of(notif_wait, struct iwl_mvm, notif_wait);
	struct iwl_mvm_time_event_data *te_data = data;
	struct iwl_time_event_resp *resp;
	int resp_len = le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK;

	if (WARN_ON(pkt->hdr.cmd != TIME_EVENT_CMD))
		return true;

	if (WARN_ON_ONCE(resp_len != sizeof(pkt->hdr) + sizeof(*resp))) {
		IWL_ERR(mvm, "Invalid TIME_EVENT_CMD response\n");
		return true;
	}

	resp = (void *)pkt->data;

	/* we should never get a response to another TIME_EVENT_CMD here */
	if (WARN_ON_ONCE(le32_to_cpu(resp->id) != te_data->id))
		return false;

	te_data->uid = le32_to_cpu(resp->unique_id);
	IWL_DEBUG_TE(mvm, "TIME_EVENT_CMD response - UID = 0x%x\n",
		     te_data->uid);
	return true;
}

/* used to convert from time event API v2 to v1 */
#define TE_V2_DEP_POLICY_MSK (TE_V2_DEP_OTHER | TE_V2_DEP_TSF |\
			     TE_V2_EVENT_SOCIOPATHIC)
static inline u16 te_v2_get_notify(__le16 policy)
{
	return le16_to_cpu(policy) & TE_V2_NOTIF_MSK;
}

static inline u16 te_v2_get_dep_policy(__le16 policy)
{
	return (le16_to_cpu(policy) & TE_V2_DEP_POLICY_MSK) >>
		TE_V2_PLACEMENT_POS;
}

static inline u16 te_v2_get_absence(__le16 policy)
{
	return (le16_to_cpu(policy) & TE_V2_ABSENCE) >> TE_V2_ABSENCE_POS;
}

static void iwl_mvm_te_v2_to_v1(const struct iwl_time_event_cmd_v2 *cmd_v2,
				struct iwl_time_event_cmd_v1 *cmd_v1)
{
	cmd_v1->id_and_color = cmd_v2->id_and_color;
	cmd_v1->action = cmd_v2->action;
	cmd_v1->id = cmd_v2->id;
	cmd_v1->apply_time = cmd_v2->apply_time;
	cmd_v1->max_delay = cmd_v2->max_delay;
	cmd_v1->depends_on = cmd_v2->depends_on;
	cmd_v1->interval = cmd_v2->interval;
	cmd_v1->duration = cmd_v2->duration;
	if (cmd_v2->repeat == TE_V2_REPEAT_ENDLESS)
		cmd_v1->repeat = cpu_to_le32(TE_V1_REPEAT_ENDLESS);
	else
		cmd_v1->repeat = cpu_to_le32(cmd_v2->repeat);
	cmd_v1->max_frags = cpu_to_le32(cmd_v2->max_frags);
	cmd_v1->interval_reciprocal = 0; /* unused */

	cmd_v1->dep_policy = cpu_to_le32(te_v2_get_dep_policy(cmd_v2->policy));
	cmd_v1->is_present = cpu_to_le32(!te_v2_get_absence(cmd_v2->policy));
	cmd_v1->notify = cpu_to_le32(te_v2_get_notify(cmd_v2->policy));
}

static int iwl_mvm_send_time_event_cmd(struct iwl_mvm *mvm,
				       const struct iwl_time_event_cmd_v2 *cmd)
{
	struct iwl_time_event_cmd_v1 cmd_v1;

	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_TIME_EVENT_API_V2)
		return iwl_mvm_send_cmd_pdu(mvm, TIME_EVENT_CMD, CMD_SYNC,
					    sizeof(*cmd), cmd);

	iwl_mvm_te_v2_to_v1(cmd, &cmd_v1);
	return iwl_mvm_send_cmd_pdu(mvm, TIME_EVENT_CMD, CMD_SYNC,
				    sizeof(cmd_v1), &cmd_v1);
}


static int iwl_mvm_time_event_send_add(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif,
				       struct iwl_mvm_time_event_data *te_data,
				       struct iwl_time_event_cmd_v2 *te_cmd)
{
	static const u8 time_event_response[] = { TIME_EVENT_CMD };
	struct iwl_notification_wait wait_time_event;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	IWL_DEBUG_TE(mvm, "Add new TE, duration %d TU\n",
		     le32_to_cpu(te_cmd->duration));

	spin_lock_bh(&mvm->time_event_lock);
	if (WARN_ON(te_data->id != TE_MAX)) {
		spin_unlock_bh(&mvm->time_event_lock);
		return -EIO;
	}
	te_data->vif = vif;
	te_data->duration = le32_to_cpu(te_cmd->duration);
	te_data->id = le32_to_cpu(te_cmd->id);
	list_add_tail(&te_data->list, &mvm->time_event_list);
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
				   iwl_mvm_time_event_response, te_data);

	ret = iwl_mvm_send_time_event_cmd(mvm, te_cmd);
	if (ret) {
		IWL_ERR(mvm, "Couldn't send TIME_EVENT_CMD: %d\n", ret);
		iwl_remove_notification(&mvm->notif_wait, &wait_time_event);
		goto out_clear_te;
	}

	/* No need to wait for anything, so just pass 1 (0 isn't valid) */
	ret = iwl_wait_notification(&mvm->notif_wait, &wait_time_event, 1);
	/* should never fail */
	WARN_ON_ONCE(ret);

	if (ret) {
 out_clear_te:
		spin_lock_bh(&mvm->time_event_lock);
		iwl_mvm_te_clear_data(mvm, te_data);
		spin_unlock_bh(&mvm->time_event_lock);
	}
	return ret;
}

void iwl_mvm_protect_session(struct iwl_mvm *mvm,
			     struct ieee80211_vif *vif,
			     u32 duration, u32 min_duration,
			     u32 max_delay)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_time_event_data *te_data = &mvmvif->time_event_data;
	struct iwl_time_event_cmd_v2 time_cmd = {};

	lockdep_assert_held(&mvm->mutex);

	if (te_data->running &&
	    time_after(te_data->end_jiffies, TU_TO_EXP_TIME(min_duration))) {
		IWL_DEBUG_TE(mvm, "We have enough time in the current TE: %u\n",
			     jiffies_to_msecs(te_data->end_jiffies - jiffies));
		return;
	}

	if (te_data->running) {
		IWL_DEBUG_TE(mvm, "extend 0x%x: only %u ms left\n",
			     te_data->uid,
			     jiffies_to_msecs(te_data->end_jiffies - jiffies));
		/*
		 * we don't have enough time
		 * cancel the current TE and issue a new one
		 * Of course it would be better to remove the old one only
		 * when the new one is added, but we don't care if we are off
		 * channel for a bit. All we need to do, is not to return
		 * before we actually begin to be on the channel.
		 */
		iwl_mvm_stop_session_protection(mvm, vif);
	}

	time_cmd.action = cpu_to_le32(FW_CTXT_ACTION_ADD);
	time_cmd.id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color));
	time_cmd.id = cpu_to_le32(TE_BSS_STA_AGGRESSIVE_ASSOC);

	time_cmd.apply_time =
		cpu_to_le32(iwl_read_prph(mvm->trans, DEVICE_SYSTEM_TIME_REG));

	time_cmd.max_frags = TE_V2_FRAG_NONE;
	time_cmd.max_delay = cpu_to_le32(max_delay);
	/* TODO: why do we need to interval = bi if it is not periodic? */
	time_cmd.interval = cpu_to_le32(1);
	time_cmd.duration = cpu_to_le32(duration);
	time_cmd.repeat = 1;
	time_cmd.policy = cpu_to_le16(TE_V2_NOTIF_HOST_EVENT_START |
				      TE_V2_NOTIF_HOST_EVENT_END);

	iwl_mvm_time_event_send_add(mvm, vif, te_data, &time_cmd);
}

/*
 * Explicit request to remove a time event. The removal of a time event needs to
 * be synchronized with the flow of a time event's end notification, which also
 * removes the time event from the op mode data structures.
 */
void iwl_mvm_remove_time_event(struct iwl_mvm *mvm,
			       struct iwl_mvm_vif *mvmvif,
			       struct iwl_mvm_time_event_data *te_data)
{
	struct iwl_time_event_cmd_v2 time_cmd = {};
	u32 id, uid;
	int ret;

	/*
	 * It is possible that by the time we got to this point the time
	 * event was already removed.
	 */
	spin_lock_bh(&mvm->time_event_lock);

	/* Save time event uid before clearing its data */
	uid = te_data->uid;
	id = te_data->id;

	/*
	 * The clear_data function handles time events that were already removed
	 */
	iwl_mvm_te_clear_data(mvm, te_data);
	spin_unlock_bh(&mvm->time_event_lock);

	/*
	 * It is possible that by the time we try to remove it, the time event
	 * has already ended and removed. In such a case there is no need to
	 * send a removal command.
	 */
	if (id == TE_MAX) {
		IWL_DEBUG_TE(mvm, "TE 0x%x has already ended\n", uid);
		return;
	}

	/* When we remove a TE, the UID is to be set in the id field */
	time_cmd.id = cpu_to_le32(uid);
	time_cmd.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE);
	time_cmd.id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color));

	IWL_DEBUG_TE(mvm, "Removing TE 0x%x\n", le32_to_cpu(time_cmd.id));
	ret = iwl_mvm_send_time_event_cmd(mvm, &time_cmd);
	if (WARN_ON(ret))
		return;
}

void iwl_mvm_stop_session_protection(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_time_event_data *te_data = &mvmvif->time_event_data;

	lockdep_assert_held(&mvm->mutex);
	iwl_mvm_remove_time_event(mvm, mvmvif, te_data);
}

int iwl_mvm_start_p2p_roc(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			  int duration, enum ieee80211_roc_type type)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_time_event_data *te_data = &mvmvif->time_event_data;
	struct iwl_time_event_cmd_v2 time_cmd = {};

	lockdep_assert_held(&mvm->mutex);
	if (te_data->running) {
		IWL_WARN(mvm, "P2P_DEVICE remain on channel already running\n");
		return -EBUSY;
	}

	/*
	 * Flush the done work, just in case it's still pending, so that
	 * the work it does can complete and we can accept new frames.
	 */
	flush_work(&mvm->roc_done_wk);

	time_cmd.action = cpu_to_le32(FW_CTXT_ACTION_ADD);
	time_cmd.id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color));

	switch (type) {
	case IEEE80211_ROC_TYPE_NORMAL:
		time_cmd.id = cpu_to_le32(IWL_MVM_ROC_TE_TYPE_NORMAL);
		break;
	case IEEE80211_ROC_TYPE_MGMT_TX:
		time_cmd.id = cpu_to_le32(IWL_MVM_ROC_TE_TYPE_MGMT_TX);
		break;
	default:
		WARN_ONCE(1, "Got an invalid ROC type\n");
		return -EINVAL;
	}

	time_cmd.apply_time = cpu_to_le32(0);
	time_cmd.interval = cpu_to_le32(1);

	/*
	 * The P2P Device TEs can have lower priority than other events
	 * that are being scheduled by the driver/fw, and thus it might not be
	 * scheduled. To improve the chances of it being scheduled, allow them
	 * to be fragmented, and in addition allow them to be delayed.
	 */
	time_cmd.max_frags = min(MSEC_TO_TU(duration)/50, TE_V2_FRAG_ENDLESS);
	time_cmd.max_delay = cpu_to_le32(MSEC_TO_TU(duration/2));
	time_cmd.duration = cpu_to_le32(MSEC_TO_TU(duration));
	time_cmd.repeat = 1;
	time_cmd.policy = cpu_to_le16(TE_V2_NOTIF_HOST_EVENT_START |
				      TE_V2_NOTIF_HOST_EVENT_END);

	return iwl_mvm_time_event_send_add(mvm, vif, te_data, &time_cmd);
}

void iwl_mvm_stop_p2p_roc(struct iwl_mvm *mvm)
{
	struct iwl_mvm_vif *mvmvif;
	struct iwl_mvm_time_event_data *te_data;

	lockdep_assert_held(&mvm->mutex);

	/*
	 * Iterate over the list of time events and find the time event that is
	 * associated with a P2P_DEVICE interface.
	 * This assumes that a P2P_DEVICE interface can have only a single time
	 * event at any given time and this time event coresponds to a ROC
	 * request
	 */
	mvmvif = NULL;
	spin_lock_bh(&mvm->time_event_lock);
	list_for_each_entry(te_data, &mvm->time_event_list, list) {
		if (te_data->vif->type == NL80211_IFTYPE_P2P_DEVICE) {
			mvmvif = iwl_mvm_vif_from_mac80211(te_data->vif);
			break;
		}
	}
	spin_unlock_bh(&mvm->time_event_lock);

	if (!mvmvif) {
		IWL_WARN(mvm, "P2P_DEVICE no remain on channel event\n");
		return;
	}

	iwl_mvm_remove_time_event(mvm, mvmvif, te_data);

	iwl_mvm_roc_finished(mvm);
}
