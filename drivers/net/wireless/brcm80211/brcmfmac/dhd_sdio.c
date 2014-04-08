/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/printk.h>
#include <linux/pci_ids.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/semaphore.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/bcma/bcma.h>
#include <linux/debugfs.h>
#include <linux/vmalloc.h>
#include <linux/platform_data/brcmfmac-sdio.h>
#include <asm/unaligned.h>
#include <defs.h>
#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include <brcm_hw_ids.h>
#include <soc.h>
#include "sdio_host.h"
#include "sdio_chip.h"

#define DCMD_RESP_TIMEOUT  2000	/* In milli second */

#ifdef DEBUG

#define BRCMF_TRAP_INFO_SIZE	80

#define CBUF_LEN	(128)

/* Device console log buffer state */
#define CONSOLE_BUFFER_MAX	2024

struct rte_log_le {
	__le32 buf;		/* Can't be pointer on (64-bit) hosts */
	__le32 buf_size;
	__le32 idx;
	char *_buf_compat;	/* Redundant pointer for backward compat. */
};

struct rte_console {
	/* Virtual UART
	 * When there is no UART (e.g. Quickturn),
	 * the host should write a complete
	 * input line directly into cbuf and then write
	 * the length into vcons_in.
	 * This may also be used when there is a real UART
	 * (at risk of conflicting with
	 * the real UART).  vcons_out is currently unused.
	 */
	uint vcons_in;
	uint vcons_out;

	/* Output (logging) buffer
	 * Console output is written to a ring buffer log_buf at index log_idx.
	 * The host may read the output when it sees log_idx advance.
	 * Output will be lost if the output wraps around faster than the host
	 * polls.
	 */
	struct rte_log_le log_le;

	/* Console input line buffer
	 * Characters are read one at a time into cbuf
	 * until <CR> is received, then
	 * the buffer is processed as a command line.
	 * Also used for virtual UART.
	 */
	uint cbuf_idx;
	char cbuf[CBUF_LEN];
};

#endif				/* DEBUG */
#include <chipcommon.h>

#include "dhd_bus.h"
#include "dhd_dbg.h"
#include "tracepoint.h"

#define TXQLEN		2048	/* bulk tx queue length */
#define TXHI		(TXQLEN - 256)	/* turn on flow control above TXHI */
#define TXLOW		(TXHI - 256)	/* turn off flow control below TXLOW */
#define PRIOMASK	7

#define TXRETRIES	2	/* # of retries for tx frames */

#define BRCMF_RXBOUND	50	/* Default for max rx frames in
				 one scheduling */

#define BRCMF_TXBOUND	20	/* Default for max tx frames in
				 one scheduling */

#define BRCMF_TXMINMAX	1	/* Max tx frames if rx still pending */

#define MEMBLOCK	2048	/* Block size used for downloading
				 of dongle image */
#define MAX_DATA_BUF	(32 * 1024)	/* Must be large enough to hold
				 biggest possible glom */

#define BRCMF_FIRSTREAD	(1 << 6)


/* SBSDIO_DEVICE_CTL */

/* 1: device will assert busy signal when receiving CMD53 */
#define SBSDIO_DEVCTL_SETBUSY		0x01
/* 1: assertion of sdio interrupt is synchronous to the sdio clock */
#define SBSDIO_DEVCTL_SPI_INTR_SYNC	0x02
/* 1: mask all interrupts to host except the chipActive (rev 8) */
#define SBSDIO_DEVCTL_CA_INT_ONLY	0x04
/* 1: isolate internal sdio signals, put external pads in tri-state; requires
 * sdio bus power cycle to clear (rev 9) */
#define SBSDIO_DEVCTL_PADS_ISO		0x08
/* Force SD->SB reset mapping (rev 11) */
#define SBSDIO_DEVCTL_SB_RST_CTL	0x30
/*   Determined by CoreControl bit */
#define SBSDIO_DEVCTL_RST_CORECTL	0x00
/*   Force backplane reset */
#define SBSDIO_DEVCTL_RST_BPRESET	0x10
/*   Force no backplane reset */
#define SBSDIO_DEVCTL_RST_NOBPRESET	0x20

/* direct(mapped) cis space */

/* MAPPED common CIS address */
#define SBSDIO_CIS_BASE_COMMON		0x1000
/* maximum bytes in one CIS */
#define SBSDIO_CIS_SIZE_LIMIT		0x200
/* cis offset addr is < 17 bits */
#define SBSDIO_CIS_OFT_ADDR_MASK	0x1FFFF

/* manfid tuple length, include tuple, link bytes */
#define SBSDIO_CIS_MANFID_TUPLE_LEN	6

/* intstatus */
#define I_SMB_SW0	(1 << 0)	/* To SB Mail S/W interrupt 0 */
#define I_SMB_SW1	(1 << 1)	/* To SB Mail S/W interrupt 1 */
#define I_SMB_SW2	(1 << 2)	/* To SB Mail S/W interrupt 2 */
#define I_SMB_SW3	(1 << 3)	/* To SB Mail S/W interrupt 3 */
#define I_SMB_SW_MASK	0x0000000f	/* To SB Mail S/W interrupts mask */
#define I_SMB_SW_SHIFT	0	/* To SB Mail S/W interrupts shift */
#define I_HMB_SW0	(1 << 4)	/* To Host Mail S/W interrupt 0 */
#define I_HMB_SW1	(1 << 5)	/* To Host Mail S/W interrupt 1 */
#define I_HMB_SW2	(1 << 6)	/* To Host Mail S/W interrupt 2 */
#define I_HMB_SW3	(1 << 7)	/* To Host Mail S/W interrupt 3 */
#define I_HMB_SW_MASK	0x000000f0	/* To Host Mail S/W interrupts mask */
#define I_HMB_SW_SHIFT	4	/* To Host Mail S/W interrupts shift */
#define I_WR_OOSYNC	(1 << 8)	/* Write Frame Out Of Sync */
#define I_RD_OOSYNC	(1 << 9)	/* Read Frame Out Of Sync */
#define	I_PC		(1 << 10)	/* descriptor error */
#define	I_PD		(1 << 11)	/* data error */
#define	I_DE		(1 << 12)	/* Descriptor protocol Error */
#define	I_RU		(1 << 13)	/* Receive descriptor Underflow */
#define	I_RO		(1 << 14)	/* Receive fifo Overflow */
#define	I_XU		(1 << 15)	/* Transmit fifo Underflow */
#define	I_RI		(1 << 16)	/* Receive Interrupt */
#define I_BUSPWR	(1 << 17)	/* SDIO Bus Power Change (rev 9) */
#define I_XMTDATA_AVAIL (1 << 23)	/* bits in fifo */
#define	I_XI		(1 << 24)	/* Transmit Interrupt */
#define I_RF_TERM	(1 << 25)	/* Read Frame Terminate */
#define I_WF_TERM	(1 << 26)	/* Write Frame Terminate */
#define I_PCMCIA_XU	(1 << 27)	/* PCMCIA Transmit FIFO Underflow */
#define I_SBINT		(1 << 28)	/* sbintstatus Interrupt */
#define I_CHIPACTIVE	(1 << 29)	/* chip from doze to active state */
#define I_SRESET	(1 << 30)	/* CCCR RES interrupt */
#define I_IOE2		(1U << 31)	/* CCCR IOE2 Bit Changed */
#define	I_ERRORS	(I_PC | I_PD | I_DE | I_RU | I_RO | I_XU)
#define I_DMA		(I_RI | I_XI | I_ERRORS)

/* corecontrol */
#define CC_CISRDY		(1 << 0)	/* CIS Ready */
#define CC_BPRESEN		(1 << 1)	/* CCCR RES signal */
#define CC_F2RDY		(1 << 2)	/* set CCCR IOR2 bit */
#define CC_CLRPADSISO		(1 << 3)	/* clear SDIO pads isolation */
#define CC_XMTDATAAVAIL_MODE	(1 << 4)
#define CC_XMTDATAAVAIL_CTRL	(1 << 5)

/* SDA_FRAMECTRL */
#define SFC_RF_TERM	(1 << 0)	/* Read Frame Terminate */
#define SFC_WF_TERM	(1 << 1)	/* Write Frame Terminate */
#define SFC_CRC4WOOS	(1 << 2)	/* CRC error for write out of sync */
#define SFC_ABORTALL	(1 << 3)	/* Abort all in-progress frames */

/*
 * Software allocation of To SB Mailbox resources
 */

/* tosbmailbox bits corresponding to intstatus bits */
#define SMB_NAK		(1 << 0)	/* Frame NAK */
#define SMB_INT_ACK	(1 << 1)	/* Host Interrupt ACK */
#define SMB_USE_OOB	(1 << 2)	/* Use OOB Wakeup */
#define SMB_DEV_INT	(1 << 3)	/* Miscellaneous Interrupt */

/* tosbmailboxdata */
#define SMB_DATA_VERSION_SHIFT	16	/* host protocol version */

/*
 * Software allocation of To Host Mailbox resources
 */

/* intstatus bits */
#define I_HMB_FC_STATE	I_HMB_SW0	/* Flow Control State */
#define I_HMB_FC_CHANGE	I_HMB_SW1	/* Flow Control State Changed */
#define I_HMB_FRAME_IND	I_HMB_SW2	/* Frame Indication */
#define I_HMB_HOST_INT	I_HMB_SW3	/* Miscellaneous Interrupt */

/* tohostmailboxdata */
#define HMB_DATA_NAKHANDLED	1	/* retransmit NAK'd frame */
#define HMB_DATA_DEVREADY	2	/* talk to host after enable */
#define HMB_DATA_FC		4	/* per prio flowcontrol update flag */
#define HMB_DATA_FWREADY	8	/* fw ready for protocol activity */

#define HMB_DATA_FCDATA_MASK	0xff000000
#define HMB_DATA_FCDATA_SHIFT	24

#define HMB_DATA_VERSION_MASK	0x00ff0000
#define HMB_DATA_VERSION_SHIFT	16

/*
 * Software-defined protocol header
 */

/* Current protocol version */
#define SDPCM_PROT_VERSION	4

/*
 * Shared structure between dongle and the host.
 * The structure contains pointers to trap or assert information.
 */
#define SDPCM_SHARED_VERSION       0x0003
#define SDPCM_SHARED_VERSION_MASK  0x00FF
#define SDPCM_SHARED_ASSERT_BUILT  0x0100
#define SDPCM_SHARED_ASSERT        0x0200
#define SDPCM_SHARED_TRAP          0x0400

/* Space for header read, limit for data packets */
#define MAX_HDR_READ	(1 << 6)
#define MAX_RX_DATASZ	2048

/* Maximum milliseconds to wait for F2 to come up */
#define BRCMF_WAIT_F2RDY	3000

/* Bump up limit on waiting for HT to account for first startup;
 * if the image is doing a CRC calculation before programming the PMU
 * for HT availability, it could take a couple hundred ms more, so
 * max out at a 1 second (1000000us).
 */
#undef PMU_MAX_TRANSITION_DLY
#define PMU_MAX_TRANSITION_DLY 1000000

/* Value for ChipClockCSR during initial setup */
#define BRCMF_INIT_CLKCTL1	(SBSDIO_FORCE_HW_CLKREQ_OFF |	\
					SBSDIO_ALP_AVAIL_REQ)

/* Flags for SDH calls */
#define F2SYNC	(SDIO_REQ_4BYTE | SDIO_REQ_FIXED)

#define BRCMF_IDLE_IMMEDIATE	(-1)	/* Enter idle immediately */
#define BRCMF_IDLE_ACTIVE	0	/* Do not request any SD clock change
					 * when idle
					 */
#define BRCMF_IDLE_INTERVAL	1

#define KSO_WAIT_US 50
#define MAX_KSO_ATTEMPTS (PMU_MAX_TRANSITION_DLY/KSO_WAIT_US)

/*
 * Conversion of 802.1D priority to precedence level
 */
static uint prio2prec(u32 prio)
{
	return (prio == PRIO_8021D_NONE || prio == PRIO_8021D_BE) ?
	       (prio^2) : prio;
}

#ifdef DEBUG
/* Device console log buffer state */
struct brcmf_console {
	uint count;		/* Poll interval msec counter */
	uint log_addr;		/* Log struct address (fixed) */
	struct rte_log_le log_le;	/* Log struct (host copy) */
	uint bufsize;		/* Size of log buffer */
	u8 *buf;		/* Log buffer (host copy) */
	uint last;		/* Last buffer read index */
};

struct brcmf_trap_info {
	__le32		type;
	__le32		epc;
	__le32		cpsr;
	__le32		spsr;
	__le32		r0;	/* a1 */
	__le32		r1;	/* a2 */
	__le32		r2;	/* a3 */
	__le32		r3;	/* a4 */
	__le32		r4;	/* v1 */
	__le32		r5;	/* v2 */
	__le32		r6;	/* v3 */
	__le32		r7;	/* v4 */
	__le32		r8;	/* v5 */
	__le32		r9;	/* sb/v6 */
	__le32		r10;	/* sl/v7 */
	__le32		r11;	/* fp/v8 */
	__le32		r12;	/* ip */
	__le32		r13;	/* sp */
	__le32		r14;	/* lr */
	__le32		pc;	/* r15 */
};
#endif				/* DEBUG */

struct sdpcm_shared {
	u32 flags;
	u32 trap_addr;
	u32 assert_exp_addr;
	u32 assert_file_addr;
	u32 assert_line;
	u32 console_addr;	/* Address of struct rte_console */
	u32 msgtrace_addr;
	u8 tag[32];
	u32 brpt_addr;
};

struct sdpcm_shared_le {
	__le32 flags;
	__le32 trap_addr;
	__le32 assert_exp_addr;
	__le32 assert_file_addr;
	__le32 assert_line;
	__le32 console_addr;	/* Address of struct rte_console */
	__le32 msgtrace_addr;
	u8 tag[32];
	__le32 brpt_addr;
};

/* dongle SDIO bus specific header info */
struct brcmf_sdio_hdrinfo {
	u8 seq_num;
	u8 channel;
	u16 len;
	u16 len_left;
	u16 len_nxtfrm;
	u8 dat_offset;
};

/* misc chip info needed by some of the routines */
/* Private data for SDIO bus interaction */
struct brcmf_sdio {
	struct brcmf_sdio_dev *sdiodev;	/* sdio device handler */
	struct chip_info *ci;	/* Chip info struct */
	char *vars;		/* Variables (from CIS and/or other) */
	uint varsz;		/* Size of variables buffer */

	u32 ramsize;		/* Size of RAM in SOCRAM (bytes) */

	u32 hostintmask;	/* Copy of Host Interrupt Mask */
	atomic_t intstatus;	/* Intstatus bits (events) pending */
	atomic_t fcstate;	/* State of dongle flow-control */

	uint blocksize;		/* Block size of SDIO transfers */
	uint roundup;		/* Max roundup limit */

	struct pktq txq;	/* Queue length used for flow-control */
	u8 flowcontrol;	/* per prio flow control bitmask */
	u8 tx_seq;		/* Transmit sequence number (next) */
	u8 tx_max;		/* Maximum transmit sequence allowed */

	u8 hdrbuf[MAX_HDR_READ + BRCMF_SDALIGN];
	u8 *rxhdr;		/* Header of current rx frame (in hdrbuf) */
	u8 rx_seq;		/* Receive sequence number (expected) */
	struct brcmf_sdio_hdrinfo cur_read;
				/* info of current read frame */
	bool rxskip;		/* Skip receive (awaiting NAK ACK) */
	bool rxpending;		/* Data frame pending in dongle */

	uint rxbound;		/* Rx frames to read before resched */
	uint txbound;		/* Tx frames to send before resched */
	uint txminmax;

	struct sk_buff *glomd;	/* Packet containing glomming descriptor */
	struct sk_buff_head glom; /* Packet list for glommed superframe */
	uint glomerr;		/* Glom packet read errors */

	u8 *rxbuf;		/* Buffer for receiving control packets */
	uint rxblen;		/* Allocated length of rxbuf */
	u8 *rxctl;		/* Aligned pointer into rxbuf */
	u8 *rxctl_orig;		/* pointer for freeing rxctl */
	uint rxlen;		/* Length of valid data in buffer */
	spinlock_t rxctl_lock;	/* protection lock for ctrl frame resources */

	u8 sdpcm_ver;	/* Bus protocol reported by dongle */

	bool intr;		/* Use interrupts */
	bool poll;		/* Use polling */
	atomic_t ipend;		/* Device interrupt is pending */
	uint spurious;		/* Count of spurious interrupts */
	uint pollrate;		/* Ticks between device polls */
	uint polltick;		/* Tick counter */

#ifdef DEBUG
	uint console_interval;
	struct brcmf_console console;	/* Console output polling support */
	uint console_addr;	/* Console address from shared struct */
#endif				/* DEBUG */

	uint clkstate;		/* State of sd and backplane clock(s) */
	bool activity;		/* Activity flag for clock down */
	s32 idletime;		/* Control for activity timeout */
	s32 idlecount;	/* Activity timeout counter */
	s32 idleclock;	/* How to set bus driver when idle */
	bool rxflow_mode;	/* Rx flow control mode */
	bool rxflow;		/* Is rx flow control on */
	bool alp_only;		/* Don't use HT clock (ALP only) */

	u8 *ctrl_frame_buf;
	u32 ctrl_frame_len;
	bool ctrl_frame_stat;

	spinlock_t txqlock;
	wait_queue_head_t ctrl_wait;
	wait_queue_head_t dcmd_resp_wait;

	struct timer_list timer;
	struct completion watchdog_wait;
	struct task_struct *watchdog_tsk;
	bool wd_timer_valid;
	uint save_ms;

	struct workqueue_struct *brcmf_wq;
	struct work_struct datawork;
	atomic_t dpc_tskcnt;

	bool txoff;		/* Transmit flow-controlled */
	struct brcmf_sdio_count sdcnt;
	bool sr_enabled; /* SaveRestore enabled */
	bool sleeping; /* SDIO bus sleeping */

	u8 tx_hdrlen;		/* sdio bus header length for tx packet */
};

/* clkstate */
#define CLK_NONE	0
#define CLK_SDONLY	1
#define CLK_PENDING	2
#define CLK_AVAIL	3

#ifdef DEBUG
static int qcount[NUMPRIO];
#endif				/* DEBUG */

#define DEFAULT_SDIO_DRIVE_STRENGTH	6	/* in milliamps */

#define RETRYCHAN(chan) ((chan) == SDPCM_EVENT_CHANNEL)

/* Retry count for register access failures */
static const uint retry_limit = 2;

/* Limit on rounding up frames */
static const uint max_roundup = 512;

#define ALIGNMENT  4

