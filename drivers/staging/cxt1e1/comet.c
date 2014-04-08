/* Copyright (C) 2003-2005  SBE, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/hdlc.h>
#include "pmcc4_sysdep.h"
#include "sbecom_inline_linux.h"
#include "libsbew.h"
#include "pmcc4.h"
#include "comet.h"
#include "comet_tables.h"

extern int  cxt1e1_log_level;

#define COMET_NUM_SAMPLES   24  /* Number of entries in the waveform table */
#define COMET_NUM_UNITS     5   /* Number of points per entry in table */

/* forward references */
static void SetPwrLevel(comet_t *comet);
static void WrtRcvEqualizerTbl(ci_t *ci, comet_t *comet, u_int32_t *table);
static void WrtXmtWaveformTbl(ci_t *ci, comet_t *comet, u_int8_t table[COMET_NUM_SAMPLES][COMET_NUM_UNITS]);


void       *TWV_table[12] = {
	TWVLongHaul0DB, TWVLongHaul7_5DB, TWVLongHaul15DB, TWVLongHaul22_5DB,
	TWVShortHaul0, TWVShortHaul1, TWVShortHaul2, TWVShortHaul3,
	TWVShortHaul4, TWVShortHaul5,
	/** PORT POINT - 75 Ohm not supported **/
	TWV_E1_75Ohm,
	TWV_E1_120Ohm
};


static int
lbo_tbl_lkup(int t1, int lbo) {
	/* error switches to default */
	if ((lbo < CFG_LBO_LH0) || (lbo > CFG_LBO_E120)) {
		if (t1)
			/* default T1 waveform table */
			lbo = CFG_LBO_LH0;
		else
			/* default E1 waveform table */
			lbo = CFG_LBO_E120;
	}
	/* make index ZERO relative */
	return lbo - 1;
}

