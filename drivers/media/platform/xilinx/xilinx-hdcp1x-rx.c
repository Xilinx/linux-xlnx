// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx HDCP1X driver
 *
 * Copyright (C) 2022 Xilinx, Inc.
 *
 * Author: Jagadeesh Banisetti <jagadeesh.banisetti@xilin.com>
 *
 */
#include <linux/io.h>
#include <linux/xilinx-hdcp1x-cipher.h>
#include "xilinx-hdcp1x-rx.h"

/*
 * HDCP1X PORT registers, plese refer
 * 'HDCP on DisplayPort Specification Rev. 1.1' from DCP-LLC.
 */
#define XHDCP1X_PORT_OFFSET_BKSV	0x00
#define XHDCP1X_PORT_OFFSET_RO		0x05
#define XHDCP1X_PORT_OFFSET_AKSV	0x07
#define XHDCP1X_PORT_OFFSET_AN		0x0C
#define XHDCP1X_PORT_OFFSET_VH0		0x14
#define XHDCP1X_PORT_OFFSET_VH1		0x18
#define XHDCP1X_PORT_OFFSET_VH2		0x1C
#define XHDCP1X_PORT_OFFSET_VH3		0x20
#define XHDCP1X_PORT_OFFSET_VH4		0x24
#define XHDCP1X_PORT_OFFSET_BCAPS	0x28
#define XHDCP1X_PORT_OFFSET_BSTATUS	0x29
#define XHDCP1X_PORT_OFFSET_BINFO	0x2A
#define XHDCP1X_PORT_OFFSET_KSVFIFO	0x2C
#define XHDCP1X_PORT_OFFSET_AINFO	0x3B
#define XHDCP1X_PORT_OFFSET_DBG		0xC0
#define XHDCP1X_PORT_HDCP_RESET_KSV	0xD0

#define XHDCP1X_PORT_SIZE_BKSV		0x05
#define XHDCP1X_PORT_SIZE_RO		0x02
#define XHDCP1X_PORT_SIZE_AKSV		0x05
#define XHDCP1X_PORT_SIZE_AN		0x08
#define XHDCP1X_PORT_SIZE_VH0		0x04
#define XHDCP1X_PORT_SIZE_VH1		0x04
#define XHDCP1X_PORT_SIZE_VH2		0x04
#define XHDCP1X_PORT_SIZE_VH3		0x04
#define XHDCP1X_PORT_SIZE_VH4		0x04
#define XHDCP1X_PORT_SIZE_BCAPS		0x01
#define XHDCP1X_PORT_SIZE_BSTATUS	0x01
#define XHDCP1X_PORT_SIZE_BINFO		0x02
#define XHDCP1X_PORT_SIZE_KSVFIFO	0x0F
#define XHDCP1X_PORT_SIZE_AINFO		0x01
#define XHDCP1X_PORT_SIZE_HDCP_RESET_KSV 0x40
#define XHDCP1X_PORT_BIT_BSTATUS_READY		BIT(0)
#define XHDCP1X_PORT_BIT_BSTATUS_RO_AVAILABLE	BIT(1)
#define XHDCP1X_PORT_BIT_BSTATUS_LINK_FAILURE	BIT(2)
#define XHDCP1X_PORT_BIT_BSTATUS_REAUTH_REQUEST	BIT(3)
#define XHDCP1X_PORT_BIT_BCAPS_HDCP_CAPABLE	BIT(0)
#define XHDCP1X_PORT_BIT_BCAPS_REPEATER		BIT(1)
#define XHDCP1X_PORT_BIT_AINFO_REAUTH_ENABLE_IRQ	BIT(0)
#define XHDCP1X_PORT_HDCP_RESET_KSV_RST		BIT(0)
#define XHDCP1X_PORT_BINFO_DEV_CNT_MASK		GENMASK(6, 0)
#define XHDCP1X_PORT_BINFO_DEV_CNT_ERR_SHIFT	7
#define XHDCP1X_PORT_BINFO_DEPTH_ERR_SHIFT	11
#define XHDCP1X_PORT_BINFO_DEPTH_SHIFT		8
#define XHDCP1X_PORT_DEBUG_VAL			0xDEADBEEF
#define XHDCP1X_PORT_SIZE_DEBUG			4
#define XHDCP1X_PORT_SIZE_REGS_TO_RESET		14
#define XHDCP1X_LANE_COUNT_VAL_1		1
#define XHDCP1X_LANE_COUNT_VAL_2		2
#define XHDCP1X_LANE_COUNT_VAL_4		4
#define XHDCP1X_RX_CIPHER_REQUEST_RETRY		100
/*
 * states of hdcp1x state machine, please refer
 * 'HDCP on DisplayPort Specification Rev. 1.1' from DCP-LLC.
 */
