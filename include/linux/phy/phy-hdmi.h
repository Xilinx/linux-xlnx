/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __PHY_HDMI_H_
#define __PHY_HDMI_H_

#include <linux/types.h>

/**
 * enum callback_type - HDMI PHY callback functions type
 * This enumerates the list of available HDMI PHY callback functions types.
 *
 * @RX_INIT_CB:		Receiver initialization callback type
 * @RX_READY_CB:	Receiver ready callback type
 * @TX_INIT_CB:		Transmitter initialization callback type
 * @TX_READY_CB:	Transmitter ready callback type
 */
enum callback_type {
	RX_INIT_CB,
	RX_READY_CB,
	TX_INIT_CB,
	TX_READY_CB,
};

/**
 * struct hdmiphy_callback - HDMI PHY callback structure
 * This structure is used to represent the callback function of a
 * HDMI phy.
 *
 * @cb:		Callback function pointer
 * @data:	Pointer to store data
 * @type:	Callback type
 */
struct hdmiphy_callback {
	void (*cb)(void *callback_func);
	void *data;
	u32 type;
};

/**
 * struct phy_configure_opts_hdmi - HDMI PHY configuration set
 *
 * This structure is used to represent the configuration state of a
 * HDMI phy.
 */
struct phy_configure_opts_hdmi {
	/**
	 * @tmdsclock_ratio_flag:
	 *
	 * SCDC tmds clock ratio flag.
	 *
	 * Allowed values: 0, 1
	 */
	u8 tmdsclock_ratio_flag : 1;
	/**
	 * @tmdsclock_ratio:
	 *
	 * SCDC tmds clock ratio bit.
	 *
	 * Allowed values: 0, 1
	 */
	u8 tmdsclock_ratio : 1;

	/**
	 * @ibufds:
	 *
	 * Flag to enable/disable the TX or RX IBUFDS configuration.
	 *
	 * Allowed values: 0, 1
	 */
	u8 ibufds : 1;
	/**
	 * @ibufds_en:
	 *
	 * enables/disable the TX or RX IBUFDS peripheral.
	 *
	 * Allowed values: 0, 1
	 */
	u8 ibufds_en : 1;
	/**
	 * @clkout1_obuftds:
	 *
	 * Flag to enable/disable the TX or RX CLKOUT1 OBUFTDS configuration.
	 *
	 * Allowed values: 0, 1
	 */
	u8 clkout1_obuftds : 1;
	/**
	 * @clkout1_obuftds_en:
	 *
	 * enable/disable the TX or RX CLKOUT1 OBUFTDS peripheral.
	 *
	 * Allowed values: 0, 1
	 */
	u8 clkout1_obuftds_en : 1;
	/**
	 * @config_hdmi20:
	 *
	 * Flag to enable/disable HDMI-PHY to be configured in 2.0 mode
	 *
	 * Allowed values: 0, 1
	 */
	u8 config_hdmi20 : 1;
	/**
	 * @config_hdmi21:
	 *
	 * Flag to enable/disable HDMI-PHY to be configured in 2.1 mode
	 *
	 * Allowed values: 0, 1
	 */
	u8 config_hdmi21 : 1;
	/* these are used with hdmi21 conf */
	u64 linerate;
	u8 nchannels;
	/**
	 * @rx_get_refclk:
	 *
	 * Flag to get the rx reference clock value from the PHY driver
	 *
	 * Allowed values: 0, 1
	 */
	u8 rx_get_refclk : 1;
	/**
	 * @rx_refclk_hz:
	 *
	 * Rx reference clock value.
	 *
	 */
	unsigned long rx_refclk_hz;
	/**
	 * @phycb:
	 *
	 * phy callback functions flag.
	 *
	 * Allowed values: 0, 1
	 */
	u8 phycb : 1;
	/**
	 * @hdmiphycb:
	 *
	 * HDMI PHY callback structure
	 */
	struct hdmiphy_callback hdmiphycb;
	/**
	 * @tx_params:
	 *
	 * Flag to update the tx stream paramerters.
	 *
	 * Allowed values: 0, 1
	 */
	u8 tx_params : 1;
	/**
	 * @cal_mmcm_param:
	 *
	 * Flage to update caliculate mmcm parameters.
	 *
	 * Allowed values: 0, 1
	 */
	u8 cal_mmcm_param : 1;
	/**
	 * @tx_tmdsclk:
	 *
	 * TX TMDS clock value.
	 *
	 */
	u64 tx_tmdsclk;
	/**
	 * @ppc:
	 *
	 * pixels per clock.
	 *
	 * Allowed values: 1, 2, 4, 8
	 */
	u8 ppc;
	/**
	 * @bpc:
	 *
	 * bits per component.
	 *
	 * Allowed  values: 8, 10, 23, 16
	 */
	u8 bpc;
	/**
	 * @fmt:
	 *
	 * color format.
	 *
	 * Allowed values: 0, 1, 2, 3
	 */
	u8 fmt;
	/**
	 * @reset_gt
	 *
	 * Reset GT
	 */
	u8 reset_gt:1;
};

#endif /* __PHY_HDMI_H_ */
