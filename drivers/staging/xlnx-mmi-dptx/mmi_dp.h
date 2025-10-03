/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Multimedia Integrated DisplayPort Tx driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __MMI_DP_H__
#define __MMI_DP_H__

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_bridge.h>

#include "mmi_dp_config.h"
#include "mmi_dp_reg.h"

#define DPTX_RECEIVER_CAP_SIZE		0x100
#define DPTX_SDP_NUM			0x10
#define DPTX_SDP_LEN			0x9
#define DPTX_SDP_SIZE			(9 * 4)

/* ALPM */
#define RECEIVER_ALPM_CAPABILITIES	0x0002E
#define RECEIVER_ALPM_CONFIGURATIONS	0x00116

struct dptx;

/* The max rate supported by the core */
#define DPTX_MAX_LINK_RATE		DPTX_PHYIF_CTRL_RATE_HBR3

/* The max number of streams supported */
#define DPTX_MAX_STREAM_NUMBER		4

/**
 * struct dptx_link - The link state.
 * @status: Holds the sink status register values.
 * @trained: True if the link is successfully trained.
 * @rate: The current rate that the link is trained at.
 * @lanes: The current number of lanes that the link is trained at.
 * @preemp_level: The pre-emphasis level used for each lane.
 * @vswing_level: The vswing level used for each lane.
 */
struct dptx_link {
	u8 status[DP_LINK_STATUS_SIZE];
	bool trained;
	u8 rate;
	u8 lanes;
	u8 preemp_level[4];
	u8 vswing_level[4];
};

enum established_timings {
	DMT_640x480_60hz,
	DMT_800x600_60hz,
	DMT_1024x768_60hz,
	NONE
};

struct dptx_aux {
	u32 sts;
	u32 data[4];
	atomic_t abort;
	atomic_t serving;
};

struct sdp_header {
	u8 HB0;
	u8 HB1;
	u8 HB2;
	u8 HB3;
} __packed;

struct sdp_full_data {
	u8 en;
	u32 payload[DPTX_SDP_LEN];
	u8 blanking;
	u8 cont;
} __packed;

enum ALPM_Status {
	NOT_AVAILABLE = -1,
	DISABLED = 0,
	ENABLED = 1
};

enum ALPM_State {
	POWER_ON = 0,
	POWER_OFF = 1
};

struct edp_alpm {
	enum ALPM_Status status;
	enum ALPM_State state;
};

enum hdcp_enum {
	HDCP_OFF = 0,
	HDCP_13,
	HDCP_22,
	HDCP_MAX
};

struct hdcp_aksv {
	u32 lsb;
	u32 msb;
};

struct hdcp_dpk {
	u32 lsb;
	u32 msb;
};

struct hdcp_params {
	struct hdcp_aksv aksv;
	struct hdcp_dpk dpk[40];
	u32 enc_key;
	u32 crc32;
	u8 auth_fail_count;
	enum hdcp_enum hdcp_en;
};

/* Interrupt Resources */
enum irq_res_enum {
	MAIN_IRQ = 0,
	MAX_IRQ_IDX
};

/* Phy */
enum rate_enum {
	RBR = 0,
	HBR1,
	HBR2,
	HBR3,
	EDP0,
	EDP1,
	EDP2,
	EDP3,
	MAX_RATE
};

/* Dpcd Configuration */
#define DPCD_REV			0x00000
#define MAX_LINK_RATE			0x00001
#define MAX_LANE_COUNT			0x00002
#define MAX_DOWNSPREAD			0x00003
#define NORP_DP_PWR_VOLTAGE_CAP		0x00004
#define DOWN_STREAM_PORT_PRESENT	0x00005
#define MAIN_LINK_CHANNEL_CODING	0x00006
#define DOWN_STREAM_PORT_COUNT		0x00007
#define RECEIVE_PORT0_CAP_0		0x00008
#define RECEIVE_PORT0_CAP_1		0x00009
#define RECEIVE_PORT1_CAP_0		0x0000A
#define RECEIVE_PORT1_CAP_1		0x0000B
#define I2C_SPEED_CONTROL		0x0000C
#define TRAINING_AUX_RD_INTERVAL	0x0000E
#define ADAPTER_CAP			0x0000F

/* Link Configuration */
#define MAX_PHY_BUSY_WAIT_ITER	20
#define DEFAULT_STREAM		0
#define LT_DONE			0
#define LT_CR_FAIL		1
#define LT_CH_EQ_FAIL		2
#define CR_DONE			3
#define CR_FAIL			4
#define CH_EQ_DONE		5
#define CH_EQ_FAIL		6
#define ELOWESTRATE		7
#define ELOWESTLANENR		8
#define LANE_REDUCTION		9
#define RATE_REDUCTION		10
#define PATTERN_MASK		0x0F
#define SCRAMBLING_DIS_MASK	0x20
#define VSWING_MASK		0x03
#define MAX_VSWING_MASK		0x04
#define PREEMPH_MASK		0x18
#define MAX_PREEMPH_MASK	0x20