enum xhdcp1x_rx_state {
	XHDCP1X_STATE_B0 = 0,
	XHDCP1X_STATE_B1 = 1,
	XHDCP1X_STATE_B2 = 2,
	XHDCP1X_STATE_B3 = 3,
	XHDCP1X_RX_NUM_STATES = 4
};

/**
 * struct xhdcp1x_rx_callbacks - Hdcp1x driver's callback handlers structure
 * @rd_handler: Handler to read hdcp data through interface driver (DP/HDMI)
 * @wr_handler: Handler to write hdcp data through interface driver (DP/HDMI)
 * @notify_handler: Handler to push hdcp notifications to interface driver
 */
struct xhdcp1x_rx_callbacks {
	int (*rd_handler)(void *interface_ref, u32 offset, u8 *buf, u32 size);
	int (*wr_handler)(void *interface_ref, u32 offset, u8 *buf, u32 size);
	void (*notify_handler)(void *interface_ref, u32 notification);
};

/**
 * struct xhdcp1x_rx - HDCP1x driver structure
 * @dev: Platfor structure
 * @handlers: Callback handlers to interface driver
 * @sm_work: state-machine worker
 * @curr_state: Current authentication state
 * @prev_state: Previous authentication state
 * @cipher: Pointer to cipher driver instance
 * @interface_ref: Pointer to interface driver instance
 * @interface_base: Pointer to interface iomem base
 * @pending_events: events that are set by ineterface driver
 * @is_repeater: flag for repeater support
 */
struct xhdcp1x_rx {
	struct device		*dev;
	struct xhdcp1x_rx_callbacks handlers;
	struct delayed_work	sm_work;
	enum xhdcp1x_rx_state	curr_state;
	enum xhdcp1x_rx_state	prev_state;
	void			*cipher;
	void			*interface_ref;
	void __iomem		*interface_base;
	u32			pending_events;
	bool			is_repeater;
};

#ifdef DEBUG
const char *state_names[XHDCP1X_RX_NUM_STATES] = {
	"STATE_B0",
	"STATE_B1",
	"STATE_B2",
	"STATE_B3"
};

/*
 * State transitions
 *    |	  B0	    B1	       B2	   B3
 *----|-----------------------------------------
 * B0 |  valid	   valid     invalid     invalid
 *    |
 * B1 |  valid     valid      valid      invalid
 *    |
 * B2 |  valid     valid      valid       valid
 *    |
 * B3 | invalid    valid     invalid     invalid
 */
static bool transition_table[XHDCP1X_RX_NUM_STATES][XHDCP1X_RX_NUM_STATES] = {
		{1, 1, 0, 0},
		{1, 1, 1, 0},
		{1, 1, 1, 1},
		{0, 0, 1, 0}
	};
#endif

static enum xhdcp1x_rx_state xhdcp1x_state_B0(void *);
static enum xhdcp1x_rx_state xhdcp1x_state_B1(void *);
static enum xhdcp1x_rx_state xhdcp1x_state_B2(void *);
static enum xhdcp1x_rx_state xhdcp1x_state_B3(void *);
static void xhdcp1x_rx_run_statemachine(struct xhdcp1x_rx *hdcp1x);
static void xhdcp1x_rx_process_aksv(struct xhdcp1x_rx *hdcp1x);
static int xhdcp1x_rx_poll_for_computations(struct xhdcp1x_rx *hdcp1x);
static void hdcp1x_rx_set_clr_bstatus(struct xhdcp1x_rx *hdcp1x, u8 mask,
				      u8 set);
