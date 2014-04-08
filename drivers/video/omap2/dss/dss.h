/*
 * linux/drivers/video/omap2/dss/dss.h
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __OMAP2_DSS_H
#define __OMAP2_DSS_H

#include <linux/interrupt.h>

#ifdef pr_fmt
#undef pr_fmt
#endif

#ifdef DSS_SUBSYS_NAME
#define pr_fmt(fmt) DSS_SUBSYS_NAME ": " fmt
#else
#define pr_fmt(fmt) fmt
#endif

#define DSSDBG(format, ...) \
	pr_debug(format, ## __VA_ARGS__)

#ifdef DSS_SUBSYS_NAME
#define DSSERR(format, ...) \
	printk(KERN_ERR "omapdss " DSS_SUBSYS_NAME " error: " format, \
	## __VA_ARGS__)
#else
#define DSSERR(format, ...) \
	printk(KERN_ERR "omapdss error: " format, ## __VA_ARGS__)
#endif

#ifdef DSS_SUBSYS_NAME
#define DSSINFO(format, ...) \
	printk(KERN_INFO "omapdss " DSS_SUBSYS_NAME ": " format, \
	## __VA_ARGS__)
#else
#define DSSINFO(format, ...) \
	printk(KERN_INFO "omapdss: " format, ## __VA_ARGS__)
#endif

#ifdef DSS_SUBSYS_NAME
#define DSSWARN(format, ...) \
	printk(KERN_WARNING "omapdss " DSS_SUBSYS_NAME ": " format, \
	## __VA_ARGS__)
#else
#define DSSWARN(format, ...) \
	printk(KERN_WARNING "omapdss: " format, ## __VA_ARGS__)
#endif

/* OMAP TRM gives bitfields as start:end, where start is the higher bit
   number. For example 7:0 */
#define FLD_MASK(start, end)	(((1 << ((start) - (end) + 1)) - 1) << (end))
#define FLD_VAL(val, start, end) (((val) << (end)) & FLD_MASK(start, end))
#define FLD_GET(val, start, end) (((val) & FLD_MASK(start, end)) >> (end))
#define FLD_MOD(orig, val, start, end) \
	(((orig) & ~FLD_MASK(start, end)) | FLD_VAL(val, start, end))

enum dss_io_pad_mode {
	DSS_IO_PAD_MODE_RESET,
	DSS_IO_PAD_MODE_RFBI,
	DSS_IO_PAD_MODE_BYPASS,
};

enum dss_hdmi_venc_clk_source_select {
	DSS_VENC_TV_CLK = 0,
	DSS_HDMI_M_PCLK = 1,
};

enum dss_dsi_content_type {
	DSS_DSI_CONTENT_DCS,
	DSS_DSI_CONTENT_GENERIC,
};

enum dss_writeback_channel {
	DSS_WB_LCD1_MGR =	0,
	DSS_WB_LCD2_MGR =	1,
	DSS_WB_TV_MGR =		2,
	DSS_WB_OVL0 =		3,
	DSS_WB_OVL1 =		4,
	DSS_WB_OVL2 =		5,
	DSS_WB_OVL3 =		6,
	DSS_WB_LCD3_MGR =	7,
};

struct dss_clock_info {
	/* rates that we get with dividers below */
	unsigned long fck;

	/* dividers */
	u16 fck_div;
};

struct dispc_clock_info {
	/* rates that we get with dividers below */
	unsigned long lck;
	unsigned long pck;

	/* dividers */
	u16 lck_div;
	u16 pck_div;
};

struct dsi_clock_info {
	/* rates that we get with dividers below */
	unsigned long fint;
	unsigned long clkin4ddr;
	unsigned long clkin;
	unsigned long dsi_pll_hsdiv_dispc_clk;	/* OMAP3: DSI1_PLL_CLK
						 * OMAP4: PLLx_CLK1 */
	unsigned long dsi_pll_hsdiv_dsi_clk;	/* OMAP3: DSI2_PLL_CLK
						 * OMAP4: PLLx_CLK2 */
	unsigned long lp_clk;

