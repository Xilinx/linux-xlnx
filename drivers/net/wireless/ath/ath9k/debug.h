/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef DEBUG_H
#define DEBUG_H

#include "hw.h"
#include "rc.h"
#include "dfs_debug.h"

struct ath_txq;
struct ath_buf;
struct fft_sample_tlv;

#ifdef CONFIG_ATH9K_DEBUGFS
#define TX_STAT_INC(q, c) sc->debug.stats.txstats[q].c++
#define RESET_STAT_INC(sc, type) sc->debug.stats.reset[type]++
#define ANT_STAT_INC(i, c) sc->debug.stats.ant_stats[i].c++
#define ANT_LNA_INC(i, c) sc->debug.stats.ant_stats[i].lna_recv_cnt[c]++;
#else
#define TX_STAT_INC(q, c) do { } while (0)
#define RESET_STAT_INC(sc, type) do { } while (0)
#define ANT_STAT_INC(i, c) do { } while (0)
#define ANT_LNA_INC(i, c) do { } while (0)
#endif

enum ath_reset_type {
	RESET_TYPE_BB_HANG,
	RESET_TYPE_BB_WATCHDOG,
	RESET_TYPE_FATAL_INT,
	RESET_TYPE_TX_ERROR,
	RESET_TYPE_TX_HANG,
	RESET_TYPE_PLL_HANG,
	RESET_TYPE_MAC_HANG,
	RESET_TYPE_BEACON_STUCK,
	RESET_TYPE_MCI,
	__RESET_TYPE_MAX
};

#ifdef CONFIG_ATH9K_DEBUGFS

/**
 * struct ath_interrupt_stats - Contains statistics about interrupts
 * @total: Total no. of interrupts generated so far
 * @rxok: RX with no errors
 * @rxlp: RX with low priority RX
 * @rxhp: RX with high priority, uapsd only
 * @rxeol: RX with no more RXDESC available
 * @rxorn: RX FIFO overrun
 * @txok: TX completed at the requested rate
 * @txurn: TX FIFO underrun
 * @mib: MIB regs reaching its threshold
 * @rxphyerr: RX with phy errors
 * @rx_keycache_miss: RX with key cache misses
 * @swba: Software Beacon Alert
 * @bmiss: Beacon Miss
 * @bnr: Beacon Not Ready
 * @cst: Carrier Sense TImeout
 * @gtt: Global TX Timeout
 * @tim: RX beacon TIM occurrence
 * @cabend: RX End of CAB traffic
 * @dtimsync: DTIM sync lossage
 * @dtim: RX Beacon with DTIM
 * @bb_watchdog: Baseband watchdog
 * @tsfoor: TSF out of range, indicates that the corrected TSF received
 * from a beacon differs from the PCU's internal TSF by more than a
 * (programmable) threshold
 * @local_timeout: Internal bus timeout.
 * @mci: MCI interrupt, specific to MCI based BTCOEX chipsets
 * @gen_timer: Generic hardware timer interrupt
 */
struct ath_interrupt_stats {
	u32 total;
	u32 rxok;
	u32 rxlp;
	u32 rxhp;
	u32 rxeol;
	u32 rxorn;
	u32 txok;
	u32 txeol;
	u32 txurn;
	u32 mib;
	u32 rxphyerr;
	u32 rx_keycache_miss;
	u32 swba;
	u32 bmiss;
	u32 bnr;
	u32 cst;
	u32 gtt;
	u32 tim;
	u32 cabend;
	u32 dtimsync;
	u32 dtim;
	u32 bb_watchdog;
	u32 tsfoor;
	u32 mci;
	u32 gen_timer;

	/* Sync-cause stats */
	u32 sync_cause_all;
	u32 sync_rtc_irq;
	u32 sync_mac_irq;
	u32 eeprom_illegal_access;
	u32 apb_timeout;
	u32 pci_mode_conflict;
	u32 host1_fatal;
	u32 host1_perr;
	u32 trcv_fifo_perr;
	u32 radm_cpl_ep;
	u32 radm_cpl_dllp_abort;
	u32 radm_cpl_tlp_abort;
	u32 radm_cpl_ecrc_err;
	u32 radm_cpl_timeout;
	u32 local_timeout;
	u32 pm_access;
	u32 mac_awake;
	u32 mac_asleep;
	u32 mac_sleep_access;
};