enum brcmf_sdio_frmtype {
	BRCMF_SDIO_FT_NORMAL,
	BRCMF_SDIO_FT_SUPER,
	BRCMF_SDIO_FT_SUB,
};

#define BCM43143_FIRMWARE_NAME		"brcm/brcmfmac43143-sdio.bin"
#define BCM43143_NVRAM_NAME		"brcm/brcmfmac43143-sdio.txt"
#define BCM43241B0_FIRMWARE_NAME	"brcm/brcmfmac43241b0-sdio.bin"
#define BCM43241B0_NVRAM_NAME		"brcm/brcmfmac43241b0-sdio.txt"
#define BCM43241B4_FIRMWARE_NAME	"brcm/brcmfmac43241b4-sdio.bin"
#define BCM43241B4_NVRAM_NAME		"brcm/brcmfmac43241b4-sdio.txt"
#define BCM4329_FIRMWARE_NAME		"brcm/brcmfmac4329-sdio.bin"
#define BCM4329_NVRAM_NAME		"brcm/brcmfmac4329-sdio.txt"
#define BCM4330_FIRMWARE_NAME		"brcm/brcmfmac4330-sdio.bin"
#define BCM4330_NVRAM_NAME		"brcm/brcmfmac4330-sdio.txt"
#define BCM4334_FIRMWARE_NAME		"brcm/brcmfmac4334-sdio.bin"
#define BCM4334_NVRAM_NAME		"brcm/brcmfmac4334-sdio.txt"
#define BCM4335_FIRMWARE_NAME		"brcm/brcmfmac4335-sdio.bin"
#define BCM4335_NVRAM_NAME		"brcm/brcmfmac4335-sdio.txt"

MODULE_FIRMWARE(BCM43143_FIRMWARE_NAME);
MODULE_FIRMWARE(BCM43143_NVRAM_NAME);
MODULE_FIRMWARE(BCM43241B0_FIRMWARE_NAME);
MODULE_FIRMWARE(BCM43241B0_NVRAM_NAME);
MODULE_FIRMWARE(BCM43241B4_FIRMWARE_NAME);
MODULE_FIRMWARE(BCM43241B4_NVRAM_NAME);
MODULE_FIRMWARE(BCM4329_FIRMWARE_NAME);
MODULE_FIRMWARE(BCM4329_NVRAM_NAME);
MODULE_FIRMWARE(BCM4330_FIRMWARE_NAME);
MODULE_FIRMWARE(BCM4330_NVRAM_NAME);
MODULE_FIRMWARE(BCM4334_FIRMWARE_NAME);
MODULE_FIRMWARE(BCM4334_NVRAM_NAME);
MODULE_FIRMWARE(BCM4335_FIRMWARE_NAME);
MODULE_FIRMWARE(BCM4335_NVRAM_NAME);

struct brcmf_firmware_names {
	u32 chipid;
	u32 revmsk;
	const char *bin;
	const char *nv;
};

enum brcmf_firmware_type {
	BRCMF_FIRMWARE_BIN,
	BRCMF_FIRMWARE_NVRAM
};

#define BRCMF_FIRMWARE_NVRAM(name) \
	name ## _FIRMWARE_NAME, name ## _NVRAM_NAME

static const struct brcmf_firmware_names brcmf_fwname_data[] = {
	{ BCM43143_CHIP_ID, 0xFFFFFFFF, BRCMF_FIRMWARE_NVRAM(BCM43143) },
	{ BCM43241_CHIP_ID, 0x0000001F, BRCMF_FIRMWARE_NVRAM(BCM43241B0) },
	{ BCM43241_CHIP_ID, 0xFFFFFFE0, BRCMF_FIRMWARE_NVRAM(BCM43241B4) },
	{ BCM4329_CHIP_ID, 0xFFFFFFFF, BRCMF_FIRMWARE_NVRAM(BCM4329) },
	{ BCM4330_CHIP_ID, 0xFFFFFFFF, BRCMF_FIRMWARE_NVRAM(BCM4330) },
	{ BCM4334_CHIP_ID, 0xFFFFFFFF, BRCMF_FIRMWARE_NVRAM(BCM4334) },
	{ BCM4335_CHIP_ID, 0xFFFFFFFF, BRCMF_FIRMWARE_NVRAM(BCM4335) }
};


static const struct firmware *brcmf_sdbrcm_get_fw(struct brcmf_sdio *bus,
						  enum brcmf_firmware_type type)
{
	const struct firmware *fw;
	const char *name;
	int err, i;

	for (i = 0; i < ARRAY_SIZE(brcmf_fwname_data); i++) {
		if (brcmf_fwname_data[i].chipid == bus->ci->chip &&
		    brcmf_fwname_data[i].revmsk & BIT(bus->ci->chiprev)) {
			switch (type) {
			case BRCMF_FIRMWARE_BIN:
				name = brcmf_fwname_data[i].bin;
				break;
			case BRCMF_FIRMWARE_NVRAM:
				name = brcmf_fwname_data[i].nv;
				break;
			default:
				brcmf_err("invalid firmware type (%d)\n", type);
				return NULL;
			}
			goto found;
		}
	}
	brcmf_err("Unknown chipid %d [%d]\n",
		  bus->ci->chip, bus->ci->chiprev);
	return NULL;

found:
	err = request_firmware(&fw, name, &bus->sdiodev->func[2]->dev);
	if ((err) || (!fw)) {
		brcmf_err("fail to request firmware %s (%d)\n", name, err);
		return NULL;
	}

	return fw;
}

static void pkt_align(struct sk_buff *p, int len, int align)
{
	uint datalign;
	datalign = (unsigned long)(p->data);
	datalign = roundup(datalign, (align)) - datalign;
	if (datalign)
		skb_pull(p, datalign);
	__skb_trim(p, len);
}

/* To check if there's window offered */
static bool data_ok(struct brcmf_sdio *bus)
{
	return (u8)(bus->tx_max - bus->tx_seq) != 0 &&
	       ((u8)(bus->tx_max - bus->tx_seq) & 0x80) == 0;
}

/*
 * Reads a register in the SDIO hardware block. This block occupies a series of
 * adresses on the 32 bit backplane bus.
 */
static int
r_sdreg32(struct brcmf_sdio *bus, u32 *regvar, u32 offset)
{
	u8 idx = brcmf_sdio_chip_getinfidx(bus->ci, BCMA_CORE_SDIO_DEV);
	int ret;

	*regvar = brcmf_sdio_regrl(bus->sdiodev,
				   bus->ci->c_inf[idx].base + offset, &ret);

	return ret;
}

static int
w_sdreg32(struct brcmf_sdio *bus, u32 regval, u32 reg_offset)
{
	u8 idx = brcmf_sdio_chip_getinfidx(bus->ci, BCMA_CORE_SDIO_DEV);
	int ret;

	brcmf_sdio_regwl(bus->sdiodev,
			 bus->ci->c_inf[idx].base + reg_offset,
			 regval, &ret);

	return ret;
}

static int
brcmf_sdbrcm_kso_control(struct brcmf_sdio *bus, bool on)
{
	u8 wr_val = 0, rd_val, cmp_val, bmask;
	int err = 0;
	int try_cnt = 0;

	brcmf_dbg(TRACE, "Enter\n");

	wr_val = (on << SBSDIO_FUNC1_SLEEPCSR_KSO_SHIFT);
	/* 1st KSO write goes to AOS wake up core if device is asleep  */
	brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR,
			 wr_val, &err);
	if (err) {
		brcmf_err("SDIO_AOS KSO write error: %d\n", err);
		return err;
	}

	if (on) {
		/* device WAKEUP through KSO:
		 * write bit 0 & read back until
		 * both bits 0 (kso bit) & 1 (dev on status) are set
		 */
		cmp_val = SBSDIO_FUNC1_SLEEPCSR_KSO_MASK |
			  SBSDIO_FUNC1_SLEEPCSR_DEVON_MASK;
		bmask = cmp_val;
		usleep_range(2000, 3000);
	} else {
		/* Put device to sleep, turn off KSO */
		cmp_val = 0;
		/* only check for bit0, bit1(dev on status) may not
		 * get cleared right away
		 */
		bmask = SBSDIO_FUNC1_SLEEPCSR_KSO_MASK;
	}

	do {
		/* reliable KSO bit set/clr:
		 * the sdiod sleep write access is synced to PMU 32khz clk
		 * just one write attempt may fail,
		 * read it back until it matches written value
		 */
		rd_val = brcmf_sdio_regrb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR,
					  &err);
		if (((rd_val & bmask) == cmp_val) && !err)
			break;
		brcmf_dbg(SDIO, "KSO wr/rd retry:%d (max: %d) ERR:%x\n",
			  try_cnt, MAX_KSO_ATTEMPTS, err);
		udelay(KSO_WAIT_US);
		brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR,
				 wr_val, &err);
	} while (try_cnt++ < MAX_KSO_ATTEMPTS);

	return err;
}

#define PKT_AVAILABLE()		(intstatus & I_HMB_FRAME_IND)

#define HOSTINTMASK		(I_HMB_SW_MASK | I_CHIPACTIVE)

/* Turn backplane clock on or off */
static int brcmf_sdbrcm_htclk(struct brcmf_sdio *bus, bool on, bool pendok)
{
	int err;
	u8 clkctl, clkreq, devctl;
	unsigned long timeout;

	brcmf_dbg(SDIO, "Enter\n");

	clkctl = 0;

	if (bus->sr_enabled) {
		bus->clkstate = (on ? CLK_AVAIL : CLK_SDONLY);
		return 0;
	}

	if (on) {
		/* Request HT Avail */
		clkreq =
		    bus->alp_only ? SBSDIO_ALP_AVAIL_REQ : SBSDIO_HT_AVAIL_REQ;

		brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR,
				 clkreq, &err);
		if (err) {
			brcmf_err("HT Avail request error: %d\n", err);
			return -EBADE;
		}

		/* Check current status */
		clkctl = brcmf_sdio_regrb(bus->sdiodev,
					  SBSDIO_FUNC1_CHIPCLKCSR, &err);
		if (err) {
			brcmf_err("HT Avail read error: %d\n", err);
			return -EBADE;
		}

		/* Go to pending and await interrupt if appropriate */
		if (!SBSDIO_CLKAV(clkctl, bus->alp_only) && pendok) {
			/* Allow only clock-available interrupt */
			devctl = brcmf_sdio_regrb(bus->sdiodev,
						  SBSDIO_DEVICE_CTL, &err);
			if (err) {
				brcmf_err("Devctl error setting CA: %d\n",
					  err);
				return -EBADE;
			}

			devctl |= SBSDIO_DEVCTL_CA_INT_ONLY;
			brcmf_sdio_regwb(bus->sdiodev, SBSDIO_DEVICE_CTL,
					 devctl, &err);
			brcmf_dbg(SDIO, "CLKCTL: set PENDING\n");
			bus->clkstate = CLK_PENDING;

			return 0;
		} else if (bus->clkstate == CLK_PENDING) {
			/* Cancel CA-only interrupt filter */
			devctl = brcmf_sdio_regrb(bus->sdiodev,
						  SBSDIO_DEVICE_CTL, &err);
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			brcmf_sdio_regwb(bus->sdiodev, SBSDIO_DEVICE_CTL,
					 devctl, &err);
		}

		/* Otherwise, wait here (polling) for HT Avail */
		timeout = jiffies +
			  msecs_to_jiffies(PMU_MAX_TRANSITION_DLY/1000);
		while (!SBSDIO_CLKAV(clkctl, bus->alp_only)) {
			clkctl = brcmf_sdio_regrb(bus->sdiodev,
						  SBSDIO_FUNC1_CHIPCLKCSR,
						  &err);
			if (time_after(jiffies, timeout))
				break;
			else
				usleep_range(5000, 10000);
		}
		if (err) {
			brcmf_err("HT Avail request error: %d\n", err);
			return -EBADE;
		}
		if (!SBSDIO_CLKAV(clkctl, bus->alp_only)) {
			brcmf_err("HT Avail timeout (%d): clkctl 0x%02x\n",
				  PMU_MAX_TRANSITION_DLY, clkctl);
			return -EBADE;
		}

		/* Mark clock available */
		bus->clkstate = CLK_AVAIL;
		brcmf_dbg(SDIO, "CLKCTL: turned ON\n");

#if defined(DEBUG)
		if (!bus->alp_only) {
			if (SBSDIO_ALPONLY(clkctl))
				brcmf_err("HT Clock should be on\n");
		}
#endif				/* defined (DEBUG) */

		bus->activity = true;
	} else {
		clkreq = 0;

		if (bus->clkstate == CLK_PENDING) {
			/* Cancel CA-only interrupt filter */
			devctl = brcmf_sdio_regrb(bus->sdiodev,
						  SBSDIO_DEVICE_CTL, &err);
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			brcmf_sdio_regwb(bus->sdiodev, SBSDIO_DEVICE_CTL,
					 devctl, &err);
		}

		bus->clkstate = CLK_SDONLY;
		brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR,
				 clkreq, &err);
		brcmf_dbg(SDIO, "CLKCTL: turned OFF\n");
		if (err) {
			brcmf_err("Failed access turning clock off: %d\n",
				  err);
			return -EBADE;
		}
	}
	return 0;
}

/* Change idle/active SD state */
static int brcmf_sdbrcm_sdclk(struct brcmf_sdio *bus, bool on)
{
	brcmf_dbg(SDIO, "Enter\n");

	if (on)
		bus->clkstate = CLK_SDONLY;
	else
		bus->clkstate = CLK_NONE;

	return 0;
}

/* Transition SD and backplane clock readiness */
static int brcmf_sdbrcm_clkctl(struct brcmf_sdio *bus, uint target, bool pendok)
{
#ifdef DEBUG
	uint oldstate = bus->clkstate;
#endif				/* DEBUG */

	brcmf_dbg(SDIO, "Enter\n");

	/* Early exit if we're already there */
	if (bus->clkstate == target) {
		if (target == CLK_AVAIL) {
			brcmf_sdbrcm_wd_timer(bus, BRCMF_WD_POLL_MS);
			bus->activity = true;
		}
		return 0;
	}

	switch (target) {
	case CLK_AVAIL:
		/* Make sure SD clock is available */
		if (bus->clkstate == CLK_NONE)
			brcmf_sdbrcm_sdclk(bus, true);
		/* Now request HT Avail on the backplane */
		brcmf_sdbrcm_htclk(bus, true, pendok);
		brcmf_sdbrcm_wd_timer(bus, BRCMF_WD_POLL_MS);
		bus->activity = true;
		break;

	case CLK_SDONLY:
		/* Remove HT request, or bring up SD clock */
		if (bus->clkstate == CLK_NONE)
			brcmf_sdbrcm_sdclk(bus, true);
		else if (bus->clkstate == CLK_AVAIL)
			brcmf_sdbrcm_htclk(bus, false, false);
		else
			brcmf_err("request for %d -> %d\n",
				  bus->clkstate, target);
		brcmf_sdbrcm_wd_timer(bus, BRCMF_WD_POLL_MS);
		break;

	case CLK_NONE:
		/* Make sure to remove HT request */
		if (bus->clkstate == CLK_AVAIL)
			brcmf_sdbrcm_htclk(bus, false, false);
		/* Now remove the SD clock */
		brcmf_sdbrcm_sdclk(bus, false);
		brcmf_sdbrcm_wd_timer(bus, 0);
		break;
	}
#ifdef DEBUG
	brcmf_dbg(SDIO, "%d -> %d\n", oldstate, bus->clkstate);
#endif				/* DEBUG */

	return 0;
}

static int
brcmf_sdbrcm_bus_sleep(struct brcmf_sdio *bus, bool sleep, bool pendok)
{
	int err = 0;
	brcmf_dbg(TRACE, "Enter\n");
	brcmf_dbg(SDIO, "request %s currently %s\n",
		  (sleep ? "SLEEP" : "WAKE"),
		  (bus->sleeping ? "SLEEP" : "WAKE"));

	/* If SR is enabled control bus state with KSO */
	if (bus->sr_enabled) {
		/* Done if we're already in the requested state */
		if (sleep == bus->sleeping)
			goto end;

		/* Going to sleep */
		if (sleep) {
			/* Don't sleep if something is pending */
			if (atomic_read(&bus->intstatus) ||
			    atomic_read(&bus->ipend) > 0 ||
			    (!atomic_read(&bus->fcstate) &&
			    brcmu_pktq_mlen(&bus->txq, ~bus->flowcontrol) &&
			    data_ok(bus)))
				 return -EBUSY;
			err = brcmf_sdbrcm_kso_control(bus, false);
			/* disable watchdog */
			if (!err)
				brcmf_sdbrcm_wd_timer(bus, 0);
		} else {
			bus->idlecount = 0;
			err = brcmf_sdbrcm_kso_control(bus, true);
		}
		if (!err) {
			/* Change state */
			bus->sleeping = sleep;
			brcmf_dbg(SDIO, "new state %s\n",
				  (sleep ? "SLEEP" : "WAKE"));
		} else {
			brcmf_err("error while changing bus sleep state %d\n",
				  err);
			return err;
		}
	}

end:
	/* control clocks */
	if (sleep) {
		if (!bus->sr_enabled)
			brcmf_sdbrcm_clkctl(bus, CLK_NONE, pendok);
	} else {
		brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, pendok);
	}

	return err;

}

static u32 brcmf_sdbrcm_hostmail(struct brcmf_sdio *bus)
{
	u32 intstatus = 0;
	u32 hmb_data;
	u8 fcbits;
	int ret;

	brcmf_dbg(SDIO, "Enter\n");

	/* Read mailbox data and ack that we did so */
	ret = r_sdreg32(bus, &hmb_data,
			offsetof(struct sdpcmd_regs, tohostmailboxdata));

	if (ret == 0)
		w_sdreg32(bus, SMB_INT_ACK,
			  offsetof(struct sdpcmd_regs, tosbmailbox));
	bus->sdcnt.f1regdata += 2;

	/* Dongle recomposed rx frames, accept them again */
	if (hmb_data & HMB_DATA_NAKHANDLED) {
		brcmf_dbg(SDIO, "Dongle reports NAK handled, expect rtx of %d\n",
			  bus->rx_seq);
		if (!bus->rxskip)
			brcmf_err("unexpected NAKHANDLED!\n");

		bus->rxskip = false;
		intstatus |= I_HMB_FRAME_IND;
	}

	/*
	 * DEVREADY does not occur with gSPI.
	 */
	if (hmb_data & (HMB_DATA_DEVREADY | HMB_DATA_FWREADY)) {
		bus->sdpcm_ver =
		    (hmb_data & HMB_DATA_VERSION_MASK) >>
		    HMB_DATA_VERSION_SHIFT;
		if (bus->sdpcm_ver != SDPCM_PROT_VERSION)
			brcmf_err("Version mismatch, dongle reports %d, "
				  "expecting %d\n",
				  bus->sdpcm_ver, SDPCM_PROT_VERSION);
		else
			brcmf_dbg(SDIO, "Dongle ready, protocol version %d\n",
				  bus->sdpcm_ver);
	}

	/*
	 * Flow Control has been moved into the RX headers and this out of band
	 * method isn't used any more.
	 * remaining backward compatible with older dongles.
	 */
	if (hmb_data & HMB_DATA_FC) {
		fcbits = (hmb_data & HMB_DATA_FCDATA_MASK) >>
							HMB_DATA_FCDATA_SHIFT;

		if (fcbits & ~bus->flowcontrol)
			bus->sdcnt.fc_xoff++;

		if (bus->flowcontrol & ~fcbits)
			bus->sdcnt.fc_xon++;

		bus->sdcnt.fc_rcvd++;
		bus->flowcontrol = fcbits;
	}

	/* Shouldn't be any others */
	if (hmb_data & ~(HMB_DATA_DEVREADY |
			 HMB_DATA_NAKHANDLED |
			 HMB_DATA_FC |
			 HMB_DATA_FWREADY |
			 HMB_DATA_FCDATA_MASK | HMB_DATA_VERSION_MASK))
		brcmf_err("Unknown mailbox data content: 0x%02x\n",
			  hmb_data);

	return intstatus;
}