	/* dividers */
	u16 regn;
	u16 regm;
	u16 regm_dispc;	/* OMAP3: REGM3
			 * OMAP4: REGM4 */
	u16 regm_dsi;	/* OMAP3: REGM4
			 * OMAP4: REGM5 */
	u16 lp_clk_div;
};

struct reg_field {
	u16 reg;
	u8 high;
	u8 low;
};

struct dss_lcd_mgr_config {
	enum dss_io_pad_mode io_pad_mode;

	bool stallmode;
	bool fifohandcheck;

	struct dispc_clock_info clock_info;

	int video_port_width;

	int lcden_sig_polarity;
};

struct seq_file;
struct platform_device;

/* core */
struct platform_device *dss_get_core_pdev(void);
int dss_dsi_enable_pads(int dsi_id, unsigned lane_mask);
void dss_dsi_disable_pads(int dsi_id, unsigned lane_mask);
int dss_set_min_bus_tput(struct device *dev, unsigned long tput);
int dss_debugfs_create_file(const char *name, void (*write)(struct seq_file *));

/* display */
int dss_suspend_all_devices(void);
int dss_resume_all_devices(void);
void dss_disable_all_devices(void);

int display_init_sysfs(struct platform_device *pdev);
void display_uninit_sysfs(struct platform_device *pdev);

/* manager */
int dss_init_overlay_managers(void);
void dss_uninit_overlay_managers(void);
int dss_init_overlay_managers_sysfs(struct platform_device *pdev);
void dss_uninit_overlay_managers_sysfs(struct platform_device *pdev);
int dss_mgr_simple_check(struct omap_overlay_manager *mgr,
		const struct omap_overlay_manager_info *info);
int dss_mgr_check_timings(struct omap_overlay_manager *mgr,
		const struct omap_video_timings *timings);
int dss_mgr_check(struct omap_overlay_manager *mgr,
		struct omap_overlay_manager_info *info,
		const struct omap_video_timings *mgr_timings,
		const struct dss_lcd_mgr_config *config,
		struct omap_overlay_info **overlay_infos);

static inline bool dss_mgr_is_lcd(enum omap_channel id)
{
	if (id == OMAP_DSS_CHANNEL_LCD || id == OMAP_DSS_CHANNEL_LCD2 ||
			id == OMAP_DSS_CHANNEL_LCD3)
		return true;
	else
		return false;
}

int dss_manager_kobj_init(struct omap_overlay_manager *mgr,
		struct platform_device *pdev);
void dss_manager_kobj_uninit(struct omap_overlay_manager *mgr);

/* overlay */
void dss_init_overlays(struct platform_device *pdev);
void dss_uninit_overlays(struct platform_device *pdev);
void dss_overlay_setup_dispc_manager(struct omap_overlay_manager *mgr);
int dss_ovl_simple_check(struct omap_overlay *ovl,
		const struct omap_overlay_info *info);
int dss_ovl_check(struct omap_overlay *ovl, struct omap_overlay_info *info,
		const struct omap_video_timings *mgr_timings);
bool dss_ovl_use_replication(struct dss_lcd_mgr_config config,
		enum omap_color_mode mode);
int dss_overlay_kobj_init(struct omap_overlay *ovl,
		struct platform_device *pdev);
void dss_overlay_kobj_uninit(struct omap_overlay *ovl);

/* DSS */
int dss_init_platform_driver(void) __init;
void dss_uninit_platform_driver(void);

unsigned long dss_get_dispc_clk_rate(void);
int dss_dpi_select_source(enum omap_channel channel);
void dss_select_hdmi_venc_clk_source(enum dss_hdmi_venc_clk_source_select);
enum dss_hdmi_venc_clk_source_select dss_get_hdmi_venc_clk_source(void);
const char *dss_get_generic_clk_source_name(enum omap_dss_clk_source clk_src);
void dss_dump_clocks(struct seq_file *s);

#if defined(CONFIG_OMAP2_DSS_DEBUGFS)
void dss_debug_dump_clocks(struct seq_file *s);
#endif

int dss_get_ctx_loss_count(void);