void init_comet(void *ci, comet_t *comet, u_int32_t port_mode, int clockmaster,
		u_int8_t moreParams)
{
	u_int8_t isT1mode;
	/* T1 default */
	u_int8_t    tix = CFG_LBO_LH0;
	isT1mode = IS_FRAME_ANY_T1(port_mode);
	/* T1 or E1 */
	if (isT1mode) {
		/* Select T1 Mode & PIO output enabled */
		pci_write_32((u_int32_t *) &comet->gbl_cfg, 0xa0);
		/* default T1 waveform table */
		tix = lbo_tbl_lkup(isT1mode, CFG_LBO_LH0);
	} else {
		/* Select E1 Mode & PIO output enabled */
		pci_write_32((u_int32_t *) &comet->gbl_cfg, 0x81);
		/* default E1 waveform table */
		tix = lbo_tbl_lkup(isT1mode, CFG_LBO_E120);
	}

	if (moreParams & CFG_LBO_MASK)
		/* dial-in requested waveform table */
		tix = lbo_tbl_lkup(isT1mode, moreParams & CFG_LBO_MASK);
	/* Tx line Intfc cfg Set for analog & no special patterns */
	/* Transmit Line Interface Config. */
	pci_write_32((u_int32_t *) &comet->tx_line_cfg, 0x00);
	/* master test Ignore Test settings for now */
	/* making sure it's Default value */
	pci_write_32((u_int32_t *) &comet->mtest, 0x00);
	/* Turn on Center (CENT) and everything else off */
	/* RJAT cfg */
	pci_write_32((u_int32_t *) &comet->rjat_cfg, 0x10);
	/* Set Jitter Attenuation to recommend T1 values */
	if (isT1mode) {
		/* RJAT Divider N1 Control */
		pci_write_32((u_int32_t *) &comet->rjat_n1clk, 0x2F);
		/* RJAT Divider N2 Control */
		pci_write_32((u_int32_t *) &comet->rjat_n2clk, 0x2F);
	} else {
		/* RJAT Divider N1 Control */
		pci_write_32((u_int32_t *) &comet->rjat_n1clk, 0xFF);
		/* RJAT Divider N2 Control */
		pci_write_32((u_int32_t *) &comet->rjat_n2clk, 0xFF);
	}

	/* Turn on Center (CENT) and everything else off */
	/* TJAT Config. */
	pci_write_32((u_int32_t *) &comet->tjat_cfg, 0x10);

	/* Do not bypass jitter attenuation and bypass elastic store */
	/* rx opts */
	pci_write_32((u_int32_t *) &comet->rx_opt, 0x00);

	/* TJAT ctrl & TJAT divider ctrl */
	/* Set Jitter Attenuation to recommended T1 values */
	if (isT1mode) {
		/* TJAT Divider N1 Control */
		pci_write_32((u_int32_t *) &comet->tjat_n1clk, 0x2F);
		/* TJAT Divider N2  Control */
		pci_write_32((u_int32_t *) &comet->tjat_n2clk, 0x2F);
	} else {
		/* TJAT Divider N1 Control */
		pci_write_32((u_int32_t *) &comet->tjat_n1clk, 0xFF);
		/* TJAT Divider N2 Control */
		pci_write_32((u_int32_t *) &comet->tjat_n2clk, 0xFF);
	}

	/* 1c: rx ELST cfg   20: tx ELST cfg  28&38: rx&tx data link ctrl */

	/* Select 193-bit frame format */
	if (isT1mode) {
		pci_write_32((u_int32_t *) &comet->rx_elst_cfg, 0x00);
		pci_write_32((u_int32_t *) &comet->tx_elst_cfg, 0x00);
	} else {
		/* Select 256-bit frame format */
		pci_write_32((u_int32_t *) &comet->rx_elst_cfg, 0x03);
		pci_write_32((u_int32_t *) &comet->tx_elst_cfg, 0x03);
		/* disable T1 data link receive */
		pci_write_32((u_int32_t *) &comet->rxce1_ctl, 0x00);
		/* disable T1 data link transmit */
		pci_write_32((u_int32_t *) &comet->txci1_ctl, 0x00);
	}

    /* the following is a default value */
    /* Enable 8 out of 10 validation */
	 /* t1RBOC enable(BOC:BitOriented Code) */
	pci_write_32((u_int32_t *) &comet->t1_rboc_ena, 0x00);
	if (isT1mode) {
		/* IBCD cfg: aka Inband Code Detection ** loopback code length set to */
		/* 6 bit down, 5 bit up (assert) */
		pci_write_32((u_int32_t *) &comet->ibcd_cfg, 0x04);
		/* line loopback activate pattern */
		pci_write_32((u_int32_t *) &comet->ibcd_act, 0x08);
		/* deactivate code pattern (i.e.001) */
		pci_write_32((u_int32_t *) &comet->ibcd_deact, 0x24);
	}
    /* 10: CDRC cfg 28&38: rx&tx data link 1 ctrl 48: t1 frmr cfg  */
    /* 50: SIGX cfg, COSS (change of signaling state) 54: XBAS cfg  */
    /* 60: t1 ALMI cfg */
    /* Configure Line Coding */

	switch (port_mode)
	{
	/* 1 - T1 B8ZS */
	case CFG_FRAME_SF:
		pci_write_32((u_int32_t *) &comet->cdrc_cfg, 0);
		pci_write_32((u_int32_t *) &comet->t1_frmr_cfg, 0);
		pci_write_32((u_int32_t *) &comet->sigx_cfg, 0);
		/* 5:B8ZS */
		pci_write_32((u_int32_t *) &comet->t1_xbas_cfg, 0x20);
		pci_write_32((u_int32_t *) &comet->t1_almi_cfg, 0);
		break;
	/* 2 - T1 B8ZS */
	case CFG_FRAME_ESF:
		pci_write_32((u_int32_t *) &comet->cdrc_cfg, 0);
		/* Bit 5: T1 DataLink Enable */
		pci_write_32((u_int32_t *) &comet->rxce1_ctl, 0x20);
		/* 5: T1 DataLink Enable */
		pci_write_32((u_int32_t *) &comet->txci1_ctl, 0x20);
		/* 4:ESF  5:ESFFA */
		pci_write_32((u_int32_t *) &comet->t1_frmr_cfg, 0x30);
		/* 2:ESF */
		pci_write_32((u_int32_t *) &comet->sigx_cfg, 0x04);
		/* 4:ESF  5:B8ZS */
		pci_write_32((u_int32_t *) &comet->t1_xbas_cfg, 0x30);
		/* 4:ESF */
		pci_write_32((u_int32_t *) &comet->t1_almi_cfg, 0x10);
		break;
	/* 3 - HDB3 */
	case CFG_FRAME_E1PLAIN:
		pci_write_32((u_int32_t *) &comet->cdrc_cfg, 0);
		pci_write_32((u_int32_t *) &comet->sigx_cfg, 0);
		pci_write_32((u_int32_t *) &comet->e1_tran_cfg, 0);
		pci_write_32((u_int32_t *) &comet->e1_frmr_aopts, 0x40);
		break;
	/* 4 - HDB3 */
	case CFG_FRAME_E1CAS:
		pci_write_32((u_int32_t *) &comet->cdrc_cfg, 0);
		pci_write_32((u_int32_t *) &comet->sigx_cfg, 0);
		pci_write_32((u_int32_t *) &comet->e1_tran_cfg, 0x60);
		pci_write_32((u_int32_t *) &comet->e1_frmr_aopts, 0);
		break;
	/* 5 - HDB3 */
	case CFG_FRAME_E1CRC:
		pci_write_32((u_int32_t *) &comet->cdrc_cfg, 0);
		pci_write_32((u_int32_t *) &comet->sigx_cfg, 0);
		pci_write_32((u_int32_t *) &comet->e1_tran_cfg, 0x10);
		pci_write_32((u_int32_t *) &comet->e1_frmr_aopts, 0xc2);
		break;
	/* 6 - HDB3 */
	case CFG_FRAME_E1CRC_CAS:
		pci_write_32((u_int32_t *) &comet->cdrc_cfg, 0);
		pci_write_32((u_int32_t *) &comet->sigx_cfg, 0);
		pci_write_32((u_int32_t *) &comet->e1_tran_cfg, 0x70);
		pci_write_32((u_int32_t *) &comet->e1_frmr_aopts, 0x82);
		break;
	/* 7 - T1 AMI */
	case CFG_FRAME_SF_AMI:
		/* Enable AMI Line Decoding */
		pci_write_32((u_int32_t *) &comet->cdrc_cfg, 0x80);
		pci_write_32((u_int32_t *) &comet->t1_frmr_cfg, 0);
		pci_write_32((u_int32_t *) &comet->t1_xbas_cfg, 0);
		pci_write_32((u_int32_t *) &comet->t1_almi_cfg, 0);
		pci_write_32((u_int32_t *) &comet->sigx_cfg, 0);
		break;
	/* 8 - T1 AMI */
	case CFG_FRAME_ESF_AMI:
		/* Enable AMI Line Decoding */
		pci_write_32((u_int32_t *) &comet->cdrc_cfg, 0x80);
		/* 5: T1 DataLink Enable */
		pci_write_32((u_int32_t *) &comet->rxce1_ctl, 0x20);
		/* 5: T1 DataLink Enable */
		pci_write_32((u_int32_t *) &comet->txci1_ctl, 0x20);
		/* Bit 4:ESF  5:ESFFA */
		pci_write_32((u_int32_t *) &comet->t1_frmr_cfg, 0x30);
		/* 2:ESF */
		pci_write_32((u_int32_t *) &comet->sigx_cfg, 0x04);
		/* 4:ESF */
		pci_write_32((u_int32_t *) &comet->t1_xbas_cfg, 0x10);
		/* 4:ESF */
		pci_write_32((u_int32_t *) &comet->t1_almi_cfg, 0x10);
		break;
	/* 9 - AMI */
	case CFG_FRAME_E1PLAIN_AMI:
		/* Enable AMI Line Decoding */
		pci_write_32((u_int32_t *) &comet->cdrc_cfg, 0x80);
		pci_write_32((u_int32_t *) &comet->sigx_cfg, 0);
		pci_write_32((u_int32_t *) &comet->e1_tran_cfg, 0x80);
		pci_write_32((u_int32_t *) &comet->e1_frmr_aopts, 0x40);
		break;
	/* 10 - AMI */
	case CFG_FRAME_E1CAS_AMI:
		/* Enable AMI Line Decoding */
		pci_write_32((u_int32_t *) &comet->cdrc_cfg, 0x80);
		pci_write_32((u_int32_t *) &comet->sigx_cfg, 0);
		pci_write_32((u_int32_t *) &comet->e1_tran_cfg, 0xe0);
		pci_write_32((u_int32_t *) &comet->e1_frmr_aopts, 0);
		break;
	/* 11 - AMI */
	case CFG_FRAME_E1CRC_AMI:
		/* Enable AMI Line Decoding */
		pci_write_32((u_int32_t *) &comet->cdrc_cfg, 0x80);
		pci_write_32((u_int32_t *) &comet->sigx_cfg, 0);
		pci_write_32((u_int32_t *) &comet->e1_tran_cfg, 0x90);
		pci_write_32((u_int32_t *) &comet->e1_frmr_aopts, 0xc2);
		break;
	/* 12 - AMI */
	case CFG_FRAME_E1CRC_CAS_AMI:
		/* Enable AMI Line Decoding */
		pci_write_32((u_int32_t *) &comet->cdrc_cfg, 0x80);
		pci_write_32((u_int32_t *) &comet->sigx_cfg, 0);
		pci_write_32((u_int32_t *) &comet->e1_tran_cfg, 0xf0);
		pci_write_32((u_int32_t *) &comet->e1_frmr_aopts, 0x82);
		break;
	}	/* end switch */

    /***
     * Set Full Frame mode (NXDSO[1] = 0, NXDSO[0] = 0)
     * CMODE=1: Clock slave mode with BRCLK as an input,
     * DE=0: Use falling edge of BRCLK for data,
     * FE=0: Use falling edge of BRCLK for frame,
     * CMS=0: Use backplane freq,
     * RATE[1:0]=0,0: T1
     ***/


    /* 0x30: "BRIF cfg"; 0x20 is 'CMODE', 0x03 is (bit) rate */
    /* note "rate bits can only be set once after reset" */
	if (clockmaster)
		{
		/* CMODE == clockMode, 0=clock master (so all 3 others should be slave) */
		/* rate = 1.544 Mb/s */
		if (isT1mode)
			/* Comet 0 Master Mode(CMODE=0) */
			pci_write_32((u_int32_t *) &comet->brif_cfg, 0x00);
		/* rate = 2.048 Mb/s */
		else
			/* Comet 0 Master Mode(CMODE=0) */
			pci_write_32((u_int32_t *) &comet->brif_cfg, 0x01);

		/* 31: BRIF frame pulse cfg  06: tx timing options */

		/* Master Mode i.e.FPMODE=0 (@0x20) */
		pci_write_32((u_int32_t *) &comet->brif_fpcfg, 0x00);
		if ((moreParams & CFG_CLK_PORT_MASK) == CFG_CLK_PORT_INTERNAL)
			{
			if (cxt1e1_log_level >= LOG_SBEBUG12)
				pr_info(">> %s: clockmaster internal clock\n", __func__);
			/* internal oscillator */
			pci_write_32((u_int32_t *) &comet->tx_time, 0x0d);
		} else {
			/* external clock source */
			if (cxt1e1_log_level >= LOG_SBEBUG12)
				pr_info(">> %s: clockmaster external clock\n", __func__);
			/* loop timing(external) */
			pci_write_32((u_int32_t *) &comet->tx_time, 0x09);
		}

	} else  {
		/* slave */
		if (isT1mode)
			/* Slave Mode(CMODE=1, see above) */
			pci_write_32((u_int32_t *) &comet->brif_cfg, 0x20);
		else
			/* Slave Mode(CMODE=1)*/
			pci_write_32((u_int32_t *) &comet->brif_cfg, 0x21);
		/* Slave Mode i.e. FPMODE=1 (@0x20) */
		pci_write_32((u_int32_t *) &comet->brif_fpcfg, 0x20);
	if (cxt1e1_log_level >= LOG_SBEBUG12)
		pr_info(">> %s: clockslave internal clock\n", __func__);
	/* oscillator timing */
	pci_write_32((u_int32_t *) &comet->tx_time, 0x0d);
	}

	/* 32: BRIF parity F-bit cfg */
	/* Totem-pole operation */
	/* Receive Backplane Parity/F-bit */
	pci_write_32((u_int32_t *) &comet->brif_pfcfg, 0x01);

    /* dc: RLPS equalizer V ref */
    /* Configuration */
	if (isT1mode)
		/* RLPS Equalizer Voltage  */
		pci_write_32((u_int32_t *) &comet->rlps_eqvr, 0x2c);
	else
		/* RLPS Equalizer Voltage  */
		pci_write_32((u_int32_t *) &comet->rlps_eqvr, 0x34);

    /* Reserved bit set and SQUELCH enabled */
    /* f8: RLPS cfg & status  f9: RLPS ALOS detect/clear threshold */
	/* RLPS Configuration Status */
	pci_write_32((u_int32_t *) &comet->rlps_cfgsts, 0x11);
	if (isT1mode)
		/* ? */
		pci_write_32((u_int32_t *) &comet->rlps_alos_thresh, 0x55);
	else
		/* ? */
		pci_write_32((u_int32_t *) &comet->rlps_alos_thresh, 0x22);


    /* Set Full Frame mode (NXDSO[1] = 0, NXDSO[0] = 0) */
    /* CMODE=0: Clock slave mode with BTCLK as an input, DE=1: Use rising */
    /* edge of BTCLK for data, FE=1: Use rising edge of BTCLK for frame, */
    /* CMS=0: Use backplane freq, RATE[1:0]=0,0: T1 */
    /***    Transmit side is always an Input, Slave Clock*/
    /* 40: BTIF cfg  41: loop timing(external) */
	/*BTIF frame pulse cfg */
	if (isT1mode)
		/* BTIF Configuration  Reg. */
		pci_write_32((u_int32_t *) &comet->btif_cfg, 0x38);
	else
		/* BTIF Configuration  Reg. */
		pci_write_32((u_int32_t *) &comet->btif_cfg, 0x39);
	/* BTIF Frame Pulse Config. */
	pci_write_32((u_int32_t *) &comet->btif_fpcfg, 0x01);

    /* 0a: master diag  06: tx timing options */
    /* if set Comet to loop back */

    /* Comets set to normal */
	pci_write_32((u_int32_t *) &comet->mdiag, 0x00);

    /* BTCLK driven by TCLKI internally (crystal driven) and Xmt Elasted  */
    /* Store is enabled. */

	WrtXmtWaveformTbl(ci, comet, TWV_table[tix]);
	if (isT1mode)
		WrtRcvEqualizerTbl((ci_t *) ci, comet, &T1_Equalizer[0]);
	else
		WrtRcvEqualizerTbl((ci_t *) ci, comet, &E1_Equalizer[0]);
	SetPwrLevel(comet);
}