static void brcmf_sdbrcm_rxfail(struct brcmf_sdio *bus, bool abort, bool rtx)
{
	uint retries = 0;
	u16 lastrbc;
	u8 hi, lo;
	int err;

	brcmf_err("%sterminate frame%s\n",
		  abort ? "abort command, " : "",
		  rtx ? ", send NAK" : "");

	if (abort)
		brcmf_sdcard_abort(bus->sdiodev, SDIO_FUNC_2);

	brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_FRAMECTRL,
			 SFC_RF_TERM, &err);
	bus->sdcnt.f1regdata++;

	/* Wait until the packet has been flushed (device/FIFO stable) */
	for (lastrbc = retries = 0xffff; retries > 0; retries--) {
		hi = brcmf_sdio_regrb(bus->sdiodev,
				      SBSDIO_FUNC1_RFRAMEBCHI, &err);
		lo = brcmf_sdio_regrb(bus->sdiodev,
				      SBSDIO_FUNC1_RFRAMEBCLO, &err);
		bus->sdcnt.f1regdata += 2;

		if ((hi == 0) && (lo == 0))
			break;

		if ((hi > (lastrbc >> 8)) && (lo > (lastrbc & 0x00ff))) {
			brcmf_err("count growing: last 0x%04x now 0x%04x\n",
				  lastrbc, (hi << 8) + lo);
		}
		lastrbc = (hi << 8) + lo;
	}

	if (!retries)
		brcmf_err("count never zeroed: last 0x%04x\n", lastrbc);
	else
		brcmf_dbg(SDIO, "flush took %d iterations\n", 0xffff - retries);

	if (rtx) {
		bus->sdcnt.rxrtx++;
		err = w_sdreg32(bus, SMB_NAK,
				offsetof(struct sdpcmd_regs, tosbmailbox));

		bus->sdcnt.f1regdata++;
		if (err == 0)
			bus->rxskip = true;
	}

	/* Clear partial in any case */
	bus->cur_read.len = 0;

	/* If we can't reach the device, signal failure */
	if (err)
		bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
}

/* return total length of buffer chain */
static uint brcmf_sdbrcm_glom_len(struct brcmf_sdio *bus)
{
	struct sk_buff *p;
	uint total;

	total = 0;
	skb_queue_walk(&bus->glom, p)
		total += p->len;
	return total;
}

static void brcmf_sdbrcm_free_glom(struct brcmf_sdio *bus)
{
	struct sk_buff *cur, *next;

	skb_queue_walk_safe(&bus->glom, cur, next) {
		skb_unlink(cur, &bus->glom);
		brcmu_pkt_buf_free_skb(cur);
	}
}

/**
 * brcmfmac sdio bus specific header
 * This is the lowest layer header wrapped on the packets transmitted between
 * host and WiFi dongle which contains information needed for SDIO core and
 * firmware
 *
 * It consists of 2 parts: hw header and software header
 * hardware header (frame tag) - 4 bytes
 * Byte 0~1: Frame length
 * Byte 2~3: Checksum, bit-wise inverse of frame length
 * software header - 8 bytes
 * Byte 0: Rx/Tx sequence number
 * Byte 1: 4 MSB Channel number, 4 LSB arbitrary flag
 * Byte 2: Length of next data frame, reserved for Tx
 * Byte 3: Data offset
 * Byte 4: Flow control bits, reserved for Tx
 * Byte 5: Maximum Sequence number allowed by firmware for Tx, N/A for Tx packet
 * Byte 6~7: Reserved
 */
#define SDPCM_HWHDR_LEN			4
#define SDPCM_SWHDR_LEN			8
#define SDPCM_HDRLEN			(SDPCM_HWHDR_LEN + SDPCM_SWHDR_LEN)
/* software header */
#define SDPCM_SEQ_MASK			0x000000ff
#define SDPCM_SEQ_WRAP			256
#define SDPCM_CHANNEL_MASK		0x00000f00
#define SDPCM_CHANNEL_SHIFT		8
#define SDPCM_CONTROL_CHANNEL		0	/* Control */
#define SDPCM_EVENT_CHANNEL		1	/* Asyc Event Indication */
#define SDPCM_DATA_CHANNEL		2	/* Data Xmit/Recv */
#define SDPCM_GLOM_CHANNEL		3	/* Coalesced packets */
#define SDPCM_TEST_CHANNEL		15	/* Test/debug packets */
#define SDPCM_GLOMDESC(p)		(((u8 *)p)[1] & 0x80)
#define SDPCM_NEXTLEN_MASK		0x00ff0000
#define SDPCM_NEXTLEN_SHIFT		16
#define SDPCM_DOFFSET_MASK		0xff000000
#define SDPCM_DOFFSET_SHIFT		24
#define SDPCM_FCMASK_MASK		0x000000ff
#define SDPCM_WINDOW_MASK		0x0000ff00
#define SDPCM_WINDOW_SHIFT		8

static inline u8 brcmf_sdio_getdatoffset(u8 *swheader)
{
	u32 hdrvalue;
	hdrvalue = *(u32 *)swheader;
	return (u8)((hdrvalue & SDPCM_DOFFSET_MASK) >> SDPCM_DOFFSET_SHIFT);
}

static int brcmf_sdio_hdparse(struct brcmf_sdio *bus, u8 *header,
			      struct brcmf_sdio_hdrinfo *rd,
			      enum brcmf_sdio_frmtype type)
{
	u16 len, checksum;
	u8 rx_seq, fc, tx_seq_max;
	u32 swheader;

	trace_brcmf_sdpcm_hdr(false, header);

	/* hw header */
	len = get_unaligned_le16(header);
	checksum = get_unaligned_le16(header + sizeof(u16));
	/* All zero means no more to read */
	if (!(len | checksum)) {
		bus->rxpending = false;
		return -ENODATA;
	}
	if ((u16)(~(len ^ checksum))) {
		brcmf_err("HW header checksum error\n");
		bus->sdcnt.rx_badhdr++;
		brcmf_sdbrcm_rxfail(bus, false, false);
		return -EIO;
	}
	if (len < SDPCM_HDRLEN) {
		brcmf_err("HW header length error\n");
		return -EPROTO;
	}
	if (type == BRCMF_SDIO_FT_SUPER &&
	    (roundup(len, bus->blocksize) != rd->len)) {
		brcmf_err("HW superframe header length error\n");
		return -EPROTO;
	}
	if (type == BRCMF_SDIO_FT_SUB && len > rd->len) {
		brcmf_err("HW subframe header length error\n");
		return -EPROTO;
	}
	rd->len = len;

	/* software header */
	header += SDPCM_HWHDR_LEN;
	swheader = le32_to_cpu(*(__le32 *)header);
	if (type == BRCMF_SDIO_FT_SUPER && SDPCM_GLOMDESC(header)) {
		brcmf_err("Glom descriptor found in superframe head\n");
		rd->len = 0;
		return -EINVAL;
	}
	rx_seq = (u8)(swheader & SDPCM_SEQ_MASK);
	rd->channel = (swheader & SDPCM_CHANNEL_MASK) >> SDPCM_CHANNEL_SHIFT;
	if (len > MAX_RX_DATASZ && rd->channel != SDPCM_CONTROL_CHANNEL &&
	    type != BRCMF_SDIO_FT_SUPER) {
		brcmf_err("HW header length too long\n");
		bus->sdcnt.rx_toolong++;
		brcmf_sdbrcm_rxfail(bus, false, false);
		rd->len = 0;
		return -EPROTO;
	}
	if (type == BRCMF_SDIO_FT_SUPER && rd->channel != SDPCM_GLOM_CHANNEL) {
		brcmf_err("Wrong channel for superframe\n");
		rd->len = 0;
		return -EINVAL;
	}
	if (type == BRCMF_SDIO_FT_SUB && rd->channel != SDPCM_DATA_CHANNEL &&
	    rd->channel != SDPCM_EVENT_CHANNEL) {
		brcmf_err("Wrong channel for subframe\n");
		rd->len = 0;
		return -EINVAL;
	}
	rd->dat_offset = brcmf_sdio_getdatoffset(header);
	if (rd->dat_offset < SDPCM_HDRLEN || rd->dat_offset > rd->len) {
		brcmf_err("seq %d: bad data offset\n", rx_seq);
		bus->sdcnt.rx_badhdr++;
		brcmf_sdbrcm_rxfail(bus, false, false);
		rd->len = 0;
		return -ENXIO;
	}
	if (rd->seq_num != rx_seq) {
		brcmf_err("seq %d: sequence number error, expect %d\n",
			  rx_seq, rd->seq_num);
		bus->sdcnt.rx_badseq++;
		rd->seq_num = rx_seq;
	}
	/* no need to check the reset for subframe */
	if (type == BRCMF_SDIO_FT_SUB)
		return 0;
	rd->len_nxtfrm = (swheader & SDPCM_NEXTLEN_MASK) >> SDPCM_NEXTLEN_SHIFT;
	if (rd->len_nxtfrm << 4 > MAX_RX_DATASZ) {
		/* only warm for NON glom packet */
		if (rd->channel != SDPCM_GLOM_CHANNEL)
			brcmf_err("seq %d: next length error\n", rx_seq);
		rd->len_nxtfrm = 0;
	}
	swheader = le32_to_cpu(*(__le32 *)(header + 4));
	fc = swheader & SDPCM_FCMASK_MASK;
	if (bus->flowcontrol != fc) {
		if (~bus->flowcontrol & fc)
			bus->sdcnt.fc_xoff++;
		if (bus->flowcontrol & ~fc)
			bus->sdcnt.fc_xon++;
		bus->sdcnt.fc_rcvd++;
		bus->flowcontrol = fc;
	}
	tx_seq_max = (swheader & SDPCM_WINDOW_MASK) >> SDPCM_WINDOW_SHIFT;
	if ((u8)(tx_seq_max - bus->tx_seq) > 0x40) {
		brcmf_err("seq %d: max tx seq number error\n", rx_seq);
		tx_seq_max = bus->tx_seq + 2;
	}
	bus->tx_max = tx_seq_max;

	return 0;
}

static inline void brcmf_sdio_update_hwhdr(u8 *header, u16 frm_length)
{
	*(__le16 *)header = cpu_to_le16(frm_length);
	*(((__le16 *)header) + 1) = cpu_to_le16(~frm_length);
}

static void brcmf_sdio_hdpack(struct brcmf_sdio *bus, u8 *header,
			      struct brcmf_sdio_hdrinfo *hd_info)
{
	u32 sw_header;

	brcmf_sdio_update_hwhdr(header, hd_info->len);

	sw_header = bus->tx_seq;
	sw_header |= (hd_info->channel << SDPCM_CHANNEL_SHIFT) &
		     SDPCM_CHANNEL_MASK;
	sw_header |= (hd_info->dat_offset << SDPCM_DOFFSET_SHIFT) &
		     SDPCM_DOFFSET_MASK;
	*(((__le32 *)header) + 1) = cpu_to_le32(sw_header);
	*(((__le32 *)header) + 2) = 0;
	trace_brcmf_sdpcm_hdr(true, header);
}

static u8 brcmf_sdbrcm_rxglom(struct brcmf_sdio *bus, u8 rxseq)
{
	u16 dlen, totlen;
	u8 *dptr, num = 0;
	u32 align = 0;
	u16 sublen;
	struct sk_buff *pfirst, *pnext;

	int errcode;
	u8 doff, sfdoff;

	struct brcmf_sdio_hdrinfo rd_new;

	/* If packets, issue read(s) and send up packet chain */
	/* Return sequence numbers consumed? */

	brcmf_dbg(SDIO, "start: glomd %p glom %p\n",
		  bus->glomd, skb_peek(&bus->glom));

	if (bus->sdiodev->pdata)
		align = bus->sdiodev->pdata->sd_sgentry_align;
	if (align < 4)
		align = 4;

	/* If there's a descriptor, generate the packet chain */
	if (bus->glomd) {
		pfirst = pnext = NULL;
		dlen = (u16) (bus->glomd->len);
		dptr = bus->glomd->data;
		if (!dlen || (dlen & 1)) {
			brcmf_err("bad glomd len(%d), ignore descriptor\n",
				  dlen);
			dlen = 0;
		}

		for (totlen = num = 0; dlen; num++) {
			/* Get (and move past) next length */
			sublen = get_unaligned_le16(dptr);
			dlen -= sizeof(u16);
			dptr += sizeof(u16);
			if ((sublen < SDPCM_HDRLEN) ||
			    ((num == 0) && (sublen < (2 * SDPCM_HDRLEN)))) {
				brcmf_err("descriptor len %d bad: %d\n",
					  num, sublen);
				pnext = NULL;
				break;
			}
			if (sublen % align) {
				brcmf_err("sublen %d not multiple of %d\n",
					  sublen, align);
			}
			totlen += sublen;

			/* For last frame, adjust read len so total
				 is a block multiple */
			if (!dlen) {
				sublen +=
				    (roundup(totlen, bus->blocksize) - totlen);
				totlen = roundup(totlen, bus->blocksize);
			}

			/* Allocate/chain packet for next subframe */
			pnext = brcmu_pkt_buf_get_skb(sublen + align);
			if (pnext == NULL) {
				brcmf_err("bcm_pkt_buf_get_skb failed, num %d len %d\n",
					  num, sublen);
				break;
			}
			skb_queue_tail(&bus->glom, pnext);

			/* Adhere to start alignment requirements */
			pkt_align(pnext, sublen, align);
		}

		/* If all allocations succeeded, save packet chain
			 in bus structure */
		if (pnext) {
			brcmf_dbg(GLOM, "allocated %d-byte packet chain for %d subframes\n",
				  totlen, num);
			if (BRCMF_GLOM_ON() && bus->cur_read.len &&
			    totlen != bus->cur_read.len) {
				brcmf_dbg(GLOM, "glomdesc mismatch: nextlen %d glomdesc %d rxseq %d\n",
					  bus->cur_read.len, totlen, rxseq);
			}
			pfirst = pnext = NULL;
		} else {
			brcmf_sdbrcm_free_glom(bus);
			num = 0;
		}

		/* Done with descriptor packet */
		brcmu_pkt_buf_free_skb(bus->glomd);
		bus->glomd = NULL;
		bus->cur_read.len = 0;
	}

	/* Ok -- either we just generated a packet chain,
		 or had one from before */
	if (!skb_queue_empty(&bus->glom)) {
		if (BRCMF_GLOM_ON()) {
			brcmf_dbg(GLOM, "try superframe read, packet chain:\n");
			skb_queue_walk(&bus->glom, pnext) {
				brcmf_dbg(GLOM, "    %p: %p len 0x%04x (%d)\n",
					  pnext, (u8 *) (pnext->data),
					  pnext->len, pnext->len);
			}
		}

		pfirst = skb_peek(&bus->glom);
		dlen = (u16) brcmf_sdbrcm_glom_len(bus);

		/* Do an SDIO read for the superframe.  Configurable iovar to
		 * read directly into the chained packet, or allocate a large
		 * packet and and copy into the chain.
		 */
		sdio_claim_host(bus->sdiodev->func[1]);
		errcode = brcmf_sdcard_recv_chain(bus->sdiodev,
				bus->sdiodev->sbwad,
				SDIO_FUNC_2, F2SYNC, &bus->glom, dlen);
		sdio_release_host(bus->sdiodev->func[1]);
		bus->sdcnt.f2rxdata++;

		/* On failure, kill the superframe, allow a couple retries */
		if (errcode < 0) {
			brcmf_err("glom read of %d bytes failed: %d\n",
				  dlen, errcode);

			sdio_claim_host(bus->sdiodev->func[1]);
			if (bus->glomerr++ < 3) {
				brcmf_sdbrcm_rxfail(bus, true, true);
			} else {
				bus->glomerr = 0;
				brcmf_sdbrcm_rxfail(bus, true, false);
				bus->sdcnt.rxglomfail++;
				brcmf_sdbrcm_free_glom(bus);
			}
			sdio_release_host(bus->sdiodev->func[1]);
			return 0;
		}

		brcmf_dbg_hex_dump(BRCMF_GLOM_ON(),
				   pfirst->data, min_t(int, pfirst->len, 48),
				   "SUPERFRAME:\n");

		rd_new.seq_num = rxseq;
		rd_new.len = dlen;
		sdio_claim_host(bus->sdiodev->func[1]);
		errcode = brcmf_sdio_hdparse(bus, pfirst->data, &rd_new,
					     BRCMF_SDIO_FT_SUPER);
		sdio_release_host(bus->sdiodev->func[1]);
		bus->cur_read.len = rd_new.len_nxtfrm << 4;

		/* Remove superframe header, remember offset */
		skb_pull(pfirst, rd_new.dat_offset);
		sfdoff = rd_new.dat_offset;
		num = 0;

		/* Validate all the subframe headers */
		skb_queue_walk(&bus->glom, pnext) {
			/* leave when invalid subframe is found */
			if (errcode)
				break;

			rd_new.len = pnext->len;
			rd_new.seq_num = rxseq++;
			sdio_claim_host(bus->sdiodev->func[1]);
			errcode = brcmf_sdio_hdparse(bus, pnext->data, &rd_new,
						     BRCMF_SDIO_FT_SUB);
			sdio_release_host(bus->sdiodev->func[1]);
			brcmf_dbg_hex_dump(BRCMF_GLOM_ON(),
					   pnext->data, 32, "subframe:\n");

			num++;
		}

		if (errcode) {
			/* Terminate frame on error, request
				 a couple retries */
			sdio_claim_host(bus->sdiodev->func[1]);
			if (bus->glomerr++ < 3) {
				/* Restore superframe header space */
				skb_push(pfirst, sfdoff);
				brcmf_sdbrcm_rxfail(bus, true, true);
			} else {
				bus->glomerr = 0;
				brcmf_sdbrcm_rxfail(bus, true, false);
				bus->sdcnt.rxglomfail++;
				brcmf_sdbrcm_free_glom(bus);
			}
			sdio_release_host(bus->sdiodev->func[1]);
			bus->cur_read.len = 0;
			return 0;
		}

		/* Basic SD framing looks ok - process each packet (header) */

		skb_queue_walk_safe(&bus->glom, pfirst, pnext) {
			dptr = (u8 *) (pfirst->data);
			sublen = get_unaligned_le16(dptr);
			doff = brcmf_sdio_getdatoffset(&dptr[SDPCM_HWHDR_LEN]);

			brcmf_dbg_hex_dump(BRCMF_BYTES_ON() && BRCMF_DATA_ON(),
					   dptr, pfirst->len,
					   "Rx Subframe Data:\n");

			__skb_trim(pfirst, sublen);
			skb_pull(pfirst, doff);

			if (pfirst->len == 0) {
				skb_unlink(pfirst, &bus->glom);
				brcmu_pkt_buf_free_skb(pfirst);
				continue;
			}

			brcmf_dbg_hex_dump(BRCMF_GLOM_ON(),
					   pfirst->data,
					   min_t(int, pfirst->len, 32),
					   "subframe %d to stack, %p (%p/%d) nxt/lnk %p/%p\n",
					   bus->glom.qlen, pfirst, pfirst->data,
					   pfirst->len, pfirst->next,
					   pfirst->prev);
			skb_unlink(pfirst, &bus->glom);
			brcmf_rx_frame(bus->sdiodev->dev, pfirst);
			bus->sdcnt.rxglompkts++;
		}

		bus->sdcnt.rxglomframes++;
	}
	return num;
}

