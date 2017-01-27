/**
 * otg.h - DesignWare USB3 DRD OTG Header
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define otg_dbg(d, fmt, args...)  dev_dbg((d)->dev, "%s(): " fmt,\
		__func__, ## args)
#define otg_vdbg(d, fmt, args...) dev_vdbg((d)->dev, "%s(): " fmt,\
		__func__, ## args)
#define otg_err(d, fmt, args...)  dev_err((d)->dev, "%s(): ERROR: " fmt,\
		__func__, ## args)
#define otg_warn(d, fmt, args...) dev_warn((d)->dev, "%s(): WARN: " fmt,\
		__func__, ## args)
#define otg_info(d, fmt, args...) dev_info((d)->dev, "%s(): INFO: " fmt,\
		__func__, ## args)

#ifdef VERBOSE_DEBUG
#define otg_write(o, reg, val)	do {					\
		otg_vdbg(o, "OTG_WRITE: reg=0x%05x, val=0x%08x\n", reg, val); \
		writel(val, ((void *)((o)->regs)) + reg);	\
	} while (0)

#define otg_read(o, reg) ({						\
		u32 __r = readl(((void *)((o)->regs)) + reg);	\
		otg_vdbg(o, "OTG_READ: reg=0x%05x, val=0x%08x\n", reg, __r); \
		__r;							\
	})
#else
#define otg_write(o, reg, val)	writel(val, ((void *)((o)->regs)) + reg)
#define otg_read(o, reg)	readl(((void *)((o)->regs)) + reg)
#endif

#define sleep_main_thread_until_condition_timeout(otg, condition, msecs) ({ \
		int __timeout = msecs;				\
		while (!(condition)) {				\
			otg_dbg(otg, "  ... sleeping for %d\n", __timeout); \
			__timeout = sleep_main_thread_timeout(otg, __timeout); \
			if (__timeout <= 0) {			\
				break;				\
			}					\
		}						\
		__timeout;					\
	})

#define sleep_main_thread_until_condition(otg, condition) ({	\
		int __rc;					\
		do {						\
			__rc = sleep_main_thread_until_condition_timeout(otg, \
					condition, 50000);	\
		} while (__rc == 0);				\
		__rc;						\
	})

#define GHWPARAMS6				0xc158
#define GHWPARAMS6_SRP_SUPPORT_ENABLED		0x0400
#define GHWPARAMS6_HNP_SUPPORT_ENABLED		0x0800

#define GCTL					0xc110
#define GCTL_PRT_CAP_DIR			0x3000
#define GCTL_PRT_CAP_DIR_SHIFT			12
#define GCTL_PRT_CAP_DIR_HOST			1
#define GCTL_PRT_CAP_DIR_DEV			2
#define GCTL_PRT_CAP_DIR_OTG			3
#define GCTL_GBL_HIBERNATION_EN			0x2

#define OCFG					0xcc00
#define OCFG_SRP_CAP				0x01
#define OCFG_SRP_CAP_SHIFT			0
#define OCFG_HNP_CAP				0x02
#define OCFG_HNP_CAP_SHIFT			1
#define OCFG_OTG_VERSION			0x04
#define OCFG_OTG_VERSION_SHIFT			2

#define OCTL					0xcc04
#define OCTL_HST_SET_HNP_EN			0x01
#define OCTL_HST_SET_HNP_EN_SHIFT		0
#define OCTL_DEV_SET_HNP_EN			0x02
#define OCTL_DEV_SET_HNP_EN_SHIFT		1
#define OCTL_TERM_SEL_DL_PULSE			0x04
#define OCTL_TERM_SEL_DL_PULSE_SHIFT		2
#define OCTL_SES_REQ				0x08
#define OCTL_SES_REQ_SHIFT			3
#define OCTL_HNP_REQ				0x10
#define OCTL_HNP_REQ_SHIFT			4
#define OCTL_PRT_PWR_CTL			0x20
#define OCTL_PRT_PWR_CTL_SHIFT			5
#define OCTL_PERI_MODE				0x40
#define OCTL_PERI_MODE_SHIFT			6

#define OEVT					0xcc08
#define OEVT_ERR				0x00000001
#define OEVT_ERR_SHIFT				0
#define OEVT_SES_REQ_SCS			0x00000002
#define OEVT_SES_REQ_SCS_SHIFT			1
#define OEVT_HST_NEG_SCS			0x00000004
#define OEVT_HST_NEG_SCS_SHIFT			2
#define OEVT_B_SES_VLD_EVT			0x00000008
#define OEVT_B_SES_VLD_EVT_SHIFT		3
#define OEVT_B_DEV_VBUS_CHNG_EVNT		0x00000100
#define OEVT_B_DEV_VBUS_CHNG_EVNT_SHIFT		8
#define OEVT_B_DEV_SES_VLD_DET_EVNT		0x00000200
#define OEVT_B_DEV_SES_VLD_DET_EVNT_SHIFT	9
#define OEVT_B_DEV_HNP_CHNG_EVNT		0x00000400
#define OEVT_B_DEV_HNP_CHNG_EVNT_SHIFT		10
#define OEVT_B_DEV_B_HOST_END_EVNT		0x00000800
#define OEVT_B_DEV_B_HOST_END_EVNT_SHIFT	11
#define OEVT_A_DEV_SESS_END_DET_EVNT		0x00010000
#define OEVT_A_DEV_SESS_END_DET_EVNT_SHIFT	16
#define OEVT_A_DEV_SRP_DET_EVNT			0x00020000
#define OEVT_A_DEV_SRP_DET_EVNT_SHIFT		17
#define OEVT_A_DEV_HNP_CHNG_EVNT		0x00040000
#define OEVT_A_DEV_HNP_CHNG_EVNT_SHIFT		18
#define OEVT_A_DEV_HOST_EVNT			0x00080000
#define OEVT_A_DEV_HOST_EVNT_SHIFT		19
#define OEVT_A_DEV_B_DEV_HOST_END_EVNT		0x00100000
#define OEVT_A_DEV_B_DEV_HOST_END_EVNT_SHIFT	20
#define OEVT_A_DEV_IDLE_EVNT			0x00200000
#define OEVT_A_DEV_IDLE_EVNT_SHIFT		21
#define OEVT_HOST_ROLE_REQ_INIT_EVNT		0x00400000
#define OEVT_HOST_ROLE_REQ_INIT_EVNT_SHIFT	22
#define OEVT_HOST_ROLE_REQ_CONFIRM_EVNT		0x00800000
#define OEVT_HOST_ROLE_REQ_CONFIRM_EVNT_SHIFT	23
#define OEVT_CONN_ID_STS_CHNG_EVNT		0x01000000
#define OEVT_CONN_ID_STS_CHNG_EVNT_SHIFT	24
#define OEVT_DEV_MOD_EVNT			0x80000000
#define OEVT_DEV_MOD_EVNT_SHIFT			31

#define OEVTEN					0xcc0c

#define OEVT_ALL (OEVT_CONN_ID_STS_CHNG_EVNT | \
		OEVT_HOST_ROLE_REQ_INIT_EVNT | \
		OEVT_HOST_ROLE_REQ_CONFIRM_EVNT | \
		OEVT_A_DEV_B_DEV_HOST_END_EVNT | \
		OEVT_A_DEV_HOST_EVNT | \
		OEVT_A_DEV_HNP_CHNG_EVNT | \
		OEVT_A_DEV_SRP_DET_EVNT | \
		OEVT_A_DEV_SESS_END_DET_EVNT | \
		OEVT_B_DEV_B_HOST_END_EVNT | \
		OEVT_B_DEV_HNP_CHNG_EVNT | \
		OEVT_B_DEV_SES_VLD_DET_EVNT | \
		OEVT_B_DEV_VBUS_CHNG_EVNT)

#define OSTS					0xcc10
#define OSTS_CONN_ID_STS			0x0001
#define OSTS_CONN_ID_STS_SHIFT			0
#define OSTS_A_SES_VLD				0x0002
#define OSTS_A_SES_VLD_SHIFT			1
#define OSTS_B_SES_VLD				0x0004
#define OSTS_B_SES_VLD_SHIFT			2
#define OSTS_XHCI_PRT_PWR			0x0008
#define OSTS_XHCI_PRT_PWR_SHIFT			3
#define OSTS_PERIP_MODE				0x0010
#define OSTS_PERIP_MODE_SHIFT			4
#define OSTS_OTG_STATES				0x0f00
#define OSTS_OTG_STATE_SHIFT			8

#define DCTL					0xc704
#define DCTL_RUN_STOP				0x80000000

#define OTG_STATE_INVALID			-1
#define OTG_STATE_EXIT				14
#define OTG_STATE_TERMINATED			15

#define PERI_MODE_HOST		0
#define PERI_MODE_PERIPHERAL	1

/** The main structure to keep track of OTG driver state. */
struct dwc3_otg {