/*
** Name:        WrtXmtWaveform
** Description: Formulate the Data for the Pulse Waveform Storage
**                Write register, (F2), from the sample and unit inputs.
**                Write the data to the Pulse Waveform Storage Data register.
** Returns:     Nothing
*/
static void
WrtXmtWaveform(ci_t *ci, comet_t *comet, u_int32_t sample, u_int32_t unit, u_int8_t data)
{
	u_int8_t    WaveformAddr;

	WaveformAddr = (sample << 3) + (unit & 7);
	pci_write_32((u_int32_t *) &comet->xlpg_pwave_addr, WaveformAddr);
	/* for write order preservation when Optimizing driver */
	pci_flush_write(ci);
	pci_write_32((u_int32_t *) &comet->xlpg_pwave_data, 0x7F & data);
}

/*
** Name:        WrtXmtWaveformTbl
** Description: Fill in the Transmit Waveform Values
**                for driving the transmitter DAC.
** Returns:     Nothing
*/
static void
WrtXmtWaveformTbl(ci_t *ci, comet_t *comet,
		  u_int8_t table[COMET_NUM_SAMPLES][COMET_NUM_UNITS])
{
	u_int32_t sample, unit;

	for (sample = 0; sample < COMET_NUM_SAMPLES; sample++)
		{
		for (unit = 0; unit < COMET_NUM_UNITS; unit++)
			WrtXmtWaveform(ci, comet, sample, unit, table[sample][unit]);
		}

    /* Enable transmitter and set output amplitude */
	pci_write_32((u_int32_t *) &comet->xlpg_cfg, table[COMET_NUM_SAMPLES][0]);
}