/**
 * struct ath_tx_stats - Statistics about TX
 * @tx_pkts_all:  No. of total frames transmitted, including ones that
	may have had errors.
 * @tx_bytes_all:  No. of total bytes transmitted, including ones that
	may have had errors.
 * @queued: Total MPDUs (non-aggr) queued
 * @completed: Total MPDUs (non-aggr) completed
 * @a_aggr: Total no. of aggregates queued
 * @a_queued_hw: Total AMPDUs queued to hardware
 * @a_queued_sw: Total AMPDUs queued to software queues
 * @a_completed: Total AMPDUs completed
 * @a_retries: No. of AMPDUs retried (SW)
 * @a_xretries: No. of AMPDUs dropped due to xretries
 * @txerr_filtered: No. of frames with TXERR_FILT flag set.
 * @fifo_underrun: FIFO underrun occurrences
	Valid only for:
		- non-aggregate condition.
		- first packet of aggregate.
 * @xtxop: No. of frames filtered because of TXOP limit
 * @timer_exp: Transmit timer expiry
 * @desc_cfg_err: Descriptor configuration errors
 * @data_urn: TX data underrun errors
 * @delim_urn: TX delimiter underrun errors
 * @puttxbuf: Number of times hardware was given txbuf to write.
 * @txstart:  Number of times hardware was told to start tx.
 * @txprocdesc:  Number of times tx descriptor was processed
 * @txfailed:  Out-of-memory or other errors in xmit path.
 */
struct ath_tx_stats {
	u32 tx_pkts_all;
	u32 tx_bytes_all;
	u32 queued;
	u32 completed;
	u32 xretries;
	u32 a_aggr;
	u32 a_queued_hw;
	u32 a_queued_sw;
	u32 a_completed;
	u32 a_retries;
	u32 a_xretries;
	u32 txerr_filtered;
	u32 fifo_underrun;
	u32 xtxop;
	u32 timer_exp;
	u32 desc_cfg_err;
	u32 data_underrun;
	u32 delim_underrun;
	u32 puttxbuf;
	u32 txstart;
	u32 txprocdesc;
	u32 txfailed;
};

/*
 * Various utility macros to print TX/Queue counters.
 */
#define PR_QNUM(_n) sc->tx.txq_map[_n]->axq_qnum
#define TXSTATS sc->debug.stats.txstats
#define PR(str, elem)							\
	do {								\
		len += scnprintf(buf + len, size - len,			\
				 "%s%13u%11u%10u%10u\n", str,		\
				 TXSTATS[PR_QNUM(IEEE80211_AC_BE)].elem,\
				 TXSTATS[PR_QNUM(IEEE80211_AC_BK)].elem,\
				 TXSTATS[PR_QNUM(IEEE80211_AC_VI)].elem,\
				 TXSTATS[PR_QNUM(IEEE80211_AC_VO)].elem); \
	} while(0)

#define RX_STAT_INC(c) (sc->debug.stats.rxstats.c++)

/**
 * struct ath_rx_stats - RX Statistics
 * @rx_pkts_all:  No. of total frames received, including ones that
	may have had errors.
 * @rx_bytes_all:  No. of total bytes received, including ones that
	may have had errors.
 * @crc_err: No. of frames with incorrect CRC value
 * @decrypt_crc_err: No. of frames whose CRC check failed after
	decryption process completed
 * @phy_err: No. of frames whose reception failed because the PHY
	encountered an error
 * @mic_err: No. of frames with incorrect TKIP MIC verification failure
 * @pre_delim_crc_err: Pre-Frame delimiter CRC error detections
 * @post_delim_crc_err: Post-Frame delimiter CRC error detections
 * @decrypt_busy_err: Decryption interruptions counter
 * @phy_err_stats: Individual PHY error statistics
 * @rx_len_err:  No. of frames discarded due to bad length.
 * @rx_oom_err:  No. of frames dropped due to OOM issues.
 * @rx_rate_err:  No. of frames dropped due to rate errors.
 * @rx_too_many_frags_err:  Frames dropped due to too-many-frags received.
 * @rx_beacons:  No. of beacons received.
 * @rx_frags:  No. of rx-fragements received.
 * @rx_spectral: No of spectral packets received.
 */