static int brcmf_sdbrcm_dcmd_resp_wait(struct brcmf_sdio *bus, uint *condition,
					bool *pending)
{
	DECLARE_WAITQUEUE(wait, current);
	int timeout = msecs_to_jiffies(DCMD_RESP_TIMEOUT);

	/* Wait until control frame is available */
	add_wait_queue(&bus->dcmd_resp_wait, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	while (!(*condition) && (!signal_pending(current) && timeout))
		timeout = schedule_timeout(timeout);

	if (signal_pending(current))
		*pending = true;

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&bus->dcmd_resp_wait, &wait);

	return timeout;
}

static int brcmf_sdbrcm_dcmd_resp_wake(struct brcmf_sdio *bus)
{
	if (waitqueue_active(&bus->dcmd_resp_wait))
		wake_up_interruptible(&bus->dcmd_resp_wait);

	return 0;
}
static void
brcmf_sdbrcm_read_control(struct brcmf_sdio *bus, u8 *hdr, uint len, uint doff)
{
	uint rdlen, pad;
	u8 *buf = NULL, *rbuf;
	int sdret;

	brcmf_dbg(TRACE, "Enter\n");

	if (bus->rxblen)
		buf = vzalloc(bus->rxblen);
	if (!buf)
		goto done;

	rbuf = bus->rxbuf;
	pad = ((unsigned long)rbuf % BRCMF_SDALIGN);
	if (pad)
		rbuf += (BRCMF_SDALIGN - pad);

	/* Copy the already-read portion over */
	memcpy(buf, hdr, BRCMF_FIRSTREAD);
	if (len <= BRCMF_FIRSTREAD)
		goto gotpkt;

	/* Raise rdlen to next SDIO block to avoid tail command */
	rdlen = len - BRCMF_FIRSTREAD;
	if (bus->roundup && bus->blocksize && (rdlen > bus->blocksize)) {
		pad = bus->blocksize - (rdlen % bus->blocksize);
		if ((pad <= bus->roundup) && (pad < bus->blocksize) &&
		    ((len + pad) < bus->sdiodev->bus_if->maxctl))
			rdlen += pad;
	} else if (rdlen % BRCMF_SDALIGN) {
		rdlen += BRCMF_SDALIGN - (rdlen % BRCMF_SDALIGN);
	}

	/* Satisfy length-alignment requirements */
	if (rdlen & (ALIGNMENT - 1))
		rdlen = roundup(rdlen, ALIGNMENT);

	/* Drop if the read is too big or it exceeds our maximum */
	if ((rdlen + BRCMF_FIRSTREAD) > bus->sdiodev->bus_if->maxctl) {
		brcmf_err("%d-byte control read exceeds %d-byte buffer\n",
			  rdlen, bus->sdiodev->bus_if->maxctl);
		brcmf_sdbrcm_rxfail(bus, false, false);
		goto done;
	}

	if ((len - doff) > bus->sdiodev->bus_if->maxctl) {
		brcmf_err("%d-byte ctl frame (%d-byte ctl data) exceeds %d-byte limit\n",
			  len, len - doff, bus->sdiodev->bus_if->maxctl);
		bus->sdcnt.rx_toolong++;
		brcmf_sdbrcm_rxfail(bus, false, false);
		goto done;
	}

	/* Read remain of frame body */
	sdret = brcmf_sdcard_recv_buf(bus->sdiodev,
				bus->sdiodev->sbwad,
				SDIO_FUNC_2,
				F2SYNC, rbuf, rdlen);
	bus->sdcnt.f2rxdata++;

	/* Control frame failures need retransmission */
	if (sdret < 0) {
		brcmf_err("read %d control bytes failed: %d\n",
			  rdlen, sdret);
		bus->sdcnt.rxc_errors++;
		brcmf_sdbrcm_rxfail(bus, true, true);
		goto done;
	} else
		memcpy(buf + BRCMF_FIRSTREAD, rbuf, rdlen);

gotpkt:

	brcmf_dbg_hex_dump(BRCMF_BYTES_ON() && BRCMF_CTL_ON(),
			   buf, len, "RxCtrl:\n");

	/* Point to valid data and indicate its length */
	spin_lock_bh(&bus->rxctl_lock);
	if (bus->rxctl) {
		brcmf_err("last control frame is being processed.\n");
		spin_unlock_bh(&bus->rxctl_lock);
		vfree(buf);
		goto done;
	}
	bus->rxctl = buf + doff;
	bus->rxctl_orig = buf;
	bus->rxlen = len - doff;
	spin_unlock_bh(&bus->rxctl_lock);

done:
	/* Awake any waiters */
	brcmf_sdbrcm_dcmd_resp_wake(bus);
}

/* Pad read to blocksize for efficiency */
static void brcmf_pad(struct brcmf_sdio *bus, u16 *pad, u16 *rdlen)
{
	if (bus->roundup && bus->blocksize && *rdlen > bus->blocksize) {
		*pad = bus->blocksize - (*rdlen % bus->blocksize);
		if (*pad <= bus->roundup && *pad < bus->blocksize &&
		    *rdlen + *pad + BRCMF_FIRSTREAD < MAX_RX_DATASZ)
			*rdlen += *pad;
	} else if (*rdlen % BRCMF_SDALIGN) {
		*rdlen += BRCMF_SDALIGN - (*rdlen % BRCMF_SDALIGN);
	}
}

static uint brcmf_sdio_readframes(struct brcmf_sdio *bus, uint maxframes)
{
	struct sk_buff *pkt;		/* Packet for event or data frames */
	u16 pad;		/* Number of pad bytes to read */
	uint rxleft = 0;	/* Remaining number of frames allowed */
	int ret;		/* Return code from calls */
	uint rxcount = 0;	/* Total frames read */
	struct brcmf_sdio_hdrinfo *rd = &bus->cur_read, rd_new;
	u8 head_read = 0;

	brcmf_dbg(TRACE, "Enter\n");

	/* Not finished unless we encounter no more frames indication */
	bus->rxpending = true;

	for (rd->seq_num = bus->rx_seq, rxleft = maxframes;
	     !bus->rxskip && rxleft &&
	     bus->sdiodev->bus_if->state != BRCMF_BUS_DOWN;
	     rd->seq_num++, rxleft--) {

		/* Handle glomming separately */
		if (bus->glomd || !skb_queue_empty(&bus->glom)) {
			u8 cnt;
			brcmf_dbg(GLOM, "calling rxglom: glomd %p, glom %p\n",
				  bus->glomd, skb_peek(&bus->glom));
			cnt = brcmf_sdbrcm_rxglom(bus, rd->seq_num);
			brcmf_dbg(GLOM, "rxglom returned %d\n", cnt);
			rd->seq_num += cnt - 1;
			rxleft = (rxleft > cnt) ? (rxleft - cnt) : 1;
			continue;
		}

		rd->len_left = rd->len;
		/* read header first for unknow frame length */
		sdio_claim_host(bus->sdiodev->func[1]);
		if (!rd->len) {
			ret = brcmf_sdcard_recv_buf(bus->sdiodev,
						      bus->sdiodev->sbwad,
						      SDIO_FUNC_2, F2SYNC,
						      bus->rxhdr,
						      BRCMF_FIRSTREAD);
			bus->sdcnt.f2rxhdrs++;
			if (ret < 0) {
				brcmf_err("RXHEADER FAILED: %d\n",
					  ret);
				bus->sdcnt.rx_hdrfail++;
				brcmf_sdbrcm_rxfail(bus, true, true);
				sdio_release_host(bus->sdiodev->func[1]);
				continue;
			}

			brcmf_dbg_hex_dump(BRCMF_BYTES_ON() || BRCMF_HDRS_ON(),
					   bus->rxhdr, SDPCM_HDRLEN,
					   "RxHdr:\n");

			if (brcmf_sdio_hdparse(bus, bus->rxhdr, rd,
					       BRCMF_SDIO_FT_NORMAL)) {
				sdio_release_host(bus->sdiodev->func[1]);
				if (!bus->rxpending)
					break;
				else
					continue;
			}

			if (rd->channel == SDPCM_CONTROL_CHANNEL) {
				brcmf_sdbrcm_read_control(bus, bus->rxhdr,
							  rd->len,
							  rd->dat_offset);
				/* prepare the descriptor for the next read */
				rd->len = rd->len_nxtfrm << 4;
				rd->len_nxtfrm = 0;
				/* treat all packet as event if we don't know */
				rd->channel = SDPCM_EVENT_CHANNEL;
				sdio_release_host(bus->sdiodev->func[1]);
				continue;
			}
			rd->len_left = rd->len > BRCMF_FIRSTREAD ?
				       rd->len - BRCMF_FIRSTREAD : 0;
			head_read = BRCMF_FIRSTREAD;
		}

		brcmf_pad(bus, &pad, &rd->len_left);

		pkt = brcmu_pkt_buf_get_skb(rd->len_left + head_read +
					    BRCMF_SDALIGN);
		if (!pkt) {
			/* Give up on data, request rtx of events */
			brcmf_err("brcmu_pkt_buf_get_skb failed\n");
			brcmf_sdbrcm_rxfail(bus, false,
					    RETRYCHAN(rd->channel));
			sdio_release_host(bus->sdiodev->func[1]);
			continue;
		}
		skb_pull(pkt, head_read);
		pkt_align(pkt, rd->len_left, BRCMF_SDALIGN);

		ret = brcmf_sdcard_recv_pkt(bus->sdiodev, bus->sdiodev->sbwad,
					      SDIO_FUNC_2, F2SYNC, pkt);
		bus->sdcnt.f2rxdata++;
		sdio_release_host(bus->sdiodev->func[1]);

		if (ret < 0) {
			brcmf_err("read %d bytes from channel %d failed: %d\n",
				  rd->len, rd->channel, ret);
			brcmu_pkt_buf_free_skb(pkt);
			sdio_claim_host(bus->sdiodev->func[1]);
			brcmf_sdbrcm_rxfail(bus, true,
					    RETRYCHAN(rd->channel));
			sdio_release_host(bus->sdiodev->func[1]);
			continue;
		}

		if (head_read) {
			skb_push(pkt, head_read);
			memcpy(pkt->data, bus->rxhdr, head_read);
			head_read = 0;
		} else {
			memcpy(bus->rxhdr, pkt->data, SDPCM_HDRLEN);
			rd_new.seq_num = rd->seq_num;
			sdio_claim_host(bus->sdiodev->func[1]);
			if (brcmf_sdio_hdparse(bus, bus->rxhdr, &rd_new,
					       BRCMF_SDIO_FT_NORMAL)) {
				rd->len = 0;
				brcmu_pkt_buf_free_skb(pkt);
			}
			bus->sdcnt.rx_readahead_cnt++;
			if (rd->len != roundup(rd_new.len, 16)) {
				brcmf_err("frame length mismatch:read %d, should be %d\n",
					  rd->len,
					  roundup(rd_new.len, 16) >> 4);
				rd->len = 0;
				brcmf_sdbrcm_rxfail(bus, true, true);
				sdio_release_host(bus->sdiodev->func[1]);
				brcmu_pkt_buf_free_skb(pkt);
				continue;
			}
			sdio_release_host(bus->sdiodev->func[1]);
			rd->len_nxtfrm = rd_new.len_nxtfrm;
			rd->channel = rd_new.channel;
			rd->dat_offset = rd_new.dat_offset;

			brcmf_dbg_hex_dump(!(BRCMF_BYTES_ON() &&
					     BRCMF_DATA_ON()) &&
					   BRCMF_HDRS_ON(),
					   bus->rxhdr, SDPCM_HDRLEN,
					   "RxHdr:\n");

			if (rd_new.channel == SDPCM_CONTROL_CHANNEL) {
				brcmf_err("readahead on control packet %d?\n",
					  rd_new.seq_num);
				/* Force retry w/normal header read */
				rd->len = 0;
				sdio_claim_host(bus->sdiodev->func[1]);
				brcmf_sdbrcm_rxfail(bus, false, true);
				sdio_release_host(bus->sdiodev->func[1]);
				brcmu_pkt_buf_free_skb(pkt);
				continue;
			}
		}

		brcmf_dbg_hex_dump(BRCMF_BYTES_ON() && BRCMF_DATA_ON(),
				   pkt->data, rd->len, "Rx Data:\n");

		/* Save superframe descriptor and allocate packet frame */
		if (rd->channel == SDPCM_GLOM_CHANNEL) {
			if (SDPCM_GLOMDESC(&bus->rxhdr[SDPCM_HWHDR_LEN])) {
				brcmf_dbg(GLOM, "glom descriptor, %d bytes:\n",
					  rd->len);
				brcmf_dbg_hex_dump(BRCMF_GLOM_ON(),
						   pkt->data, rd->len,
						   "Glom Data:\n");
				__skb_trim(pkt, rd->len);
				skb_pull(pkt, SDPCM_HDRLEN);
				bus->glomd = pkt;
			} else {
				brcmf_err("%s: glom superframe w/o "
					  "descriptor!\n", __func__);
				sdio_claim_host(bus->sdiodev->func[1]);
				brcmf_sdbrcm_rxfail(bus, false, false);
				sdio_release_host(bus->sdiodev->func[1]);
			}
			/* prepare the descriptor for the next read */
			rd->len = rd->len_nxtfrm << 4;
			rd->len_nxtfrm = 0;
			/* treat all packet as event if we don't know */
			rd->channel = SDPCM_EVENT_CHANNEL;
			continue;
		}

		/* Fill in packet len and prio, deliver upward */
		__skb_trim(pkt, rd->len);
		skb_pull(pkt, rd->dat_offset);

		/* prepare the descriptor for the next read */
		rd->len = rd->len_nxtfrm << 4;
		rd->len_nxtfrm = 0;
		/* treat all packet as event if we don't know */
		rd->channel = SDPCM_EVENT_CHANNEL;

		if (pkt->len == 0) {
			brcmu_pkt_buf_free_skb(pkt);
			continue;
		}

		brcmf_rx_frame(bus->sdiodev->dev, pkt);
	}

	rxcount = maxframes - rxleft;
	/* Message if we hit the limit */
	if (!rxleft)
		brcmf_dbg(DATA, "hit rx limit of %d frames\n", maxframes);
	else
		brcmf_dbg(DATA, "processed %d frames\n", rxcount);
	/* Back off rxseq if awaiting rtx, update rx_seq */
	if (bus->rxskip)
		rd->seq_num--;
	bus->rx_seq = rd->seq_num;

	return rxcount;
}

static void
brcmf_sdbrcm_wait_event_wakeup(struct brcmf_sdio *bus)
{
	if (waitqueue_active(&bus->ctrl_wait))
		wake_up_interruptible(&bus->ctrl_wait);
	return;
}

/**
 * struct brcmf_skbuff_cb reserves first two bytes in sk_buff::cb for
 * bus layer usage.
 */
/* flag marking a dummy skb added for DMA alignment requirement */
#define ALIGN_SKB_FLAG		0x8000
/* bit mask of data length chopped from the previous packet */
#define ALIGN_SKB_CHOP_LEN_MASK	0x7fff

static int brcmf_sdio_txpkt_prep_sg(struct brcmf_sdio_dev *sdiodev,
				    struct sk_buff_head *pktq,
				    struct sk_buff *pkt, uint chan)
{
	struct sk_buff *pkt_pad;
	u16 tail_pad, tail_chop, sg_align;
	unsigned int blksize;
	u8 *dat_buf;
	int ntail;

	blksize = sdiodev->func[SDIO_FUNC_2]->cur_blksize;
	sg_align = 4;
	if (sdiodev->pdata && sdiodev->pdata->sd_sgentry_align > 4)
		sg_align = sdiodev->pdata->sd_sgentry_align;
	/* sg entry alignment should be a divisor of block size */
	WARN_ON(blksize % sg_align);

	/* Check tail padding */
	pkt_pad = NULL;
	tail_chop = pkt->len % sg_align;
	tail_pad = sg_align - tail_chop;
	tail_pad += blksize - (pkt->len + tail_pad) % blksize;
	if (skb_tailroom(pkt) < tail_pad && pkt->len > blksize) {
		pkt_pad = brcmu_pkt_buf_get_skb(tail_pad + tail_chop);
		if (pkt_pad == NULL)
			return -ENOMEM;
		memcpy(pkt_pad->data,
		       pkt->data + pkt->len - tail_chop,
		       tail_chop);
		*(u32 *)(pkt_pad->cb) = ALIGN_SKB_FLAG + tail_chop;
		skb_trim(pkt, pkt->len - tail_chop);
		__skb_queue_after(pktq, pkt, pkt_pad);
	} else {
		ntail = pkt->data_len + tail_pad -
			(pkt->end - pkt->tail);
		if (skb_cloned(pkt) || ntail > 0)
			if (pskb_expand_head(pkt, 0, ntail, GFP_ATOMIC))
				return -ENOMEM;
		if (skb_linearize(pkt))
			return -ENOMEM;
		dat_buf = (u8 *)(pkt->data);
		__skb_put(pkt, tail_pad);
	}