/*
** Name:        WrtXmtWaveform
** Description: Fill in the Receive Equalizer RAM from the desired
**                table.
** Returns:     Nothing
**
** Remarks:  Per PM4351 Device Errata, Receive Equalizer RAM Initialization
**           is coded with early setup of indirect address.
*/

static void
WrtRcvEqualizerTbl(ci_t *ci, comet_t *comet, u_int32_t *table)
{
	u_int32_t   ramaddr;
	volatile u_int32_t value;

	for (ramaddr = 0; ramaddr < 256; ramaddr++) {
		/*** the following lines are per Errata 7, 2.5 ***/
		{
		/* Set up for a read operation */
		pci_write_32((u_int32_t *) &comet->rlps_eq_rwsel, 0x80);
		/* for write order preservation when Optimizing driver */
		pci_flush_write(ci);
		/* write the addr, initiate a read */
		pci_write_32((u_int32_t *) &comet->rlps_eq_iaddr, (u_int8_t) ramaddr);
		/* for write order preservation when Optimizing driver */
		pci_flush_write(ci);
		/*
		* wait 3 line rate clock cycles to ensure address bits are
		* captured by T1/E1 clock
		*/

		/* 683ns * 3 = 1366 ns, approx 2us (but use 4us) */
		OS_uwait(4, "wret");
	}

	value = *table++;
	pci_write_32((u_int32_t *) &comet->rlps_idata3, (u_int8_t) (value >> 24));
	pci_write_32((u_int32_t *) &comet->rlps_idata2, (u_int8_t) (value >> 16));
	pci_write_32((u_int32_t *) &comet->rlps_idata1, (u_int8_t) (value >> 8));
	pci_write_32((u_int32_t *) &comet->rlps_idata0, (u_int8_t) value);
	 /* for write order preservation when Optimizing driver */
	pci_flush_write(ci);

	/* Storing RAM address, causes RAM to be updated */

		/* Set up for a write operation */
		pci_write_32((u_int32_t *) &comet->rlps_eq_rwsel, 0);
		/* for write order preservation when optimizing driver */
		pci_flush_write(ci);
		/* write the addr, initiate a read */
		pci_write_32((u_int32_t *) &comet->rlps_eq_iaddr, (u_int8_t) ramaddr);
		 /* for write order preservation when optimizing driver */
		pci_flush_write(ci);

	/*
	* wait 3 line rate clock cycles to ensure address bits are captured
	* by T1/E1 clock
	*/
		/* 683ns * 3 = 1366 ns, approx 2us (but use 4us) */
		OS_uwait(4, "wret");
	}

	/* Enable Equalizer & set it to use 256 periods */
	pci_write_32((u_int32_t *) &comet->rlps_eq_cfg, 0xCB);
}


