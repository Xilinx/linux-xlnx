/* ////////////////////////////////////////////////////////////////////////// */
/*  */
/* Copyright (c) Atmel Corporation.  All rights reserved. */
/*  */
/* Module Name:  wilc_wlan_cfg.h */
/*  */
/*  */
/* ///////////////////////////////////////////////////////////////////////// */

#ifndef WILC_WLAN_CFG_H
#define WILC_WLAN_CFG_H

struct wilc_cfg_byte {
	u16 id;
	u16 val;
};

struct wilc_cfg_hword {
	u16 id;
	u16 val;
};

struct wilc_cfg_word {
	u32 id;
	u32 val;
};

struct wilc_cfg_str {
	u32 id;
	u8 *str;
};

struct wilc;
int wilc_wlan_cfg_set_wid(u8 *frame, u32 offset, u16 id, u8 *buf, int size);
int wilc_wlan_cfg_get_wid(u8 *frame, u32 offset, u16 id);
int wilc_wlan_cfg_get_wid_value(u16 wid, u8 *buffer, u32 buffer_size);
int wilc_wlan_cfg_indicate_rx(struct wilc *wilc, u8 *frame, int size,
			      struct wilc_cfg_rsp *rsp);
int wilc_wlan_cfg_init(void);

#endif