void dss_sdi_init(int datapairs);
int dss_sdi_enable(void);
void dss_sdi_disable(void);

void dss_select_dsi_clk_source(int dsi_module,
		enum omap_dss_clk_source clk_src);
void dss_select_lcd_clk_source(enum omap_channel channel,
		enum omap_dss_clk_source clk_src);
enum omap_dss_clk_source dss_get_dispc_clk_source(void);
enum omap_dss_clk_source dss_get_dsi_clk_source(int dsi_module);
enum omap_dss_clk_source dss_get_lcd_clk_source(enum omap_channel channel);

void dss_set_venc_output(enum omap_dss_venc_type type);
void dss_set_dac_pwrdn_bgz(bool enable);

unsigned long dss_get_dpll4_rate(void);
int dss_calc_clock_rates(struct dss_clock_info *cinfo);
int dss_set_clock_div(struct dss_clock_info *cinfo);

typedef bool (*dss_div_calc_func)(int fckd, unsigned long fck, void *data);
bool dss_div_calc(unsigned long fck_min, dss_div_calc_func func, void *data);

/* SDI */
int sdi_init_platform_driver(void) __init;
void sdi_uninit_platform_driver(void) __exit;

/* DSI */

typedef bool (*dsi_pll_calc_func)(int regn, int regm, unsigned long fint,
		unsigned long pll, void *data);
typedef bool (*dsi_hsdiv_calc_func)(int regm_dispc, unsigned long dispc,
		void *data);

#ifdef CONFIG_OMAP2_DSS_DSI

struct dentry;
struct file_operations;

int dsi_init_platform_driver(void) __init;
void dsi_uninit_platform_driver(void) __exit;

int dsi_runtime_get(struct platform_device *dsidev);
void dsi_runtime_put(struct platform_device *dsidev);

void dsi_dump_clocks(struct seq_file *s);

void dsi_irq_handler(void);
u8 dsi_get_pixel_size(enum omap_dss_dsi_pixel_format fmt);

unsigned long dsi_get_pll_clkin(struct platform_device *dsidev);

bool dsi_hsdiv_calc(struct platform_device *dsidev, unsigned long pll,
		unsigned long out_min, dsi_hsdiv_calc_func func, void *data);
bool dsi_pll_calc(struct platform_device *dsidev, unsigned long clkin,
		unsigned long pll_min, unsigned long pll_max,
		dsi_pll_calc_func func, void *data);

unsigned long dsi_get_pll_hsdiv_dispc_rate(struct platform_device *dsidev);
int dsi_pll_set_clock_div(struct platform_device *dsidev,
		struct dsi_clock_info *cinfo);
int dsi_pll_init(struct platform_device *dsidev, bool enable_hsclk,
		bool enable_hsdiv);
void dsi_pll_uninit(struct platform_device *dsidev, bool disconnect_lanes);
void dsi_wait_pll_hsdiv_dispc_active(struct platform_device *dsidev);
void dsi_wait_pll_hsdiv_dsi_active(struct platform_device *dsidev);
struct platform_device *dsi_get_dsidev_from_id(int module);
#else
static inline int dsi_runtime_get(struct platform_device *dsidev)
{
	return 0;
}
static inline void dsi_runtime_put(struct platform_device *dsidev)
{
}
static inline u8 dsi_get_pixel_size(enum omap_dss_dsi_pixel_format fmt)
{
	WARN("%s: DSI not compiled in, returning pixel_size as 0\n", __func__);
	return 0;
}
static inline unsigned long dsi_get_pll_hsdiv_dispc_rate(struct platform_device *dsidev)
{
	WARN("%s: DSI not compiled in, returning rate as 0\n", __func__);
	return 0;
}
static inline int dsi_pll_set_clock_div(struct platform_device *dsidev,
		struct dsi_clock_info *cinfo)
{
	WARN("%s: DSI not compiled in\n", __func__);
	return -ENODEV;
}
static inline int dsi_pll_init(struct platform_device *dsidev,
		bool enable_hsclk, bool enable_hsdiv)
{
	WARN("%s: DSI not compiled in\n", __func__);
	return -ENODEV;
}
static inline void dsi_pll_uninit(struct platform_device *dsidev,
		bool disconnect_lanes)
{
}
static inline void dsi_wait_pll_hsdiv_dispc_active(struct platform_device *dsidev)
{
}
static inline void dsi_wait_pll_hsdiv_dsi_active(struct platform_device *dsidev)
{
}
static inline struct platform_device *dsi_get_dsidev_from_id(int module)
{
	return NULL;
}