static inline void xhdcp1x_buf_to_uint(u64 *dst, u8 *src, int length)
{
	int byte;

	if (length) {
		*dst = 0;
		for (byte = length; byte >= 0; byte--) {
			*dst <<= 8;
			*dst  |= src[byte];
		}
	}
}

static inline void xhdcp1x_rx_reset_port(struct xhdcp1x_rx *hdcp1x)
{
	u8 buf[XHDCP1X_PORT_SIZE_REGS_TO_RESET] = {0};

	hdcp1x->handlers.wr_handler(hdcp1x->interface_ref,
				    XHDCP1X_PORT_OFFSET_RO, buf,
				    XHDCP1X_PORT_SIZE_REGS_TO_RESET);
}

static inline void xhdcp1x_rx_reset_bstatus(struct xhdcp1x_rx *hdcp1x)
{
	u8 buf = 0;

	hdcp1x->handlers.wr_handler(hdcp1x->interface_ref,
				      XHDCP1X_PORT_OFFSET_BSTATUS, &buf, 1);
}

static inline void xhdcp1x_rx_reset_binfo(struct xhdcp1x_rx *hdcp1x)
{
	u8 buf[XHDCP1X_PORT_SIZE_BINFO] = {0};

	hdcp1x->handlers.wr_handler(hdcp1x->interface_ref,
				    XHDCP1X_PORT_OFFSET_BINFO, buf,
				    XHDCP1X_PORT_SIZE_BINFO);
}

static inline void xhdcp1x_rx_init_debug_regs(struct xhdcp1x_rx *hdcp1x)
{
	u32 buf = XHDCP1X_PORT_DEBUG_VAL;

	hdcp1x->handlers.wr_handler(hdcp1x->interface_ref,
				    XHDCP1X_PORT_OFFSET_DBG, (u8 *)&buf,
				    XHDCP1X_PORT_SIZE_DEBUG);
}

static inline void xhdcp1x_rx_read_aksv(struct xhdcp1x_rx *hdcp1x, u64 *value)
{
	u8 buf[XHDCP1X_PORT_SIZE_AKSV];

	hdcp1x->handlers.rd_handler(hdcp1x->interface_ref,
				    XHDCP1X_PORT_OFFSET_AKSV, buf,
				    XHDCP1X_PORT_SIZE_AKSV);
	xhdcp1x_buf_to_uint(value, buf, XHDCP1X_PORT_SIZE_AKSV);
}

static inline void xhdcp1x_rx_read_an(struct xhdcp1x_rx *hdcp1x, u64 *value)
{
	u8 buf[XHDCP1X_PORT_SIZE_AN] = {0};

	hdcp1x->handlers.rd_handler(hdcp1x->interface_ref,
				    XHDCP1X_PORT_OFFSET_AN, buf,
				    XHDCP1X_PORT_SIZE_AN);
	xhdcp1x_buf_to_uint(value, buf, XHDCP1X_PORT_SIZE_AN);
}

static inline void xhdcp1x_rx_reset_ksv_fifo(struct xhdcp1x_rx *hdcp1x)
{
	u32 ksv_ptr_reset;

	hdcp1x->handlers.rd_handler(hdcp1x->interface_ref,
				    XHDCP1X_PORT_HDCP_RESET_KSV,
				    (u8 *)&ksv_ptr_reset,
				    sizeof(ksv_ptr_reset));
	ksv_ptr_reset |= XHDCP1X_PORT_HDCP_RESET_KSV_RST;
	hdcp1x->handlers.wr_handler(hdcp1x->interface_ref,
				    XHDCP1X_PORT_HDCP_RESET_KSV,
				    (u8 *)&ksv_ptr_reset,
				    sizeof(ksv_ptr_reset));
	ksv_ptr_reset &= ~XHDCP1X_PORT_HDCP_RESET_KSV_RST;
	hdcp1x->handlers.wr_handler(hdcp1x->interface_ref,
				    XHDCP1X_PORT_HDCP_RESET_KSV,
				    (u8 *)&ksv_ptr_reset,
				    sizeof(ksv_ptr_reset));
}