	/** OTG PHY */
	struct usb_otg otg;
	struct device *dev;
	struct dwc3 *dwc;

	void __iomem *regs;

	int main_wakeup_needed;
	struct task_struct *main_thread;
	wait_queue_head_t main_wq;

	spinlock_t lock;

	int otg_srp_reqd;

	/* Events */
	u32 otg_events;

	u32 user_events;

	/** User initiated SRP.
	 *
	 * Valid in B-device during sensing/probing. Initiates SRP signalling
	 * across the bus.
	 *
	 * Also valid as an A-device during probing. This causes the A-device to
	 * apply V-bus manually and check for a device. Can be used if the
	 * device does not support SRP and the host does not support ADP.
	 */
#define USER_SRP_EVENT			0x1
	/** User initiated HNP (only valid in B-peripheral) */
#define USER_HNP_EVENT			0x2
	/** User has ended the session (only valid in B-peripheral) */
#define USER_END_SESSION		0x4
	/** User initiated VBUS. This will cause the A-device to turn on the
	 * VBUS and see if a device will connect (only valid in A-device during
	 * sensing/probing)
	 */
#define USER_VBUS_ON			0x8
	/** User has initiated RSP */
#define USER_RSP_EVENT			0x10
	/** Host release event */
#define PCD_RECEIVED_HOST_RELEASE_EVENT	0x20
	/** Initial SRP */
#define INITIAL_SRP			0x40
	/** A-device connected event*/
#define USER_A_CONN_EVENT		0x80

	/* States */
	enum usb_otg_state prev;
	enum usb_otg_state state;

	u32 hwparams6;
	int hcd_irq;
	int irq;
	int host_started;
	int peripheral_started;
	int dev_enum;

	struct delayed_work hp_work;	/* drives HNP polling */

};

extern int usb_port_suspend(struct usb_device *udev, pm_message_t msg);
extern void usb_kick_hub_wq(struct usb_device *dev);