static inline unsigned long dsi_get_pll_clkin(struct platform_device *dsidev)
{
	return 0;
}

static inline bool dsi_hsdiv_calc(struct platform_device *dsidev,
		unsigned long pll, unsigned long out_min,
		dsi_hsdiv_calc_func func, void *data)
{
	return false;
}

static inline bool dsi_pll_calc(struct platform_device *dsidev,
		unsigned long clkin,
		unsigned long pll_min, unsigned long pll_max,
		dsi_pll_calc_func func, void *data)
{
	return false;
}

#endif

/* DPI */
int dpi_init_platform_driver(void) __init;
void dpi_uninit_platform_driver(void) __exit;

/* DISPC */
int dispc_init_platform_driver(void) __init;
void dispc_uninit_platform_driver(void) __exit;
void dispc_dump_clocks(struct seq_file *s);

void dispc_enable_sidle(void);
void dispc_disable_sidle(void);

void dispc_lcd_enable_signal(bool enable);
void dispc_pck_free_enable(bool enable);
void dispc_enable_fifomerge(bool enable);
void dispc_enable_gamma_table(bool enable);
void dispc_set_loadmode(enum omap_dss_load_mode mode);

typedef bool (*dispc_div_calc_func)(int lckd, int pckd, unsigned long lck,
		unsigned long pck, void *data);
bool dispc_div_calc(unsigned long dispc,
		unsigned long pck_min, unsigned long pck_max,
		dispc_div_calc_func func, void *data);

bool dispc_mgr_timings_ok(enum omap_channel channel,
		const struct omap_video_timings *timings);
unsigned long dispc_fclk_rate(void);
int dispc_calc_clock_rates(unsigned long dispc_fclk_rate,
		struct dispc_clock_info *cinfo);


void dispc_ovl_set_fifo_threshold(enum omap_plane plane, u32 low, u32 high);
void dispc_ovl_compute_fifo_thresholds(enum omap_plane plane,
		u32 *fifo_low, u32 *fifo_high, bool use_fifomerge,
		bool manual_update);

unsigned long dispc_mgr_lclk_rate(enum omap_channel channel);
unsigned long dispc_mgr_pclk_rate(enum omap_channel channel);
unsigned long dispc_core_clk_rate(void);
void dispc_mgr_set_clock_div(enum omap_channel channel,
		const struct dispc_clock_info *cinfo);
int dispc_mgr_get_clock_div(enum omap_channel channel,
		struct dispc_clock_info *cinfo);
void dispc_set_tv_pclk(unsigned long pclk);

u32 dispc_wb_get_framedone_irq(void);
bool dispc_wb_go_busy(void);
void dispc_wb_go(void);
void dispc_wb_enable(bool enable);
bool dispc_wb_is_enabled(void);
void dispc_wb_set_channel_in(enum dss_writeback_channel channel);
int dispc_wb_setup(const struct omap_dss_writeback_info *wi,
		bool mem_to_mem, const struct omap_video_timings *timings);

/* VENC */
int venc_init_platform_driver(void) __init;
void venc_uninit_platform_driver(void) __exit;

/* HDMI */
int hdmi4_init_platform_driver(void) __init;
void hdmi4_uninit_platform_driver(void) __exit;

/* RFBI */
int rfbi_init_platform_driver(void) __init;
void rfbi_uninit_platform_driver(void) __exit;


#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
static inline void dss_collect_irq_stats(u32 irqstatus, unsigned *irq_arr)
{
	int b;
	for (b = 0; b < 32; ++b) {
		if (irqstatus & (1 << b))
			irq_arr[b]++;
	}
}
#endif

#endif