static void xhdcp1x_sm_work_func(struct work_struct *work)
{
	struct xhdcp1x_rx *hdcp1x;

	hdcp1x = container_of(work, struct xhdcp1x_rx, sm_work.work);
	if (hdcp1x->pending_events)
		xhdcp1x_rx_run_statemachine(hdcp1x);
}

/* function pointers declarations */
static enum xhdcp1x_rx_state (*xhdcp1x_rx_state_table[])(void *) = {
	xhdcp1x_state_B0,
	xhdcp1x_state_B1,
	xhdcp1x_state_B2,
	xhdcp1x_state_B3
};

/*****************************************************************************/
/**
 * xhdcp1x_rx_init - Initialise HDCP1x driver instance
 * @dev: Platform data
 * @interface_ref: void pointer to interface driver instance
 * @interface_base: Pointer to interface iomem base
 * @is_repeater: flag for repeater support
 * This function instantiates the hdcp1x driver and initializes it.
 *
 * Return: void reference to hdcp1x driver instance on success, error otherwise
 */
void *xhdcp1x_rx_init(struct device *dev, void *interface_ref,
		      void __iomem *interface_base, bool is_repeater)
{
	struct xhdcp1x_rx *hdcp1x;

	if (!dev || !interface_ref || !interface_base)
		return ERR_PTR(-EINVAL);

	if (is_repeater) {
		dev_info(dev, "Hdcp1x repeater functionality not supported\n");
		return ERR_PTR(-EINVAL);
	}

	hdcp1x = devm_kzalloc(dev, sizeof(*hdcp1x), GFP_KERNEL);
	if (!hdcp1x)
		return ERR_PTR(-ENOMEM);

	hdcp1x->dev = dev;
	hdcp1x->interface_ref = interface_ref;
	hdcp1x->interface_base = interface_base;
	hdcp1x->is_repeater = is_repeater;

	/* cipher initializsation */
	hdcp1x->cipher = xhdcp1x_cipher_init(dev, interface_base);
	if (IS_ERR(hdcp1x->cipher))
		return hdcp1x->cipher;

	INIT_DELAYED_WORK(&hdcp1x->sm_work, xhdcp1x_sm_work_func);

	return (void *)hdcp1x;
}
EXPORT_SYMBOL_GPL(xhdcp1x_rx_init);