/*
** Name:        SetPwrLevel
** Description: Implement power level setting algorithm described below
** Returns:     Nothing
*/

static void
SetPwrLevel(comet_t *comet)
{
	volatile u_int32_t temp;

/*
**    Algorithm to Balance the Power Distribution of Ttip Tring
**
**    Zero register F6
**    Write 0x01 to register F4
**    Write another 0x01 to register F4
**    Read register F4
**    Remove the 0x01 bit by Anding register F4 with 0xFE
**    Write the resultant value to register F4
**    Repeat these steps for register F5
**    Write 0x01 to register F6
*/
	/* XLPG Fuse Data Select */
	pci_write_32((u_int32_t *) &comet->xlpg_fdata_sel, 0x00);
	/* XLPG Analog Test Positive control */
	pci_write_32((u_int32_t *) &comet->xlpg_atest_pctl, 0x01);
	pci_write_32((u_int32_t *) &comet->xlpg_atest_pctl, 0x01);
	temp = pci_read_32((u_int32_t *) &comet->xlpg_atest_pctl) & 0xfe;
	pci_write_32((u_int32_t *) &comet->xlpg_atest_pctl, temp);
	pci_write_32((u_int32_t *) &comet->xlpg_atest_nctl, 0x01);
	pci_write_32((u_int32_t *) &comet->xlpg_atest_nctl, 0x01);
	/* XLPG Analog Test Negative control */
	temp = pci_read_32((u_int32_t *) &comet->xlpg_atest_nctl) & 0xfe;
	pci_write_32((u_int32_t *) &comet->xlpg_atest_nctl, temp);
	/* XLPG */
	pci_write_32((u_int32_t *) &comet->xlpg_fdata_sel, 0x01);
}