struct rx_capabilities {
	u8 minor_rev_num;
	u8 major_rev_num;
	u8 max_link_rate;
	u8 max_lane_count;
	bool post_lt_adj_req_supported;
	bool tps3_supported;
	bool enhanced_frame_cap;
	bool max_downspread;
	bool no_aux_transaction_link_training;
	bool tps4_supported;
	bool norp;
	bool crc_3d_option_supported;
	bool dp_pwer_cap_5v;
	bool dp_pwer_cap_12v;
	bool dp_pwer_cap_18v;
	bool dfp_present;
	u8 dfp_type;
	bool format_conversion;
	bool detailed_cap_info_available;
	bool channel_coding_8b10b_supported;
	u8 dfp_count;
	bool msa_timing_par_ignored;
	bool oui_support;
	bool port0_local_edid_present;
	bool port0_associated_to_preceding_port;
	bool port0_hblank_expansion_capable;
	bool port0_buffer_size_unit;
	bool port0_buffer_size_per_port;
	u8 port0_buffer_size;
	bool port1_local_edid_present;
	bool port1_associated_to_preceding_port;
	bool port1_hblank_expansion_capable;
	bool port1_buffer_size_unit;
	bool port1_buffer_size_per_port;
	u8 port1_buffer_size;
	u8 i2c_speed;
	u8 training_aux_rd_interval;
	bool extended_receiver_cap_present;
	bool force_load_sense_cap;
	bool alternate_i2c_pattern_cap;
};

/**
 * struct dptx - The representation of the DP TX core
 * @mutex: dptx mutex
 * @hwparams: HW config parameters
 * @base: Base address of the registers
 * @irq: IRQ number
 * @max_rate: The maximum rate that the controller supports
 * @max_lanes: The maximum lane count that the controller supports
 * @bpp: Bits per pixel
 * @ycbcr420: sending YUV 420 data flag
 * @streams: Number of streams
 * @hdcp_en: Type of hdcp enabled
 * @selected_pixel_clock: Selected pixel clock
 * @mst: Flag for MST mode or not
 * @cr_fail: Clock recovery fail flag
 * @ssc_en: Spread Spectrum clocking enabled flag
 * @enhanced_frame_cap: Enhanced frame capabilities flag
 * @edp: EDP flag
 * @dev: The struct device
 * @bridge: DRM Bridge
 * @conn_status: connection status
 * @dp_aux: DRM DP Aux
 * @vparams: The video params to use
 * @hparams: The HDCP params to use
 * @waitq: The waitq
 * @shutdown: Signals that the driver is shutting down and that all
 *	    operations should be aborted.
 * @c_connect: Signals that a HOT_PLUG or HOT_UNPLUG has occurred.
 * @sink_request: Signals the a HPD_IRQ has occurred.
 * @alpm: ALPM state and status
 * @rx_caps: The sink's receiver capabilities.
 * @sdp_list: The array of SDP elements
 * @aux: AUX channel state for performing an AUX transfer.
 * @link: The current link state.
 * @multipixel: Controls multipixel configuration. 0-Single, 1-Dual, 2-Quad.
 */
struct dptx {
	struct mutex mutex; /* generic mutex for dptx */

	struct {
		u8 sdp_reg_bank_size;
		u8 audio_select;
		u8 num_streams;
		u8 psr_version;
		u8 sync_depth;
		u8 phy_type;
		u8 mp_mode;
		bool gen2_phy;
		bool adsync;
		bool fpga;
		bool hdcp;
		bool edp;
		bool fec;
		bool dsc;
	} hwparams;

	void __iomem *base;
	int irq;

	u8 max_rate;
	u8 max_lanes;
	u8 bpp;
	bool ycbcr420;
	u8 streams;
	enum hdcp_enum hdcp_en;
	u32 selected_pixel_clock;
	bool mst;
	bool cr_fail;
	u8 multipixel;
	bool ssc_en;
	bool enhanced_frame_cap;
	bool edp;

	struct device *dev;
	struct drm_bridge bridge;
	/* struct drm_bridge *next_bridge; */
	enum drm_connector_status conn_status;
	struct drm_dp_aux dp_aux;

	struct video_params vparams[DPTX_MAX_STREAM_NUMBER];
	struct hdcp_params hparams;

	wait_queue_head_t waitq;

	atomic_t shutdown;
	atomic_t c_connect;
	atomic_t sink_request;

	struct edp_alpm alpm;
	struct rx_capabilities rx_caps;

	struct sdp_full_data sdp_list[DPTX_SDP_NUM];
	struct dptx_aux aux;
	struct dptx_link link;
};

/* DP Registers Accessors */

/**
 * mmi_dp_read - Read mmi_dp register
 * @base: base register address
 * @offset: register offset
 *
 * Return: value stored in the hardware register
 */