/**
 * xhdcp1x_rx_enable - Enable hdcp1x
 * @ref: reference to hdcp1x instance
 * @lane_count: count of active lanes in interface driver
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_rx_enable(void *ref, u8 lane_count)
{
	struct xhdcp1x_rx *hdcp1x = (struct xhdcp1x_rx *)ref;
	int ret;
	u8 buf;

	if (!hdcp1x)
		return -EINVAL;

	if (lane_count != XHDCP1X_LANE_COUNT_VAL_1 &&
	    lane_count != XHDCP1X_LANE_COUNT_VAL_2 &&
	    lane_count != XHDCP1X_LANE_COUNT_VAL_4)
		return -EINVAL;

	xhdcp1x_rx_reset_port(hdcp1x);
	xhdcp1x_rx_reset_bstatus(hdcp1x);
	xhdcp1x_rx_reset_binfo(hdcp1x);

	ret = xhdcp1x_cipher_set_num_lanes(hdcp1x->cipher, lane_count);
	if (ret)
		return ret;

	buf |= XHDCP1X_PORT_BIT_BCAPS_HDCP_CAPABLE;
	if (hdcp1x->is_repeater)
		buf |= XHDCP1X_PORT_BIT_BCAPS_REPEATER;
	hdcp1x->handlers.wr_handler(hdcp1x->interface_ref,
				    XHDCP1X_PORT_OFFSET_BCAPS, &buf,
				    XHDCP1X_PORT_SIZE_BCAPS);
	xhdcp1x_rx_init_debug_regs(hdcp1x);

	ret = xhdcp1x_cipher_enable(hdcp1x->cipher);
	return ret;
}
EXPORT_SYMBOL_GPL(xhdcp1x_rx_enable);

/**
 * xhdcp1x_rx_disable - Disable hdcp1x
 * @ref: reference to hdcp1x instance
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_rx_disable(void *ref)
{
	struct xhdcp1x_rx *hdcp1x = (struct xhdcp1x_rx *)ref;
	int ret;

	if (!hdcp1x)
		return -EINVAL;

	ret = xhdcp1x_cipher_disable(hdcp1x->cipher);
	if (ret)
		return ret;

	hdcp1x->curr_state = XHDCP1X_STATE_B0;
	hdcp1x->prev_state = XHDCP1X_STATE_B0;
	hdcp1x->pending_events = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_rx_disable);

/**
 * xhdcp1x_rx_set_callback - register callback handlers of interface driver
 * @ref: reference to hdcp1x instance
 * @handler_type: type of the handler
 * @handler: pointer to the callback handler
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_rx_set_callback(void *ref, u32 handler_type, void *handler)
{
	struct xhdcp1x_rx *hdcp1x = (struct xhdcp1x_rx *)ref;

	if (!hdcp1x || !handler)
		return -EINVAL;

	switch (handler_type) {
	case XHDCP1X_RX_RD_HANDLER:
		hdcp1x->handlers.rd_handler = handler;
		break;
	case XHDCP1X_RX_WR_HANDLER:
		hdcp1x->handlers.wr_handler = handler;
		break;
	case XHDCP1X_RX_NOTIFICATION_HANDLER:
		hdcp1x->handlers.notify_handler = handler;
		break;
	default:
		dev_info(hdcp1x->dev, "wrong handler type\n");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_rx_set_callback);

/**
 * xhdcp1x_rx_handle_intr - Handles the hdcp interrupts
 * @ref: reference to hdcp1x instance
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_rx_handle_intr(void *ref)
{
	struct xhdcp1x_rx *hdcp1x = (struct xhdcp1x_rx *)ref;
	u32 interrupts;

	if (!hdcp1x)
		return -EINVAL;

	if (xhdcp1x_cipher_get_interrupts(hdcp1x->cipher, &interrupts))
		return -EIO;
	if (interrupts)
		xhdcp1x_rx_push_events(hdcp1x, XHDCP1X_RX_CIPHER_EVENT_RCVD);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_rx_handle_intr);

/**
 * xhdcp1x_rx_push_events - Pushes events from interface driver to hdcp driver
 * @ref: reference to hdcp1x instance
 * @events: events that are pushed from interface driver
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_rx_push_events(void *ref, u32 events)
{
	struct xhdcp1x_rx *hdcp1x = (struct xhdcp1x_rx *)ref;

	if (!hdcp1x)
		return -EINVAL;

	if (events) {
		hdcp1x->pending_events |= events;
		schedule_delayed_work(&hdcp1x->sm_work, 0);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_rx_push_events);

/**
 * xhdcp1x_rx_set_keyselect - Selects the keyvector form key management block
 * @ref: reference to hdcp1x instance
 * @keyselect: keyvector selection
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_rx_set_keyselect(void *ref, u8 keyselect)
{
	struct xhdcp1x_rx *hdcp1x = (struct xhdcp1x_rx *)ref;

	if (!hdcp1x)
		return -EINVAL;

	return xhdcp1x_cipher_set_keyselect(hdcp1x->cipher, keyselect);
}
EXPORT_SYMBOL_GPL(xhdcp1x_rx_set_keyselect);

/**
 * xhdcp1x_rx_load_bksv - loads the local ksv to hdcp port
 * @ref: reference to hdcp1x instance
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_rx_load_bksv(void *ref)
{
	struct xhdcp1x_rx *hdcp1x = (struct xhdcp1x_rx *)ref;
	u8 buf[XHDCP1X_PORT_SIZE_BKSV];

	if (!hdcp1x)
		return -EINVAL;

	if (xhdcp1x_cipher_load_bksv(hdcp1x->cipher, buf))
		return -EAGAIN;

	hdcp1x->handlers.wr_handler(hdcp1x->interface_ref,
				    XHDCP1X_PORT_OFFSET_BKSV, buf,
				    XHDCP1X_PORT_SIZE_BKSV);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_rx_load_bksv);

/********************** Static function definitions ***************************/
static void xhdcp1x_rx_run_statemachine(struct xhdcp1x_rx *hdcp1x)
{
	enum xhdcp1x_rx_state new_state;

	do {
#ifdef DEBUG
		if (!transition_table[hdcp1x->prev_state][hdcp1x->curr_state])
			dev_info(hdcp1x->dev,
				 "Invalid HDCP1X State transition %s -> %s\n",
				 state_names[hdcp1x->prev_state],
				 state_names[hdcp1x->curr_state]);
#endif

		new_state = xhdcp1x_rx_state_table[hdcp1x->curr_state](hdcp1x);

		hdcp1x->prev_state = hdcp1x->curr_state;
		hdcp1x->curr_state = new_state;
	} while (hdcp1x->prev_state != hdcp1x->curr_state);
}