struct ath_rx_stats {
	u32 rx_pkts_all;
	u32 rx_bytes_all;
	u32 crc_err;
	u32 decrypt_crc_err;
	u32 phy_err;
	u32 mic_err;
	u32 pre_delim_crc_err;
	u32 post_delim_crc_err;
	u32 decrypt_busy_err;
	u32 phy_err_stats[ATH9K_PHYERR_MAX];
	u32 rx_len_err;
	u32 rx_oom_err;
	u32 rx_rate_err;
	u32 rx_too_many_frags_err;
	u32 rx_beacons;
	u32 rx_frags;
	u32 rx_spectral;
};

#define ANT_MAIN 0
#define ANT_ALT  1

struct ath_antenna_stats {
	u32 recv_cnt;
	u32 rssi_avg;
	u32 lna_recv_cnt[4];
	u32 lna_attempt_cnt[4];
};

struct ath_stats {
	struct ath_interrupt_stats istats;
	struct ath_tx_stats txstats[ATH9K_NUM_TX_QUEUES];
	struct ath_rx_stats rxstats;
	struct ath_dfs_stats dfs_stats;
	struct ath_antenna_stats ant_stats[2];
	u32 reset[__RESET_TYPE_MAX];
};

struct ath9k_debug {
	struct dentry *debugfs_phy;
	u32 regidx;
	struct ath_stats stats;
};

int ath9k_init_debug(struct ath_hw *ah);
void ath9k_deinit_debug(struct ath_softc *sc);

void ath_debug_stat_interrupt(struct ath_softc *sc, enum ath9k_int status);
void ath_debug_stat_tx(struct ath_softc *sc, struct ath_buf *bf,
		       struct ath_tx_status *ts, struct ath_txq *txq,
		       unsigned int flags);
void ath_debug_stat_rx(struct ath_softc *sc, struct ath_rx_status *rs);
int ath9k_get_et_sset_count(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif, int sset);
void ath9k_get_et_stats(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			struct ethtool_stats *stats, u64 *data);
void ath9k_get_et_strings(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif,
			  u32 sset, u8 *data);
void ath9k_sta_add_debugfs(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta,
			   struct dentry *dir);
void ath_debug_send_fft_sample(struct ath_softc *sc,
			       struct fft_sample_tlv *fft_sample);
void ath9k_debug_stat_ant(struct ath_softc *sc,
			  struct ath_hw_antcomb_conf *div_ant_conf,
			  int main_rssi_avg, int alt_rssi_avg);
#else

#define RX_STAT_INC(c) /* NOP */

static inline int ath9k_init_debug(struct ath_hw *ah)
{
	return 0;
}

static inline void ath9k_deinit_debug(struct ath_softc *sc)
{
}
static inline void ath_debug_stat_interrupt(struct ath_softc *sc,
					    enum ath9k_int status)
{
}
static inline void ath_debug_stat_tx(struct ath_softc *sc,
				     struct ath_buf *bf,
				     struct ath_tx_status *ts,
				     struct ath_txq *txq,
				     unsigned int flags)
{
}
static inline void ath_debug_stat_rx(struct ath_softc *sc,
				     struct ath_rx_status *rs)
{
}
static inline void ath9k_debug_stat_ant(struct ath_softc *sc,
					struct ath_hw_antcomb_conf *div_ant_conf,
					int main_rssi_avg, int alt_rssi_avg)
{

}

#endif /* CONFIG_ATH9K_DEBUGFS */

#endif /* DEBUG_H */