static inline u32 mmi_dp_read(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

/**
 * mmi_dp_write - Write the value into mmi_dp register
 * @base: base register address
 * @offset: register offset
 * @val: value to write
 */
static inline void mmi_dp_write(void __iomem *base, u32 offset, u32 val)
{
	writel(val, base + offset);
}

/**
 * mmi_dp_clr - Clear bits in mmi_dp register
 * @base: base register address
 * @offset: register offset
 * @clr: bits to clear
 */
static inline void mmi_dp_clr(void __iomem *base, u32 offset, u32 clr)
{
	mmi_dp_write(base, offset, mmi_dp_read(base, offset) & ~clr);
}

/**
 * mmi_dp_set - Set bits in mmi_dp register
 * @base: base register address
 * @offset: register offset
 * @set: bits to set
 */
static inline void mmi_dp_set(void __iomem *base, u32 offset, u32 set)
{
	mmi_dp_write(base, offset, mmi_dp_read(base, offset) | set);
}

/*
 * Core interface functions
 */
void mmi_dp_core_init_phy(struct dptx *dptx);
void mmi_dp_soft_reset(struct dptx *dptx, u32 bits);

irqreturn_t mmi_dp_irq(int irq, void *dev);
irqreturn_t mmi_dp_threaded_irq(int irq, void *dev);

void mmi_dp_global_intr_en(struct dptx *dp);
void mmi_dp_global_intr_dis(struct dptx *dp);
void mmi_dp_enable_hpd_intr(struct dptx *dp);
void mmi_dp_video_intr_dis(struct dptx *dp);
void mmi_dp_clean_interrupts(struct dptx *dp);

/*
 * PHY IF Control
 */
void mmi_dp_phy_set_lanes(struct dptx *dptx, unsigned int num);
void mmi_dp_phy_set_rate(struct dptx *dptx, unsigned int rate);
void mmi_dp_phy_set_pre_emphasis(struct dptx *dptx,
				 unsigned int lane, unsigned int level);
void mmi_dp_phy_set_vswing(struct dptx *dptx, unsigned int lane,
			   unsigned int level);
void mmi_dp_phy_set_pattern(struct dptx *dptx, unsigned int pattern);
void mmi_dp_phy_enable_xmit(struct dptx *dptx, unsigned int lane, bool enable);

int mmi_dp_phy_rate_to_bw(unsigned int rate);
int mmi_dp_bw_to_phy_rate(unsigned int bw);

/*
 * AUX Channel
 */

int __mmi_dp_read_dpcd(struct dptx *dptx, u32 addr, u8 *byte);
int __mmi_dp_write_dpcd(struct dptx *dptx, u32 addr, u8 byte);

int __mmi_dp_read_bytes_from_dpcd(struct dptx *dptx, unsigned int reg_addr,
				  u8 *bytes, u32 len);

int __mmi_dp_write_bytes_to_dpcd(struct dptx *dptx, unsigned int reg_addr,
				 u8 *bytes, u32 len);

static inline int mmi_dp_read_dpcd(struct dptx *dptx, u32 addr, u8 *byte)
{
	return __mmi_dp_read_dpcd(dptx, addr, byte);
}

static inline int mmi_dp_write_dpcd(struct dptx *dptx, u32 addr, u8 byte)
{
	return __mmi_dp_write_dpcd(dptx, addr, byte);
}

static inline int mmi_dp_read_bytes_from_dpcd(struct dptx *dptx,
					      unsigned int reg_addr,
					      u8 *bytes, u32 len)
{
	return __mmi_dp_read_bytes_from_dpcd(dptx, reg_addr, bytes, len);
}

static inline int mmi_dp_write_bytes_to_dpcd(struct dptx *dptx,
					     unsigned int reg_addr,
					     u8 *bytes, u32 len)
{
	return __mmi_dp_write_bytes_to_dpcd(dptx, reg_addr, bytes, len);
}

#define mmi_dp_read_regfield(_base, _offset, _bit_mask) ({ \
	FIELD_GET(_bit_mask, mmi_dp_read(_base, _offset)); \
})

/* Link training */
int mmi_dp_full_link_training(struct dptx *dptx);
int mmi_dp_fast_link_training(struct dptx *dptx);

int mmi_dp_adjust_vswing_and_preemphasis(struct dptx *dptx);
void mmi_dp_notify(struct dptx *dptx);

/* Phy */
int mmi_dp_power_state_change_phy(struct dptx *dptx, u8 power_state);
int mmi_dp_disable_datapath_phy(struct dptx *dptx);

/* Dptx Util utils */
u32 mmi_dp_set_field(u32 data, u32 mask, u32 value);
u8 mmi_dp_set8_field(u8 data, u8 mask, u8 value);
void mmi_dp_write_mask(struct dptx *dptx, u32 addr, u32 mask, u32 data);

/* Debug */
#define dptx_dbg(_dp, _fmt...) dev_dbg((_dp)->dev, _fmt)
#define dptx_info(_dp, _fmt...) dev_info((_dp)->dev, _fmt)
#define dptx_warn(_dp, _fmt...) dev_warn((_dp)->dev, _fmt)
#define dptx_err(_dp, _fmt...) dev_err((_dp)->dev, _fmt)

#endif