	if (pkt_pad)
		return pkt->len + tail_chop;
	else
		return pkt->len - tail_pad;
}

/**
 * brcmf_sdio_txpkt_prep - packet preparation for transmit
 * @bus: brcmf_sdio structure pointer
 * @pktq: packet list pointer
 * @chan: virtual channel to transmit the packet
 *
 * Processes to be applied to the packet
 *	- Align data buffer pointer
 *	- Align data buffer length
 *	- Prepare header
 * Return: negative value if there is error
 */
static int
brcmf_sdio_txpkt_prep(struct brcmf_sdio *bus, struct sk_buff_head *pktq,
		      uint chan)
{
	u16 head_pad, head_align;
	struct sk_buff *pkt_next;
	u8 *dat_buf;
	int err;
	struct brcmf_sdio_hdrinfo hd_info = {0};

	/* SDIO ADMA requires at least 32 bit alignment */
	head_align = 4;
	if (bus->sdiodev->pdata && bus->sdiodev->pdata->sd_head_align > 4)
		head_align = bus->sdiodev->pdata->sd_head_align;

	pkt_next = pktq->next;
	dat_buf = (u8 *)(pkt_next->data);

	/* Check head padding */
	head_pad = ((unsigned long)dat_buf % head_align);
	if (head_pad) {
		if (skb_headroom(pkt_next) < head_pad) {
			bus->sdiodev->bus_if->tx_realloc++;
			head_pad = 0;
			if (skb_cow(pkt_next, head_pad))
				return -ENOMEM;
		}
		skb_push(pkt_next, head_pad);
		dat_buf = (u8 *)(pkt_next->data);
		memset(dat_buf, 0, head_pad + bus->tx_hdrlen);
	}

	if (bus->sdiodev->sg_support && pktq->qlen > 1) {
		err = brcmf_sdio_txpkt_prep_sg(bus->sdiodev, pktq,
					       pkt_next, chan);
		if (err < 0)
			return err;
		hd_info.len = (u16)err;
	} else {
		hd_info.len = pkt_next->len;
	}

	hd_info.channel = chan;
	hd_info.dat_offset = head_pad + bus->tx_hdrlen;

	/* Now fill the header */
	brcmf_sdio_hdpack(bus, dat_buf, &hd_info);

	if (BRCMF_BYTES_ON() &&
	    ((BRCMF_CTL_ON() && chan == SDPCM_CONTROL_CHANNEL) ||
	     (BRCMF_DATA_ON() && chan != SDPCM_CONTROL_CHANNEL)))
		brcmf_dbg_hex_dump(true, pkt_next, hd_info.len, "Tx Frame:\n");
	else if (BRCMF_HDRS_ON())
		brcmf_dbg_hex_dump(true, pkt_next, head_pad + bus->tx_hdrlen,
				   "Tx Header:\n");

	return 0;
}

/**
 * brcmf_sdio_txpkt_postp - packet post processing for transmit
 * @bus: brcmf_sdio structure pointer
 * @pktq: packet list pointer
 *
 * Processes to be applied to the packet
 *	- Remove head padding
 *	- Remove tail padding
 */
static void
brcmf_sdio_txpkt_postp(struct brcmf_sdio *bus, struct sk_buff_head *pktq)
{
	u8 *hdr;
	u32 dat_offset;
	u32 dummy_flags, chop_len;
	struct sk_buff *pkt_next, *tmp, *pkt_prev;

	skb_queue_walk_safe(pktq, pkt_next, tmp) {
		dummy_flags = *(u32 *)(pkt_next->cb);
		if (dummy_flags & ALIGN_SKB_FLAG) {
			chop_len = dummy_flags & ALIGN_SKB_CHOP_LEN_MASK;
			if (chop_len) {
				pkt_prev = pkt_next->prev;
				memcpy(pkt_prev->data + pkt_prev->len,
				       pkt_next->data, chop_len);
				skb_put(pkt_prev, chop_len);
			}
			__skb_unlink(pkt_next, pktq);
			brcmu_pkt_buf_free_skb(pkt_next);
		} else {
			hdr = pkt_next->data + SDPCM_HWHDR_LEN;
			dat_offset = le32_to_cpu(*(__le32 *)hdr);
			dat_offset = (dat_offset & SDPCM_DOFFSET_MASK) >>
				     SDPCM_DOFFSET_SHIFT;
			skb_pull(pkt_next, dat_offset);
		}
	}
}

/* Writes a HW/SW header into the packet and sends it. */
/* Assumes: (a) header space already there, (b) caller holds lock */
static int brcmf_sdbrcm_txpkt(struct brcmf_sdio *bus, struct sk_buff *pkt,
			      uint chan)
{
	int ret;
	int i;
	struct sk_buff_head localq;

	brcmf_dbg(TRACE, "Enter\n");

	__skb_queue_head_init(&localq);
	__skb_queue_tail(&localq, pkt);
	ret = brcmf_sdio_txpkt_prep(bus, &localq, chan);
	if (ret)
		goto done;

	sdio_claim_host(bus->sdiodev->func[1]);
	ret = brcmf_sdcard_send_pkt(bus->sdiodev, bus->sdiodev->sbwad,
				    SDIO_FUNC_2, F2SYNC, &localq);
	bus->sdcnt.f2txdata++;

	if (ret < 0) {
		/* On failure, abort the command and terminate the frame */
		brcmf_dbg(INFO, "sdio error %d, abort command and terminate frame\n",
			  ret);
		bus->sdcnt.tx_sderrs++;

		brcmf_sdcard_abort(bus->sdiodev, SDIO_FUNC_2);
		brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_FRAMECTRL,
				 SFC_WF_TERM, NULL);
		bus->sdcnt.f1regdata++;

		for (i = 0; i < 3; i++) {
			u8 hi, lo;
			hi = brcmf_sdio_regrb(bus->sdiodev,
					      SBSDIO_FUNC1_WFRAMEBCHI, NULL);
			lo = brcmf_sdio_regrb(bus->sdiodev,
					      SBSDIO_FUNC1_WFRAMEBCLO, NULL);
			bus->sdcnt.f1regdata += 2;
			if ((hi == 0) && (lo == 0))
				break;
		}

	}
	sdio_release_host(bus->sdiodev->func[1]);
	if (ret == 0)
		bus->tx_seq = (bus->tx_seq + 1) % SDPCM_SEQ_WRAP;

done:
	brcmf_sdio_txpkt_postp(bus, &localq);
	__skb_dequeue_tail(&localq);
	brcmf_txcomplete(bus->sdiodev->dev, pkt, ret == 0);
	return ret;
}

static uint brcmf_sdbrcm_sendfromq(struct brcmf_sdio *bus, uint maxframes)
{
	struct sk_buff *pkt;
	u32 intstatus = 0;
	int ret = 0, prec_out;
	uint cnt = 0;
	u8 tx_prec_map;

	brcmf_dbg(TRACE, "Enter\n");

	tx_prec_map = ~bus->flowcontrol;

	/* Send frames until the limit or some other event */
	for (cnt = 0; (cnt < maxframes) && data_ok(bus); cnt++) {
		spin_lock_bh(&bus->txqlock);
		pkt = brcmu_pktq_mdeq(&bus->txq, tx_prec_map, &prec_out);
		if (pkt == NULL) {
			spin_unlock_bh(&bus->txqlock);
			break;
		}
		spin_unlock_bh(&bus->txqlock);

		ret = brcmf_sdbrcm_txpkt(bus, pkt, SDPCM_DATA_CHANNEL);

		/* In poll mode, need to check for other events */
		if (!bus->intr && cnt) {
			/* Check device status, signal pending interrupt */
			sdio_claim_host(bus->sdiodev->func[1]);
			ret = r_sdreg32(bus, &intstatus,
					offsetof(struct sdpcmd_regs,
						 intstatus));
			sdio_release_host(bus->sdiodev->func[1]);
			bus->sdcnt.f2txdata++;
			if (ret != 0)
				break;
			if (intstatus & bus->hostintmask)
				atomic_set(&bus->ipend, 1);
		}
	}

	/* Deflow-control stack if needed */
	if ((bus->sdiodev->bus_if->state == BRCMF_BUS_DATA) &&
	    bus->txoff && (pktq_len(&bus->txq) < TXLOW)) {
		bus->txoff = false;
		brcmf_txflowblock(bus->sdiodev->dev, false);
	}

	return cnt;
}

static void brcmf_sdbrcm_bus_stop(struct device *dev)
{
	u32 local_hostintmask;
	u8 saveclk;
	int err;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	struct brcmf_sdio *bus = sdiodev->bus;

	brcmf_dbg(TRACE, "Enter\n");

	if (bus->watchdog_tsk) {
		send_sig(SIGTERM, bus->watchdog_tsk, 1);
		kthread_stop(bus->watchdog_tsk);
		bus->watchdog_tsk = NULL;
	}

	sdio_claim_host(bus->sdiodev->func[1]);

	/* Enable clock for device interrupts */
	brcmf_sdbrcm_bus_sleep(bus, false, false);

	/* Disable and clear interrupts at the chip level also */
	w_sdreg32(bus, 0, offsetof(struct sdpcmd_regs, hostintmask));
	local_hostintmask = bus->hostintmask;
	bus->hostintmask = 0;

	/* Change our idea of bus state */
	bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;

	/* Force clocks on backplane to be sure F2 interrupt propagates */
	saveclk = brcmf_sdio_regrb(bus->sdiodev,
				   SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (!err) {
		brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR,
				 (saveclk | SBSDIO_FORCE_HT), &err);
	}
	if (err)
		brcmf_err("Failed to force clock for F2: err %d\n", err);

	/* Turn off the bus (F2), free any pending packets */
	brcmf_dbg(INTR, "disable SDIO interrupts\n");
	brcmf_sdio_regwb(bus->sdiodev, SDIO_CCCR_IOEx, SDIO_FUNC_ENABLE_1,
			 NULL);

	/* Clear any pending interrupts now that F2 is disabled */
	w_sdreg32(bus, local_hostintmask,
		  offsetof(struct sdpcmd_regs, intstatus));

	/* Turn off the backplane clock (only) */
	brcmf_sdbrcm_clkctl(bus, CLK_SDONLY, false);
	sdio_release_host(bus->sdiodev->func[1]);

	/* Clear the data packet queues */
	brcmu_pktq_flush(&bus->txq, true, NULL, NULL);

	/* Clear any held glomming stuff */
	if (bus->glomd)
		brcmu_pkt_buf_free_skb(bus->glomd);
	brcmf_sdbrcm_free_glom(bus);

	/* Clear rx control and wake any waiters */
	spin_lock_bh(&bus->rxctl_lock);
	bus->rxlen = 0;
	spin_unlock_bh(&bus->rxctl_lock);
	brcmf_sdbrcm_dcmd_resp_wake(bus);

	/* Reset some F2 state stuff */
	bus->rxskip = false;
	bus->tx_seq = bus->rx_seq = 0;
}

static inline void brcmf_sdbrcm_clrintr(struct brcmf_sdio *bus)
{
	unsigned long flags;

	if (bus->sdiodev->oob_irq_requested) {
		spin_lock_irqsave(&bus->sdiodev->irq_en_lock, flags);
		if (!bus->sdiodev->irq_en && !atomic_read(&bus->ipend)) {
			enable_irq(bus->sdiodev->pdata->oob_irq_nr);
			bus->sdiodev->irq_en = true;
		}
		spin_unlock_irqrestore(&bus->sdiodev->irq_en_lock, flags);
	}
}

static int brcmf_sdio_intr_rstatus(struct brcmf_sdio *bus)
{
	u8 idx;
	u32 addr;
	unsigned long val;
	int n, ret;

	idx = brcmf_sdio_chip_getinfidx(bus->ci, BCMA_CORE_SDIO_DEV);
	addr = bus->ci->c_inf[idx].base +
	       offsetof(struct sdpcmd_regs, intstatus);

	ret = brcmf_sdio_regrw_helper(bus->sdiodev, addr, &val, false);
	bus->sdcnt.f1regdata++;
	if (ret != 0)
		val = 0;

	val &= bus->hostintmask;
	atomic_set(&bus->fcstate, !!(val & I_HMB_FC_STATE));

	/* Clear interrupts */
	if (val) {
		ret = brcmf_sdio_regrw_helper(bus->sdiodev, addr, &val, true);
		bus->sdcnt.f1regdata++;
	}

	if (ret) {
		atomic_set(&bus->intstatus, 0);
	} else if (val) {
		for_each_set_bit(n, &val, 32)
			set_bit(n, (unsigned long *)&bus->intstatus.counter);
	}

	return ret;
}

static void brcmf_sdbrcm_dpc(struct brcmf_sdio *bus)
{
	u32 newstatus = 0;
	unsigned long intstatus;
	uint rxlimit = bus->rxbound;	/* Rx frames to read before resched */
	uint txlimit = bus->txbound;	/* Tx frames to send before resched */
	uint framecnt = 0;	/* Temporary counter of tx/rx frames */
	int err = 0, n;

	brcmf_dbg(TRACE, "Enter\n");

	sdio_claim_host(bus->sdiodev->func[1]);

	/* If waiting for HTAVAIL, check status */
	if (!bus->sr_enabled && bus->clkstate == CLK_PENDING) {
		u8 clkctl, devctl = 0;

#ifdef DEBUG
		/* Check for inconsistent device control */
		devctl = brcmf_sdio_regrb(bus->sdiodev,
					  SBSDIO_DEVICE_CTL, &err);
		if (err) {
			brcmf_err("error reading DEVCTL: %d\n", err);
			bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
		}
#endif				/* DEBUG */

		/* Read CSR, if clock on switch to AVAIL, else ignore */
		clkctl = brcmf_sdio_regrb(bus->sdiodev,
					  SBSDIO_FUNC1_CHIPCLKCSR, &err);
		if (err) {
			brcmf_err("error reading CSR: %d\n",
				  err);
			bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
		}

		brcmf_dbg(SDIO, "DPC: PENDING, devctl 0x%02x clkctl 0x%02x\n",
			  devctl, clkctl);

		if (SBSDIO_HTAV(clkctl)) {
			devctl = brcmf_sdio_regrb(bus->sdiodev,
						  SBSDIO_DEVICE_CTL, &err);
			if (err) {
				brcmf_err("error reading DEVCTL: %d\n",
					  err);
				bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
			}
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			brcmf_sdio_regwb(bus->sdiodev, SBSDIO_DEVICE_CTL,
					 devctl, &err);
			if (err) {
				brcmf_err("error writing DEVCTL: %d\n",
					  err);
				bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
			}
			bus->clkstate = CLK_AVAIL;
		}
	}

	/* Make sure backplane clock is on */
	brcmf_sdbrcm_bus_sleep(bus, false, true);

	/* Pending interrupt indicates new device status */
	if (atomic_read(&bus->ipend) > 0) {
		atomic_set(&bus->ipend, 0);
		err = brcmf_sdio_intr_rstatus(bus);
	}

	/* Start with leftover status bits */
	intstatus = atomic_xchg(&bus->intstatus, 0);

	/* Handle flow-control change: read new state in case our ack
	 * crossed another change interrupt.  If change still set, assume
	 * FC ON for safety, let next loop through do the debounce.
	 */
	if (intstatus & I_HMB_FC_CHANGE) {
		intstatus &= ~I_HMB_FC_CHANGE;
		err = w_sdreg32(bus, I_HMB_FC_CHANGE,
				offsetof(struct sdpcmd_regs, intstatus));

		err = r_sdreg32(bus, &newstatus,
				offsetof(struct sdpcmd_regs, intstatus));
		bus->sdcnt.f1regdata += 2;
		atomic_set(&bus->fcstate,
			   !!(newstatus & (I_HMB_FC_STATE | I_HMB_FC_CHANGE)));
		intstatus |= (newstatus & bus->hostintmask);
	}

	/* Handle host mailbox indication */
	if (intstatus & I_HMB_HOST_INT) {
		intstatus &= ~I_HMB_HOST_INT;
		intstatus |= brcmf_sdbrcm_hostmail(bus);
	}

	sdio_release_host(bus->sdiodev->func[1]);

	/* Generally don't ask for these, can get CRC errors... */
	if (intstatus & I_WR_OOSYNC) {
		brcmf_err("Dongle reports WR_OOSYNC\n");
		intstatus &= ~I_WR_OOSYNC;
	}

	if (intstatus & I_RD_OOSYNC) {
		brcmf_err("Dongle reports RD_OOSYNC\n");
		intstatus &= ~I_RD_OOSYNC;
	}

	if (intstatus & I_SBINT) {
		brcmf_err("Dongle reports SBINT\n");
		intstatus &= ~I_SBINT;
	}

	/* Would be active due to wake-wlan in gSPI */
	if (intstatus & I_CHIPACTIVE) {
		brcmf_dbg(INFO, "Dongle reports CHIPACTIVE\n");
		intstatus &= ~I_CHIPACTIVE;
	}

	/* Ignore frame indications if rxskip is set */
	if (bus->rxskip)
		intstatus &= ~I_HMB_FRAME_IND;

	/* On frame indication, read available frames */
	if (PKT_AVAILABLE() && bus->clkstate == CLK_AVAIL) {
		framecnt = brcmf_sdio_readframes(bus, rxlimit);
		if (!bus->rxpending)
			intstatus &= ~I_HMB_FRAME_IND;
		rxlimit -= min(framecnt, rxlimit);
	}

	/* Keep still-pending events for next scheduling */
	if (intstatus) {
		for_each_set_bit(n, &intstatus, 32)
			set_bit(n, (unsigned long *)&bus->intstatus.counter);
	}

	brcmf_sdbrcm_clrintr(bus);

	if (data_ok(bus) && bus->ctrl_frame_stat &&
		(bus->clkstate == CLK_AVAIL)) {
		int i;

		sdio_claim_host(bus->sdiodev->func[1]);
		err = brcmf_sdcard_send_buf(bus->sdiodev, bus->sdiodev->sbwad,
			SDIO_FUNC_2, F2SYNC, bus->ctrl_frame_buf,
			(u32) bus->ctrl_frame_len);

		if (err < 0) {
			/* On failure, abort the command and
				terminate the frame */
			brcmf_dbg(INFO, "sdio error %d, abort command and terminate frame\n",
				  err);
			bus->sdcnt.tx_sderrs++;

			brcmf_sdcard_abort(bus->sdiodev, SDIO_FUNC_2);

			brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_FRAMECTRL,
					 SFC_WF_TERM, &err);
			bus->sdcnt.f1regdata++;

			for (i = 0; i < 3; i++) {
				u8 hi, lo;
				hi = brcmf_sdio_regrb(bus->sdiodev,
						      SBSDIO_FUNC1_WFRAMEBCHI,
						      &err);
				lo = brcmf_sdio_regrb(bus->sdiodev,
						      SBSDIO_FUNC1_WFRAMEBCLO,
						      &err);
				bus->sdcnt.f1regdata += 2;
				if ((hi == 0) && (lo == 0))
					break;
			}

		} else {
			bus->tx_seq = (bus->tx_seq + 1) % SDPCM_SEQ_WRAP;
		}
		sdio_release_host(bus->sdiodev->func[1]);
		bus->ctrl_frame_stat = false;
		brcmf_sdbrcm_wait_event_wakeup(bus);
	}
	/* Send queued frames (limit 1 if rx may still be pending) */
	else if ((bus->clkstate == CLK_AVAIL) && !atomic_read(&bus->fcstate) &&
		 brcmu_pktq_mlen(&bus->txq, ~bus->flowcontrol) && txlimit
		 && data_ok(bus)) {
		framecnt = bus->rxpending ? min(txlimit, bus->txminmax) :
					    txlimit;
		framecnt = brcmf_sdbrcm_sendfromq(bus, framecnt);
		txlimit -= framecnt;
	}

	if ((bus->sdiodev->bus_if->state == BRCMF_BUS_DOWN) || (err != 0)) {
		brcmf_err("failed backplane access over SDIO, halting operation\n");
		bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
		atomic_set(&bus->intstatus, 0);
	} else if (atomic_read(&bus->intstatus) ||
		   atomic_read(&bus->ipend) > 0 ||
		   (!atomic_read(&bus->fcstate) &&
		    brcmu_pktq_mlen(&bus->txq, ~bus->flowcontrol) &&
		    data_ok(bus)) || PKT_AVAILABLE()) {
		atomic_inc(&bus->dpc_tskcnt);
	}

	/* If we're done for now, turn off clock request. */
	if ((bus->clkstate != CLK_PENDING)
	    && bus->idletime == BRCMF_IDLE_IMMEDIATE) {
		bus->activity = false;
		brcmf_dbg(SDIO, "idle state\n");
		sdio_claim_host(bus->sdiodev->func[1]);
		brcmf_sdbrcm_bus_sleep(bus, true, false);
		sdio_release_host(bus->sdiodev->func[1]);
	}
}