/*
** Name:        SetCometOps
** Description: Set up the selected Comet's clock edge drive for both
**                the transmit out the analog side and receive to the
**                backplane side.
** Returns:     Nothing
*/
#if 0
static void
SetCometOps(comet_t *comet)
{
	volatile u_int8_t rd_value;

	if (comet == mConfig.C4Func1Base + (COMET0_OFFSET >> 2))
	{
		/* read the BRIF Configuration */
		rd_value = (u_int8_t) pci_read_32((u_int32_t *) &comet->brif_cfg);
		rd_value &= ~0x20;
		pci_write_32((u_int32_t *) &comet->brif_cfg, (u_int32_t) rd_value);
		/* read the BRIF Frame Pulse Configuration */
		rd_value = (u_int8_t) pci_read_32((u_int32_t *) &comet->brif_fpcfg);
		rd_value &= ~0x20;
		pci_write_32((u_int32_t *) &comet->brif_fpcfg, (u_int8_t) rd_value);
	} else {
	/* read the BRIF Configuration */
	rd_value = (u_int8_t) pci_read_32((u_int32_t *) &comet->brif_cfg);
	rd_value |= 0x20;
	pci_write_32((u_int32_t *) &comet->brif_cfg, (u_int32_t) rd_value);
	/* read the BRIF Frame Pulse Configuration */
	rd_value = (u_int8_t) pci_read_32((u_int32_t *) &comet->brif_fpcfg);
	rd_value |= 0x20;
	pci_write_32(u_int32_t *) & comet->brif_fpcfg, (u_int8_t) rd_value);
	}
}
#endif

/***  End-of-File  ***/