static enum xhdcp1x_rx_state xhdcp1x_state_B0(void *instance)
{
	struct xhdcp1x_rx *hdcp1x = (struct xhdcp1x_rx *)instance;

	/* Nothing to be done here, just wait for the Aksv */
	if (hdcp1x->pending_events & XHDCP1X_RX_AKSV_RCVD)
		return XHDCP1X_STATE_B1;
	else
		return XHDCP1X_STATE_B0;
}

static enum xhdcp1x_rx_state xhdcp1x_state_B1(void *instance)
{
	struct xhdcp1x_rx *hdcp1x = (struct xhdcp1x_rx *)instance;

	if (hdcp1x->pending_events & XHDCP1X_RX_AKSV_RCVD) {
		xhdcp1x_rx_process_aksv(hdcp1x);
		xhdcp1x_rx_poll_for_computations(hdcp1x);
		hdcp1x->pending_events &= ~XHDCP1X_RX_AKSV_RCVD;
		return XHDCP1X_STATE_B2;
	} else {
		return XHDCP1X_STATE_B1;
	}
}

/* TODO: Need to cancel the workqueue of hdcp1x before disabling it */
static enum xhdcp1x_rx_state xhdcp1x_state_B2(void *instance)
{
	struct xhdcp1x_rx *hdcp1x = (struct xhdcp1x_rx *)instance;

	if (hdcp1x->pending_events & XHDCP1X_RX_AKSV_RCVD)
		return XHDCP1X_STATE_B1;

	if (hdcp1x->pending_events & XHDCP1X_RX_RO_PRIME_READ_DONE) {
		hdcp1x_rx_set_clr_bstatus(hdcp1x,
					  XHDCP1X_PORT_BIT_BSTATUS_RO_AVAILABLE,
					  0);
		hdcp1x->pending_events &= ~XHDCP1X_RX_RO_PRIME_READ_DONE;
		return XHDCP1X_STATE_B3;
	}

	if (hdcp1x->pending_events & XHDCP1X_RX_CIPHER_EVENT_RCVD) {
		if (xhdcp1x_cipher_is_linkintegrity_failed(hdcp1x->cipher)) {
			hdcp1x_rx_set_clr_bstatus(hdcp1x,
						  XHDCP1X_PORT_BIT_BSTATUS_LINK_FAILURE,
						  1);
			if (hdcp1x->handlers.notify_handler)
				hdcp1x->handlers.notify_handler(hdcp1x->interface_ref,
								XHDCP1X_RX_NOTIFY_SET_CP_IRQ);
			xhdcp1x_cipher_disable(hdcp1x->cipher);
			xhdcp1x_cipher_reset(hdcp1x->cipher);
			xhdcp1x_cipher_enable(hdcp1x->cipher);

			return XHDCP1X_STATE_B1;
		}
	}