static struct pktq *brcmf_sdbrcm_bus_gettxq(struct device *dev)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	struct brcmf_sdio *bus = sdiodev->bus;

	return &bus->txq;
}

static int brcmf_sdbrcm_bus_txdata(struct device *dev, struct sk_buff *pkt)
{
	int ret = -EBADE;
	uint datalen, prec;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	struct brcmf_sdio *bus = sdiodev->bus;
	ulong flags;

	brcmf_dbg(TRACE, "Enter\n");

	datalen = pkt->len;

	/* Add space for the header */
	skb_push(pkt, bus->tx_hdrlen);
	/* precondition: IS_ALIGNED((unsigned long)(pkt->data), 2) */

	prec = prio2prec((pkt->priority & PRIOMASK));

	/* Check for existing queue, current flow-control,
			 pending event, or pending clock */
	brcmf_dbg(TRACE, "deferring pktq len %d\n", pktq_len(&bus->txq));
	bus->sdcnt.fcqueued++;

	/* Priority based enq */
	spin_lock_irqsave(&bus->txqlock, flags);
	if (!brcmf_c_prec_enq(bus->sdiodev->dev, &bus->txq, pkt, prec)) {
		skb_pull(pkt, bus->tx_hdrlen);
		brcmf_err("out of bus->txq !!!\n");
		ret = -ENOSR;
	} else {
		ret = 0;
	}

	if (pktq_len(&bus->txq) >= TXHI) {
		bus->txoff = true;
		brcmf_txflowblock(bus->sdiodev->dev, true);
	}
	spin_unlock_irqrestore(&bus->txqlock, flags);

#ifdef DEBUG
	if (pktq_plen(&bus->txq, prec) > qcount[prec])
		qcount[prec] = pktq_plen(&bus->txq, prec);
#endif

	if (atomic_read(&bus->dpc_tskcnt) == 0) {
		atomic_inc(&bus->dpc_tskcnt);
		queue_work(bus->brcmf_wq, &bus->datawork);
	}

	return ret;
}

#ifdef DEBUG
#define CONSOLE_LINE_MAX	192

static int brcmf_sdbrcm_readconsole(struct brcmf_sdio *bus)
{
	struct brcmf_console *c = &bus->console;
	u8 line[CONSOLE_LINE_MAX], ch;
	u32 n, idx, addr;
	int rv;

	/* Don't do anything until FWREADY updates console address */
	if (bus->console_addr == 0)
		return 0;

	/* Read console log struct */
	addr = bus->console_addr + offsetof(struct rte_console, log_le);
	rv = brcmf_sdio_ramrw(bus->sdiodev, false, addr, (u8 *)&c->log_le,
			      sizeof(c->log_le));
	if (rv < 0)
		return rv;

	/* Allocate console buffer (one time only) */
	if (c->buf == NULL) {
		c->bufsize = le32_to_cpu(c->log_le.buf_size);
		c->buf = kmalloc(c->bufsize, GFP_ATOMIC);
		if (c->buf == NULL)
			return -ENOMEM;
	}

	idx = le32_to_cpu(c->log_le.idx);

	/* Protect against corrupt value */
	if (idx > c->bufsize)
		return -EBADE;

	/* Skip reading the console buffer if the index pointer
	 has not moved */
	if (idx == c->last)
		return 0;

	/* Read the console buffer */
	addr = le32_to_cpu(c->log_le.buf);
	rv = brcmf_sdio_ramrw(bus->sdiodev, false, addr, c->buf, c->bufsize);
	if (rv < 0)
		return rv;

	while (c->last != idx) {
		for (n = 0; n < CONSOLE_LINE_MAX - 2; n++) {
			if (c->last == idx) {
				/* This would output a partial line.
				 * Instead, back up
				 * the buffer pointer and output this
				 * line next time around.
				 */
				if (c->last >= n)
					c->last -= n;
				else
					c->last = c->bufsize - n;
				goto break2;
			}
			ch = c->buf[c->last];
			c->last = (c->last + 1) % c->bufsize;
			if (ch == '\n')
				break;
			line[n] = ch;
		}

		if (n > 0) {
			if (line[n - 1] == '\r')
				n--;
			line[n] = 0;
			pr_debug("CONSOLE: %s\n", line);
		}
	}
break2:

	return 0;
}
#endif				/* DEBUG */

static int brcmf_tx_frame(struct brcmf_sdio *bus, u8 *frame, u16 len)
{
	int i;
	int ret;

	bus->ctrl_frame_stat = false;
	ret = brcmf_sdcard_send_buf(bus->sdiodev, bus->sdiodev->sbwad,
				    SDIO_FUNC_2, F2SYNC, frame, len);

	if (ret < 0) {
		/* On failure, abort the command and terminate the frame */
		brcmf_dbg(INFO, "sdio error %d, abort command and terminate frame\n",
			  ret);
		bus->sdcnt.tx_sderrs++;

		brcmf_sdcard_abort(bus->sdiodev, SDIO_FUNC_2);

		brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_FRAMECTRL,
				 SFC_WF_TERM, NULL);
		bus->sdcnt.f1regdata++;

		for (i = 0; i < 3; i++) {
			u8 hi, lo;
			hi = brcmf_sdio_regrb(bus->sdiodev,
					      SBSDIO_FUNC1_WFRAMEBCHI, NULL);
			lo = brcmf_sdio_regrb(bus->sdiodev,
					      SBSDIO_FUNC1_WFRAMEBCLO, NULL);
			bus->sdcnt.f1regdata += 2;
			if (hi == 0 && lo == 0)
				break;
		}
		return ret;
	}

	bus->tx_seq = (bus->tx_seq + 1) % SDPCM_SEQ_WRAP;

	return ret;
}

static int
brcmf_sdbrcm_bus_txctl(struct device *dev, unsigned char *msg, uint msglen)
{
	u8 *frame;
	u16 len;
	uint retries = 0;
	u8 doff = 0;
	int ret = -1;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	struct brcmf_sdio *bus = sdiodev->bus;
	struct brcmf_sdio_hdrinfo hd_info = {0};

	brcmf_dbg(TRACE, "Enter\n");

	/* Back the pointer to make a room for bus header */
	frame = msg - bus->tx_hdrlen;
	len = (msglen += bus->tx_hdrlen);

	/* Add alignment padding (optional for ctl frames) */
	doff = ((unsigned long)frame % BRCMF_SDALIGN);
	if (doff) {
		frame -= doff;
		len += doff;
		msglen += doff;
		memset(frame, 0, doff + bus->tx_hdrlen);
	}
	/* precondition: doff < BRCMF_SDALIGN */
	doff += bus->tx_hdrlen;

	/* Round send length to next SDIO block */
	if (bus->roundup && bus->blocksize && (len > bus->blocksize)) {
		u16 pad = bus->blocksize - (len % bus->blocksize);
		if ((pad <= bus->roundup) && (pad < bus->blocksize))
			len += pad;
	} else if (len % BRCMF_SDALIGN) {
		len += BRCMF_SDALIGN - (len % BRCMF_SDALIGN);
	}

	/* Satisfy length-alignment requirements */
	if (len & (ALIGNMENT - 1))
		len = roundup(len, ALIGNMENT);

	/* precondition: IS_ALIGNED((unsigned long)frame, 2) */

	/* Make sure backplane clock is on */
	sdio_claim_host(bus->sdiodev->func[1]);
	brcmf_sdbrcm_bus_sleep(bus, false, false);
	sdio_release_host(bus->sdiodev->func[1]);

	hd_info.len = (u16)msglen;
	hd_info.channel = SDPCM_CONTROL_CHANNEL;
	hd_info.dat_offset = doff;
	brcmf_sdio_hdpack(bus, frame, &hd_info);

	if (!data_ok(bus)) {
		brcmf_dbg(INFO, "No bus credit bus->tx_max %d, bus->tx_seq %d\n",
			  bus->tx_max, bus->tx_seq);
		bus->ctrl_frame_stat = true;
		/* Send from dpc */
		bus->ctrl_frame_buf = frame;
		bus->ctrl_frame_len = len;

		wait_event_interruptible_timeout(bus->ctrl_wait,
						 !bus->ctrl_frame_stat,
						 msecs_to_jiffies(2000));

		if (!bus->ctrl_frame_stat) {
			brcmf_dbg(SDIO, "ctrl_frame_stat == false\n");
			ret = 0;
		} else {
			brcmf_dbg(SDIO, "ctrl_frame_stat == true\n");
			ret = -1;
		}
	}

	if (ret == -1) {
		brcmf_dbg_hex_dump(BRCMF_BYTES_ON() && BRCMF_CTL_ON(),
				   frame, len, "Tx Frame:\n");
		brcmf_dbg_hex_dump(!(BRCMF_BYTES_ON() && BRCMF_CTL_ON()) &&
				   BRCMF_HDRS_ON(),
				   frame, min_t(u16, len, 16), "TxHdr:\n");

		do {
			sdio_claim_host(bus->sdiodev->func[1]);
			ret = brcmf_tx_frame(bus, frame, len);
			sdio_release_host(bus->sdiodev->func[1]);
		} while (ret < 0 && retries++ < TXRETRIES);
	}

	if ((bus->idletime == BRCMF_IDLE_IMMEDIATE) &&
	    atomic_read(&bus->dpc_tskcnt) == 0) {
		bus->activity = false;
		sdio_claim_host(bus->sdiodev->func[1]);
		brcmf_dbg(INFO, "idle\n");
		brcmf_sdbrcm_clkctl(bus, CLK_NONE, true);
		sdio_release_host(bus->sdiodev->func[1]);
	}

	if (ret)
		bus->sdcnt.tx_ctlerrs++;
	else
		bus->sdcnt.tx_ctlpkts++;

	return ret ? -EIO : 0;
}

#ifdef DEBUG
static inline bool brcmf_sdio_valid_shared_address(u32 addr)
{
	return !(addr == 0 || ((~addr >> 16) & 0xffff) == (addr & 0xffff));
}

static int brcmf_sdio_readshared(struct brcmf_sdio *bus,
				 struct sdpcm_shared *sh)
{
	u32 addr;
	int rv;
	u32 shaddr = 0;
	struct sdpcm_shared_le sh_le;
	__le32 addr_le;

	shaddr = bus->ci->rambase + bus->ramsize - 4;

	/*
	 * Read last word in socram to determine
	 * address of sdpcm_shared structure
	 */
	sdio_claim_host(bus->sdiodev->func[1]);
	brcmf_sdbrcm_bus_sleep(bus, false, false);
	rv = brcmf_sdio_ramrw(bus->sdiodev, false, shaddr, (u8 *)&addr_le, 4);
	sdio_release_host(bus->sdiodev->func[1]);
	if (rv < 0)
		return rv;

	addr = le32_to_cpu(addr_le);

	brcmf_dbg(SDIO, "sdpcm_shared address 0x%08X\n", addr);

	/*
	 * Check if addr is valid.
	 * NVRAM length at the end of memory should have been overwritten.
	 */
	if (!brcmf_sdio_valid_shared_address(addr)) {
			brcmf_err("invalid sdpcm_shared address 0x%08X\n",
				  addr);
			return -EINVAL;
	}

	/* Read hndrte_shared structure */
	rv = brcmf_sdio_ramrw(bus->sdiodev, false, addr, (u8 *)&sh_le,
			      sizeof(struct sdpcm_shared_le));
	if (rv < 0)
		return rv;

	/* Endianness */
	sh->flags = le32_to_cpu(sh_le.flags);
	sh->trap_addr = le32_to_cpu(sh_le.trap_addr);
	sh->assert_exp_addr = le32_to_cpu(sh_le.assert_exp_addr);
	sh->assert_file_addr = le32_to_cpu(sh_le.assert_file_addr);
	sh->assert_line = le32_to_cpu(sh_le.assert_line);
	sh->console_addr = le32_to_cpu(sh_le.console_addr);
	sh->msgtrace_addr = le32_to_cpu(sh_le.msgtrace_addr);

	if ((sh->flags & SDPCM_SHARED_VERSION_MASK) > SDPCM_SHARED_VERSION) {
		brcmf_err("sdpcm shared version unsupported: dhd %d dongle %d\n",
			  SDPCM_SHARED_VERSION,
			  sh->flags & SDPCM_SHARED_VERSION_MASK);
		return -EPROTO;
	}

	return 0;
}

static int brcmf_sdio_dump_console(struct brcmf_sdio *bus,
				   struct sdpcm_shared *sh, char __user *data,
				   size_t count)
{
	u32 addr, console_ptr, console_size, console_index;
	char *conbuf = NULL;
	__le32 sh_val;
	int rv;
	loff_t pos = 0;
	int nbytes = 0;

	/* obtain console information from device memory */
	addr = sh->console_addr + offsetof(struct rte_console, log_le);
	rv = brcmf_sdio_ramrw(bus->sdiodev, false, addr,
			      (u8 *)&sh_val, sizeof(u32));
	if (rv < 0)
		return rv;
	console_ptr = le32_to_cpu(sh_val);

	addr = sh->console_addr + offsetof(struct rte_console, log_le.buf_size);
	rv = brcmf_sdio_ramrw(bus->sdiodev, false, addr,
			      (u8 *)&sh_val, sizeof(u32));
	if (rv < 0)
		return rv;
	console_size = le32_to_cpu(sh_val);

	addr = sh->console_addr + offsetof(struct rte_console, log_le.idx);
	rv = brcmf_sdio_ramrw(bus->sdiodev, false, addr,
			      (u8 *)&sh_val, sizeof(u32));
	if (rv < 0)
		return rv;
	console_index = le32_to_cpu(sh_val);

	/* allocate buffer for console data */
	if (console_size <= CONSOLE_BUFFER_MAX)
		conbuf = vzalloc(console_size+1);

	if (!conbuf)
		return -ENOMEM;

	/* obtain the console data from device */
	conbuf[console_size] = '\0';
	rv = brcmf_sdio_ramrw(bus->sdiodev, false, console_ptr, (u8 *)conbuf,
			      console_size);
	if (rv < 0)
		goto done;

	rv = simple_read_from_buffer(data, count, &pos,
				     conbuf + console_index,
				     console_size - console_index);
	if (rv < 0)
		goto done;

	nbytes = rv;
	if (console_index > 0) {
		pos = 0;
		rv = simple_read_from_buffer(data+nbytes, count, &pos,
					     conbuf, console_index - 1);
		if (rv < 0)
			goto done;
		rv += nbytes;
	}
done:
	vfree(conbuf);
	return rv;
}

static int brcmf_sdio_trap_info(struct brcmf_sdio *bus, struct sdpcm_shared *sh,
				char __user *data, size_t count)
{
	int error, res;
	char buf[350];
	struct brcmf_trap_info tr;
	loff_t pos = 0;

	if ((sh->flags & SDPCM_SHARED_TRAP) == 0) {
		brcmf_dbg(INFO, "no trap in firmware\n");
		return 0;
	}

	error = brcmf_sdio_ramrw(bus->sdiodev, false, sh->trap_addr, (u8 *)&tr,
				 sizeof(struct brcmf_trap_info));
	if (error < 0)
		return error;

	res = scnprintf(buf, sizeof(buf),
			"dongle trap info: type 0x%x @ epc 0x%08x\n"
			"  cpsr 0x%08x spsr 0x%08x sp 0x%08x\n"
			"  lr   0x%08x pc   0x%08x offset 0x%x\n"
			"  r0   0x%08x r1   0x%08x r2 0x%08x r3 0x%08x\n"
			"  r4   0x%08x r5   0x%08x r6 0x%08x r7 0x%08x\n",
			le32_to_cpu(tr.type), le32_to_cpu(tr.epc),
			le32_to_cpu(tr.cpsr), le32_to_cpu(tr.spsr),
			le32_to_cpu(tr.r13), le32_to_cpu(tr.r14),
			le32_to_cpu(tr.pc), sh->trap_addr,
			le32_to_cpu(tr.r0), le32_to_cpu(tr.r1),
			le32_to_cpu(tr.r2), le32_to_cpu(tr.r3),
			le32_to_cpu(tr.r4), le32_to_cpu(tr.r5),
			le32_to_cpu(tr.r6), le32_to_cpu(tr.r7));

	return simple_read_from_buffer(data, count, &pos, buf, res);
}

