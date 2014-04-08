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

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/export.h>
#include <linux/relay.h>
#include <asm/unaligned.h>

#include "ath9k.h"

#define REG_WRITE_D(_ah, _reg, _val) \
	ath9k_hw_common(_ah)->ops->write((_ah), (_val), (_reg))
#define REG_READ_D(_ah, _reg) \
	ath9k_hw_common(_ah)->ops->read((_ah), (_reg))


static ssize_t ath9k_debugfs_read_buf(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	u8 *buf = file->private_data;
	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

static int ath9k_debugfs_release_buf(struct inode *inode, struct file *file)
{
	vfree(file->private_data);
	return 0;
}

#ifdef CONFIG_ATH_DEBUG

static ssize_t read_file_debug(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "0x%08x\n", common->debug_mask);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_debug(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	unsigned long mask;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoul(buf, 0, &mask))
		return -EINVAL;

	common->debug_mask = mask;
	return count;
}

static const struct file_operations fops_debug = {
	.read = read_file_debug,
	.write = write_file_debug,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#endif

#define DMA_BUF_LEN 1024


static ssize_t read_file_ani(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_hw *ah = sc->sc_ah;
	unsigned int len = 0, size = 1024;
	ssize_t retval = 0;
	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (common->disable_ani) {
		len += scnprintf(buf + len, size - len, "%s: %s\n",
				 "ANI", "DISABLED");
		goto exit;
	}

	len += scnprintf(buf + len, size - len, "%15s: %s\n",
			 "ANI", "ENABLED");
	len += scnprintf(buf + len, size - len, "%15s: %u\n",
			 "ANI RESET", ah->stats.ast_ani_reset);
	len += scnprintf(buf + len, size - len, "%15s: %u\n",
			 "SPUR UP", ah->stats.ast_ani_spurup);
	len += scnprintf(buf + len, size - len, "%15s: %u\n",
			 "SPUR DOWN", ah->stats.ast_ani_spurup);
	len += scnprintf(buf + len, size - len, "%15s: %u\n",
			 "OFDM WS-DET ON", ah->stats.ast_ani_ofdmon);
	len += scnprintf(buf + len, size - len, "%15s: %u\n",
			 "OFDM WS-DET OFF", ah->stats.ast_ani_ofdmoff);
	len += scnprintf(buf + len, size - len, "%15s: %u\n",
			 "MRC-CCK ON", ah->stats.ast_ani_ccklow);
	len += scnprintf(buf + len, size - len, "%15s: %u\n",
			 "MRC-CCK OFF", ah->stats.ast_ani_cckhigh);
	len += scnprintf(buf + len, size - len, "%15s: %u\n",
			 "FIR-STEP UP", ah->stats.ast_ani_stepup);
	len += scnprintf(buf + len, size - len, "%15s: %u\n",
			 "FIR-STEP DOWN", ah->stats.ast_ani_stepdown);
	len += scnprintf(buf + len, size - len, "%15s: %u\n",
			 "INV LISTENTIME", ah->stats.ast_ani_lneg_or_lzero);
	len += scnprintf(buf + len, size - len, "%15s: %u\n",
			 "OFDM ERRORS", ah->stats.ast_ani_ofdmerrs);
	len += scnprintf(buf + len, size - len, "%15s: %u\n",
			 "CCK ERRORS", ah->stats.ast_ani_cckerrs);
exit:
	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static ssize_t write_file_ani(struct file *file,
			      const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	unsigned long ani;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoul(buf, 0, &ani))
		return -EINVAL;

	if (ani < 0 || ani > 1)
		return -EINVAL;

	common->disable_ani = !ani;

	if (common->disable_ani) {
		clear_bit(SC_OP_ANI_RUN, &sc->sc_flags);
		ath_stop_ani(sc);
	} else {
		ath_check_ani(sc);
	}

	return count;
}

static const struct file_operations fops_ani = {
	.read = read_file_ani,
	.write = write_file_ani,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT

static ssize_t read_file_bt_ant_diversity(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d\n", common->bt_ant_diversity);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_bt_ant_diversity(struct file *file,
					   const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath9k_hw_capabilities *pCap = &sc->sc_ah->caps;
	unsigned long bt_ant_diversity;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	if (!(pCap->hw_caps & ATH9K_HW_CAP_BT_ANT_DIV))
		goto exit;

	buf[len] = '\0';
	if (kstrtoul(buf, 0, &bt_ant_diversity))
		return -EINVAL;

	common->bt_ant_diversity = !!bt_ant_diversity;
	ath9k_ps_wakeup(sc);
	ath9k_hw_set_bt_ant_diversity(sc->sc_ah, common->bt_ant_diversity);
	ath_dbg(common, CONFIG, "Enable WLAN/BT RX Antenna diversity: %d\n",
		common->bt_ant_diversity);
	ath9k_ps_restore(sc);
exit:
	return count;
}

static const struct file_operations fops_bt_ant_diversity = {
	.read = read_file_bt_ant_diversity,
	.write = write_file_bt_ant_diversity,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#endif

void ath9k_debug_stat_ant(struct ath_softc *sc,
			  struct ath_hw_antcomb_conf *div_ant_conf,
			  int main_rssi_avg, int alt_rssi_avg)
{
	struct ath_antenna_stats *as_main = &sc->debug.stats.ant_stats[ANT_MAIN];
	struct ath_antenna_stats *as_alt = &sc->debug.stats.ant_stats[ANT_ALT];

	as_main->lna_attempt_cnt[div_ant_conf->main_lna_conf]++;
	as_alt->lna_attempt_cnt[div_ant_conf->alt_lna_conf]++;

	as_main->rssi_avg = main_rssi_avg;
	as_alt->rssi_avg = alt_rssi_avg;
}

static ssize_t read_file_antenna_diversity(struct file *file,
					   char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_hw *ah = sc->sc_ah;
	struct ath9k_hw_capabilities *pCap = &ah->caps;
	struct ath_antenna_stats *as_main = &sc->debug.stats.ant_stats[ANT_MAIN];
	struct ath_antenna_stats *as_alt = &sc->debug.stats.ant_stats[ANT_ALT];
	struct ath_hw_antcomb_conf div_ant_conf;
	unsigned int len = 0, size = 1024;
	ssize_t retval = 0;
	char *buf;
	char *lna_conf_str[4] = {"LNA1_MINUS_LNA2",
				 "LNA2",
				 "LNA1",
				 "LNA1_PLUS_LNA2"};

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (!(pCap->hw_caps & ATH9K_HW_CAP_ANT_DIV_COMB)) {
		len += scnprintf(buf + len, size - len, "%s\n",
				 "Antenna Diversity Combining is disabled");
		goto exit;
	}

	ath9k_ps_wakeup(sc);
	ath9k_hw_antdiv_comb_conf_get(ah, &div_ant_conf);
	len += scnprintf(buf + len, size - len, "Current MAIN config : %s\n",
			 lna_conf_str[div_ant_conf.main_lna_conf]);
	len += scnprintf(buf + len, size - len, "Current ALT config  : %s\n",
			 lna_conf_str[div_ant_conf.alt_lna_conf]);
	len += scnprintf(buf + len, size - len, "Average MAIN RSSI   : %d\n",
			 as_main->rssi_avg);
	len += scnprintf(buf + len, size - len, "Average ALT RSSI    : %d\n\n",
			 as_alt->rssi_avg);
	ath9k_ps_restore(sc);

	len += scnprintf(buf + len, size - len, "Packet Receive Cnt:\n");
	len += scnprintf(buf + len, size - len, "-------------------\n");

	len += scnprintf(buf + len, size - len, "%30s%15s\n",
			 "MAIN", "ALT");
	len += scnprintf(buf + len, size - len, "%-14s:%15d%15d\n",
			 "TOTAL COUNT",
			 as_main->recv_cnt,
			 as_alt->recv_cnt);
	len += scnprintf(buf + len, size - len, "%-14s:%15d%15d\n",
			 "LNA1",
			 as_main->lna_recv_cnt[ATH_ANT_DIV_COMB_LNA1],
			 as_alt->lna_recv_cnt[ATH_ANT_DIV_COMB_LNA1]);
	len += scnprintf(buf + len, size - len, "%-14s:%15d%15d\n",
			 "LNA2",
			 as_main->lna_recv_cnt[ATH_ANT_DIV_COMB_LNA2],
			 as_alt->lna_recv_cnt[ATH_ANT_DIV_COMB_LNA2]);
	len += scnprintf(buf + len, size - len, "%-14s:%15d%15d\n",
			 "LNA1 + LNA2",
			 as_main->lna_recv_cnt[ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2],
			 as_alt->lna_recv_cnt[ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2]);
	len += scnprintf(buf + len, size - len, "%-14s:%15d%15d\n",
			 "LNA1 - LNA2",
			 as_main->lna_recv_cnt[ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2],
			 as_alt->lna_recv_cnt[ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2]);

	len += scnprintf(buf + len, size - len, "\nLNA Config Attempts:\n");
	len += scnprintf(buf + len, size - len, "--------------------\n");

	len += scnprintf(buf + len, size - len, "%30s%15s\n",
			 "MAIN", "ALT");
	len += scnprintf(buf + len, size - len, "%-14s:%15d%15d\n",
			 "LNA1",
			 as_main->lna_attempt_cnt[ATH_ANT_DIV_COMB_LNA1],
			 as_alt->lna_attempt_cnt[ATH_ANT_DIV_COMB_LNA1]);
	len += scnprintf(buf + len, size - len, "%-14s:%15d%15d\n",
			 "LNA2",
			 as_main->lna_attempt_cnt[ATH_ANT_DIV_COMB_LNA2],
			 as_alt->lna_attempt_cnt[ATH_ANT_DIV_COMB_LNA2]);
	len += scnprintf(buf + len, size - len, "%-14s:%15d%15d\n",
			 "LNA1 + LNA2",
			 as_main->lna_attempt_cnt[ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2],
			 as_alt->lna_attempt_cnt[ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2]);
	len += scnprintf(buf + len, size - len, "%-14s:%15d%15d\n",
			 "LNA1 - LNA2",
			 as_main->lna_attempt_cnt[ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2],
			 as_alt->lna_attempt_cnt[ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2]);

exit:
	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_antenna_diversity = {
	.read = read_file_antenna_diversity,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_dma(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_hw *ah = sc->sc_ah;
	char *buf;
	int retval;
	unsigned int len = 0;
	u32 val[ATH9K_NUM_DMA_DEBUG_REGS];
	int i, qcuOffset = 0, dcuOffset = 0;
	u32 *qcuBase = &val[0], *dcuBase = &val[4];

	buf = kmalloc(DMA_BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ath9k_ps_wakeup(sc);

	REG_WRITE_D(ah, AR_MACMISC,
		  ((AR_MACMISC_DMA_OBS_LINE_8 << AR_MACMISC_DMA_OBS_S) |
		   (AR_MACMISC_MISC_OBS_BUS_1 <<
		    AR_MACMISC_MISC_OBS_BUS_MSB_S)));

	len += scnprintf(buf + len, DMA_BUF_LEN - len,
			 "Raw DMA Debug values:\n");

	for (i = 0; i < ATH9K_NUM_DMA_DEBUG_REGS; i++) {
		if (i % 4 == 0)
			len += scnprintf(buf + len, DMA_BUF_LEN - len, "\n");

		val[i] = REG_READ_D(ah, AR_DMADBG_0 + (i * sizeof(u32)));
		len += scnprintf(buf + len, DMA_BUF_LEN - len, "%d: %08x ",
				 i, val[i]);
	}

	len += scnprintf(buf + len, DMA_BUF_LEN - len, "\n\n");
	len += scnprintf(buf + len, DMA_BUF_LEN - len,
			 "Num QCU: chain_st fsp_ok fsp_st DCU: chain_st\n");

	for (i = 0; i < ATH9K_NUM_QUEUES; i++, qcuOffset += 4, dcuOffset += 5) {
		if (i == 8) {
			qcuOffset = 0;
			qcuBase++;
		}

		if (i == 6) {
			dcuOffset = 0;
			dcuBase++;
		}

		len += scnprintf(buf + len, DMA_BUF_LEN - len,
			 "%2d          %2x      %1x     %2x           %2x\n",
			 i, (*qcuBase & (0x7 << qcuOffset)) >> qcuOffset,
			 (*qcuBase & (0x8 << qcuOffset)) >> (qcuOffset + 3),
			 val[2] & (0x7 << (i * 3)) >> (i * 3),
			 (*dcuBase & (0x1f << dcuOffset)) >> dcuOffset);
	}

	len += scnprintf(buf + len, DMA_BUF_LEN - len, "\n");

	len += scnprintf(buf + len, DMA_BUF_LEN - len,
		"qcu_stitch state:   %2x    qcu_fetch state:        %2x\n",
		(val[3] & 0x003c0000) >> 18, (val[3] & 0x03c00000) >> 22);
	len += scnprintf(buf + len, DMA_BUF_LEN - len,
		"qcu_complete state: %2x    dcu_complete state:     %2x\n",
		(val[3] & 0x1c000000) >> 26, (val[6] & 0x3));
	len += scnprintf(buf + len, DMA_BUF_LEN - len,
		"dcu_arb state:      %2x    dcu_fp state:           %2x\n",
		(val[5] & 0x06000000) >> 25, (val[5] & 0x38000000) >> 27);
	len += scnprintf(buf + len, DMA_BUF_LEN - len,
		"chan_idle_dur:     %3d    chan_idle_dur_valid:     %1d\n",
		(val[6] & 0x000003fc) >> 2, (val[6] & 0x00000400) >> 10);
	len += scnprintf(buf + len, DMA_BUF_LEN - len,
		"txfifo_valid_0:      %1d    txfifo_valid_1:          %1d\n",
		(val[6] & 0x00000800) >> 11, (val[6] & 0x00001000) >> 12);
	len += scnprintf(buf + len, DMA_BUF_LEN - len,
		"txfifo_dcu_num_0:   %2d    txfifo_dcu_num_1:       %2d\n",
		(val[6] & 0x0001e000) >> 13, (val[6] & 0x001e0000) >> 17);

	len += scnprintf(buf + len, DMA_BUF_LEN - len, "pcu observe: 0x%x\n",
			 REG_READ_D(ah, AR_OBS_BUS_1));
	len += scnprintf(buf + len, DMA_BUF_LEN - len,
			 "AR_CR: 0x%x\n", REG_READ_D(ah, AR_CR));

	ath9k_ps_restore(sc);

	if (len > DMA_BUF_LEN)
		len = DMA_BUF_LEN;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return retval;
}

static const struct file_operations fops_dma = {
	.read = read_file_dma,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};


void ath_debug_stat_interrupt(struct ath_softc *sc, enum ath9k_int status)
{
	if (status)
		sc->debug.stats.istats.total++;
	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		if (status & ATH9K_INT_RXLP)
			sc->debug.stats.istats.rxlp++;
		if (status & ATH9K_INT_RXHP)
			sc->debug.stats.istats.rxhp++;
		if (status & ATH9K_INT_BB_WATCHDOG)
			sc->debug.stats.istats.bb_watchdog++;
	} else {
		if (status & ATH9K_INT_RX)
			sc->debug.stats.istats.rxok++;
	}
	if (status & ATH9K_INT_RXEOL)
		sc->debug.stats.istats.rxeol++;
	if (status & ATH9K_INT_RXORN)
		sc->debug.stats.istats.rxorn++;
	if (status & ATH9K_INT_TX)
		sc->debug.stats.istats.txok++;
	if (status & ATH9K_INT_TXURN)
		sc->debug.stats.istats.txurn++;
	if (status & ATH9K_INT_RXPHY)
		sc->debug.stats.istats.rxphyerr++;
	if (status & ATH9K_INT_RXKCM)
		sc->debug.stats.istats.rx_keycache_miss++;
	if (status & ATH9K_INT_SWBA)
		sc->debug.stats.istats.swba++;
	if (status & ATH9K_INT_BMISS)
		sc->debug.stats.istats.bmiss++;
	if (status & ATH9K_INT_BNR)
		sc->debug.stats.istats.bnr++;
	if (status & ATH9K_INT_CST)
		sc->debug.stats.istats.cst++;
	if (status & ATH9K_INT_GTT)
		sc->debug.stats.istats.gtt++;
	if (status & ATH9K_INT_TIM)
		sc->debug.stats.istats.tim++;
	if (status & ATH9K_INT_CABEND)
		sc->debug.stats.istats.cabend++;
	if (status & ATH9K_INT_DTIMSYNC)
		sc->debug.stats.istats.dtimsync++;
	if (status & ATH9K_INT_DTIM)
		sc->debug.stats.istats.dtim++;
	if (status & ATH9K_INT_TSFOOR)
		sc->debug.stats.istats.tsfoor++;
	if (status & ATH9K_INT_MCI)
		sc->debug.stats.istats.mci++;
	if (status & ATH9K_INT_GENTIMER)
		sc->debug.stats.istats.gen_timer++;
}

static ssize_t read_file_interrupt(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	unsigned int len = 0;
	int rv;
	int mxlen = 4000;
	char *buf = kmalloc(mxlen, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

#define PR_IS(a, s)						\
	do {							\
		len += scnprintf(buf + len, mxlen - len,	\
				 "%21s: %10u\n", a,		\
				 sc->debug.stats.istats.s);	\
	} while (0)

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		PR_IS("RXLP", rxlp);
		PR_IS("RXHP", rxhp);
		PR_IS("WATHDOG", bb_watchdog);
	} else {
		PR_IS("RX", rxok);
	}
	PR_IS("RXEOL", rxeol);
	PR_IS("RXORN", rxorn);
	PR_IS("TX", txok);
	PR_IS("TXURN", txurn);
	PR_IS("MIB", mib);
	PR_IS("RXPHY", rxphyerr);
	PR_IS("RXKCM", rx_keycache_miss);
	PR_IS("SWBA", swba);
	PR_IS("BMISS", bmiss);
	PR_IS("BNR", bnr);
	PR_IS("CST", cst);
	PR_IS("GTT", gtt);
	PR_IS("TIM", tim);
	PR_IS("CABEND", cabend);
	PR_IS("DTIMSYNC", dtimsync);
	PR_IS("DTIM", dtim);
	PR_IS("TSFOOR", tsfoor);
	PR_IS("MCI", mci);
	PR_IS("GENTIMER", gen_timer);
	PR_IS("TOTAL", total);

	len += scnprintf(buf + len, mxlen - len,
			 "SYNC_CAUSE stats:\n");

	PR_IS("Sync-All", sync_cause_all);
	PR_IS("RTC-IRQ", sync_rtc_irq);
	PR_IS("MAC-IRQ", sync_mac_irq);
	PR_IS("EEPROM-Illegal-Access", eeprom_illegal_access);
	PR_IS("APB-Timeout", apb_timeout);
	PR_IS("PCI-Mode-Conflict", pci_mode_conflict);
	PR_IS("HOST1-Fatal", host1_fatal);
	PR_IS("HOST1-Perr", host1_perr);
	PR_IS("TRCV-FIFO-Perr", trcv_fifo_perr);
	PR_IS("RADM-CPL-EP", radm_cpl_ep);
	PR_IS("RADM-CPL-DLLP-Abort", radm_cpl_dllp_abort);
	PR_IS("RADM-CPL-TLP-Abort", radm_cpl_tlp_abort);
	PR_IS("RADM-CPL-ECRC-Err", radm_cpl_ecrc_err);
	PR_IS("RADM-CPL-Timeout", radm_cpl_timeout);
	PR_IS("Local-Bus-Timeout", local_timeout);
	PR_IS("PM-Access", pm_access);
	PR_IS("MAC-Awake", mac_awake);
	PR_IS("MAC-Asleep", mac_asleep);
	PR_IS("MAC-Sleep-Access", mac_sleep_access);

	if (len > mxlen)
		len = mxlen;

	rv = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return rv;
}

static const struct file_operations fops_interrupt = {
	.read = read_file_interrupt,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_xmit(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char *buf;
	unsigned int len = 0, size = 2048;
	ssize_t retval = 0;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len += sprintf(buf, "%30s %10s%10s%10s\n\n",
		       "BE", "BK", "VI", "VO");

	PR("MPDUs Queued:    ", queued);
	PR("MPDUs Completed: ", completed);
	PR("MPDUs XRetried:  ", xretries);
	PR("Aggregates:      ", a_aggr);
	PR("AMPDUs Queued HW:", a_queued_hw);
	PR("AMPDUs Queued SW:", a_queued_sw);
	PR("AMPDUs Completed:", a_completed);
	PR("AMPDUs Retried:  ", a_retries);
	PR("AMPDUs XRetried: ", a_xretries);
	PR("TXERR Filtered:  ", txerr_filtered);
	PR("FIFO Underrun:   ", fifo_underrun);
	PR("TXOP Exceeded:   ", xtxop);
	PR("TXTIMER Expiry:  ", timer_exp);
	PR("DESC CFG Error:  ", desc_cfg_err);
	PR("DATA Underrun:   ", data_underrun);
	PR("DELIM Underrun:  ", delim_underrun);
	PR("TX-Pkts-All:     ", tx_pkts_all);
	PR("TX-Bytes-All:    ", tx_bytes_all);
	PR("HW-put-tx-buf:   ", puttxbuf);
	PR("HW-tx-start:     ", txstart);
	PR("HW-tx-proc-desc: ", txprocdesc);
	PR("TX-Failed:       ", txfailed);

	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static ssize_t print_queue(struct ath_softc *sc, struct ath_txq *txq,
			   char *buf, ssize_t size)
{
	ssize_t len = 0;

	ath_txq_lock(sc, txq);

	len += scnprintf(buf + len, size - len, "%s: %d ",
			 "qnum", txq->axq_qnum);
	len += scnprintf(buf + len, size - len, "%s: %2d ",
			 "qdepth", txq->axq_depth);
	len += scnprintf(buf + len, size - len, "%s: %2d ",
			 "ampdu-depth", txq->axq_ampdu_depth);
	len += scnprintf(buf + len, size - len, "%s: %3d ",
			 "pending", txq->pending_frames);
	len += scnprintf(buf + len, size - len, "%s: %d\n",
			 "stopped", txq->stopped);

	ath_txq_unlock(sc, txq);
	return len;
}

static ssize_t read_file_queues(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_txq *txq;
	char *buf;
	unsigned int len = 0, size = 1024;
	ssize_t retval = 0;
	int i;
	char *qname[4] = {"VO", "VI", "BE", "BK"};

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		txq = sc->tx.txq_map[i];
		len += scnprintf(buf + len, size - len, "(%s):  ", qname[i]);
		len += print_queue(sc, txq, buf + len, size - len);
	}

	len += scnprintf(buf + len, size - len, "(CAB): ");
	len += print_queue(sc, sc->beacon.cabq, buf + len, size - len);

	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static ssize_t read_file_misc(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ieee80211_hw *hw = sc->hw;
	struct ath9k_vif_iter_data iter_data;
	char buf[512];
	unsigned int len = 0;
	ssize_t retval = 0;
	unsigned int reg;
	u32 rxfilter;

	len += scnprintf(buf + len, sizeof(buf) - len,
			 "BSSID: %pM\n", common->curbssid);
	len += scnprintf(buf + len, sizeof(buf) - len,
			 "BSSID-MASK: %pM\n", common->bssidmask);
	len += scnprintf(buf + len, sizeof(buf) - len,
			 "OPMODE: %s\n",
			 ath_opmode_to_string(sc->sc_ah->opmode));

	ath9k_ps_wakeup(sc);
	rxfilter = ath9k_hw_getrxfilter(sc->sc_ah);
	ath9k_ps_restore(sc);

	len += scnprintf(buf + len, sizeof(buf) - len,
			 "RXFILTER: 0x%x", rxfilter);

	if (rxfilter & ATH9K_RX_FILTER_UCAST)
		len += scnprintf(buf + len, sizeof(buf) - len, " UCAST");
	if (rxfilter & ATH9K_RX_FILTER_MCAST)
		len += scnprintf(buf + len, sizeof(buf) - len, " MCAST");
	if (rxfilter & ATH9K_RX_FILTER_BCAST)
		len += scnprintf(buf + len, sizeof(buf) - len, " BCAST");
	if (rxfilter & ATH9K_RX_FILTER_CONTROL)
		len += scnprintf(buf + len, sizeof(buf) - len, " CONTROL");
	if (rxfilter & ATH9K_RX_FILTER_BEACON)
		len += scnprintf(buf + len, sizeof(buf) - len, " BEACON");
	if (rxfilter & ATH9K_RX_FILTER_PROM)
		len += scnprintf(buf + len, sizeof(buf) - len, " PROM");
	if (rxfilter & ATH9K_RX_FILTER_PROBEREQ)
		len += scnprintf(buf + len, sizeof(buf) - len, " PROBEREQ");
	if (rxfilter & ATH9K_RX_FILTER_PHYERR)
		len += scnprintf(buf + len, sizeof(buf) - len, " PHYERR");
	if (rxfilter & ATH9K_RX_FILTER_MYBEACON)
		len += scnprintf(buf + len, sizeof(buf) - len, " MYBEACON");
	if (rxfilter & ATH9K_RX_FILTER_COMP_BAR)
		len += scnprintf(buf + len, sizeof(buf) - len, " COMP_BAR");
	if (rxfilter & ATH9K_RX_FILTER_PSPOLL)
		len += scnprintf(buf + len, sizeof(buf) - len, " PSPOLL");
	if (rxfilter & ATH9K_RX_FILTER_PHYRADAR)
		len += scnprintf(buf + len, sizeof(buf) - len, " PHYRADAR");
	if (rxfilter & ATH9K_RX_FILTER_MCAST_BCAST_ALL)
		len += scnprintf(buf + len, sizeof(buf) - len, " MCAST_BCAST_ALL");
	if (rxfilter & ATH9K_RX_FILTER_CONTROL_WRAPPER)
		len += scnprintf(buf + len, sizeof(buf) - len, " CONTROL_WRAPPER");

	len += scnprintf(buf + len, sizeof(buf) - len, "\n");

	reg = sc->sc_ah->imask;

	len += scnprintf(buf + len, sizeof(buf) - len,
			 "INTERRUPT-MASK: 0x%x", reg);

	if (reg & ATH9K_INT_SWBA)
		len += scnprintf(buf + len, sizeof(buf) - len, " SWBA");
	if (reg & ATH9K_INT_BMISS)
		len += scnprintf(buf + len, sizeof(buf) - len, " BMISS");
	if (reg & ATH9K_INT_CST)
		len += scnprintf(buf + len, sizeof(buf) - len, " CST");
	if (reg & ATH9K_INT_RX)
		len += scnprintf(buf + len, sizeof(buf) - len, " RX");
	if (reg & ATH9K_INT_RXHP)
		len += scnprintf(buf + len, sizeof(buf) - len, " RXHP");
	if (reg & ATH9K_INT_RXLP)
		len += scnprintf(buf + len, sizeof(buf) - len, " RXLP");
	if (reg & ATH9K_INT_BB_WATCHDOG)
		len += scnprintf(buf + len, sizeof(buf) - len, " BB_WATCHDOG");

	len += scnprintf(buf + len, sizeof(buf) - len, "\n");

	ath9k_calculate_iter_data(hw, NULL, &iter_data);

	len += scnprintf(buf + len, sizeof(buf) - len,
			 "VIF-COUNTS: AP: %i STA: %i MESH: %i WDS: %i"
			 " ADHOC: %i TOTAL: %hi BEACON-VIF: %hi\n",
			 iter_data.naps, iter_data.nstations, iter_data.nmeshes,
			 iter_data.nwds, iter_data.nadhocs,
			 sc->nvifs, sc->nbcnvifs);

	if (len > sizeof(buf))
		len = sizeof(buf);

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	return retval;
}

static ssize_t read_file_reset(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[512];
	unsigned int len = 0;

	len += scnprintf(buf + len, sizeof(buf) - len,
			 "%17s: %2d\n", "Baseband Hang",
			 sc->debug.stats.reset[RESET_TYPE_BB_HANG]);
	len += scnprintf(buf + len, sizeof(buf) - len,
			 "%17s: %2d\n", "Baseband Watchdog",
			 sc->debug.stats.reset[RESET_TYPE_BB_WATCHDOG]);
	len += scnprintf(buf + len, sizeof(buf) - len,
			 "%17s: %2d\n", "Fatal HW Error",
			 sc->debug.stats.reset[RESET_TYPE_FATAL_INT]);
	len += scnprintf(buf + len, sizeof(buf) - len,
			 "%17s: %2d\n", "TX HW error",
			 sc->debug.stats.reset[RESET_TYPE_TX_ERROR]);
	len += scnprintf(buf + len, sizeof(buf) - len,
			 "%17s: %2d\n", "TX Path Hang",
			 sc->debug.stats.reset[RESET_TYPE_TX_HANG]);
	len += scnprintf(buf + len, sizeof(buf) - len,
			 "%17s: %2d\n", "PLL RX Hang",
			 sc->debug.stats.reset[RESET_TYPE_PLL_HANG]);
	len += scnprintf(buf + len, sizeof(buf) - len,
			 "%17s: %2d\n", "MCI Reset",
			 sc->debug.stats.reset[RESET_TYPE_MCI]);

	if (len > sizeof(buf))
		len = sizeof(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

void ath_debug_stat_tx(struct ath_softc *sc, struct ath_buf *bf,
		       struct ath_tx_status *ts, struct ath_txq *txq,
		       unsigned int flags)
{
	int qnum = txq->axq_qnum;

	TX_STAT_INC(qnum, tx_pkts_all);
	sc->debug.stats.txstats[qnum].tx_bytes_all += bf->bf_mpdu->len;

	if (bf_isampdu(bf)) {
		if (flags & ATH_TX_ERROR)
			TX_STAT_INC(qnum, a_xretries);
		else
			TX_STAT_INC(qnum, a_completed);
	} else {
		if (ts->ts_status & ATH9K_TXERR_XRETRY)
			TX_STAT_INC(qnum, xretries);
		else
			TX_STAT_INC(qnum, completed);
	}

	if (ts->ts_status & ATH9K_TXERR_FILT)
		TX_STAT_INC(qnum, txerr_filtered);
	if (ts->ts_status & ATH9K_TXERR_FIFO)
		TX_STAT_INC(qnum, fifo_underrun);
	if (ts->ts_status & ATH9K_TXERR_XTXOP)
		TX_STAT_INC(qnum, xtxop);
	if (ts->ts_status & ATH9K_TXERR_TIMER_EXPIRED)
		TX_STAT_INC(qnum, timer_exp);
	if (ts->ts_flags & ATH9K_TX_DESC_CFG_ERR)
		TX_STAT_INC(qnum, desc_cfg_err);
	if (ts->ts_flags & ATH9K_TX_DATA_UNDERRUN)
		TX_STAT_INC(qnum, data_underrun);
	if (ts->ts_flags & ATH9K_TX_DELIM_UNDERRUN)
		TX_STAT_INC(qnum, delim_underrun);
}

static const struct file_operations fops_xmit = {
	.read = read_file_xmit,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static const struct file_operations fops_queues = {
	.read = read_file_queues,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static const struct file_operations fops_misc = {
	.read = read_file_misc,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static const struct file_operations fops_reset = {
	.read = read_file_reset,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_recv(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
#define PHY_ERR(s, p) \
	len += scnprintf(buf + len, size - len, "%22s : %10u\n", s, \
			 sc->debug.stats.rxstats.phy_err_stats[p]);

#define RXS_ERR(s, e)					    \
	do {						    \
		len += scnprintf(buf + len, size - len,	    \
				 "%22s : %10u\n", s,	    \
				 sc->debug.stats.rxstats.e);\
	} while (0)

	struct ath_softc *sc = file->private_data;
	char *buf;
	unsigned int len = 0, size = 1600;
	ssize_t retval = 0;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	RXS_ERR("CRC ERR", crc_err);
	RXS_ERR("DECRYPT CRC ERR", decrypt_crc_err);
	RXS_ERR("PHY ERR", phy_err);
	RXS_ERR("MIC ERR", mic_err);
	RXS_ERR("PRE-DELIM CRC ERR", pre_delim_crc_err);
	RXS_ERR("POST-DELIM CRC ERR", post_delim_crc_err);
	RXS_ERR("DECRYPT BUSY ERR", decrypt_busy_err);
	RXS_ERR("RX-LENGTH-ERR", rx_len_err);
	RXS_ERR("RX-OOM-ERR", rx_oom_err);
	RXS_ERR("RX-RATE-ERR", rx_rate_err);
	RXS_ERR("RX-TOO-MANY-FRAGS", rx_too_many_frags_err);

	PHY_ERR("UNDERRUN ERR", ATH9K_PHYERR_UNDERRUN);
	PHY_ERR("TIMING ERR", ATH9K_PHYERR_TIMING);
	PHY_ERR("PARITY ERR", ATH9K_PHYERR_PARITY);
	PHY_ERR("RATE ERR", ATH9K_PHYERR_RATE);
	PHY_ERR("LENGTH ERR", ATH9K_PHYERR_LENGTH);
	PHY_ERR("RADAR ERR", ATH9K_PHYERR_RADAR);
	PHY_ERR("SERVICE ERR", ATH9K_PHYERR_SERVICE);
	PHY_ERR("TOR ERR", ATH9K_PHYERR_TOR);
	PHY_ERR("OFDM-TIMING ERR", ATH9K_PHYERR_OFDM_TIMING);
	PHY_ERR("OFDM-SIGNAL-PARITY ERR", ATH9K_PHYERR_OFDM_SIGNAL_PARITY);
	PHY_ERR("OFDM-RATE ERR", ATH9K_PHYERR_OFDM_RATE_ILLEGAL);
	PHY_ERR("OFDM-LENGTH ERR", ATH9K_PHYERR_OFDM_LENGTH_ILLEGAL);
	PHY_ERR("OFDM-POWER-DROP ERR", ATH9K_PHYERR_OFDM_POWER_DROP);
	PHY_ERR("OFDM-SERVICE ERR", ATH9K_PHYERR_OFDM_SERVICE);
	PHY_ERR("OFDM-RESTART ERR", ATH9K_PHYERR_OFDM_RESTART);
	PHY_ERR("FALSE-RADAR-EXT ERR", ATH9K_PHYERR_FALSE_RADAR_EXT);
	PHY_ERR("CCK-TIMING ERR", ATH9K_PHYERR_CCK_TIMING);
	PHY_ERR("CCK-HEADER-CRC ERR", ATH9K_PHYERR_CCK_HEADER_CRC);
	PHY_ERR("CCK-RATE ERR", ATH9K_PHYERR_CCK_RATE_ILLEGAL);
	PHY_ERR("CCK-SERVICE ERR", ATH9K_PHYERR_CCK_SERVICE);
	PHY_ERR("CCK-RESTART ERR", ATH9K_PHYERR_CCK_RESTART);
	PHY_ERR("CCK-LENGTH ERR", ATH9K_PHYERR_CCK_LENGTH_ILLEGAL);
	PHY_ERR("CCK-POWER-DROP ERR", ATH9K_PHYERR_CCK_POWER_DROP);
	PHY_ERR("HT-CRC ERR", ATH9K_PHYERR_HT_CRC_ERROR);
	PHY_ERR("HT-LENGTH ERR", ATH9K_PHYERR_HT_LENGTH_ILLEGAL);
	PHY_ERR("HT-RATE ERR", ATH9K_PHYERR_HT_RATE_ILLEGAL);

	RXS_ERR("RX-Pkts-All", rx_pkts_all);
	RXS_ERR("RX-Bytes-All", rx_bytes_all);
	RXS_ERR("RX-Beacons", rx_beacons);
	RXS_ERR("RX-Frags", rx_frags);
	RXS_ERR("RX-Spectral", rx_spectral);

	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;

#undef RXS_ERR
#undef PHY_ERR
}

void ath_debug_stat_rx(struct ath_softc *sc, struct ath_rx_status *rs)
{
#define RX_PHY_ERR_INC(c) sc->debug.stats.rxstats.phy_err_stats[c]++

	RX_STAT_INC(rx_pkts_all);
	sc->debug.stats.rxstats.rx_bytes_all += rs->rs_datalen;

	if (rs->rs_status & ATH9K_RXERR_CRC)
		RX_STAT_INC(crc_err);
	if (rs->rs_status & ATH9K_RXERR_DECRYPT)
		RX_STAT_INC(decrypt_crc_err);
	if (rs->rs_status & ATH9K_RXERR_MIC)
		RX_STAT_INC(mic_err);
	if (rs->rs_status & ATH9K_RX_DELIM_CRC_PRE)
		RX_STAT_INC(pre_delim_crc_err);
	if (rs->rs_status & ATH9K_RX_DELIM_CRC_POST)
		RX_STAT_INC(post_delim_crc_err);
	if (rs->rs_status & ATH9K_RX_DECRYPT_BUSY)
		RX_STAT_INC(decrypt_busy_err);

	if (rs->rs_status & ATH9K_RXERR_PHY) {
		RX_STAT_INC(phy_err);
		if (rs->rs_phyerr < ATH9K_PHYERR_MAX)
			RX_PHY_ERR_INC(rs->rs_phyerr);
	}

#undef RX_PHY_ERR_INC
}

static const struct file_operations fops_recv = {
	.read = read_file_recv,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_spec_scan_ctl(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char *mode = "";
	unsigned int len;

	switch (sc->spectral_mode) {
	case SPECTRAL_DISABLED:
		mode = "disable";
		break;
	case SPECTRAL_BACKGROUND:
		mode = "background";
		break;
	case SPECTRAL_CHANSCAN:
		mode = "chanscan";
		break;
	case SPECTRAL_MANUAL:
		mode = "manual";
		break;
	}
	len = strlen(mode);
	return simple_read_from_buffer(user_buf, count, ppos, mode, len);
}

static ssize_t write_file_spec_scan_ctl(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	char buf[32];
	ssize_t len;

	if (config_enabled(CONFIG_ATH9K_TX99))
		return -EOPNOTSUPP;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if (strncmp("trigger", buf, 7) == 0) {
		ath9k_spectral_scan_trigger(sc->hw);
	} else if (strncmp("background", buf, 9) == 0) {
		ath9k_spectral_scan_config(sc->hw, SPECTRAL_BACKGROUND);
		ath_dbg(common, CONFIG, "spectral scan: background mode enabled\n");
	} else if (strncmp("chanscan", buf, 8) == 0) {
		ath9k_spectral_scan_config(sc->hw, SPECTRAL_CHANSCAN);
		ath_dbg(common, CONFIG, "spectral scan: channel scan mode enabled\n");
	} else if (strncmp("manual", buf, 6) == 0) {
		ath9k_spectral_scan_config(sc->hw, SPECTRAL_MANUAL);
		ath_dbg(common, CONFIG, "spectral scan: manual mode enabled\n");
	} else if (strncmp("disable", buf, 7) == 0) {
		ath9k_spectral_scan_config(sc->hw, SPECTRAL_DISABLED);
		ath_dbg(common, CONFIG, "spectral scan: disabled\n");
	} else {
		return -EINVAL;
	}

	return count;
}

static const struct file_operations fops_spec_scan_ctl = {
	.read = read_file_spec_scan_ctl,
	.write = write_file_spec_scan_ctl,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_spectral_short_repeat(struct file *file,
					       char __user *user_buf,
					       size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d\n", sc->spec_config.short_repeat);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_spectral_short_repeat(struct file *file,
						const char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	unsigned long val;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val < 0 || val > 1)
		return -EINVAL;

	sc->spec_config.short_repeat = val;
	return count;
}

static const struct file_operations fops_spectral_short_repeat = {
	.read = read_file_spectral_short_repeat,
	.write = write_file_spectral_short_repeat,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_spectral_count(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d\n", sc->spec_config.count);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_spectral_count(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	unsigned long val;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val < 0 || val > 255)
		return -EINVAL;

	sc->spec_config.count = val;
	return count;
}

static const struct file_operations fops_spectral_count = {
	.read = read_file_spectral_count,
	.write = write_file_spectral_count,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_spectral_period(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d\n", sc->spec_config.period);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_spectral_period(struct file *file,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	unsigned long val;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val < 0 || val > 255)
		return -EINVAL;

	sc->spec_config.period = val;
	return count;
}

static const struct file_operations fops_spectral_period = {
	.read = read_file_spectral_period,
	.write = write_file_spectral_period,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_spectral_fft_period(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d\n", sc->spec_config.fft_period);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_spectral_fft_period(struct file *file,
					      const char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	unsigned long val;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val < 0 || val > 15)
		return -EINVAL;

	sc->spec_config.fft_period = val;
	return count;
}

static const struct file_operations fops_spectral_fft_period = {
	.read = read_file_spectral_fft_period,
	.write = write_file_spectral_fft_period,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static struct dentry *create_buf_file_handler(const char *filename,
					      struct dentry *parent,
					      umode_t mode,
					      struct rchan_buf *buf,
					      int *is_global)
{
	struct dentry *buf_file;

	buf_file = debugfs_create_file(filename, mode, parent, buf,
				       &relay_file_operations);
	*is_global = 1;
	return buf_file;
}

static int remove_buf_file_handler(struct dentry *dentry)
{
	debugfs_remove(dentry);

	return 0;
}

void ath_debug_send_fft_sample(struct ath_softc *sc,
			       struct fft_sample_tlv *fft_sample_tlv)
{
	int length;
	if (!sc->rfs_chan_spec_scan)
		return;

	length = __be16_to_cpu(fft_sample_tlv->length) +
		 sizeof(*fft_sample_tlv);
	relay_write(sc->rfs_chan_spec_scan, fft_sample_tlv, length);
}

static struct rchan_callbacks rfs_spec_scan_cb = {
	.create_buf_file = create_buf_file_handler,
	.remove_buf_file = remove_buf_file_handler,
};


static ssize_t read_file_regidx(struct file *file, char __user *user_buf,
                                size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "0x%08x\n", sc->debug.regidx);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_regidx(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	unsigned long regidx;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoul(buf, 0, &regidx))
		return -EINVAL;

	sc->debug.regidx = regidx;
	return count;
}

static const struct file_operations fops_regidx = {
	.read = read_file_regidx,
	.write = write_file_regidx,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_regval(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_hw *ah = sc->sc_ah;
	char buf[32];
	unsigned int len;
	u32 regval;

	ath9k_ps_wakeup(sc);
	regval = REG_READ_D(ah, sc->debug.regidx);
	ath9k_ps_restore(sc);
	len = sprintf(buf, "0x%08x\n", regval);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_regval(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_hw *ah = sc->sc_ah;
	unsigned long regval;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoul(buf, 0, &regval))
		return -EINVAL;

	ath9k_ps_wakeup(sc);
	REG_WRITE_D(ah, sc->debug.regidx, regval);
	ath9k_ps_restore(sc);
	return count;
}

static const struct file_operations fops_regval = {
	.read = read_file_regval,
	.write = write_file_regval,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#define REGDUMP_LINE_SIZE	20

static int open_file_regdump(struct inode *inode, struct file *file)
{
	struct ath_softc *sc = inode->i_private;
	unsigned int len = 0;
	u8 *buf;
	int i;
	unsigned long num_regs, regdump_len, max_reg_offset;

	max_reg_offset = AR_SREV_9300_20_OR_LATER(sc->sc_ah) ? 0x16bd4 : 0xb500;
	num_regs = max_reg_offset / 4 + 1;
	regdump_len = num_regs * REGDUMP_LINE_SIZE + 1;
	buf = vmalloc(regdump_len);
	if (!buf)
		return -ENOMEM;

	ath9k_ps_wakeup(sc);
	for (i = 0; i < num_regs; i++)
		len += scnprintf(buf + len, regdump_len - len,
			"0x%06x 0x%08x\n", i << 2, REG_READ(sc->sc_ah, i << 2));
	ath9k_ps_restore(sc);

	file->private_data = buf;

	return 0;
}

static const struct file_operations fops_regdump = {
	.open = open_file_regdump,
	.read = ath9k_debugfs_read_buf,
	.release = ath9k_debugfs_release_buf,
	.owner = THIS_MODULE,
	.llseek = default_llseek,/* read accesses f_pos */
};

static ssize_t read_file_dump_nfcal(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_hw *ah = sc->sc_ah;
	struct ath9k_nfcal_hist *h = sc->caldata.nfCalHist;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_conf *conf = &common->hw->conf;
	u32 len = 0, size = 1500;
	u32 i, j;
	ssize_t retval = 0;
	char *buf;
	u8 chainmask = (ah->rxchainmask << 3) | ah->rxchainmask;
	u8 nread;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, size - len,
			 "Channel Noise Floor : %d\n", ah->noise);
	len += scnprintf(buf + len, size - len,
			 "Chain | privNF | # Readings | NF Readings\n");
	for (i = 0; i < NUM_NF_READINGS; i++) {
		if (!(chainmask & (1 << i)) ||
		    ((i >= AR5416_MAX_CHAINS) && !conf_is_ht40(conf)))
			continue;

		nread = AR_PHY_CCA_FILTERWINDOW_LENGTH - h[i].invalidNFcount;
		len += scnprintf(buf + len, size - len, " %d\t %d\t %d\t\t",
				 i, h[i].privNF, nread);
		for (j = 0; j < nread; j++)
			len += scnprintf(buf + len, size - len,
					 " %d", h[i].nfCalBuffer[j]);
		len += scnprintf(buf + len, size - len, "\n");
	}

	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_dump_nfcal = {
	.read = read_file_dump_nfcal,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_base_eeprom(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_hw *ah = sc->sc_ah;
	u32 len = 0, size = 1500;
	ssize_t retval = 0;
	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len = ah->eep_ops->dump_eeprom(ah, true, buf, len, size);

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_base_eeprom = {
	.read = read_file_base_eeprom,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_modal_eeprom(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_hw *ah = sc->sc_ah;
	u32 len = 0, size = 6000;
	char *buf;
	size_t retval;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len = ah->eep_ops->dump_eeprom(ah, false, buf, len, size);

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_modal_eeprom = {
	.read = read_file_modal_eeprom,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT
static ssize_t read_file_btcoex(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	u32 len = 0, size = 1500;
	char *buf;
	size_t retval;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (!sc->sc_ah->common.btcoex_enabled) {
		len = scnprintf(buf, size, "%s\n",
				"BTCOEX is disabled");
		goto exit;
	}

	len = ath9k_dump_btcoex(sc, buf, size);
exit:
	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_btcoex = {
	.read = read_file_btcoex,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};
#endif

static ssize_t read_file_node_stat(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath_node *an = file->private_data;
	struct ath_softc *sc = an->sc;
	struct ath_atx_tid *tid;
	struct ath_atx_ac *ac;
	struct ath_txq *txq;
	u32 len = 0, size = 4096;
	char *buf;
	size_t retval;
	int tidno, acno;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (!an->sta->ht_cap.ht_supported) {
		len = scnprintf(buf, size, "%s\n",
				"HT not supported");
		goto exit;
	}

	len = scnprintf(buf, size, "Max-AMPDU: %d\n",
			an->maxampdu);
	len += scnprintf(buf + len, size - len, "MPDU Density: %d\n\n",
			 an->mpdudensity);

	len += scnprintf(buf + len, size - len,
			 "%2s%7s\n", "AC", "SCHED");

	for (acno = 0, ac = &an->ac[acno];
	     acno < IEEE80211_NUM_ACS; acno++, ac++) {
		txq = ac->txq;
		ath_txq_lock(sc, txq);
		len += scnprintf(buf + len, size - len,
				 "%2d%7d\n",
				 acno, ac->sched);
		ath_txq_unlock(sc, txq);
	}

	len += scnprintf(buf + len, size - len,
			 "\n%3s%11s%10s%10s%10s%10s%9s%6s%8s\n",
			 "TID", "SEQ_START", "SEQ_NEXT", "BAW_SIZE",
			 "BAW_HEAD", "BAW_TAIL", "BAR_IDX", "SCHED", "PAUSED");

	for (tidno = 0, tid = &an->tid[tidno];
	     tidno < IEEE80211_NUM_TIDS; tidno++, tid++) {
		txq = tid->ac->txq;
		ath_txq_lock(sc, txq);
		len += scnprintf(buf + len, size - len,
				 "%3d%11d%10d%10d%10d%10d%9d%6d%8d\n",
				 tid->tidno, tid->seq_start, tid->seq_next,
				 tid->baw_size, tid->baw_head, tid->baw_tail,
				 tid->bar_index, tid->sched, tid->paused);
		ath_txq_unlock(sc, txq);
	}
exit:
	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_node_stat = {
	.read = read_file_node_stat,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath9k_sta_add_debugfs(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta,
			   struct dentry *dir)
{
	struct ath_node *an = (struct ath_node *)sta->drv_priv;
	debugfs_create_file("node_stat", S_IRUGO, dir, an, &fops_node_stat);
}

/* Ethtool support for get-stats */

#define AMKSTR(nm) #nm "_BE", #nm "_BK", #nm "_VI", #nm "_VO"
static const char ath9k_gstrings_stats[][ETH_GSTRING_LEN] = {
	"tx_pkts_nic",
	"tx_bytes_nic",
	"rx_pkts_nic",
	"rx_bytes_nic",
	AMKSTR(d_tx_pkts),
	AMKSTR(d_tx_bytes),
	AMKSTR(d_tx_mpdus_queued),
	AMKSTR(d_tx_mpdus_completed),
	AMKSTR(d_tx_mpdu_xretries),
	AMKSTR(d_tx_aggregates),
	AMKSTR(d_tx_ampdus_queued_hw),
	AMKSTR(d_tx_ampdus_queued_sw),
	AMKSTR(d_tx_ampdus_completed),
	AMKSTR(d_tx_ampdu_retries),
	AMKSTR(d_tx_ampdu_xretries),
	AMKSTR(d_tx_fifo_underrun),
	AMKSTR(d_tx_op_exceeded),
	AMKSTR(d_tx_timer_expiry),
	AMKSTR(d_tx_desc_cfg_err),
	AMKSTR(d_tx_data_underrun),
	AMKSTR(d_tx_delim_underrun),
	"d_rx_crc_err",
	"d_rx_decrypt_crc_err",
	"d_rx_phy_err",
	"d_rx_mic_err",
	"d_rx_pre_delim_crc_err",
	"d_rx_post_delim_crc_err",
	"d_rx_decrypt_busy_err",

	"d_rx_phyerr_radar",
	"d_rx_phyerr_ofdm_timing",
	"d_rx_phyerr_cck_timing",

};
#define ATH9K_SSTATS_LEN ARRAY_SIZE(ath9k_gstrings_stats)

void ath9k_get_et_strings(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif,
			  u32 sset, u8 *data)
{
	if (sset == ETH_SS_STATS)
		memcpy(data, *ath9k_gstrings_stats,
		       sizeof(ath9k_gstrings_stats));
}

int ath9k_get_et_sset_count(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif, int sset)
{
	if (sset == ETH_SS_STATS)
		return ATH9K_SSTATS_LEN;
	return 0;
}

#define AWDATA(elem)							\
	do {								\
		data[i++] = sc->debug.stats.txstats[PR_QNUM(IEEE80211_AC_BE)].elem; \
		data[i++] = sc->debug.stats.txstats[PR_QNUM(IEEE80211_AC_BK)].elem; \
		data[i++] = sc->debug.stats.txstats[PR_QNUM(IEEE80211_AC_VI)].elem; \
		data[i++] = sc->debug.stats.txstats[PR_QNUM(IEEE80211_AC_VO)].elem; \
	} while (0)

#define AWDATA_RX(elem)						\
	do {							\
		data[i++] = sc->debug.stats.rxstats.elem;	\
	} while (0)

void ath9k_get_et_stats(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			struct ethtool_stats *stats, u64 *data)
{
	struct ath_softc *sc = hw->priv;
	int i = 0;

	data[i++] = (sc->debug.stats.txstats[PR_QNUM(IEEE80211_AC_BE)].tx_pkts_all +
		     sc->debug.stats.txstats[PR_QNUM(IEEE80211_AC_BK)].tx_pkts_all +
		     sc->debug.stats.txstats[PR_QNUM(IEEE80211_AC_VI)].tx_pkts_all +
		     sc->debug.stats.txstats[PR_QNUM(IEEE80211_AC_VO)].tx_pkts_all);
	data[i++] = (sc->debug.stats.txstats[PR_QNUM(IEEE80211_AC_BE)].tx_bytes_all +
		     sc->debug.stats.txstats[PR_QNUM(IEEE80211_AC_BK)].tx_bytes_all +
		     sc->debug.stats.txstats[PR_QNUM(IEEE80211_AC_VI)].tx_bytes_all +
		     sc->debug.stats.txstats[PR_QNUM(IEEE80211_AC_VO)].tx_bytes_all);
	AWDATA_RX(rx_pkts_all);
	AWDATA_RX(rx_bytes_all);

	AWDATA(tx_pkts_all);
	AWDATA(tx_bytes_all);
	AWDATA(queued);
	AWDATA(completed);
	AWDATA(xretries);
	AWDATA(a_aggr);
	AWDATA(a_queued_hw);
	AWDATA(a_queued_sw);
	AWDATA(a_completed);
	AWDATA(a_retries);
	AWDATA(a_xretries);
	AWDATA(fifo_underrun);
	AWDATA(xtxop);
	AWDATA(timer_exp);
	AWDATA(desc_cfg_err);
	AWDATA(data_underrun);
	AWDATA(delim_underrun);

	AWDATA_RX(crc_err);
	AWDATA_RX(decrypt_crc_err);
	AWDATA_RX(phy_err);
	AWDATA_RX(mic_err);
	AWDATA_RX(pre_delim_crc_err);
	AWDATA_RX(post_delim_crc_err);
	AWDATA_RX(decrypt_busy_err);

	AWDATA_RX(phy_err_stats[ATH9K_PHYERR_RADAR]);
	AWDATA_RX(phy_err_stats[ATH9K_PHYERR_OFDM_TIMING]);
	AWDATA_RX(phy_err_stats[ATH9K_PHYERR_CCK_TIMING]);

	WARN_ON(i != ATH9K_SSTATS_LEN);
}

void ath9k_deinit_debug(struct ath_softc *sc)
{
	if (config_enabled(CONFIG_ATH9K_DEBUGFS) && sc->rfs_chan_spec_scan) {
		relay_close(sc->rfs_chan_spec_scan);
		sc->rfs_chan_spec_scan = NULL;
	}
}

static ssize_t read_file_tx99(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[3];
	unsigned int len;

	len = sprintf(buf, "%d\n", sc->tx99_state);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_tx99(struct file *file, const char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	char buf[32];
	bool start;
	ssize_t len;
	int r;

	if (sc->nvifs > 1)
		return -EOPNOTSUPP;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	if (strtobool(buf, &start))
		return -EINVAL;

	if (start == sc->tx99_state) {
		if (!start)
			return count;
		ath_dbg(common, XMIT, "Resetting TX99\n");
		ath9k_tx99_deinit(sc);
	}

	if (!start) {
		ath9k_tx99_deinit(sc);
		return count;
	}

	r = ath9k_tx99_init(sc);
	if (r)
		return r;

	return count;
}

static const struct file_operations fops_tx99 = {
	.read = read_file_tx99,
	.write = write_file_tx99,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_tx99_power(struct file *file,
				    char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d (%d dBm)\n",
		      sc->tx99_power,
		      sc->tx99_power / 2);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_tx99_power(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	int r;
	u8 tx_power;

	r = kstrtou8_from_user(user_buf, count, 0, &tx_power);
	if (r)
		return r;

	if (tx_power > MAX_RATE_POWER)
		return -EINVAL;

	sc->tx99_power = tx_power;

	ath9k_ps_wakeup(sc);
	ath9k_hw_tx99_set_txpower(sc->sc_ah, sc->tx99_power);
	ath9k_ps_restore(sc);

	return count;
}

static const struct file_operations fops_tx99_power = {
	.read = read_file_tx99_power,
	.write = write_file_tx99_power,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

int ath9k_init_debug(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_softc *sc = (struct ath_softc *) common->priv;

	sc->debug.debugfs_phy = debugfs_create_dir("ath9k",
						   sc->hw->wiphy->debugfsdir);
	if (!sc->debug.debugfs_phy)
		return -ENOMEM;

#ifdef CONFIG_ATH_DEBUG
	debugfs_create_file("debug", S_IRUSR | S_IWUSR, sc->debug.debugfs_phy,
			    sc, &fops_debug);
#endif

	ath9k_dfs_init_debug(sc);

	debugfs_create_file("dma", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_dma);
	debugfs_create_file("interrupt", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_interrupt);
	debugfs_create_file("xmit", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_xmit);
	debugfs_create_file("queues", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_queues);
	debugfs_create_u32("qlen_bk", S_IRUSR | S_IWUSR, sc->debug.debugfs_phy,
			   &sc->tx.txq_max_pending[IEEE80211_AC_BK]);
	debugfs_create_u32("qlen_be", S_IRUSR | S_IWUSR, sc->debug.debugfs_phy,
			   &sc->tx.txq_max_pending[IEEE80211_AC_BE]);
	debugfs_create_u32("qlen_vi", S_IRUSR | S_IWUSR, sc->debug.debugfs_phy,
			   &sc->tx.txq_max_pending[IEEE80211_AC_VI]);
	debugfs_create_u32("qlen_vo", S_IRUSR | S_IWUSR, sc->debug.debugfs_phy,
			   &sc->tx.txq_max_pending[IEEE80211_AC_VO]);
	debugfs_create_file("misc", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_misc);
	debugfs_create_file("reset", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_reset);
	debugfs_create_file("recv", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_recv);
	debugfs_create_u8("rx_chainmask", S_IRUSR, sc->debug.debugfs_phy,
			  &ah->rxchainmask);
	debugfs_create_u8("tx_chainmask", S_IRUSR, sc->debug.debugfs_phy,
			  &ah->txchainmask);
	debugfs_create_file("ani", S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc, &fops_ani);
	debugfs_create_bool("paprd", S_IRUSR | S_IWUSR, sc->debug.debugfs_phy,
			    &sc->sc_ah->config.enable_paprd);
	debugfs_create_file("regidx", S_IRUSR | S_IWUSR, sc->debug.debugfs_phy,
			    sc, &fops_regidx);
	debugfs_create_file("regval", S_IRUSR | S_IWUSR, sc->debug.debugfs_phy,
			    sc, &fops_regval);
	debugfs_create_bool("ignore_extcca", S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy,
			    &ah->config.cwm_ignore_extcca);
	debugfs_create_file("regdump", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_regdump);
	debugfs_create_file("dump_nfcal", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_dump_nfcal);
	debugfs_create_file("base_eeprom", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_base_eeprom);
	debugfs_create_file("modal_eeprom", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_modal_eeprom);
	sc->rfs_chan_spec_scan = relay_open("spectral_scan",
					    sc->debug.debugfs_phy,
					    1024, 256, &rfs_spec_scan_cb,
					    NULL);
	debugfs_create_file("spectral_scan_ctl", S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc,
			    &fops_spec_scan_ctl);
	debugfs_create_file("spectral_short_repeat", S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc,
			    &fops_spectral_short_repeat);
	debugfs_create_file("spectral_count", S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc, &fops_spectral_count);
	debugfs_create_file("spectral_period", S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc, &fops_spectral_period);
	debugfs_create_file("spectral_fft_period", S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc,
			    &fops_spectral_fft_period);
	debugfs_create_u32("gpio_mask", S_IRUSR | S_IWUSR,
			   sc->debug.debugfs_phy, &sc->sc_ah->gpio_mask);
	debugfs_create_u32("gpio_val", S_IRUSR | S_IWUSR,
			   sc->debug.debugfs_phy, &sc->sc_ah->gpio_val);
	debugfs_create_file("antenna_diversity", S_IRUSR,
			    sc->debug.debugfs_phy, sc, &fops_antenna_diversity);
#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT
	debugfs_create_file("bt_ant_diversity", S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc, &fops_bt_ant_diversity);
	debugfs_create_file("btcoex", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_btcoex);
#endif
	if (config_enabled(CONFIG_ATH9K_TX99) &&
	    AR_SREV_9300_20_OR_LATER(ah)) {
		debugfs_create_file("tx99", S_IRUSR | S_IWUSR,
				    sc->debug.debugfs_phy, sc,
				    &fops_tx99);
		debugfs_create_file("tx99_power", S_IRUSR | S_IWUSR,
				    sc->debug.debugfs_phy, sc,
				    &fops_tx99_power);
	}

	return 0;
}