	if (hdcp1x->prev_state == XHDCP1X_STATE_B3 &&
	    hdcp1x->handlers.notify_handler)
		hdcp1x->handlers.notify_handler(hdcp1x->interface_ref,
						XHDCP1X_RX_NOTIFY_AUTHENTICATED);

	return XHDCP1X_STATE_B2;
}

static enum xhdcp1x_rx_state xhdcp1x_state_B3(void *instance)
{
	struct xhdcp1x_rx *hdcp1x = (struct xhdcp1x_rx *)instance;

	/*
	 * For DP, the link integrity will be checked in cipher and
	 * an interrupt will be raised if the integrity is failed. Here
	 * it just required to enable the interrupts for link integrity
	 * and go to state_B2(Authenticated)
	 */
	xhdcp1x_cipher_set_link_state_check(hdcp1x->cipher, true);

	return XHDCP1X_STATE_B2;
}

static void xhdcp1x_rx_process_aksv(struct xhdcp1x_rx *hdcp1x)
{
	u64 value;

	xhdcp1x_rx_reset_bstatus(hdcp1x);
	xhdcp1x_rx_read_aksv(hdcp1x, &value);
	if (xhdcp1x_cipher_set_remoteksv(hdcp1x->cipher, value))
		dev_dbg(hdcp1x->dev, "Failed to configure Aksv into cipher\n");
	xhdcp1x_rx_read_an(hdcp1x, &value);

	/* Load the cipher B registers with An */
	if (xhdcp1x_cipher_set_b(hdcp1x->cipher, value, hdcp1x->is_repeater))
		dev_dbg(hdcp1x->dev, "Failed to configure An into cipher\n");
}

static int xhdcp1x_rx_poll_for_computations(struct xhdcp1x_rx *hdcp1x)
{
	u16 ro;
	u16 retry = XHDCP1X_RX_CIPHER_REQUEST_RETRY;
	u8 buf[4];

	while (!xhdcp1x_cipher_is_request_complete(hdcp1x->cipher) && retry--)
		;

	if (!retry)
		return -EAGAIN;

	if (xhdcp1x_cipher_get_ro(hdcp1x->cipher, &ro))
		return -EIO;

	memcpy(buf, &ro, XHDCP1X_PORT_SIZE_RO);
	hdcp1x->handlers.wr_handler(hdcp1x->interface_ref,
				    XHDCP1X_PORT_OFFSET_RO, buf, 2);

	/* Reset the KSV FIFO read pointer to 0x6802C */
	xhdcp1x_rx_reset_ksv_fifo(hdcp1x);

	/* Update the Bstatus to indicate Ro' available */
	hdcp1x_rx_set_clr_bstatus(hdcp1x, XHDCP1X_PORT_BIT_BSTATUS_RO_AVAILABLE,
				  1);
	if (hdcp1x->handlers.notify_handler)
		hdcp1x->handlers.notify_handler(hdcp1x->interface_ref,
						XHDCP1X_RX_NOTIFY_SET_CP_IRQ);

	return 0;
}

static void hdcp1x_rx_set_clr_bstatus(struct xhdcp1x_rx *hdcp1x, u8 mask,
				      u8 set)
{
	u8 val;

	hdcp1x->handlers.rd_handler(hdcp1x->interface_ref,
				    XHDCP1X_PORT_OFFSET_BSTATUS,
				    &val, XHDCP1X_PORT_SIZE_BSTATUS);
	if (set)
		val |= mask;
	else
		val &= ~mask;
	hdcp1x->handlers.wr_handler(hdcp1x->interface_ref,
				    XHDCP1X_PORT_OFFSET_BSTATUS,
				    &val, XHDCP1X_PORT_SIZE_BSTATUS);
}