static int brcmf_sdio_assert_info(struct brcmf_sdio *bus,
				  struct sdpcm_shared *sh, char __user *data,
				  size_t count)
{
	int error = 0;
	char buf[200];
	char file[80] = "?";
	char expr[80] = "<???>";
	int res;
	loff_t pos = 0;

	if ((sh->flags & SDPCM_SHARED_ASSERT_BUILT) == 0) {
		brcmf_dbg(INFO, "firmware not built with -assert\n");
		return 0;
	} else if ((sh->flags & SDPCM_SHARED_ASSERT) == 0) {
		brcmf_dbg(INFO, "no assert in dongle\n");
		return 0;
	}

	sdio_claim_host(bus->sdiodev->func[1]);
	if (sh->assert_file_addr != 0) {
		error = brcmf_sdio_ramrw(bus->sdiodev, false,
					 sh->assert_file_addr, (u8 *)file, 80);
		if (error < 0)
			return error;
	}
	if (sh->assert_exp_addr != 0) {
		error = brcmf_sdio_ramrw(bus->sdiodev, false,
					 sh->assert_exp_addr, (u8 *)expr, 80);
		if (error < 0)
			return error;
	}
	sdio_release_host(bus->sdiodev->func[1]);

	res = scnprintf(buf, sizeof(buf),
			"dongle assert: %s:%d: assert(%s)\n",
			file, sh->assert_line, expr);
	return simple_read_from_buffer(data, count, &pos, buf, res);
}

static int brcmf_sdbrcm_checkdied(struct brcmf_sdio *bus)
{
	int error;
	struct sdpcm_shared sh;

	error = brcmf_sdio_readshared(bus, &sh);

	if (error < 0)
		return error;

	if ((sh.flags & SDPCM_SHARED_ASSERT_BUILT) == 0)
		brcmf_dbg(INFO, "firmware not built with -assert\n");
	else if (sh.flags & SDPCM_SHARED_ASSERT)
		brcmf_err("assertion in dongle\n");

	if (sh.flags & SDPCM_SHARED_TRAP)
		brcmf_err("firmware trap in dongle\n");

	return 0;
}

static int brcmf_sdbrcm_died_dump(struct brcmf_sdio *bus, char __user *data,
				  size_t count, loff_t *ppos)
{
	int error = 0;
	struct sdpcm_shared sh;
	int nbytes = 0;
	loff_t pos = *ppos;

	if (pos != 0)
		return 0;

	error = brcmf_sdio_readshared(bus, &sh);
	if (error < 0)
		goto done;

	error = brcmf_sdio_assert_info(bus, &sh, data, count);
	if (error < 0)
		goto done;
	nbytes = error;

	error = brcmf_sdio_trap_info(bus, &sh, data+nbytes, count);
	if (error < 0)
		goto done;
	nbytes += error;

	error = brcmf_sdio_dump_console(bus, &sh, data+nbytes, count);
	if (error < 0)
		goto done;
	nbytes += error;

	error = nbytes;
	*ppos += nbytes;
done:
	return error;
}

static ssize_t brcmf_sdio_forensic_read(struct file *f, char __user *data,
					size_t count, loff_t *ppos)
{
	struct brcmf_sdio *bus = f->private_data;
	int res;

	res = brcmf_sdbrcm_died_dump(bus, data, count, ppos);
	if (res > 0)
		*ppos += res;
	return (ssize_t)res;
}

static const struct file_operations brcmf_sdio_forensic_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = brcmf_sdio_forensic_read
};

static void brcmf_sdio_debugfs_create(struct brcmf_sdio *bus)
{
	struct brcmf_pub *drvr = bus->sdiodev->bus_if->drvr;
	struct dentry *dentry = brcmf_debugfs_get_devdir(drvr);

	if (IS_ERR_OR_NULL(dentry))
		return;

	debugfs_create_file("forensics", S_IRUGO, dentry, bus,
			    &brcmf_sdio_forensic_ops);
	brcmf_debugfs_create_sdio_count(drvr, &bus->sdcnt);
}
#else
static int brcmf_sdbrcm_checkdied(struct brcmf_sdio *bus)
{
	return 0;
}

static void brcmf_sdio_debugfs_create(struct brcmf_sdio *bus)
{
}
#endif /* DEBUG */

static int
brcmf_sdbrcm_bus_rxctl(struct device *dev, unsigned char *msg, uint msglen)
{
	int timeleft;
	uint rxlen = 0;
	bool pending;
	u8 *buf;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	struct brcmf_sdio *bus = sdiodev->bus;

	brcmf_dbg(TRACE, "Enter\n");

	/* Wait until control frame is available */
	timeleft = brcmf_sdbrcm_dcmd_resp_wait(bus, &bus->rxlen, &pending);

	spin_lock_bh(&bus->rxctl_lock);
	rxlen = bus->rxlen;
	memcpy(msg, bus->rxctl, min(msglen, rxlen));
	bus->rxctl = NULL;
	buf = bus->rxctl_orig;
	bus->rxctl_orig = NULL;
	bus->rxlen = 0;
	spin_unlock_bh(&bus->rxctl_lock);
	vfree(buf);

	if (rxlen) {
		brcmf_dbg(CTL, "resumed on rxctl frame, got %d expected %d\n",
			  rxlen, msglen);
	} else if (timeleft == 0) {
		brcmf_err("resumed on timeout\n");
		brcmf_sdbrcm_checkdied(bus);
	} else if (pending) {
		brcmf_dbg(CTL, "cancelled\n");
		return -ERESTARTSYS;
	} else {
		brcmf_dbg(CTL, "resumed for unknown reason?\n");
		brcmf_sdbrcm_checkdied(bus);
	}

	if (rxlen)
		bus->sdcnt.rx_ctlpkts++;
	else
		bus->sdcnt.rx_ctlerrs++;

	return rxlen ? (int)rxlen : -ETIMEDOUT;
}

static bool brcmf_sdbrcm_download_state(struct brcmf_sdio *bus, bool enter)
{
	struct chip_info *ci = bus->ci;

	/* To enter download state, disable ARM and reset SOCRAM.
	 * To exit download state, simply reset ARM (default is RAM boot).
	 */
	if (enter) {
		bus->alp_only = true;

		brcmf_sdio_chip_enter_download(bus->sdiodev, ci);
	} else {
		if (!brcmf_sdio_chip_exit_download(bus->sdiodev, ci, bus->vars,
						   bus->varsz))
			return false;

		/* Allow HT Clock now that the ARM is running. */
		bus->alp_only = false;

		bus->sdiodev->bus_if->state = BRCMF_BUS_LOAD;
	}

	return true;
}

static int brcmf_sdbrcm_download_code_file(struct brcmf_sdio *bus)
{
	const struct firmware *fw;
	int err;
	int offset;
	int address;
	int len;

	fw = brcmf_sdbrcm_get_fw(bus, BRCMF_FIRMWARE_BIN);
	if (fw == NULL)
		return -ENOENT;

	if (brcmf_sdio_chip_getinfidx(bus->ci, BCMA_CORE_ARM_CR4) !=
	    BRCMF_MAX_CORENUM)
		memcpy(&bus->ci->rst_vec, fw->data, sizeof(bus->ci->rst_vec));

	err = 0;
	offset = 0;
	address = bus->ci->rambase;
	while (offset < fw->size) {
		len = ((offset + MEMBLOCK) < fw->size) ? MEMBLOCK :
		      fw->size - offset;
		err = brcmf_sdio_ramrw(bus->sdiodev, true, address,
				       (u8 *)&fw->data[offset], len);
		if (err) {
			brcmf_err("error %d on writing %d membytes at 0x%08x\n",
				  err, len, address);
			goto failure;
		}
		offset += len;
		address += len;
	}

failure:
	release_firmware(fw);

	return err;
}

/*
 * ProcessVars:Takes a buffer of "<var>=<value>\n" lines read from a file
 * and ending in a NUL.
 * Removes carriage returns, empty lines, comment lines, and converts
 * newlines to NULs.
 * Shortens buffer as needed and pads with NULs.  End of buffer is marked
 * by two NULs.
*/

static int brcmf_process_nvram_vars(struct brcmf_sdio *bus,
				    const struct firmware *nv)
{
	char *varbuf;
	char *dp;
	bool findNewline;
	int column;
	int ret = 0;
	uint buf_len, n, len;

	len = nv->size;
	varbuf = vmalloc(len);
	if (!varbuf)
		return -ENOMEM;

	memcpy(varbuf, nv->data, len);
	dp = varbuf;

	findNewline = false;
	column = 0;

	for (n = 0; n < len; n++) {
		if (varbuf[n] == 0)
			break;
		if (varbuf[n] == '\r')
			continue;
		if (findNewline && varbuf[n] != '\n')
			continue;
		findNewline = false;
		if (varbuf[n] == '#') {
			findNewline = true;
			continue;
		}
		if (varbuf[n] == '\n') {
			if (column == 0)
				continue;
			*dp++ = 0;
			column = 0;
			continue;
		}
		*dp++ = varbuf[n];
		column++;
	}
	buf_len = dp - varbuf;
	while (dp < varbuf + n)
		*dp++ = 0;

	kfree(bus->vars);
	/* roundup needed for download to device */
	bus->varsz = roundup(buf_len + 1, 4);
	bus->vars = kmalloc(bus->varsz, GFP_KERNEL);
	if (bus->vars == NULL) {
		bus->varsz = 0;
		ret = -ENOMEM;
		goto err;
	}

	/* copy the processed variables and add null termination */
	memcpy(bus->vars, varbuf, buf_len);
	bus->vars[buf_len] = 0;
err:
	vfree(varbuf);
	return ret;
}

static int brcmf_sdbrcm_download_nvram(struct brcmf_sdio *bus)
{
	const struct firmware *nv;
	int ret;

	nv = brcmf_sdbrcm_get_fw(bus, BRCMF_FIRMWARE_NVRAM);
	if (nv == NULL)
		return -ENOENT;

	ret = brcmf_process_nvram_vars(bus, nv);

	release_firmware(nv);

	return ret;
}

static int _brcmf_sdbrcm_download_firmware(struct brcmf_sdio *bus)
{
	int bcmerror = -1;

	/* Keep arm in reset */
	if (!brcmf_sdbrcm_download_state(bus, true)) {
		brcmf_err("error placing ARM core in reset\n");
		goto err;
	}

	if (brcmf_sdbrcm_download_code_file(bus)) {
		brcmf_err("dongle image file download failed\n");
		goto err;
	}

	if (brcmf_sdbrcm_download_nvram(bus)) {
		brcmf_err("dongle nvram file download failed\n");
		goto err;
	}

	/* Take arm out of reset */
	if (!brcmf_sdbrcm_download_state(bus, false)) {
		brcmf_err("error getting out of ARM core reset\n");
		goto err;
	}

	bcmerror = 0;

err:
	return bcmerror;
}

static bool brcmf_sdbrcm_sr_capable(struct brcmf_sdio *bus)
{
	u32 addr, reg;

	brcmf_dbg(TRACE, "Enter\n");

	/* old chips with PMU version less than 17 don't support save restore */
	if (bus->ci->pmurev < 17)
		return false;

	/* read PMU chipcontrol register 3*/
	addr = CORE_CC_REG(bus->ci->c_inf[0].base, chipcontrol_addr);
	brcmf_sdio_regwl(bus->sdiodev, addr, 3, NULL);
	addr = CORE_CC_REG(bus->ci->c_inf[0].base, chipcontrol_data);
	reg = brcmf_sdio_regrl(bus->sdiodev, addr, NULL);

	return (bool)reg;
}

static void brcmf_sdbrcm_sr_init(struct brcmf_sdio *bus)
{
	int err = 0;
	u8 val;

	brcmf_dbg(TRACE, "Enter\n");

	val = brcmf_sdio_regrb(bus->sdiodev, SBSDIO_FUNC1_WAKEUPCTRL,
			       &err);
	if (err) {
		brcmf_err("error reading SBSDIO_FUNC1_WAKEUPCTRL\n");
		return;
	}

	val |= 1 << SBSDIO_FUNC1_WCTRL_HTWAIT_SHIFT;
	brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_WAKEUPCTRL,
			 val, &err);
	if (err) {
		brcmf_err("error writing SBSDIO_FUNC1_WAKEUPCTRL\n");
		return;
	}

	/* Add CMD14 Support */
	brcmf_sdio_regwb(bus->sdiodev, SDIO_CCCR_BRCM_CARDCAP,
			 (SDIO_CCCR_BRCM_CARDCAP_CMD14_SUPPORT |
			  SDIO_CCCR_BRCM_CARDCAP_CMD14_EXT),
			 &err);
	if (err) {
		brcmf_err("error writing SDIO_CCCR_BRCM_CARDCAP\n");
		return;
	}

	brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR,
			 SBSDIO_FORCE_HT, &err);
	if (err) {
		brcmf_err("error writing SBSDIO_FUNC1_CHIPCLKCSR\n");
		return;
	}

	/* set flag */
	bus->sr_enabled = true;
	brcmf_dbg(INFO, "SR enabled\n");
}

/* enable KSO bit */
static int brcmf_sdbrcm_kso_init(struct brcmf_sdio *bus)
{
	u8 val;
	int err = 0;

	brcmf_dbg(TRACE, "Enter\n");

	/* KSO bit added in SDIO core rev 12 */
	if (bus->ci->c_inf[1].rev < 12)
		return 0;

	val = brcmf_sdio_regrb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR,
			       &err);
	if (err) {
		brcmf_err("error reading SBSDIO_FUNC1_SLEEPCSR\n");
		return err;
	}

	if (!(val & SBSDIO_FUNC1_SLEEPCSR_KSO_MASK)) {
		val |= (SBSDIO_FUNC1_SLEEPCSR_KSO_EN <<
			SBSDIO_FUNC1_SLEEPCSR_KSO_SHIFT);
		brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_SLEEPCSR,
				 val, &err);
		if (err) {
			brcmf_err("error writing SBSDIO_FUNC1_SLEEPCSR\n");
			return err;
		}
	}

	return 0;
}


static bool
brcmf_sdbrcm_download_firmware(struct brcmf_sdio *bus)
{
	bool ret;

	sdio_claim_host(bus->sdiodev->func[1]);

	brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);

	ret = _brcmf_sdbrcm_download_firmware(bus) == 0;

	brcmf_sdbrcm_clkctl(bus, CLK_SDONLY, false);

	sdio_release_host(bus->sdiodev->func[1]);

	return ret;
}

static int brcmf_sdbrcm_bus_init(struct device *dev)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	struct brcmf_sdio *bus = sdiodev->bus;
	unsigned long timeout;
	u8 ready, enable;
	int err, ret = 0;
	u8 saveclk;

	brcmf_dbg(TRACE, "Enter\n");

	/* try to download image and nvram to the dongle */
	if (bus_if->state == BRCMF_BUS_DOWN) {
		if (!(brcmf_sdbrcm_download_firmware(bus)))
			return -1;
	}

	if (!bus->sdiodev->bus_if->drvr)
		return 0;

	/* Start the watchdog timer */
	bus->sdcnt.tickcnt = 0;
	brcmf_sdbrcm_wd_timer(bus, BRCMF_WD_POLL_MS);

	sdio_claim_host(bus->sdiodev->func[1]);

	/* Make sure backplane clock is on, needed to generate F2 interrupt */
	brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);
	if (bus->clkstate != CLK_AVAIL)
		goto exit;

	/* Force clocks on backplane to be sure F2 interrupt propagates */
	saveclk = brcmf_sdio_regrb(bus->sdiodev,
				   SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (!err) {
		brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR,
				 (saveclk | SBSDIO_FORCE_HT), &err);
	}
	if (err) {
		brcmf_err("Failed to force clock for F2: err %d\n", err);
		goto exit;
	}

	/* Enable function 2 (frame transfers) */
	w_sdreg32(bus, SDPCM_PROT_VERSION << SMB_DATA_VERSION_SHIFT,
		  offsetof(struct sdpcmd_regs, tosbmailboxdata));
	enable = (SDIO_FUNC_ENABLE_1 | SDIO_FUNC_ENABLE_2);

	brcmf_sdio_regwb(bus->sdiodev, SDIO_CCCR_IOEx, enable, NULL);

	timeout = jiffies + msecs_to_jiffies(BRCMF_WAIT_F2RDY);
	ready = 0;
	while (enable != ready) {
		ready = brcmf_sdio_regrb(bus->sdiodev,
					 SDIO_CCCR_IORx, NULL);
		if (time_after(jiffies, timeout))
			break;
		else if (time_after(jiffies, timeout - BRCMF_WAIT_F2RDY + 50))
			/* prevent busy waiting if it takes too long */
			msleep_interruptible(20);
	}

	brcmf_dbg(INFO, "enable 0x%02x, ready 0x%02x\n", enable, ready);

	/* If F2 successfully enabled, set core and enable interrupts */
	if (ready == enable) {
		/* Set up the interrupt mask and enable interrupts */
		bus->hostintmask = HOSTINTMASK;
		w_sdreg32(bus, bus->hostintmask,
			  offsetof(struct sdpcmd_regs, hostintmask));

		brcmf_sdio_regwb(bus->sdiodev, SBSDIO_WATERMARK, 8, &err);
	} else {
		/* Disable F2 again */
		enable = SDIO_FUNC_ENABLE_1;
		brcmf_sdio_regwb(bus->sdiodev, SDIO_CCCR_IOEx, enable, NULL);
		ret = -ENODEV;
	}

	if (brcmf_sdbrcm_sr_capable(bus)) {
		brcmf_sdbrcm_sr_init(bus);
	} else {
		/* Restore previous clock setting */
		brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR,
				 saveclk, &err);
	}

	if (ret == 0) {
		ret = brcmf_sdio_intr_register(bus->sdiodev);
		if (ret != 0)
			brcmf_err("intr register failed:%d\n", ret);
	}

	/* If we didn't come up, turn off backplane clock */
	if (bus_if->state != BRCMF_BUS_DATA)
		brcmf_sdbrcm_clkctl(bus, CLK_NONE, false);

exit:
	sdio_release_host(bus->sdiodev->func[1]);

	return ret;
}

void brcmf_sdbrcm_isr(void *arg)
{
	struct brcmf_sdio *bus = (struct brcmf_sdio *) arg;

	brcmf_dbg(TRACE, "Enter\n");

	if (!bus) {
		brcmf_err("bus is null pointer, exiting\n");
		return;
	}

	if (bus->sdiodev->bus_if->state == BRCMF_BUS_DOWN) {
		brcmf_err("bus is down. we have nothing to do\n");
		return;
	}
	/* Count the interrupt call */
	bus->sdcnt.intrcount++;
	if (in_interrupt())
		atomic_set(&bus->ipend, 1);
	else
		if (brcmf_sdio_intr_rstatus(bus)) {
			brcmf_err("failed backplane access\n");
			bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
		}

	/* Disable additional interrupts (is this needed now)? */
	if (!bus->intr)
		brcmf_err("isr w/o interrupt configured!\n");

	atomic_inc(&bus->dpc_tskcnt);
	queue_work(bus->brcmf_wq, &bus->datawork);
}

static bool brcmf_sdbrcm_bus_watchdog(struct brcmf_sdio *bus)
{
#ifdef DEBUG
	struct brcmf_bus *bus_if = dev_get_drvdata(bus->sdiodev->dev);
#endif	/* DEBUG */

	brcmf_dbg(TIMER, "Enter\n");

	/* Poll period: check device if appropriate. */
	if (!bus->sr_enabled &&
	    bus->poll && (++bus->polltick >= bus->pollrate)) {
		u32 intstatus = 0;

		/* Reset poll tick */
		bus->polltick = 0;

		/* Check device if no interrupts */
		if (!bus->intr ||
		    (bus->sdcnt.intrcount == bus->sdcnt.lastintrs)) {

			if (atomic_read(&bus->dpc_tskcnt) == 0) {
				u8 devpend;

				sdio_claim_host(bus->sdiodev->func[1]);
				devpend = brcmf_sdio_regrb(bus->sdiodev,
							   SDIO_CCCR_INTx,
							   NULL);
				sdio_release_host(bus->sdiodev->func[1]);
				intstatus =
				    devpend & (INTR_STATUS_FUNC1 |
					       INTR_STATUS_FUNC2);
			}

			/* If there is something, make like the ISR and
				 schedule the DPC */
			if (intstatus) {
				bus->sdcnt.pollcnt++;
				atomic_set(&bus->ipend, 1);

				atomic_inc(&bus->dpc_tskcnt);
				queue_work(bus->brcmf_wq, &bus->datawork);
			}
		}

		/* Update interrupt tracking */
		bus->sdcnt.lastintrs = bus->sdcnt.intrcount;
	}
#ifdef DEBUG
	/* Poll for console output periodically */
	if (bus_if && bus_if->state == BRCMF_BUS_DATA &&
	    bus->console_interval != 0) {
		bus->console.count += BRCMF_WD_POLL_MS;
		if (bus->console.count >= bus->console_interval) {
			bus->console.count -= bus->console_interval;
			sdio_claim_host(bus->sdiodev->func[1]);
			/* Make sure backplane clock is on */
			brcmf_sdbrcm_bus_sleep(bus, false, false);
			if (brcmf_sdbrcm_readconsole(bus) < 0)
				/* stop on error */
				bus->console_interval = 0;
			sdio_release_host(bus->sdiodev->func[1]);
		}
	}
#endif				/* DEBUG */

	/* On idle timeout clear activity flag and/or turn off clock */
	if ((bus->idletime > 0) && (bus->clkstate == CLK_AVAIL)) {
		if (++bus->idlecount >= bus->idletime) {
			bus->idlecount = 0;
			if (bus->activity) {
				bus->activity = false;
				brcmf_sdbrcm_wd_timer(bus, BRCMF_WD_POLL_MS);
			} else {
				brcmf_dbg(SDIO, "idle\n");
				sdio_claim_host(bus->sdiodev->func[1]);
				brcmf_sdbrcm_bus_sleep(bus, true, false);
				sdio_release_host(bus->sdiodev->func[1]);
			}
		}
	}

	return (atomic_read(&bus->ipend) > 0);
}

static void brcmf_sdio_dataworker(struct work_struct *work)
{
	struct brcmf_sdio *bus = container_of(work, struct brcmf_sdio,
					      datawork);

	while (atomic_read(&bus->dpc_tskcnt)) {
		brcmf_sdbrcm_dpc(bus);
		atomic_dec(&bus->dpc_tskcnt);
	}
}

static void brcmf_sdbrcm_release_malloc(struct brcmf_sdio *bus)
{
	brcmf_dbg(TRACE, "Enter\n");

	kfree(bus->rxbuf);
	bus->rxctl = bus->rxbuf = NULL;
	bus->rxlen = 0;
}

static bool brcmf_sdbrcm_probe_malloc(struct brcmf_sdio *bus)
{
	brcmf_dbg(TRACE, "Enter\n");

	if (bus->sdiodev->bus_if->maxctl) {
		bus->rxblen =
		    roundup((bus->sdiodev->bus_if->maxctl + SDPCM_HDRLEN),
			    ALIGNMENT) + BRCMF_SDALIGN;
		bus->rxbuf = kmalloc(bus->rxblen, GFP_ATOMIC);
		if (!(bus->rxbuf))
			return false;
	}

	return true;
}

static bool
brcmf_sdbrcm_probe_attach(struct brcmf_sdio *bus, u32 regsva)
{
	u8 clkctl = 0;
	int err = 0;
	int reg_addr;
	u32 reg_val;
	u32 drivestrength;

	bus->alp_only = true;

	sdio_claim_host(bus->sdiodev->func[1]);

	pr_debug("F1 signature read @0x18000000=0x%4x\n",
		 brcmf_sdio_regrl(bus->sdiodev, SI_ENUM_BASE, NULL));

	/*
	 * Force PLL off until brcmf_sdio_chip_attach()
	 * programs PLL control regs
	 */

	brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR,
			 BRCMF_INIT_CLKCTL1, &err);
	if (!err)
		clkctl = brcmf_sdio_regrb(bus->sdiodev,
					  SBSDIO_FUNC1_CHIPCLKCSR, &err);

	if (err || ((clkctl & ~SBSDIO_AVBITS) != BRCMF_INIT_CLKCTL1)) {
		brcmf_err("ChipClkCSR access: err %d wrote 0x%02x read 0x%02x\n",
			  err, BRCMF_INIT_CLKCTL1, clkctl);
		goto fail;
	}

	if (brcmf_sdio_chip_attach(bus->sdiodev, &bus->ci, regsva)) {
		brcmf_err("brcmf_sdio_chip_attach failed!\n");
		goto fail;
	}

	if (brcmf_sdbrcm_kso_init(bus)) {
		brcmf_err("error enabling KSO\n");
		goto fail;
	}

	if ((bus->sdiodev->pdata) && (bus->sdiodev->pdata->drive_strength))
		drivestrength = bus->sdiodev->pdata->drive_strength;
	else
		drivestrength = DEFAULT_SDIO_DRIVE_STRENGTH;
	brcmf_sdio_chip_drivestrengthinit(bus->sdiodev, bus->ci, drivestrength);

	/* Get info on the SOCRAM cores... */
	bus->ramsize = bus->ci->ramsize;
	if (!(bus->ramsize)) {
		brcmf_err("failed to find SOCRAM memory!\n");
		goto fail;
	}

	/* Set card control so an SDIO card reset does a WLAN backplane reset */
	reg_val = brcmf_sdio_regrb(bus->sdiodev,
				   SDIO_CCCR_BRCM_CARDCTRL, &err);
	if (err)
		goto fail;

	reg_val |= SDIO_CCCR_BRCM_CARDCTRL_WLANRESET;

	brcmf_sdio_regwb(bus->sdiodev,
			 SDIO_CCCR_BRCM_CARDCTRL, reg_val, &err);
	if (err)
		goto fail;

	/* set PMUControl so a backplane reset does PMU state reload */
	reg_addr = CORE_CC_REG(bus->ci->c_inf[0].base,
			       pmucontrol);
	reg_val = brcmf_sdio_regrl(bus->sdiodev,
				   reg_addr,
				   &err);
	if (err)
		goto fail;

	reg_val |= (BCMA_CC_PMU_CTL_RES_RELOAD << BCMA_CC_PMU_CTL_RES_SHIFT);

	brcmf_sdio_regwl(bus->sdiodev,
			 reg_addr,
			 reg_val,
			 &err);
	if (err)
		goto fail;


	sdio_release_host(bus->sdiodev->func[1]);

	brcmu_pktq_init(&bus->txq, (PRIOMASK + 1), TXQLEN);

	/* Locate an appropriately-aligned portion of hdrbuf */
	bus->rxhdr = (u8 *) roundup((unsigned long)&bus->hdrbuf[0],
				    BRCMF_SDALIGN);

	/* Set the poll and/or interrupt flags */
	bus->intr = true;
	bus->poll = false;
	if (bus->poll)
		bus->pollrate = 1;

	return true;

fail:
	sdio_release_host(bus->sdiodev->func[1]);
	return false;
}

static bool brcmf_sdbrcm_probe_init(struct brcmf_sdio *bus)
{
	brcmf_dbg(TRACE, "Enter\n");

	sdio_claim_host(bus->sdiodev->func[1]);

	/* Disable F2 to clear any intermediate frame state on the dongle */
	brcmf_sdio_regwb(bus->sdiodev, SDIO_CCCR_IOEx,
			 SDIO_FUNC_ENABLE_1, NULL);

	bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
	bus->rxflow = false;

	/* Done with backplane-dependent accesses, can drop clock... */
	brcmf_sdio_regwb(bus->sdiodev, SBSDIO_FUNC1_CHIPCLKCSR, 0, NULL);

	sdio_release_host(bus->sdiodev->func[1]);

	/* ...and initialize clock/power states */
	bus->clkstate = CLK_SDONLY;
	bus->idletime = BRCMF_IDLE_INTERVAL;
	bus->idleclock = BRCMF_IDLE_ACTIVE;

	/* Query the F2 block size, set roundup accordingly */
	bus->blocksize = bus->sdiodev->func[2]->cur_blksize;
	bus->roundup = min(max_roundup, bus->blocksize);

	/* SR state */
	bus->sleeping = false;
	bus->sr_enabled = false;

	return true;
}

static int
brcmf_sdbrcm_watchdog_thread(void *data)
{
	struct brcmf_sdio *bus = (struct brcmf_sdio *)data;

	allow_signal(SIGTERM);
	/* Run until signal received */
	while (1) {
		if (kthread_should_stop())
			break;
		if (!wait_for_completion_interruptible(&bus->watchdog_wait)) {
			brcmf_sdbrcm_bus_watchdog(bus);
			/* Count the tick for reference */
			bus->sdcnt.tickcnt++;
		} else
			break;
	}
	return 0;
}

static void
brcmf_sdbrcm_watchdog(unsigned long data)
{
	struct brcmf_sdio *bus = (struct brcmf_sdio *)data;

	if (bus->watchdog_tsk) {
		complete(&bus->watchdog_wait);
		/* Reschedule the watchdog */
		if (bus->wd_timer_valid)
			mod_timer(&bus->timer,
				  jiffies + BRCMF_WD_POLL_MS * HZ / 1000);
	}
}

static void brcmf_sdbrcm_release_dongle(struct brcmf_sdio *bus)
{
	brcmf_dbg(TRACE, "Enter\n");

	if (bus->ci) {
		sdio_claim_host(bus->sdiodev->func[1]);
		brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);
		brcmf_sdbrcm_clkctl(bus, CLK_NONE, false);
		sdio_release_host(bus->sdiodev->func[1]);
		brcmf_sdio_chip_detach(&bus->ci);
		if (bus->vars && bus->varsz)
			kfree(bus->vars);
		bus->vars = NULL;
	}

	brcmf_dbg(TRACE, "Disconnected\n");
}

/* Detach and free everything */
static void brcmf_sdbrcm_release(struct brcmf_sdio *bus)
{
	brcmf_dbg(TRACE, "Enter\n");

	if (bus) {
		/* De-register interrupt handler */
		brcmf_sdio_intr_unregister(bus->sdiodev);

		cancel_work_sync(&bus->datawork);
		if (bus->brcmf_wq)
			destroy_workqueue(bus->brcmf_wq);

		if (bus->sdiodev->bus_if->drvr) {
			brcmf_detach(bus->sdiodev->dev);
			brcmf_sdbrcm_release_dongle(bus);
		}

		brcmf_sdbrcm_release_malloc(bus);

		kfree(bus);
	}

	brcmf_dbg(TRACE, "Disconnected\n");
}

static struct brcmf_bus_ops brcmf_sdio_bus_ops = {
	.stop = brcmf_sdbrcm_bus_stop,
	.init = brcmf_sdbrcm_bus_init,
	.txdata = brcmf_sdbrcm_bus_txdata,
	.txctl = brcmf_sdbrcm_bus_txctl,
	.rxctl = brcmf_sdbrcm_bus_rxctl,
	.gettxq = brcmf_sdbrcm_bus_gettxq,
};

void *brcmf_sdbrcm_probe(u32 regsva, struct brcmf_sdio_dev *sdiodev)
{
	int ret;
	struct brcmf_sdio *bus;
	struct brcmf_bus_dcmd *dlst;
	u32 dngl_txglom;
	u32 txglomalign = 0;
	u8 idx;

	brcmf_dbg(TRACE, "Enter\n");

	/* We make an assumption about address window mappings:
	 * regsva == SI_ENUM_BASE*/

	/* Allocate private bus interface state */
	bus = kzalloc(sizeof(struct brcmf_sdio), GFP_ATOMIC);
	if (!bus)
		goto fail;

	bus->sdiodev = sdiodev;
	sdiodev->bus = bus;
	skb_queue_head_init(&bus->glom);
	bus->txbound = BRCMF_TXBOUND;
	bus->rxbound = BRCMF_RXBOUND;
	bus->txminmax = BRCMF_TXMINMAX;
	bus->tx_seq = SDPCM_SEQ_WRAP - 1;

	INIT_WORK(&bus->datawork, brcmf_sdio_dataworker);
	bus->brcmf_wq = create_singlethread_workqueue("brcmf_wq");
	if (bus->brcmf_wq == NULL) {
		brcmf_err("insufficient memory to create txworkqueue\n");
		goto fail;
	}

	/* attempt to attach to the dongle */
	if (!(brcmf_sdbrcm_probe_attach(bus, regsva))) {
		brcmf_err("brcmf_sdbrcm_probe_attach failed\n");
		goto fail;
	}

	spin_lock_init(&bus->rxctl_lock);
	spin_lock_init(&bus->txqlock);
	init_waitqueue_head(&bus->ctrl_wait);
	init_waitqueue_head(&bus->dcmd_resp_wait);

	/* Set up the watchdog timer */
	init_timer(&bus->timer);
	bus->timer.data = (unsigned long)bus;
	bus->timer.function = brcmf_sdbrcm_watchdog;

	/* Initialize watchdog thread */
	init_completion(&bus->watchdog_wait);
	bus->watchdog_tsk = kthread_run(brcmf_sdbrcm_watchdog_thread,
					bus, "brcmf_watchdog");
	if (IS_ERR(bus->watchdog_tsk)) {
		pr_warn("brcmf_watchdog thread failed to start\n");
		bus->watchdog_tsk = NULL;
	}
	/* Initialize DPC thread */
	atomic_set(&bus->dpc_tskcnt, 0);

	/* Assign bus interface call back */
	bus->sdiodev->bus_if->dev = bus->sdiodev->dev;
	bus->sdiodev->bus_if->ops = &brcmf_sdio_bus_ops;
	bus->sdiodev->bus_if->chip = bus->ci->chip;
	bus->sdiodev->bus_if->chiprev = bus->ci->chiprev;

	/* default sdio bus header length for tx packet */
	bus->tx_hdrlen = SDPCM_HWHDR_LEN + SDPCM_SWHDR_LEN;

	/* Attach to the common layer, reserve hdr space */
	ret = brcmf_attach(bus->tx_hdrlen, bus->sdiodev->dev);
	if (ret != 0) {
		brcmf_err("brcmf_attach failed\n");
		goto fail;
	}

	/* Allocate buffers */
	if (!(brcmf_sdbrcm_probe_malloc(bus))) {
		brcmf_err("brcmf_sdbrcm_probe_malloc failed\n");
		goto fail;
	}

	if (!(brcmf_sdbrcm_probe_init(bus))) {
		brcmf_err("brcmf_sdbrcm_probe_init failed\n");
		goto fail;
	}

	brcmf_sdio_debugfs_create(bus);
	brcmf_dbg(INFO, "completed!!\n");

	/* sdio bus core specific dcmd */
	idx = brcmf_sdio_chip_getinfidx(bus->ci, BCMA_CORE_SDIO_DEV);
	dlst = kzalloc(sizeof(struct brcmf_bus_dcmd), GFP_KERNEL);
	if (dlst) {
		if (bus->ci->c_inf[idx].rev < 12) {
			/* for sdio core rev < 12, disable txgloming */
			dngl_txglom = 0;
			dlst->name = "bus:txglom";
			dlst->param = (char *)&dngl_txglom;
			dlst->param_len = sizeof(u32);
		} else {
			/* otherwise, set txglomalign */
			if (sdiodev->pdata)
				txglomalign = sdiodev->pdata->sd_sgentry_align;
			/* SDIO ADMA requires at least 32 bit alignment */
			if (txglomalign < 4)
				txglomalign = 4;
			dlst->name = "bus:txglomalign";
			dlst->param = (char *)&txglomalign;
			dlst->param_len = sizeof(u32);
		}
		list_add(&dlst->list, &bus->sdiodev->bus_if->dcmd_list);
	}

	/* if firmware path present try to download and bring up bus */
	ret = brcmf_bus_start(bus->sdiodev->dev);
	if (ret != 0) {
		brcmf_err("dongle is not responding\n");
		goto fail;
	}

	return bus;

fail:
	brcmf_sdbrcm_release(bus);
	return NULL;
}

void brcmf_sdbrcm_disconnect(void *ptr)
{
	struct brcmf_sdio *bus = (struct brcmf_sdio *)ptr;

	brcmf_dbg(TRACE, "Enter\n");

	if (bus)
		brcmf_sdbrcm_release(bus);

	brcmf_dbg(TRACE, "Disconnected\n");
}

void
brcmf_sdbrcm_wd_timer(struct brcmf_sdio *bus, uint wdtick)
{
	/* Totally stop the timer */
	if (!wdtick && bus->wd_timer_valid) {
		del_timer_sync(&bus->timer);
		bus->wd_timer_valid = false;
		bus->save_ms = wdtick;
		return;
	}

	/* don't start the wd until fw is loaded */
	if (bus->sdiodev->bus_if->state == BRCMF_BUS_DOWN)
		return;

	if (wdtick) {
		if (bus->save_ms != BRCMF_WD_POLL_MS) {
			if (bus->wd_timer_valid)
				/* Stop timer and restart at new value */
				del_timer_sync(&bus->timer);

			/* Create timer again when watchdog period is
			   dynamically changed or in the first instance
			 */
			bus->timer.expires =
				jiffies + BRCMF_WD_POLL_MS * HZ / 1000;
			add_timer(&bus->timer);

		} else {
			/* Re arm the timer, at last watchdog period */
			mod_timer(&bus->timer,
				jiffies + BRCMF_WD_POLL_MS * HZ / 1000);
		}

		bus->wd_timer_valid = true;
		bus->save_ms = wdtick;
	}
}
