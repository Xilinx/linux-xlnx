/*
 * XILINX PS PCIe DMA driver
 *
 * Copyright (C) 2017 Xilinx, Inc. All rights reserved.
 *
 * Description
 * PS PCIe DMA is memory mapped DMA used to execute PS to PL transfers
 * on ZynqMP UltraScale+ Devices
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation
 */

#include "xilinx_ps_pcie.h"
#include "../dmaengine.h"

#define PLATFORM_DRIVER_NAME		  "ps_pcie_pform_dma"
#define MAX_BARS 6

#define DMA_BAR_NUMBER 0

#define MIN_SW_INTR_TRANSACTIONS       2

#define CHANNEL_PROPERTY_LENGTH 50
#define WORKQ_NAME_SIZE		100
#define INTR_HANDLR_NAME_SIZE   100

#define PS_PCIE_DMA_IRQ_NOSHARE    0

#define MAX_COALESCE_COUNT     255

#define DMA_CHANNEL_REGS_SIZE 0x80

#define DMA_SRCQPTRLO_REG_OFFSET  (0x00) /* Source Q pointer Lo */
#define DMA_SRCQPTRHI_REG_OFFSET  (0x04) /* Source Q pointer Hi */
#define DMA_SRCQSZ_REG_OFFSET     (0x08) /* Source Q size */
#define DMA_SRCQLMT_REG_OFFSET    (0x0C) /* Source Q limit */
#define DMA_DSTQPTRLO_REG_OFFSET  (0x10) /* Destination Q pointer Lo */
#define DMA_DSTQPTRHI_REG_OFFSET  (0x14) /* Destination Q pointer Hi */
#define DMA_DSTQSZ_REG_OFFSET     (0x18) /* Destination Q size */
#define DMA_DSTQLMT_REG_OFFSET    (0x1C) /* Destination Q limit */
#define DMA_SSTAQPTRLO_REG_OFFSET (0x20) /* Source Status Q pointer Lo */
#define DMA_SSTAQPTRHI_REG_OFFSET (0x24) /* Source Status Q pointer Hi */
#define DMA_SSTAQSZ_REG_OFFSET    (0x28) /* Source Status Q size */
#define DMA_SSTAQLMT_REG_OFFSET   (0x2C) /* Source Status Q limit */
#define DMA_DSTAQPTRLO_REG_OFFSET (0x30) /* Destination Status Q pointer Lo */
#define DMA_DSTAQPTRHI_REG_OFFSET (0x34) /* Destination Status Q pointer Hi */
#define DMA_DSTAQSZ_REG_OFFSET    (0x38) /* Destination Status Q size */
#define DMA_DSTAQLMT_REG_OFFSET   (0x3C) /* Destination Status Q limit */
#define DMA_SRCQNXT_REG_OFFSET    (0x40) /* Source Q next */
#define DMA_DSTQNXT_REG_OFFSET    (0x44) /* Destination Q next */
#define DMA_SSTAQNXT_REG_OFFSET   (0x48) /* Source Status Q next */
#define DMA_DSTAQNXT_REG_OFFSET   (0x4C) /* Destination Status Q next */
#define DMA_SCRATCH0_REG_OFFSET   (0x50) /* Scratch pad register 0 */

#define DMA_PCIE_INTR_CNTRL_REG_OFFSET  (0x60) /* DMA PCIe intr control reg */
#define DMA_PCIE_INTR_STATUS_REG_OFFSET (0x64) /* DMA PCIe intr status reg */
#define DMA_AXI_INTR_CNTRL_REG_OFFSET   (0x68) /* DMA AXI intr control reg */
#define DMA_AXI_INTR_STATUS_REG_OFFSET  (0x6C) /* DMA AXI intr status reg */
#define DMA_PCIE_INTR_ASSRT_REG_OFFSET  (0x70) /* PCIe intr assert reg */
#define DMA_AXI_INTR_ASSRT_REG_OFFSET   (0x74) /* AXI intr assert register */
#define DMA_CNTRL_REG_OFFSET            (0x78) /* DMA control register */
#define DMA_STATUS_REG_OFFSET           (0x7C) /* DMA status register */

#define DMA_CNTRL_RST_BIT               BIT(1)
#define DMA_CNTRL_64BIT_STAQ_ELEMSZ_BIT BIT(2)
#define DMA_CNTRL_ENABL_BIT             BIT(0)
#define DMA_STATUS_DMA_PRES_BIT         BIT(15)
#define DMA_STATUS_DMA_RUNNING_BIT      BIT(0)
#define DMA_QPTRLO_QLOCAXI_BIT          BIT(0)
#define DMA_QPTRLO_Q_ENABLE_BIT         BIT(1)
#define DMA_INTSTATUS_DMAERR_BIT        BIT(1)
#define DMA_INTSTATUS_SGLINTR_BIT       BIT(2)
#define DMA_INTSTATUS_SWINTR_BIT        BIT(3)
#define DMA_INTCNTRL_ENABLINTR_BIT      BIT(0)
#define DMA_INTCNTRL_DMAERRINTR_BIT     BIT(1)
#define DMA_INTCNTRL_DMASGINTR_BIT      BIT(2)
#define DMA_SW_INTR_ASSRT_BIT           BIT(3)

#define SOURCE_CONTROL_BD_BYTE_COUNT_MASK       GENMASK(23, 0)
#define SOURCE_CONTROL_BD_LOC_AXI		BIT(24)
#define SOURCE_CONTROL_BD_EOP_BIT               BIT(25)
#define SOURCE_CONTROL_BD_INTR_BIT              BIT(26)
#define SOURCE_CONTROL_BACK_TO_BACK_PACK_BIT    BIT(25)
#define SOURCE_CONTROL_ATTRIBUTES_MASK          GENMASK(31, 28)
#define SRC_CTL_ATTRIB_BIT_SHIFT                (29)

#define STA_BD_COMPLETED_BIT            BIT(0)
#define STA_BD_SOURCE_ERROR_BIT         BIT(1)
#define STA_BD_DESTINATION_ERROR_BIT    BIT(2)
#define STA_BD_INTERNAL_ERROR_BIT       BIT(3)
#define STA_BD_UPPER_STATUS_NONZERO_BIT BIT(31)
#define STA_BD_BYTE_COUNT_MASK          GENMASK(30, 4)

#define STA_BD_BYTE_COUNT_SHIFT         4

#define DMA_INTCNTRL_SGCOLSCCNT_BIT_SHIFT (16)

#define DMA_SRC_Q_LOW_BIT_SHIFT   GENMASK(5, 0)

#define MAX_TRANSFER_LENGTH       0x1000000

#define AXI_ATTRIBUTE       0x3
#define PCI_ATTRIBUTE       0x2

#define ROOTDMA_Q_READ_ATTRIBUTE 0x8

/*
 * User Id programmed into Source Q will be copied into Status Q of Destination
 */
#define DEFAULT_UID 1

/*
 * DMA channel registers
 */
struct DMA_ENGINE_REGISTERS {
	u32 src_q_low;          /* 0x00 */
	u32 src_q_high;         /* 0x04 */
	u32 src_q_size;         /* 0x08 */
	u32 src_q_limit;        /* 0x0C */
	u32 dst_q_low;          /* 0x10 */
	u32 dst_q_high;         /* 0x14 */
	u32 dst_q_size;         /* 0x18 */
	u32 dst_q_limit;        /* 0x1c */
	u32 stas_q_low;         /* 0x20 */
	u32 stas_q_high;        /* 0x24 */
	u32 stas_q_size;        /* 0x28 */
	u32 stas_q_limit;       /* 0x2C */
	u32 stad_q_low;         /* 0x30 */
	u32 stad_q_high;        /* 0x34 */
	u32 stad_q_size;        /* 0x38 */
	u32 stad_q_limit;       /* 0x3C */
	u32 src_q_next;         /* 0x40 */
	u32 dst_q_next;         /* 0x44 */
	u32 stas_q_next;        /* 0x48 */
	u32 stad_q_next;        /* 0x4C */
	u32 scrathc0;           /* 0x50 */
	u32 scrathc1;           /* 0x54 */
	u32 scrathc2;           /* 0x58 */
	u32 scrathc3;           /* 0x5C */
	u32 pcie_intr_cntrl;    /* 0x60 */
	u32 pcie_intr_status;   /* 0x64 */
	u32 axi_intr_cntrl;     /* 0x68 */
	u32 axi_intr_status;    /* 0x6C */
	u32 pcie_intr_assert;   /* 0x70 */
	u32 axi_intr_assert;    /* 0x74 */
	u32 dma_channel_ctrl;   /* 0x78 */
	u32 dma_channel_status; /* 0x7C */
} __attribute__((__packed__));

/**
 * struct SOURCE_DMA_DESCRIPTOR - Source Hardware Descriptor
 * @system_address: 64 bit buffer physical address
 * @control_byte_count: Byte count/buffer length and control flags
 * @user_handle: User handle gets copied to status q on completion
 * @user_id: User id gets copied to status q of destination
 */
struct SOURCE_DMA_DESCRIPTOR {
	u64 system_address;
	u32 control_byte_count;
	u16 user_handle;
	u16 user_id;
} __attribute__((__packed__));

/**
 * struct DEST_DMA_DESCRIPTOR - Destination Hardware Descriptor
 * @system_address: 64 bit buffer physical address
 * @control_byte_count: Byte count/buffer length and control flags
 * @user_handle: User handle gets copied to status q on completion
 * @reserved: Reserved field
 */
struct DEST_DMA_DESCRIPTOR {
	u64 system_address;
	u32 control_byte_count;
	u16 user_handle;
	u16 reserved;
} __attribute__((__packed__));

/**
 * struct STATUS_DMA_DESCRIPTOR - Status Hardware Descriptor
 * @status_flag_byte_count: Byte count/buffer length and status flags
 * @user_handle: User handle gets copied from src/dstq on completion
 * @user_id: User id gets copied from srcq
 */
struct STATUS_DMA_DESCRIPTOR {
	u32 status_flag_byte_count;
	u16 user_handle;
	u16 user_id;
} __attribute__((__packed__));

enum PACKET_CONTEXT_AVAILABILITY {
	FREE = 0,    /*Packet transfer Parameter context is free.*/
	IN_USE       /*Packet transfer Parameter context is in use.*/
};

struct ps_pcie_transfer_elements {
	struct list_head node;
	dma_addr_t src_pa;
	dma_addr_t dst_pa;
	u32 transfer_bytes;
};

struct  ps_pcie_tx_segment {
	struct list_head node;
	struct dma_async_tx_descriptor async_tx;
	struct list_head transfer_nodes;
	u32 src_elements;
	u32 dst_elements;
	u32 total_transfer_bytes;
};

struct ps_pcie_intr_segment {
	struct list_head node;
	struct dma_async_tx_descriptor async_intr_tx;
};

/*
 * The context structure stored for each DMA transaction
 * This structure is maintained separately for Src Q and Destination Q
 * @availability_status: Indicates whether packet context is available
 * @idx_sop: Indicates starting index of buffer descriptor for a transfer
 * @idx_eop: Indicates ending index of buffer descriptor for a transfer
 * @sgl: Indicates either src or dst sglist for the transaction
 */
struct PACKET_TRANSFER_PARAMS {
	enum PACKET_CONTEXT_AVAILABILITY availability_status;
	u16 idx_sop;
	u16 idx_eop;
	struct ps_pcie_tx_segment *seg;
};

enum CHANNEL_STATE {
	CHANNEL_RESOURCE_UNALLOCATED = 0, /*  Channel resources not allocated */
	CHANNEL_UNAVIALBLE,               /*  Channel inactive */
	CHANNEL_AVAILABLE,                /*  Channel available for transfers */
	CHANNEL_ERROR                     /*  Channel encountered errors */
};

enum BUFFER_LOCATION {
	BUFFER_LOC_PCI = 0,
	BUFFER_LOC_AXI,
	BUFFER_LOC_INVALID
};

enum dev_channel_properties {
	DMA_CHANNEL_DIRECTION = 0,
	NUM_DESCRIPTORS,
	NUM_QUEUES,
	COALESE_COUNT,
	POLL_TIMER_FREQUENCY
};

/*
 * struct ps_pcie_dma_chan - Driver specific DMA channel structure
 * @xdev: Driver specific device structure
 * @dev: The dma device
 * @common:  DMA common channel
 * @chan_base: Pointer to Channel registers
 * @channel_number: DMA channel number in the device
 * @num_queues: Number of queues per channel.
 *		It should be four for memory mapped case and
 *		two for Streaming case
 * @direction: Transfer direction
 * @state: Indicates channel state
 * @channel_lock: Spin lock to be used before changing channel state
 * @cookie_lock: Spin lock to be used before assigning cookie for a transaction
 * @coalesce_count: Indicates number of packet transfers before interrupts
 * @poll_timer_freq:Indicates frequency of polling for completed transactions
 * @poll_timer: Timer to poll dma buffer descriptors if coalesce count is > 0
 * @src_avail_descriptors: Available sgl source descriptors
 * @src_desc_lock: Lock for synchronizing src_avail_descriptors
 * @dst_avail_descriptors: Available sgl destination descriptors
 * @dst_desc_lock: Lock for synchronizing
 *		dst_avail_descriptors
 * @src_sgl_bd_pa: Physical address of Source SGL buffer Descriptors
 * @psrc_sgl_bd: Virtual address of Source SGL buffer Descriptors
 * @src_sgl_freeidx: Holds index of Source SGL buffer descriptor to be filled
 * @sglDestinationQLock:Lock to serialize Destination Q updates
 * @dst_sgl_bd_pa: Physical address of Dst SGL buffer Descriptors
 * @pdst_sgl_bd: Virtual address of Dst SGL buffer Descriptors
 * @dst_sgl_freeidx: Holds index of Destination SGL
 * @src_sta_bd_pa: Physical address of StatusQ buffer Descriptors
 * @psrc_sta_bd: Virtual address of Src StatusQ buffer Descriptors
 * @src_staprobe_idx: Holds index of Status Q to be examined for SrcQ updates
 * @src_sta_hw_probe_idx: Holds index of maximum limit of Status Q for hardware
 * @dst_sta_bd_pa: Physical address of Dst StatusQ buffer Descriptor
 * @pdst_sta_bd: Virtual address of Dst Status Q buffer Descriptors
 * @dst_staprobe_idx: Holds index of Status Q to be examined for updates
 * @dst_sta_hw_probe_idx: Holds index of max limit of Dst Status Q for hardware
 * @@read_attribute: Describes the attributes of buffer in srcq
 * @@write_attribute: Describes the attributes of buffer in dstq
 * @@intr_status_offset: Register offset to be cheked on receiving interrupt
 * @@intr_status_offset: Register offset to be used to control interrupts
 * @ppkt_ctx_srcq: Virtual address of packet context to Src Q updates
 * @idx_ctx_srcq_head: Holds index of packet context to be filled for Source Q
 * @idx_ctx_srcq_tail: Holds index of packet context to be examined for Source Q
 * @ppkt_ctx_dstq: Virtual address of packet context to Dst Q updates
 * @idx_ctx_dstq_head: Holds index of packet context to be filled for Dst Q
 * @idx_ctx_dstq_tail: Holds index of packet context to be examined for Dst Q
 * @pending_list_lock: Lock to be taken before updating pending transfers list
 * @pending_list: List of transactions submitted to channel
 * @active_list_lock: Lock to be taken before transferring transactions from
 *			pending list to active list which will be subsequently
 *				submitted to hardware
 * @active_list: List of transactions that will be submitted to hardware
 * @pending_interrupts_lock: Lock to be taken before updating pending Intr list
 * @pending_interrupts_list: List of interrupt transactions submitted to channel
 * @active_interrupts_lock: Lock to be taken before transferring transactions
 *			from pending interrupt list to active interrupt list
 * @active_interrupts_list: List of interrupt transactions that are active
 * @transactions_pool: Mem pool to allocate dma transactions quickly
 * @intr_transactions_pool: Mem pool to allocate interrupt transactions quickly
 * @sw_intrs_wrkq: Work Q which performs handling of software intrs
 * @handle_sw_intrs:Work function handling software interrupts
 * @maintenance_workq: Work Q to perform maintenance tasks during stop or error
 * @handle_chan_reset: Work that invokes channel reset function
 * @handle_chan_shutdown: Work that invokes channel shutdown function
 * @handle_chan_terminate: Work that invokes channel transactions termination
 * @chan_shutdown_complt: Completion variable which says shutdown is done
 * @chan_terminate_complete: Completion variable which says terminate is done
 * @primary_desc_cleanup: Work Q which performs work related to sgl handling
 * @handle_primary_desc_cleanup: Work that invokes src Q, dst Q cleanup
 *				and programming
 * @chan_programming: Work Q which performs work related to channel programming
 * @handle_chan_programming: Work that invokes channel programming function
 * @srcq_desc_cleanup: Work Q which performs src Q descriptor cleanup
 * @handle_srcq_desc_cleanup: Work function handling Src Q completions
 * @dstq_desc_cleanup: Work Q which performs dst Q descriptor cleanup
 * @handle_dstq_desc_cleanup: Work function handling Dst Q completions
 * @srcq_work_complete: Src Q Work completion variable for primary work
 * @dstq_work_complete: Dst Q Work completion variable for primary work
 */
struct ps_pcie_dma_chan {
	struct xlnx_pcie_dma_device *xdev;
	struct device *dev;

	struct dma_chan common;

	struct DMA_ENGINE_REGISTERS *chan_base;
	u16 channel_number;

	u32 num_queues;
	enum dma_data_direction direction;
	enum BUFFER_LOCATION srcq_buffer_location;
	enum BUFFER_LOCATION dstq_buffer_location;

	u32 total_descriptors;

	enum CHANNEL_STATE state;
	spinlock_t channel_lock; /* For changing channel state */

	spinlock_t cookie_lock;  /* For acquiring cookie from dma framework*/

	u32 coalesce_count;
	u32 poll_timer_freq;

	struct timer_list poll_timer;

	u32 src_avail_descriptors;
	spinlock_t src_desc_lock; /* For handling srcq available descriptors */

	u32 dst_avail_descriptors;
	spinlock_t dst_desc_lock; /* For handling dstq available descriptors */

	dma_addr_t src_sgl_bd_pa;
	struct SOURCE_DMA_DESCRIPTOR *psrc_sgl_bd;
	u32 src_sgl_freeidx;

	dma_addr_t dst_sgl_bd_pa;
	struct DEST_DMA_DESCRIPTOR *pdst_sgl_bd;
	u32 dst_sgl_freeidx;

	dma_addr_t src_sta_bd_pa;
	struct STATUS_DMA_DESCRIPTOR *psrc_sta_bd;
	u32 src_staprobe_idx;
	u32 src_sta_hw_probe_idx;

	dma_addr_t dst_sta_bd_pa;
	struct STATUS_DMA_DESCRIPTOR *pdst_sta_bd;
	u32 dst_staprobe_idx;
	u32 dst_sta_hw_probe_idx;

	u32 read_attribute;
	u32 write_attribute;

	u32 intr_status_offset;
	u32 intr_control_offset;

	struct PACKET_TRANSFER_PARAMS *ppkt_ctx_srcq;
	u16 idx_ctx_srcq_head;
	u16 idx_ctx_srcq_tail;

	struct PACKET_TRANSFER_PARAMS *ppkt_ctx_dstq;
	u16 idx_ctx_dstq_head;
	u16 idx_ctx_dstq_tail;

	spinlock_t  pending_list_lock; /* For handling dma pending_list */
	struct list_head pending_list;
	spinlock_t  active_list_lock; /* For handling dma active_list */
	struct list_head active_list;

	spinlock_t pending_interrupts_lock; /* For dma pending interrupts list*/
	struct list_head pending_interrupts_list;
	spinlock_t active_interrupts_lock;  /* For dma active interrupts list*/
	struct list_head active_interrupts_list;

	mempool_t *transactions_pool;
	mempool_t *tx_elements_pool;
	mempool_t *intr_transactions_pool;

	struct workqueue_struct *sw_intrs_wrkq;
	struct work_struct handle_sw_intrs;

	struct workqueue_struct *maintenance_workq;
	struct work_struct handle_chan_reset;
	struct work_struct handle_chan_shutdown;
	struct work_struct handle_chan_terminate;

	struct completion chan_shutdown_complt;
	struct completion chan_terminate_complete;

	struct workqueue_struct *primary_desc_cleanup;
	struct work_struct handle_primary_desc_cleanup;

	struct workqueue_struct *chan_programming;
	struct work_struct handle_chan_programming;

	struct workqueue_struct *srcq_desc_cleanup;
	struct work_struct handle_srcq_desc_cleanup;
	struct completion srcq_work_complete;

	struct workqueue_struct *dstq_desc_cleanup;
	struct work_struct handle_dstq_desc_cleanup;
	struct completion dstq_work_complete;
};

/*
 * struct xlnx_pcie_dma_device - Driver specific platform device structure
 * @is_rootdma: Indicates whether the dma instance is root port dma
 * @dma_buf_ext_addr: Indicates whether target system is 32 bit or 64 bit
 * @bar_mask: Indicates available pcie bars
 * @board_number: Count value of platform device
 * @dev: Device structure pointer for pcie device
 * @channels: Pointer to device DMA channels structure
 * @common: DMA device structure
 * @num_channels: Number of channels active for the device
 * @reg_base: Base address of first DMA channel of the device
 * @irq_vecs: Number of irq vectors allocated to pci device
 * @pci_dev: Parent pci device which created this platform device
 * @bar_info: PCIe bar related information
 * @platform_irq_vec: Platform irq vector number for root dma
 * @rootdma_vendor: PCI Vendor id for root dma
 * @rootdma_device: PCI Device id for root dma
 */
struct xlnx_pcie_dma_device {
	bool is_rootdma;
	bool dma_buf_ext_addr;
	u32 bar_mask;
	u16 board_number;
	struct device *dev;
	struct ps_pcie_dma_chan *channels;
	struct dma_device common;
	int num_channels;
	int irq_vecs;
	void __iomem *reg_base;
	struct pci_dev *pci_dev;
	struct BAR_PARAMS bar_info[MAX_BARS];
	int platform_irq_vec;
	u16 rootdma_vendor;
	u16 rootdma_device;
};

#define to_xilinx_chan(chan) \
	container_of(chan, struct ps_pcie_dma_chan, common)
#define to_ps_pcie_dma_tx_descriptor(tx) \
	container_of(tx, struct ps_pcie_tx_segment, async_tx)
#define to_ps_pcie_dma_tx_intr_descriptor(tx) \
	container_of(tx, struct ps_pcie_intr_segment, async_intr_tx)

/* Function Protypes */
static u32 ps_pcie_dma_read(struct ps_pcie_dma_chan *chan, u32 reg);
static void ps_pcie_dma_write(struct ps_pcie_dma_chan *chan, u32 reg,
			      u32 value);
static void ps_pcie_dma_clr_mask(struct ps_pcie_dma_chan *chan, u32 reg,
				 u32 mask);
static void ps_pcie_dma_set_mask(struct ps_pcie_dma_chan *chan, u32 reg,
				 u32 mask);
static int irq_setup(struct xlnx_pcie_dma_device *xdev);
static int platform_irq_setup(struct xlnx_pcie_dma_device *xdev);
static int chan_intr_setup(struct xlnx_pcie_dma_device *xdev);
static int device_intr_setup(struct xlnx_pcie_dma_device *xdev);
static int irq_probe(struct xlnx_pcie_dma_device *xdev);
static int ps_pcie_check_intr_status(struct ps_pcie_dma_chan *chan);
static irqreturn_t ps_pcie_dma_dev_intr_handler(int irq, void *data);
static irqreturn_t ps_pcie_dma_chan_intr_handler(int irq, void *data);
static int init_hw_components(struct ps_pcie_dma_chan *chan);
static int init_sw_components(struct ps_pcie_dma_chan *chan);
static void update_channel_read_attribute(struct ps_pcie_dma_chan *chan);
static void update_channel_write_attribute(struct ps_pcie_dma_chan *chan);
static void ps_pcie_chan_reset(struct ps_pcie_dma_chan *chan);
static void poll_completed_transactions(struct timer_list *t);
static bool check_descriptors_for_two_queues(struct ps_pcie_dma_chan *chan,
					     struct ps_pcie_tx_segment *seg);
static bool check_descriptors_for_all_queues(struct ps_pcie_dma_chan *chan,
					     struct ps_pcie_tx_segment *seg);
static bool check_descriptor_availability(struct ps_pcie_dma_chan *chan,
					  struct ps_pcie_tx_segment *seg);
static void handle_error(struct ps_pcie_dma_chan *chan);
static void xlnx_ps_pcie_update_srcq(struct ps_pcie_dma_chan *chan,
				     struct ps_pcie_tx_segment *seg);
static void xlnx_ps_pcie_update_dstq(struct ps_pcie_dma_chan *chan,
				     struct ps_pcie_tx_segment *seg);
static void ps_pcie_chan_program_work(struct work_struct *work);
static void dst_cleanup_work(struct work_struct *work);
static void src_cleanup_work(struct work_struct *work);
static void ps_pcie_chan_primary_work(struct work_struct *work);
static int probe_channel_properties(struct platform_device *platform_dev,
				    struct xlnx_pcie_dma_device *xdev,
				    u16 channel_number);
static void xlnx_ps_pcie_destroy_mempool(struct ps_pcie_dma_chan *chan);
static void xlnx_ps_pcie_free_worker_queues(struct ps_pcie_dma_chan *chan);
static void xlnx_ps_pcie_free_pkt_ctxts(struct ps_pcie_dma_chan *chan);
static void xlnx_ps_pcie_free_descriptors(struct ps_pcie_dma_chan *chan);
static int xlnx_ps_pcie_channel_activate(struct ps_pcie_dma_chan *chan);
static void xlnx_ps_pcie_channel_quiesce(struct ps_pcie_dma_chan *chan);
static void ivk_cbk_for_pending(struct ps_pcie_dma_chan *chan);
static void xlnx_ps_pcie_reset_channel(struct ps_pcie_dma_chan *chan);
static void xlnx_ps_pcie_free_poll_timer(struct ps_pcie_dma_chan *chan);
static int xlnx_ps_pcie_alloc_poll_timer(struct ps_pcie_dma_chan *chan);
static void terminate_transactions_work(struct work_struct *work);
static void chan_shutdown_work(struct work_struct *work);
static void chan_reset_work(struct work_struct *work);
static int xlnx_ps_pcie_alloc_worker_threads(struct ps_pcie_dma_chan *chan);
static int xlnx_ps_pcie_alloc_mempool(struct ps_pcie_dma_chan *chan);
static int xlnx_ps_pcie_alloc_pkt_contexts(struct ps_pcie_dma_chan *chan);
static int dma_alloc_descriptors_two_queues(struct ps_pcie_dma_chan *chan);
static int dma_alloc_decriptors_all_queues(struct ps_pcie_dma_chan *chan);
static void xlnx_ps_pcie_dma_free_chan_resources(struct dma_chan *dchan);
static int xlnx_ps_pcie_dma_alloc_chan_resources(struct dma_chan *dchan);
static dma_cookie_t xilinx_dma_tx_submit(struct dma_async_tx_descriptor *tx);
static dma_cookie_t xilinx_intr_tx_submit(struct dma_async_tx_descriptor *tx);
static struct dma_async_tx_descriptor *
xlnx_ps_pcie_dma_prep_memcpy(struct dma_chan *channel, dma_addr_t dma_dst,
			     dma_addr_t dma_src, size_t len,
			     unsigned long flags);
static struct dma_async_tx_descriptor *xlnx_ps_pcie_dma_prep_slave_sg(
		struct dma_chan *channel, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context);
static struct dma_async_tx_descriptor *xlnx_ps_pcie_dma_prep_interrupt(
		struct dma_chan *channel, unsigned long flags);
static void xlnx_ps_pcie_dma_issue_pending(struct dma_chan *channel);
static int xlnx_ps_pcie_dma_terminate_all(struct dma_chan *channel);
static int read_rootdma_config(struct platform_device *platform_dev,
			       struct xlnx_pcie_dma_device *xdev);
static int read_epdma_config(struct platform_device *platform_dev,
			     struct xlnx_pcie_dma_device *xdev);
static int xlnx_pcie_dma_driver_probe(struct platform_device *platform_dev);
static int xlnx_pcie_dma_driver_remove(struct platform_device *platform_dev);

/* IO accessors */
static inline u32 ps_pcie_dma_read(struct ps_pcie_dma_chan *chan, u32 reg)
{
	return ioread32((void __iomem *)((char *)(chan->chan_base) + reg));
}

static inline void ps_pcie_dma_write(struct ps_pcie_dma_chan *chan, u32 reg,
				     u32 value)
{
	iowrite32(value, (void __iomem *)((char *)(chan->chan_base) + reg));
}

static inline void ps_pcie_dma_clr_mask(struct ps_pcie_dma_chan *chan, u32 reg,
					u32 mask)
{
	ps_pcie_dma_write(chan, reg, ps_pcie_dma_read(chan, reg) & ~mask);
}

static inline void ps_pcie_dma_set_mask(struct ps_pcie_dma_chan *chan, u32 reg,
					u32 mask)
{
	ps_pcie_dma_write(chan, reg, ps_pcie_dma_read(chan, reg) | mask);
}

/**
 * ps_pcie_dma_dev_intr_handler - This will be invoked for MSI/Legacy interrupts
 *
 * @irq: IRQ number
 * @data: Pointer to the PS PCIe DMA channel structure
 *
 * Return: IRQ_HANDLED/IRQ_NONE
 */
static irqreturn_t ps_pcie_dma_dev_intr_handler(int irq, void *data)
{
	struct xlnx_pcie_dma_device *xdev =
		(struct xlnx_pcie_dma_device *)data;
	struct ps_pcie_dma_chan *chan = NULL;
	int i;
	int err = -1;
	int ret = -1;

	for (i = 0; i < xdev->num_channels; i++) {
		chan = &xdev->channels[i];
		err = ps_pcie_check_intr_status(chan);
		if (err == 0)
			ret = 0;
	}

	return (ret == 0) ? IRQ_HANDLED : IRQ_NONE;
}

/**
 * ps_pcie_dma_chan_intr_handler - This will be invoked for MSI-X interrupts
 *
 * @irq: IRQ number
 * @data: Pointer to the PS PCIe DMA channel structure
 *
 * Return: IRQ_HANDLED
 */
static irqreturn_t ps_pcie_dma_chan_intr_handler(int irq, void *data)
{
	struct ps_pcie_dma_chan *chan = (struct ps_pcie_dma_chan *)data;

	ps_pcie_check_intr_status(chan);

	return IRQ_HANDLED;
}

/**
 * chan_intr_setup - Requests Interrupt handler for individual channels
 *
 * @xdev: Driver specific data for device
 *
 * Return: 0 on success and non zero value on failure.
 */
static int chan_intr_setup(struct xlnx_pcie_dma_device *xdev)
{
	struct ps_pcie_dma_chan *chan;
	int i;
	int err = 0;

	for (i = 0; i < xdev->num_channels; i++) {
		chan = &xdev->channels[i];
		err = devm_request_irq(xdev->dev,
				       pci_irq_vector(xdev->pci_dev, i),
				       ps_pcie_dma_chan_intr_handler,
				       PS_PCIE_DMA_IRQ_NOSHARE,
				       "PS PCIe DMA Chan Intr handler", chan);
		if (err) {
			dev_err(xdev->dev,
				"Irq %d for chan %d error %d\n",
				pci_irq_vector(xdev->pci_dev, i),
				chan->channel_number, err);
			break;
		}
	}

	if (err) {
		while (--i >= 0) {
			chan = &xdev->channels[i];
			devm_free_irq(xdev->dev,
				      pci_irq_vector(xdev->pci_dev, i), chan);
		}
	}

	return err;
}

/**
 * device_intr_setup - Requests interrupt handler for DMA device
 *
 * @xdev: Driver specific data for device
 *
 * Return: 0 on success and non zero value on failure.
 */
static int device_intr_setup(struct xlnx_pcie_dma_device *xdev)
{
	int err;
	unsigned long intr_flags = IRQF_SHARED;

	if (xdev->pci_dev->msix_enabled || xdev->pci_dev->msi_enabled)
		intr_flags = PS_PCIE_DMA_IRQ_NOSHARE;

	err = devm_request_irq(xdev->dev,
			       pci_irq_vector(xdev->pci_dev, 0),
			       ps_pcie_dma_dev_intr_handler,
			       intr_flags,
			       "PS PCIe DMA Intr Handler", xdev);
	if (err)
		dev_err(xdev->dev, "Couldn't request irq %d\n",
			pci_irq_vector(xdev->pci_dev, 0));

	return err;
}

/**
 * irq_setup - Requests interrupts based on the interrupt type detected
 *
 * @xdev: Driver specific data for device
 *
 * Return: 0 on success and non zero value on failure.
 */
static int irq_setup(struct xlnx_pcie_dma_device *xdev)
{
	int err;

	if (xdev->irq_vecs == xdev->num_channels)
		err = chan_intr_setup(xdev);
	else
		err = device_intr_setup(xdev);

	return err;
}

static int platform_irq_setup(struct xlnx_pcie_dma_device *xdev)
{
	int err;

	err = devm_request_irq(xdev->dev,
			       xdev->platform_irq_vec,
			       ps_pcie_dma_dev_intr_handler,
			       IRQF_SHARED,
			       "PS PCIe Root DMA Handler", xdev);
	if (err)
		dev_err(xdev->dev, "Couldn't request irq %d\n",
			xdev->platform_irq_vec);

	return err;
}

/**
 * irq_probe - Checks which interrupt types can be serviced by hardware
 *
 * @xdev: Driver specific data for device
 *
 * Return: Number of interrupt vectors when successful or -ENOSPC on failure
 */
static int irq_probe(struct xlnx_pcie_dma_device *xdev)
{
	struct pci_dev *pdev;

	pdev = xdev->pci_dev;

	xdev->irq_vecs = pci_alloc_irq_vectors(pdev, 1, xdev->num_channels,
					       PCI_IRQ_ALL_TYPES);
	return xdev->irq_vecs;
}

/**
 * ps_pcie_check_intr_status - Checks channel interrupt status
 *
 * @chan: Pointer to the PS PCIe DMA channel structure
 *
 * Return: 0 if interrupt is pending on channel
 *		   -1 if no interrupt is pending on channel
 */
static int ps_pcie_check_intr_status(struct ps_pcie_dma_chan *chan)
{
	int err = -1;
	u32 status;

	if (chan->state != CHANNEL_AVAILABLE)
		return err;

	status = ps_pcie_dma_read(chan, chan->intr_status_offset);

	if (status & DMA_INTSTATUS_SGLINTR_BIT) {
		if (chan->primary_desc_cleanup) {
			queue_work(chan->primary_desc_cleanup,
				   &chan->handle_primary_desc_cleanup);
		}
		/* Clearing Persistent bit */
		ps_pcie_dma_set_mask(chan, chan->intr_status_offset,
				     DMA_INTSTATUS_SGLINTR_BIT);
		err = 0;
	}

	if (status & DMA_INTSTATUS_SWINTR_BIT) {
		if (chan->sw_intrs_wrkq)
			queue_work(chan->sw_intrs_wrkq, &chan->handle_sw_intrs);
		/* Clearing Persistent bit */
		ps_pcie_dma_set_mask(chan, chan->intr_status_offset,
				     DMA_INTSTATUS_SWINTR_BIT);
		err = 0;
	}

	if (status & DMA_INTSTATUS_DMAERR_BIT) {
		dev_err(chan->dev,
			"DMA Channel %d ControlStatus Reg: 0x%x",
			chan->channel_number, status);
		dev_err(chan->dev,
			"Chn %d SrcQLmt = %d SrcQSz = %d SrcQNxt = %d",
			chan->channel_number,
			chan->chan_base->src_q_limit,
			chan->chan_base->src_q_size,
			chan->chan_base->src_q_next);
		dev_err(chan->dev,
			"Chn %d SrcStaLmt = %d SrcStaSz = %d SrcStaNxt = %d",
			chan->channel_number,
			chan->chan_base->stas_q_limit,
			chan->chan_base->stas_q_size,
			chan->chan_base->stas_q_next);
		dev_err(chan->dev,
			"Chn %d DstQLmt = %d DstQSz = %d DstQNxt = %d",
			chan->channel_number,
			chan->chan_base->dst_q_limit,
			chan->chan_base->dst_q_size,
			chan->chan_base->dst_q_next);
		dev_err(chan->dev,
			"Chan %d DstStaLmt = %d DstStaSz = %d DstStaNxt = %d",
			chan->channel_number,
			chan->chan_base->stad_q_limit,
			chan->chan_base->stad_q_size,
			chan->chan_base->stad_q_next);
		/* Clearing Persistent bit */
		ps_pcie_dma_set_mask(chan, chan->intr_status_offset,
				     DMA_INTSTATUS_DMAERR_BIT);

		handle_error(chan);

		err = 0;
	}

	return err;
}

static int init_hw_components(struct ps_pcie_dma_chan *chan)
{
	if (chan->psrc_sgl_bd && chan->psrc_sta_bd) {
		/*  Programming SourceQ and StatusQ bd addresses */
		chan->chan_base->src_q_next = 0;
		chan->chan_base->src_q_high =
			upper_32_bits(chan->src_sgl_bd_pa);
		chan->chan_base->src_q_size = chan->total_descriptors;
		chan->chan_base->src_q_limit = 0;
		if (chan->xdev->is_rootdma) {
			chan->chan_base->src_q_low = ROOTDMA_Q_READ_ATTRIBUTE
						     | DMA_QPTRLO_QLOCAXI_BIT;
		} else {
			chan->chan_base->src_q_low = 0;
		}
		chan->chan_base->src_q_low |=
			(lower_32_bits((chan->src_sgl_bd_pa))
			 & ~(DMA_SRC_Q_LOW_BIT_SHIFT))
			| DMA_QPTRLO_Q_ENABLE_BIT;

		chan->chan_base->stas_q_next = 0;
		chan->chan_base->stas_q_high =
			upper_32_bits(chan->src_sta_bd_pa);
		chan->chan_base->stas_q_size = chan->total_descriptors;
		chan->chan_base->stas_q_limit = chan->total_descriptors - 1;
		if (chan->xdev->is_rootdma) {
			chan->chan_base->stas_q_low = ROOTDMA_Q_READ_ATTRIBUTE
						      | DMA_QPTRLO_QLOCAXI_BIT;
		} else {
			chan->chan_base->stas_q_low = 0;
		}
		chan->chan_base->stas_q_low |=
			(lower_32_bits(chan->src_sta_bd_pa)
			 & ~(DMA_SRC_Q_LOW_BIT_SHIFT))
			| DMA_QPTRLO_Q_ENABLE_BIT;
	}

	if (chan->pdst_sgl_bd && chan->pdst_sta_bd) {
		/*  Programming DestinationQ and StatusQ buffer descriptors */
		chan->chan_base->dst_q_next = 0;
		chan->chan_base->dst_q_high =
			upper_32_bits(chan->dst_sgl_bd_pa);
		chan->chan_base->dst_q_size = chan->total_descriptors;
		chan->chan_base->dst_q_limit = 0;
		if (chan->xdev->is_rootdma) {
			chan->chan_base->dst_q_low = ROOTDMA_Q_READ_ATTRIBUTE
						     | DMA_QPTRLO_QLOCAXI_BIT;
		} else {
			chan->chan_base->dst_q_low = 0;
		}
		chan->chan_base->dst_q_low |=
			(lower_32_bits(chan->dst_sgl_bd_pa)
			 & ~(DMA_SRC_Q_LOW_BIT_SHIFT))
			| DMA_QPTRLO_Q_ENABLE_BIT;

		chan->chan_base->stad_q_next = 0;
		chan->chan_base->stad_q_high =
			upper_32_bits(chan->dst_sta_bd_pa);
		chan->chan_base->stad_q_size = chan->total_descriptors;
		chan->chan_base->stad_q_limit = chan->total_descriptors - 1;
		if (chan->xdev->is_rootdma) {
			chan->chan_base->stad_q_low = ROOTDMA_Q_READ_ATTRIBUTE
						      | DMA_QPTRLO_QLOCAXI_BIT;
		} else {
			chan->chan_base->stad_q_low = 0;
		}
		chan->chan_base->stad_q_low |=
			(lower_32_bits(chan->dst_sta_bd_pa)
			 & ~(DMA_SRC_Q_LOW_BIT_SHIFT))
			| DMA_QPTRLO_Q_ENABLE_BIT;
	}

	return 0;
}

static void update_channel_read_attribute(struct ps_pcie_dma_chan *chan)
{
	if (chan->xdev->is_rootdma) {
		/* For Root DMA, Host Memory and Buffer Descriptors
		 * will be on AXI side
		 */
		if (chan->srcq_buffer_location == BUFFER_LOC_PCI) {
			chan->read_attribute = (AXI_ATTRIBUTE <<
						SRC_CTL_ATTRIB_BIT_SHIFT) |
						SOURCE_CONTROL_BD_LOC_AXI;
		} else if (chan->srcq_buffer_location == BUFFER_LOC_AXI) {
			chan->read_attribute = AXI_ATTRIBUTE <<
					       SRC_CTL_ATTRIB_BIT_SHIFT;
		}
	} else {
		if (chan->srcq_buffer_location == BUFFER_LOC_PCI) {
			chan->read_attribute = PCI_ATTRIBUTE <<
					       SRC_CTL_ATTRIB_BIT_SHIFT;
		} else if (chan->srcq_buffer_location == BUFFER_LOC_AXI) {
			chan->read_attribute = (AXI_ATTRIBUTE <<
						SRC_CTL_ATTRIB_BIT_SHIFT) |
						SOURCE_CONTROL_BD_LOC_AXI;
		}
	}
}

static void update_channel_write_attribute(struct ps_pcie_dma_chan *chan)
{
	if (chan->xdev->is_rootdma) {
		/* For Root DMA, Host Memory and Buffer Descriptors
		 * will be on AXI side
		 */
		if (chan->dstq_buffer_location == BUFFER_LOC_PCI) {
			chan->write_attribute = (AXI_ATTRIBUTE <<
						 SRC_CTL_ATTRIB_BIT_SHIFT) |
						SOURCE_CONTROL_BD_LOC_AXI;
		} else if (chan->srcq_buffer_location == BUFFER_LOC_AXI) {
			chan->write_attribute = AXI_ATTRIBUTE <<
						SRC_CTL_ATTRIB_BIT_SHIFT;
		}
	} else {
		if (chan->dstq_buffer_location == BUFFER_LOC_PCI) {
			chan->write_attribute = PCI_ATTRIBUTE <<
						SRC_CTL_ATTRIB_BIT_SHIFT;
		} else if (chan->dstq_buffer_location == BUFFER_LOC_AXI) {
			chan->write_attribute = (AXI_ATTRIBUTE <<
						 SRC_CTL_ATTRIB_BIT_SHIFT) |
						SOURCE_CONTROL_BD_LOC_AXI;
		}
	}
	chan->write_attribute |= SOURCE_CONTROL_BACK_TO_BACK_PACK_BIT;
}

static int init_sw_components(struct ps_pcie_dma_chan *chan)
{
	if (chan->ppkt_ctx_srcq && chan->psrc_sgl_bd &&
	    chan->psrc_sta_bd) {
		memset(chan->ppkt_ctx_srcq, 0,
		       sizeof(struct PACKET_TRANSFER_PARAMS)
		       * chan->total_descriptors);

		memset(chan->psrc_sgl_bd, 0,
		       sizeof(struct SOURCE_DMA_DESCRIPTOR)
		       * chan->total_descriptors);

		memset(chan->psrc_sta_bd, 0,
		       sizeof(struct STATUS_DMA_DESCRIPTOR)
		       * chan->total_descriptors);

		chan->src_avail_descriptors = chan->total_descriptors;

		chan->src_sgl_freeidx = 0;
		chan->src_staprobe_idx = 0;
		chan->src_sta_hw_probe_idx = chan->total_descriptors - 1;
		chan->idx_ctx_srcq_head = 0;
		chan->idx_ctx_srcq_tail = 0;
	}

	if (chan->ppkt_ctx_dstq && chan->pdst_sgl_bd &&
	    chan->pdst_sta_bd) {
		memset(chan->ppkt_ctx_dstq, 0,
		       sizeof(struct PACKET_TRANSFER_PARAMS)
		       * chan->total_descriptors);

		memset(chan->pdst_sgl_bd, 0,
		       sizeof(struct DEST_DMA_DESCRIPTOR)
		       * chan->total_descriptors);

		memset(chan->pdst_sta_bd, 0,
		       sizeof(struct STATUS_DMA_DESCRIPTOR)
		       * chan->total_descriptors);

		chan->dst_avail_descriptors = chan->total_descriptors;

		chan->dst_sgl_freeidx = 0;
		chan->dst_staprobe_idx = 0;
		chan->dst_sta_hw_probe_idx = chan->total_descriptors - 1;
		chan->idx_ctx_dstq_head = 0;
		chan->idx_ctx_dstq_tail = 0;
	}

	return 0;
}

/**
 * ps_pcie_chan_reset - Resets channel, by programming relevant registers
 *
 * @chan: PS PCIe DMA channel information holder
 * Return: void
 */
static void ps_pcie_chan_reset(struct ps_pcie_dma_chan *chan)
{
	/* Enable channel reset */
	ps_pcie_dma_set_mask(chan, DMA_CNTRL_REG_OFFSET, DMA_CNTRL_RST_BIT);

	mdelay(10);

	/* Disable channel reset */
	ps_pcie_dma_clr_mask(chan, DMA_CNTRL_REG_OFFSET, DMA_CNTRL_RST_BIT);
}

/**
 * poll_completed_transactions - Function invoked by poll timer
 *
 * @t: Pointer to timer triggering this callback
 * Return: void
 */
static void poll_completed_transactions(struct timer_list *t)
{
	struct ps_pcie_dma_chan *chan = from_timer(chan, t, poll_timer);

	if (chan->state == CHANNEL_AVAILABLE) {
		queue_work(chan->primary_desc_cleanup,
			   &chan->handle_primary_desc_cleanup);
	}

	mod_timer(&chan->poll_timer, jiffies + chan->poll_timer_freq);
}

static bool check_descriptors_for_two_queues(struct ps_pcie_dma_chan *chan,
					     struct ps_pcie_tx_segment *seg)
{
	if (seg->src_elements) {
		if (chan->src_avail_descriptors >=
		    seg->src_elements) {
			return true;
		}
	} else if (seg->dst_elements) {
		if (chan->dst_avail_descriptors >=
		    seg->dst_elements) {
			return true;
		}
	}

	return false;
}

static bool check_descriptors_for_all_queues(struct ps_pcie_dma_chan *chan,
					     struct ps_pcie_tx_segment *seg)
{
	if (chan->src_avail_descriptors >=
		seg->src_elements &&
	    chan->dst_avail_descriptors >=
		seg->dst_elements) {
		return true;
	}

	return false;
}

static bool check_descriptor_availability(struct ps_pcie_dma_chan *chan,
					  struct ps_pcie_tx_segment *seg)
{
	if (chan->num_queues == DEFAULT_DMA_QUEUES)
		return check_descriptors_for_all_queues(chan, seg);
	else
		return check_descriptors_for_two_queues(chan, seg);
}

static void handle_error(struct ps_pcie_dma_chan *chan)
{
	if (chan->state != CHANNEL_AVAILABLE)
		return;

	spin_lock(&chan->channel_lock);
	chan->state = CHANNEL_ERROR;
	spin_unlock(&chan->channel_lock);

	if (chan->maintenance_workq)
		queue_work(chan->maintenance_workq, &chan->handle_chan_reset);
}

static void xlnx_ps_pcie_update_srcq(struct ps_pcie_dma_chan *chan,
				     struct ps_pcie_tx_segment *seg)
{
	struct SOURCE_DMA_DESCRIPTOR *pdesc;
	struct PACKET_TRANSFER_PARAMS *pkt_ctx = NULL;
	struct ps_pcie_transfer_elements *ele = NULL;
	u32 i = 0;

	pkt_ctx = chan->ppkt_ctx_srcq + chan->idx_ctx_srcq_head;
	if (pkt_ctx->availability_status == IN_USE) {
		dev_err(chan->dev,
			"src pkt context not avail for channel %d\n",
			chan->channel_number);
		handle_error(chan);
		return;
	}

	pkt_ctx->availability_status = IN_USE;

	if (chan->srcq_buffer_location == BUFFER_LOC_PCI)
		pkt_ctx->seg = seg;

	/*  Get the address of the next available DMA Descriptor */
	pdesc = chan->psrc_sgl_bd + chan->src_sgl_freeidx;
	pkt_ctx->idx_sop = chan->src_sgl_freeidx;

	/* Build transactions using information in the scatter gather list */
	list_for_each_entry(ele, &seg->transfer_nodes, node) {
		if (chan->xdev->dma_buf_ext_addr) {
			pdesc->system_address =
				(u64)ele->src_pa;
		} else {
			pdesc->system_address =
				(u32)ele->src_pa;
		}

		pdesc->control_byte_count = (ele->transfer_bytes &
					    SOURCE_CONTROL_BD_BYTE_COUNT_MASK) |
					    chan->read_attribute;

		pdesc->user_handle = chan->idx_ctx_srcq_head;
		pdesc->user_id = DEFAULT_UID;
		/* Check if this is last descriptor */
		if (i == (seg->src_elements - 1)) {
			pkt_ctx->idx_eop = chan->src_sgl_freeidx;
			pdesc->control_byte_count |= SOURCE_CONTROL_BD_EOP_BIT;
			if ((seg->async_tx.flags & DMA_PREP_INTERRUPT) ==
						   DMA_PREP_INTERRUPT) {
				pdesc->control_byte_count |=
					SOURCE_CONTROL_BD_INTR_BIT;
			}
		}
		chan->src_sgl_freeidx++;
		if (chan->src_sgl_freeidx == chan->total_descriptors)
			chan->src_sgl_freeidx = 0;
		pdesc = chan->psrc_sgl_bd + chan->src_sgl_freeidx;
		spin_lock(&chan->src_desc_lock);
		chan->src_avail_descriptors--;
		spin_unlock(&chan->src_desc_lock);
		i++;
	}

	chan->chan_base->src_q_limit = chan->src_sgl_freeidx;
	chan->idx_ctx_srcq_head++;
	if (chan->idx_ctx_srcq_head == chan->total_descriptors)
		chan->idx_ctx_srcq_head = 0;
}

static void xlnx_ps_pcie_update_dstq(struct ps_pcie_dma_chan *chan,
				     struct ps_pcie_tx_segment *seg)
{
	struct DEST_DMA_DESCRIPTOR *pdesc;
	struct PACKET_TRANSFER_PARAMS *pkt_ctx = NULL;
	struct ps_pcie_transfer_elements *ele = NULL;
	u32 i = 0;

	pkt_ctx = chan->ppkt_ctx_dstq + chan->idx_ctx_dstq_head;
	if (pkt_ctx->availability_status == IN_USE) {
		dev_err(chan->dev,
			"dst pkt context not avail for channel %d\n",
			chan->channel_number);
		handle_error(chan);

		return;
	}

	pkt_ctx->availability_status = IN_USE;

	if (chan->dstq_buffer_location == BUFFER_LOC_PCI)
		pkt_ctx->seg = seg;

	pdesc = chan->pdst_sgl_bd + chan->dst_sgl_freeidx;
	pkt_ctx->idx_sop = chan->dst_sgl_freeidx;

	/* Build transactions using information in the scatter gather list */
	list_for_each_entry(ele, &seg->transfer_nodes, node) {
		if (chan->xdev->dma_buf_ext_addr) {
			pdesc->system_address =
				(u64)ele->dst_pa;
		} else {
			pdesc->system_address =
				(u32)ele->dst_pa;
		}
		pdesc->control_byte_count = (ele->transfer_bytes &
					SOURCE_CONTROL_BD_BYTE_COUNT_MASK) |
						chan->write_attribute;

		pdesc->user_handle = chan->idx_ctx_dstq_head;
		/* Check if this is last descriptor */
		if (i == (seg->dst_elements - 1))
			pkt_ctx->idx_eop = chan->dst_sgl_freeidx;
		chan->dst_sgl_freeidx++;
		if (chan->dst_sgl_freeidx == chan->total_descriptors)
			chan->dst_sgl_freeidx = 0;
		pdesc = chan->pdst_sgl_bd + chan->dst_sgl_freeidx;
		spin_lock(&chan->dst_desc_lock);
		chan->dst_avail_descriptors--;
		spin_unlock(&chan->dst_desc_lock);
		i++;
	}

	chan->chan_base->dst_q_limit = chan->dst_sgl_freeidx;
	chan->idx_ctx_dstq_head++;
	if (chan->idx_ctx_dstq_head == chan->total_descriptors)
		chan->idx_ctx_dstq_head = 0;
}

static void ps_pcie_chan_program_work(struct work_struct *work)
{
	struct ps_pcie_dma_chan *chan =
		(struct ps_pcie_dma_chan *)container_of(work,
				struct ps_pcie_dma_chan,
				handle_chan_programming);
	struct ps_pcie_tx_segment *seg = NULL;

	while (chan->state == CHANNEL_AVAILABLE) {
		spin_lock(&chan->active_list_lock);
		seg = list_first_entry_or_null(&chan->active_list,
					       struct ps_pcie_tx_segment, node);
		spin_unlock(&chan->active_list_lock);

		if (!seg)
			break;

		if (check_descriptor_availability(chan, seg) == false)
			break;

		spin_lock(&chan->active_list_lock);
		list_del(&seg->node);
		spin_unlock(&chan->active_list_lock);

		if (seg->src_elements)
			xlnx_ps_pcie_update_srcq(chan, seg);

		if (seg->dst_elements)
			xlnx_ps_pcie_update_dstq(chan, seg);
	}
}

/**
 * dst_cleanup_work - Goes through all completed elements in status Q
 * and invokes callbacks for the concerned DMA transaction.
 *
 * @work: Work associated with the task
 *
 * Return: void
 */
static void dst_cleanup_work(struct work_struct *work)
{
	struct ps_pcie_dma_chan *chan =
		(struct ps_pcie_dma_chan *)container_of(work,
			struct ps_pcie_dma_chan, handle_dstq_desc_cleanup);

	struct STATUS_DMA_DESCRIPTOR *psta_bd;
	struct DEST_DMA_DESCRIPTOR *pdst_bd;
	struct PACKET_TRANSFER_PARAMS *ppkt_ctx;
	struct dmaengine_result rslt;
	u32 completed_bytes;
	u32 dstq_desc_idx;
	struct ps_pcie_transfer_elements *ele, *ele_nxt;

	psta_bd = chan->pdst_sta_bd + chan->dst_staprobe_idx;

	while (psta_bd->status_flag_byte_count & STA_BD_COMPLETED_BIT) {
		if (psta_bd->status_flag_byte_count &
				STA_BD_DESTINATION_ERROR_BIT) {
			dev_err(chan->dev,
				"Dst Sts Elmnt %d chan %d has Destination Err",
				chan->dst_staprobe_idx + 1,
				chan->channel_number);
			handle_error(chan);
			break;
		}
		if (psta_bd->status_flag_byte_count & STA_BD_SOURCE_ERROR_BIT) {
			dev_err(chan->dev,
				"Dst Sts Elmnt %d chan %d has Source Error",
				chan->dst_staprobe_idx + 1,
				chan->channel_number);
			handle_error(chan);
			break;
		}
		if (psta_bd->status_flag_byte_count &
				STA_BD_INTERNAL_ERROR_BIT) {
			dev_err(chan->dev,
				"Dst Sts Elmnt %d chan %d has Internal Error",
				chan->dst_staprobe_idx + 1,
				chan->channel_number);
			handle_error(chan);
			break;
		}
		/* we are using 64 bit USER field. */
		if ((psta_bd->status_flag_byte_count &
					STA_BD_UPPER_STATUS_NONZERO_BIT) == 0) {
			dev_err(chan->dev,
				"Dst Sts Elmnt %d for chan %d has NON ZERO",
				chan->dst_staprobe_idx + 1,
				chan->channel_number);
			handle_error(chan);
			break;
		}

		chan->idx_ctx_dstq_tail = psta_bd->user_handle;
		ppkt_ctx = chan->ppkt_ctx_dstq + chan->idx_ctx_dstq_tail;
		completed_bytes = (psta_bd->status_flag_byte_count &
					STA_BD_BYTE_COUNT_MASK) >>
						STA_BD_BYTE_COUNT_SHIFT;

		memset(psta_bd, 0, sizeof(struct STATUS_DMA_DESCRIPTOR));

		chan->dst_staprobe_idx++;

		if (chan->dst_staprobe_idx == chan->total_descriptors)
			chan->dst_staprobe_idx = 0;

		chan->dst_sta_hw_probe_idx++;

		if (chan->dst_sta_hw_probe_idx == chan->total_descriptors)
			chan->dst_sta_hw_probe_idx = 0;

		chan->chan_base->stad_q_limit = chan->dst_sta_hw_probe_idx;

		psta_bd = chan->pdst_sta_bd + chan->dst_staprobe_idx;

		dstq_desc_idx = ppkt_ctx->idx_sop;

		do {
			pdst_bd = chan->pdst_sgl_bd + dstq_desc_idx;
			memset(pdst_bd, 0,
			       sizeof(struct DEST_DMA_DESCRIPTOR));

			spin_lock(&chan->dst_desc_lock);
			chan->dst_avail_descriptors++;
			spin_unlock(&chan->dst_desc_lock);

			if (dstq_desc_idx == ppkt_ctx->idx_eop)
				break;

			dstq_desc_idx++;

			if (dstq_desc_idx == chan->total_descriptors)
				dstq_desc_idx = 0;

		} while (1);

		/* Invoking callback */
		if (ppkt_ctx->seg) {
			spin_lock(&chan->cookie_lock);
			dma_cookie_complete(&ppkt_ctx->seg->async_tx);
			spin_unlock(&chan->cookie_lock);
			rslt.result = DMA_TRANS_NOERROR;
			rslt.residue = ppkt_ctx->seg->total_transfer_bytes -
					completed_bytes;
			dmaengine_desc_get_callback_invoke(&ppkt_ctx->seg->async_tx,
							   &rslt);
			list_for_each_entry_safe(ele, ele_nxt,
						 &ppkt_ctx->seg->transfer_nodes,
						 node) {
				list_del(&ele->node);
				mempool_free(ele, chan->tx_elements_pool);
			}
			mempool_free(ppkt_ctx->seg, chan->transactions_pool);
		}
		memset(ppkt_ctx, 0, sizeof(struct PACKET_TRANSFER_PARAMS));
	}

	complete(&chan->dstq_work_complete);
}

/**
 * src_cleanup_work - Goes through all completed elements in status Q and
 * invokes callbacks for the concerned DMA transaction.
 *
 * @work: Work associated with the task
 *
 * Return: void
 */
static void src_cleanup_work(struct work_struct *work)
{
	struct ps_pcie_dma_chan *chan =
		(struct ps_pcie_dma_chan *)container_of(
		work, struct ps_pcie_dma_chan, handle_srcq_desc_cleanup);

	struct STATUS_DMA_DESCRIPTOR *psta_bd;
	struct SOURCE_DMA_DESCRIPTOR *psrc_bd;
	struct PACKET_TRANSFER_PARAMS *ppkt_ctx;
	struct dmaengine_result rslt;
	u32 completed_bytes;
	u32 srcq_desc_idx;
	struct ps_pcie_transfer_elements *ele, *ele_nxt;

	psta_bd = chan->psrc_sta_bd + chan->src_staprobe_idx;

	while (psta_bd->status_flag_byte_count & STA_BD_COMPLETED_BIT) {
		if (psta_bd->status_flag_byte_count &
				STA_BD_DESTINATION_ERROR_BIT) {
			dev_err(chan->dev,
				"Src Sts Elmnt %d chan %d has Dst Error",
				chan->src_staprobe_idx + 1,
				chan->channel_number);
			handle_error(chan);
			break;
		}
		if (psta_bd->status_flag_byte_count & STA_BD_SOURCE_ERROR_BIT) {
			dev_err(chan->dev,
				"Src Sts Elmnt %d chan %d has Source Error",
				chan->src_staprobe_idx + 1,
				chan->channel_number);
			handle_error(chan);
			break;
		}
		if (psta_bd->status_flag_byte_count &
				STA_BD_INTERNAL_ERROR_BIT) {
			dev_err(chan->dev,
				"Src Sts Elmnt %d chan %d has Internal Error",
				chan->src_staprobe_idx + 1,
				chan->channel_number);
			handle_error(chan);
			break;
		}
		if ((psta_bd->status_flag_byte_count
				& STA_BD_UPPER_STATUS_NONZERO_BIT) == 0) {
			dev_err(chan->dev,
				"Src Sts Elmnt %d chan %d has NonZero",
				chan->src_staprobe_idx + 1,
				chan->channel_number);
			handle_error(chan);
			break;
		}
		chan->idx_ctx_srcq_tail = psta_bd->user_handle;
		ppkt_ctx = chan->ppkt_ctx_srcq + chan->idx_ctx_srcq_tail;
		completed_bytes = (psta_bd->status_flag_byte_count
					& STA_BD_BYTE_COUNT_MASK) >>
						STA_BD_BYTE_COUNT_SHIFT;

		memset(psta_bd, 0, sizeof(struct STATUS_DMA_DESCRIPTOR));

		chan->src_staprobe_idx++;

		if (chan->src_staprobe_idx == chan->total_descriptors)
			chan->src_staprobe_idx = 0;

		chan->src_sta_hw_probe_idx++;

		if (chan->src_sta_hw_probe_idx == chan->total_descriptors)
			chan->src_sta_hw_probe_idx = 0;

		chan->chan_base->stas_q_limit = chan->src_sta_hw_probe_idx;

		psta_bd = chan->psrc_sta_bd + chan->src_staprobe_idx;

		srcq_desc_idx = ppkt_ctx->idx_sop;

		do {
			psrc_bd = chan->psrc_sgl_bd + srcq_desc_idx;
			memset(psrc_bd, 0,
			       sizeof(struct SOURCE_DMA_DESCRIPTOR));

			spin_lock(&chan->src_desc_lock);
			chan->src_avail_descriptors++;
			spin_unlock(&chan->src_desc_lock);

			if (srcq_desc_idx == ppkt_ctx->idx_eop)
				break;
			srcq_desc_idx++;

			if (srcq_desc_idx == chan->total_descriptors)
				srcq_desc_idx = 0;

		} while (1);

		/* Invoking callback */
		if (ppkt_ctx->seg) {
			spin_lock(&chan->cookie_lock);
			dma_cookie_complete(&ppkt_ctx->seg->async_tx);
			spin_unlock(&chan->cookie_lock);
			rslt.result = DMA_TRANS_NOERROR;
			rslt.residue = ppkt_ctx->seg->total_transfer_bytes -
					completed_bytes;
			dmaengine_desc_get_callback_invoke(&ppkt_ctx->seg->async_tx,
							   &rslt);
			list_for_each_entry_safe(ele, ele_nxt,
						 &ppkt_ctx->seg->transfer_nodes,
						 node) {
				list_del(&ele->node);
				mempool_free(ele, chan->tx_elements_pool);
			}
			mempool_free(ppkt_ctx->seg, chan->transactions_pool);
		}
		memset(ppkt_ctx, 0, sizeof(struct PACKET_TRANSFER_PARAMS));
	}

	complete(&chan->srcq_work_complete);
}

/**
 * ps_pcie_chan_primary_work - Masks out interrupts, invokes source Q and
 * destination Q processing. Waits for source Q and destination Q processing
 * and re enables interrupts. Same work is invoked by timer if coalesce count
 * is greater than zero and interrupts are not invoked before the timeout period
 *
 * @work: Work associated with the task
 *
 * Return: void
 */
static void ps_pcie_chan_primary_work(struct work_struct *work)
{
	struct ps_pcie_dma_chan *chan =
		(struct ps_pcie_dma_chan *)container_of(
				work, struct ps_pcie_dma_chan,
				handle_primary_desc_cleanup);

	/* Disable interrupts for Channel */
	ps_pcie_dma_clr_mask(chan, chan->intr_control_offset,
			     DMA_INTCNTRL_ENABLINTR_BIT);

	if (chan->psrc_sgl_bd) {
		reinit_completion(&chan->srcq_work_complete);
		if (chan->srcq_desc_cleanup)
			queue_work(chan->srcq_desc_cleanup,
				   &chan->handle_srcq_desc_cleanup);
	}
	if (chan->pdst_sgl_bd) {
		reinit_completion(&chan->dstq_work_complete);
		if (chan->dstq_desc_cleanup)
			queue_work(chan->dstq_desc_cleanup,
				   &chan->handle_dstq_desc_cleanup);
	}

	if (chan->psrc_sgl_bd)
		wait_for_completion_interruptible(&chan->srcq_work_complete);
	if (chan->pdst_sgl_bd)
		wait_for_completion_interruptible(&chan->dstq_work_complete);

	/* Enable interrupts for channel */
	ps_pcie_dma_set_mask(chan, chan->intr_control_offset,
			     DMA_INTCNTRL_ENABLINTR_BIT);

	if (chan->chan_programming) {
		queue_work(chan->chan_programming,
			   &chan->handle_chan_programming);
	}

	if (chan->coalesce_count > 0 && chan->poll_timer.function)
		mod_timer(&chan->poll_timer, jiffies + chan->poll_timer_freq);
}

static int read_rootdma_config(struct platform_device *platform_dev,
			       struct xlnx_pcie_dma_device *xdev)
{
	int err;
	struct resource *r;

	err = dma_set_mask(&platform_dev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_info(&platform_dev->dev, "Cannot set 64 bit DMA mask\n");
		err = dma_set_mask(&platform_dev->dev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&platform_dev->dev, "DMA mask set error\n");
			return err;
		}
	}

	err = dma_set_coherent_mask(&platform_dev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_info(&platform_dev->dev, "Cannot set 64 bit consistent DMA mask\n");
		err = dma_set_coherent_mask(&platform_dev->dev,
					    DMA_BIT_MASK(32));
		if (err) {
			dev_err(&platform_dev->dev, "Cannot set consistent DMA mask\n");
			return err;
		}
	}

	r = platform_get_resource_byname(platform_dev, IORESOURCE_MEM,
					 "ps_pcie_regbase");
	if (!r) {
		dev_err(&platform_dev->dev,
			"Unable to find memory resource for root dma\n");
		return PTR_ERR(r);
	}

	xdev->reg_base = devm_ioremap_resource(&platform_dev->dev, r);
	if (IS_ERR(xdev->reg_base)) {
		dev_err(&platform_dev->dev, "ioresource error for root dma\n");
		return PTR_ERR(xdev->reg_base);
	}

	xdev->platform_irq_vec =
		platform_get_irq_byname(platform_dev,
					"ps_pcie_rootdma_intr");
	if (xdev->platform_irq_vec < 0) {
		dev_err(&platform_dev->dev,
			"Unable to get interrupt number for root dma\n");
		return xdev->platform_irq_vec;
	}

	err = device_property_read_u16(&platform_dev->dev, "dma_vendorid",
				       &xdev->rootdma_vendor);
	if (err) {
		dev_err(&platform_dev->dev,
			"Unable to find RootDMA PCI Vendor Id\n");
		return err;
	}

	err = device_property_read_u16(&platform_dev->dev, "dma_deviceid",
				       &xdev->rootdma_device);
	if (err) {
		dev_err(&platform_dev->dev,
			"Unable to find RootDMA PCI Device Id\n");
		return err;
	}

	xdev->common.dev = xdev->dev;

	return 0;
}

static int read_epdma_config(struct platform_device *platform_dev,
			     struct xlnx_pcie_dma_device *xdev)
{
	int err;
	struct pci_dev *pdev;
	u16 i;
	void __iomem * const *pci_iomap;
	unsigned long pci_bar_length;

	pdev = *((struct pci_dev **)(platform_dev->dev.platform_data));
	xdev->pci_dev = pdev;

	for (i = 0; i < MAX_BARS; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		xdev->bar_mask = xdev->bar_mask | (1 << (i));
	}

	err = pcim_iomap_regions(pdev, xdev->bar_mask, PLATFORM_DRIVER_NAME);
	if (err) {
		dev_err(&pdev->dev, "Cannot request PCI regions, aborting\n");
		return err;
	}

	pci_iomap = pcim_iomap_table(pdev);
	if (!pci_iomap) {
		err = -ENOMEM;
		return err;
	}

	for (i = 0; i < MAX_BARS; i++) {
		pci_bar_length = pci_resource_len(pdev, i);
		if (pci_bar_length == 0) {
			xdev->bar_info[i].BAR_LENGTH = 0;
			xdev->bar_info[i].BAR_PHYS_ADDR = 0;
			xdev->bar_info[i].BAR_VIRT_ADDR = NULL;
		} else {
			xdev->bar_info[i].BAR_LENGTH =
				pci_bar_length;
			xdev->bar_info[i].BAR_PHYS_ADDR =
				pci_resource_start(pdev, i);
			xdev->bar_info[i].BAR_VIRT_ADDR =
				(void *)pci_iomap[i];
		}
	}

	xdev->reg_base = pci_iomap[DMA_BAR_NUMBER];

	err = irq_probe(xdev);
	if (err < 0) {
		dev_err(&pdev->dev, "Cannot probe irq lines for device %d\n",
			platform_dev->id);
		return err;
	}

	xdev->common.dev = &pdev->dev;

	return 0;
}

static int probe_channel_properties(struct platform_device *platform_dev,
				    struct xlnx_pcie_dma_device *xdev,
				    u16 channel_number)
{
	int i;
	char propertyname[CHANNEL_PROPERTY_LENGTH];
	int numvals, ret;
	u32 *val;
	struct ps_pcie_dma_chan *channel;
	struct ps_pcie_dma_channel_match *xlnx_match;

	snprintf(propertyname, CHANNEL_PROPERTY_LENGTH,
		 "ps_pcie_channel%d", channel_number);

	channel = &xdev->channels[channel_number];

	spin_lock_init(&channel->channel_lock);
	spin_lock_init(&channel->cookie_lock);

	INIT_LIST_HEAD(&channel->pending_list);
	spin_lock_init(&channel->pending_list_lock);

	INIT_LIST_HEAD(&channel->active_list);
	spin_lock_init(&channel->active_list_lock);

	spin_lock_init(&channel->src_desc_lock);
	spin_lock_init(&channel->dst_desc_lock);

	INIT_LIST_HEAD(&channel->pending_interrupts_list);
	spin_lock_init(&channel->pending_interrupts_lock);

	INIT_LIST_HEAD(&channel->active_interrupts_list);
	spin_lock_init(&channel->active_interrupts_lock);

	init_completion(&channel->srcq_work_complete);
	init_completion(&channel->dstq_work_complete);
	init_completion(&channel->chan_shutdown_complt);
	init_completion(&channel->chan_terminate_complete);

	if (device_property_present(&platform_dev->dev, propertyname)) {
		numvals = device_property_read_u32_array(&platform_dev->dev,
							 propertyname, NULL, 0);

		if (numvals < 0)
			return numvals;

		val = devm_kzalloc(&platform_dev->dev, sizeof(u32) * numvals,
				   GFP_KERNEL);

		if (!val)
			return -ENOMEM;

		ret = device_property_read_u32_array(&platform_dev->dev,
						     propertyname, val,
						     numvals);
		if (ret < 0) {
			dev_err(&platform_dev->dev,
				"Unable to read property %s\n", propertyname);
			return ret;
		}

		for (i = 0; i < numvals; i++) {
			switch (i) {
			case DMA_CHANNEL_DIRECTION:
				channel->direction =
					(val[DMA_CHANNEL_DIRECTION] ==
						PCIE_AXI_DIRECTION) ?
						DMA_TO_DEVICE : DMA_FROM_DEVICE;
				break;
			case NUM_DESCRIPTORS:
				channel->total_descriptors =
						val[NUM_DESCRIPTORS];
				if (channel->total_descriptors >
					MAX_DESCRIPTORS) {
					dev_info(&platform_dev->dev,
						 "Descriptors > alowd max\n");
					channel->total_descriptors =
							MAX_DESCRIPTORS;
				}
				break;
			case NUM_QUEUES:
				channel->num_queues = val[NUM_QUEUES];
				switch (channel->num_queues) {
				case DEFAULT_DMA_QUEUES:
						break;
				case TWO_DMA_QUEUES:
						break;
				default:
				dev_info(&platform_dev->dev,
					 "Incorrect Q number for dma chan\n");
				channel->num_queues = DEFAULT_DMA_QUEUES;
				}
				break;
			case COALESE_COUNT:
				channel->coalesce_count = val[COALESE_COUNT];

				if (channel->coalesce_count >
					MAX_COALESCE_COUNT) {
					dev_info(&platform_dev->dev,
						 "Invalid coalesce Count\n");
					channel->coalesce_count =
						MAX_COALESCE_COUNT;
				}
				break;
			case POLL_TIMER_FREQUENCY:
				channel->poll_timer_freq =
					val[POLL_TIMER_FREQUENCY];
				break;
			default:
				dev_err(&platform_dev->dev,
					"Check order of channel properties!\n");
			}
		}
	} else {
		dev_err(&platform_dev->dev,
			"Property %s not present. Invalid configuration!\n",
				propertyname);
		return -ENOTSUPP;
	}

	if (channel->direction == DMA_TO_DEVICE) {
		if (channel->num_queues == DEFAULT_DMA_QUEUES) {
			channel->srcq_buffer_location = BUFFER_LOC_PCI;
			channel->dstq_buffer_location = BUFFER_LOC_AXI;
		} else {
			channel->srcq_buffer_location = BUFFER_LOC_PCI;
			channel->dstq_buffer_location = BUFFER_LOC_INVALID;
		}
	} else {
		if (channel->num_queues == DEFAULT_DMA_QUEUES) {
			channel->srcq_buffer_location = BUFFER_LOC_AXI;
			channel->dstq_buffer_location = BUFFER_LOC_PCI;
		} else {
			channel->srcq_buffer_location = BUFFER_LOC_INVALID;
			channel->dstq_buffer_location = BUFFER_LOC_PCI;
		}
	}

	channel->xdev = xdev;
	channel->channel_number = channel_number;

	if (xdev->is_rootdma) {
		channel->dev = xdev->dev;
		channel->intr_status_offset = DMA_AXI_INTR_STATUS_REG_OFFSET;
		channel->intr_control_offset = DMA_AXI_INTR_CNTRL_REG_OFFSET;
	} else {
		channel->dev = &xdev->pci_dev->dev;
		channel->intr_status_offset = DMA_PCIE_INTR_STATUS_REG_OFFSET;
		channel->intr_control_offset = DMA_PCIE_INTR_CNTRL_REG_OFFSET;
	}

	channel->chan_base =
	(struct DMA_ENGINE_REGISTERS *)((__force char *)(xdev->reg_base) +
				 (channel_number * DMA_CHANNEL_REGS_SIZE));

	if ((channel->chan_base->dma_channel_status &
				DMA_STATUS_DMA_PRES_BIT) == 0) {
		dev_err(&platform_dev->dev,
			"Hardware reports channel not present\n");
		return -ENOTSUPP;
	}

	update_channel_read_attribute(channel);
	update_channel_write_attribute(channel);

	xlnx_match = devm_kzalloc(&platform_dev->dev,
				  sizeof(struct ps_pcie_dma_channel_match),
				  GFP_KERNEL);

	if (!xlnx_match)
		return -ENOMEM;

	if (xdev->is_rootdma) {
		xlnx_match->pci_vendorid = xdev->rootdma_vendor;
		xlnx_match->pci_deviceid = xdev->rootdma_device;
	} else {
		xlnx_match->pci_vendorid = xdev->pci_dev->vendor;
		xlnx_match->pci_deviceid = xdev->pci_dev->device;
		xlnx_match->bar_params = xdev->bar_info;
	}

	xlnx_match->board_number = xdev->board_number;
	xlnx_match->channel_number = channel_number;
	xlnx_match->direction = xdev->channels[channel_number].direction;

	channel->common.private = (void *)xlnx_match;

	channel->common.device = &xdev->common;
	list_add_tail(&channel->common.device_node, &xdev->common.channels);

	return 0;
}

static void xlnx_ps_pcie_destroy_mempool(struct ps_pcie_dma_chan *chan)
{
	mempool_destroy(chan->transactions_pool);

	mempool_destroy(chan->tx_elements_pool);

	mempool_destroy(chan->intr_transactions_pool);
}

static void xlnx_ps_pcie_free_worker_queues(struct ps_pcie_dma_chan *chan)
{
	if (chan->maintenance_workq)
		destroy_workqueue(chan->maintenance_workq);

	if (chan->sw_intrs_wrkq)
		destroy_workqueue(chan->sw_intrs_wrkq);

	if (chan->srcq_desc_cleanup)
		destroy_workqueue(chan->srcq_desc_cleanup);

	if (chan->dstq_desc_cleanup)
		destroy_workqueue(chan->dstq_desc_cleanup);

	if (chan->chan_programming)
		destroy_workqueue(chan->chan_programming);

	if (chan->primary_desc_cleanup)
		destroy_workqueue(chan->primary_desc_cleanup);
}

static void xlnx_ps_pcie_free_pkt_ctxts(struct ps_pcie_dma_chan *chan)
{
	kfree(chan->ppkt_ctx_srcq);

	kfree(chan->ppkt_ctx_dstq);
}

static void xlnx_ps_pcie_free_descriptors(struct ps_pcie_dma_chan *chan)
{
	ssize_t size;

	if (chan->psrc_sgl_bd) {
		size = chan->total_descriptors *
			sizeof(struct SOURCE_DMA_DESCRIPTOR);
		dma_free_coherent(chan->dev, size, chan->psrc_sgl_bd,
				  chan->src_sgl_bd_pa);
	}

	if (chan->pdst_sgl_bd) {
		size = chan->total_descriptors *
			sizeof(struct DEST_DMA_DESCRIPTOR);
		dma_free_coherent(chan->dev, size, chan->pdst_sgl_bd,
				  chan->dst_sgl_bd_pa);
	}

	if (chan->psrc_sta_bd) {
		size = chan->total_descriptors *
			sizeof(struct STATUS_DMA_DESCRIPTOR);
		dma_free_coherent(chan->dev, size, chan->psrc_sta_bd,
				  chan->src_sta_bd_pa);
	}

	if (chan->pdst_sta_bd) {
		size = chan->total_descriptors *
			sizeof(struct STATUS_DMA_DESCRIPTOR);
		dma_free_coherent(chan->dev, size, chan->pdst_sta_bd,
				  chan->dst_sta_bd_pa);
	}
}

static int xlnx_ps_pcie_channel_activate(struct ps_pcie_dma_chan *chan)
{
	u32 reg = chan->coalesce_count;

	reg = reg << DMA_INTCNTRL_SGCOLSCCNT_BIT_SHIFT;

	/* Enable Interrupts for channel */
	ps_pcie_dma_set_mask(chan, chan->intr_control_offset,
			     reg | DMA_INTCNTRL_ENABLINTR_BIT |
			     DMA_INTCNTRL_DMAERRINTR_BIT |
			     DMA_INTCNTRL_DMASGINTR_BIT);

	/* Enable DMA */
	ps_pcie_dma_set_mask(chan, DMA_CNTRL_REG_OFFSET,
			     DMA_CNTRL_ENABL_BIT |
			     DMA_CNTRL_64BIT_STAQ_ELEMSZ_BIT);

	spin_lock(&chan->channel_lock);
	chan->state = CHANNEL_AVAILABLE;
	spin_unlock(&chan->channel_lock);

	/* Activate timer if required */
	if (chan->coalesce_count > 0 && !chan->poll_timer.function)
		xlnx_ps_pcie_alloc_poll_timer(chan);

	return 0;
}

static void xlnx_ps_pcie_channel_quiesce(struct ps_pcie_dma_chan *chan)
{
	/* Disable interrupts for Channel */
	ps_pcie_dma_clr_mask(chan, chan->intr_control_offset,
			     DMA_INTCNTRL_ENABLINTR_BIT);

	/* Delete timer if it is created */
	if (chan->coalesce_count > 0 && !chan->poll_timer.function)
		xlnx_ps_pcie_free_poll_timer(chan);

	/* Flush descriptor cleaning work queues */
	if (chan->primary_desc_cleanup)
		flush_workqueue(chan->primary_desc_cleanup);

	/* Flush channel programming work queue */
	if (chan->chan_programming)
		flush_workqueue(chan->chan_programming);

	/*  Clear the persistent bits */
	ps_pcie_dma_set_mask(chan, chan->intr_status_offset,
			     DMA_INTSTATUS_DMAERR_BIT |
			     DMA_INTSTATUS_SGLINTR_BIT |
			     DMA_INTSTATUS_SWINTR_BIT);

	/* Disable DMA channel */
	ps_pcie_dma_clr_mask(chan, DMA_CNTRL_REG_OFFSET, DMA_CNTRL_ENABL_BIT);

	spin_lock(&chan->channel_lock);
	chan->state = CHANNEL_UNAVIALBLE;
	spin_unlock(&chan->channel_lock);
}

static void ivk_cbk_intr_seg(struct ps_pcie_intr_segment *intr_seg,
			     struct ps_pcie_dma_chan *chan,
			     enum dmaengine_tx_result result)
{
	struct dmaengine_result rslt;

	rslt.result = result;
	rslt.residue = 0;

	spin_lock(&chan->cookie_lock);
	dma_cookie_complete(&intr_seg->async_intr_tx);
	spin_unlock(&chan->cookie_lock);

	dmaengine_desc_get_callback_invoke(&intr_seg->async_intr_tx, &rslt);
}

static void ivk_cbk_seg(struct ps_pcie_tx_segment *seg,
			struct ps_pcie_dma_chan *chan,
			enum dmaengine_tx_result result)
{
	struct dmaengine_result rslt, *prslt;

	spin_lock(&chan->cookie_lock);
	dma_cookie_complete(&seg->async_tx);
	spin_unlock(&chan->cookie_lock);

	rslt.result = result;
	if (seg->src_elements &&
	    chan->srcq_buffer_location == BUFFER_LOC_PCI) {
		rslt.residue = seg->total_transfer_bytes;
		prslt = &rslt;
	} else if (seg->dst_elements &&
		   chan->dstq_buffer_location == BUFFER_LOC_PCI) {
		rslt.residue = seg->total_transfer_bytes;
		prslt = &rslt;
	} else {
		prslt = NULL;
	}

	dmaengine_desc_get_callback_invoke(&seg->async_tx, prslt);
}

static void ivk_cbk_ctx(struct PACKET_TRANSFER_PARAMS *ppkt_ctxt,
			struct ps_pcie_dma_chan *chan,
			enum dmaengine_tx_result result)
{
	if (ppkt_ctxt->availability_status == IN_USE) {
		if (ppkt_ctxt->seg) {
			ivk_cbk_seg(ppkt_ctxt->seg, chan, result);
			mempool_free(ppkt_ctxt->seg,
				     chan->transactions_pool);
		}
	}
}

static void ivk_cbk_for_pending(struct ps_pcie_dma_chan *chan)
{
	int i;
	struct PACKET_TRANSFER_PARAMS *ppkt_ctxt;
	struct ps_pcie_tx_segment *seg, *seg_nxt;
	struct ps_pcie_intr_segment *intr_seg, *intr_seg_next;
	struct ps_pcie_transfer_elements *ele, *ele_nxt;

	if (chan->ppkt_ctx_srcq) {
		if (chan->idx_ctx_srcq_tail != chan->idx_ctx_srcq_head) {
			i = chan->idx_ctx_srcq_tail;
			while (i != chan->idx_ctx_srcq_head) {
				ppkt_ctxt = chan->ppkt_ctx_srcq + i;
				ivk_cbk_ctx(ppkt_ctxt, chan,
					    DMA_TRANS_READ_FAILED);
				memset(ppkt_ctxt, 0,
				       sizeof(struct PACKET_TRANSFER_PARAMS));
				i++;
				if (i == chan->total_descriptors)
					i = 0;
			}
		}
	}

	if (chan->ppkt_ctx_dstq) {
		if (chan->idx_ctx_dstq_tail != chan->idx_ctx_dstq_head) {
			i = chan->idx_ctx_dstq_tail;
			while (i != chan->idx_ctx_dstq_head) {
				ppkt_ctxt = chan->ppkt_ctx_dstq + i;
				ivk_cbk_ctx(ppkt_ctxt, chan,
					    DMA_TRANS_WRITE_FAILED);
				memset(ppkt_ctxt, 0,
				       sizeof(struct PACKET_TRANSFER_PARAMS));
				i++;
				if (i == chan->total_descriptors)
					i = 0;
			}
		}
	}

	list_for_each_entry_safe(seg, seg_nxt, &chan->active_list, node) {
		ivk_cbk_seg(seg, chan, DMA_TRANS_ABORTED);
		spin_lock(&chan->active_list_lock);
		list_del(&seg->node);
		spin_unlock(&chan->active_list_lock);
		list_for_each_entry_safe(ele, ele_nxt,
					 &seg->transfer_nodes, node) {
			list_del(&ele->node);
			mempool_free(ele, chan->tx_elements_pool);
		}
		mempool_free(seg, chan->transactions_pool);
	}

	list_for_each_entry_safe(seg, seg_nxt, &chan->pending_list, node) {
		ivk_cbk_seg(seg, chan, DMA_TRANS_ABORTED);
		spin_lock(&chan->pending_list_lock);
		list_del(&seg->node);
		spin_unlock(&chan->pending_list_lock);
		list_for_each_entry_safe(ele, ele_nxt,
					 &seg->transfer_nodes, node) {
			list_del(&ele->node);
			mempool_free(ele, chan->tx_elements_pool);
		}
		mempool_free(seg, chan->transactions_pool);
	}

	list_for_each_entry_safe(intr_seg, intr_seg_next,
				 &chan->active_interrupts_list, node) {
		ivk_cbk_intr_seg(intr_seg, chan, DMA_TRANS_ABORTED);
		spin_lock(&chan->active_interrupts_lock);
		list_del(&intr_seg->node);
		spin_unlock(&chan->active_interrupts_lock);
		mempool_free(intr_seg, chan->intr_transactions_pool);
	}

	list_for_each_entry_safe(intr_seg, intr_seg_next,
				 &chan->pending_interrupts_list, node) {
		ivk_cbk_intr_seg(intr_seg, chan, DMA_TRANS_ABORTED);
		spin_lock(&chan->pending_interrupts_lock);
		list_del(&intr_seg->node);
		spin_unlock(&chan->pending_interrupts_lock);
		mempool_free(intr_seg, chan->intr_transactions_pool);
	}
}

static void xlnx_ps_pcie_reset_channel(struct ps_pcie_dma_chan *chan)
{
	xlnx_ps_pcie_channel_quiesce(chan);

	ivk_cbk_for_pending(chan);

	ps_pcie_chan_reset(chan);

	init_sw_components(chan);
	init_hw_components(chan);

	xlnx_ps_pcie_channel_activate(chan);
}

static void xlnx_ps_pcie_free_poll_timer(struct ps_pcie_dma_chan *chan)
{
	if (chan->poll_timer.function) {
		del_timer_sync(&chan->poll_timer);
		chan->poll_timer.function = NULL;
	}
}

static int xlnx_ps_pcie_alloc_poll_timer(struct ps_pcie_dma_chan *chan)
{
	timer_setup(&chan->poll_timer, poll_completed_transactions, 0);
	chan->poll_timer.expires = jiffies + chan->poll_timer_freq;

	add_timer(&chan->poll_timer);

	return 0;
}

static void terminate_transactions_work(struct work_struct *work)
{
	struct ps_pcie_dma_chan *chan =
		(struct ps_pcie_dma_chan *)container_of(work,
			struct ps_pcie_dma_chan, handle_chan_terminate);

	xlnx_ps_pcie_channel_quiesce(chan);
	ivk_cbk_for_pending(chan);
	xlnx_ps_pcie_channel_activate(chan);

	complete(&chan->chan_terminate_complete);
}

static void chan_shutdown_work(struct work_struct *work)
{
	struct ps_pcie_dma_chan *chan =
		(struct ps_pcie_dma_chan *)container_of(work,
				struct ps_pcie_dma_chan, handle_chan_shutdown);

	xlnx_ps_pcie_channel_quiesce(chan);

	complete(&chan->chan_shutdown_complt);
}

static void chan_reset_work(struct work_struct *work)
{
	struct ps_pcie_dma_chan *chan =
		(struct ps_pcie_dma_chan *)container_of(work,
				struct ps_pcie_dma_chan, handle_chan_reset);

	xlnx_ps_pcie_reset_channel(chan);
}

static void sw_intr_work(struct work_struct *work)
{
	struct ps_pcie_dma_chan *chan =
		(struct ps_pcie_dma_chan *)container_of(work,
				struct ps_pcie_dma_chan, handle_sw_intrs);
	struct ps_pcie_intr_segment *intr_seg, *intr_seg_next;

	list_for_each_entry_safe(intr_seg, intr_seg_next,
				 &chan->active_interrupts_list, node) {
		spin_lock(&chan->cookie_lock);
		dma_cookie_complete(&intr_seg->async_intr_tx);
		spin_unlock(&chan->cookie_lock);
		dmaengine_desc_get_callback_invoke(&intr_seg->async_intr_tx,
						   NULL);
		spin_lock(&chan->active_interrupts_lock);
		list_del(&intr_seg->node);
		spin_unlock(&chan->active_interrupts_lock);
	}
}

static int xlnx_ps_pcie_alloc_worker_threads(struct ps_pcie_dma_chan *chan)
{
	char wq_name[WORKQ_NAME_SIZE];

	snprintf(wq_name, WORKQ_NAME_SIZE,
		 "PS PCIe channel %d descriptor programming wq",
		 chan->channel_number);
	chan->chan_programming =
		create_singlethread_workqueue((const char *)wq_name);
	if (!chan->chan_programming) {
		dev_err(chan->dev,
			"Unable to create programming wq for chan %d",
			chan->channel_number);
		goto err_no_desc_program_wq;
	} else {
		INIT_WORK(&chan->handle_chan_programming,
			  ps_pcie_chan_program_work);
	}
	memset(wq_name, 0, WORKQ_NAME_SIZE);

	snprintf(wq_name, WORKQ_NAME_SIZE,
		 "PS PCIe channel %d primary cleanup wq", chan->channel_number);
	chan->primary_desc_cleanup =
		create_singlethread_workqueue((const char *)wq_name);
	if (!chan->primary_desc_cleanup) {
		dev_err(chan->dev,
			"Unable to create primary cleanup wq for channel %d",
			chan->channel_number);
		goto err_no_primary_clean_wq;
	} else {
		INIT_WORK(&chan->handle_primary_desc_cleanup,
			  ps_pcie_chan_primary_work);
	}
	memset(wq_name, 0, WORKQ_NAME_SIZE);

	snprintf(wq_name, WORKQ_NAME_SIZE,
		 "PS PCIe channel %d maintenance works wq",
		 chan->channel_number);
	chan->maintenance_workq =
		create_singlethread_workqueue((const char *)wq_name);
	if (!chan->maintenance_workq) {
		dev_err(chan->dev,
			"Unable to create maintenance wq for channel %d",
			chan->channel_number);
		goto err_no_maintenance_wq;
	} else {
		INIT_WORK(&chan->handle_chan_reset, chan_reset_work);
		INIT_WORK(&chan->handle_chan_shutdown, chan_shutdown_work);
		INIT_WORK(&chan->handle_chan_terminate,
			  terminate_transactions_work);
	}
	memset(wq_name, 0, WORKQ_NAME_SIZE);

	snprintf(wq_name, WORKQ_NAME_SIZE,
		 "PS PCIe channel %d software Interrupts wq",
		 chan->channel_number);
	chan->sw_intrs_wrkq =
		create_singlethread_workqueue((const char *)wq_name);
	if (!chan->sw_intrs_wrkq) {
		dev_err(chan->dev,
			"Unable to create sw interrupts wq for channel %d",
			chan->channel_number);
		goto err_no_sw_intrs_wq;
	} else {
		INIT_WORK(&chan->handle_sw_intrs, sw_intr_work);
	}
	memset(wq_name, 0, WORKQ_NAME_SIZE);

	if (chan->psrc_sgl_bd) {
		snprintf(wq_name, WORKQ_NAME_SIZE,
			 "PS PCIe channel %d srcq handling wq",
			 chan->channel_number);
		chan->srcq_desc_cleanup =
			create_singlethread_workqueue((const char *)wq_name);
		if (!chan->srcq_desc_cleanup) {
			dev_err(chan->dev,
				"Unable to create src q completion wq chan %d",
				chan->channel_number);
			goto err_no_src_q_completion_wq;
		} else {
			INIT_WORK(&chan->handle_srcq_desc_cleanup,
				  src_cleanup_work);
		}
		memset(wq_name, 0, WORKQ_NAME_SIZE);
	}

	if (chan->pdst_sgl_bd) {
		snprintf(wq_name, WORKQ_NAME_SIZE,
			 "PS PCIe channel %d dstq handling wq",
			 chan->channel_number);
		chan->dstq_desc_cleanup =
			create_singlethread_workqueue((const char *)wq_name);
		if (!chan->dstq_desc_cleanup) {
			dev_err(chan->dev,
				"Unable to create dst q completion wq chan %d",
				chan->channel_number);
			goto err_no_dst_q_completion_wq;
		} else {
			INIT_WORK(&chan->handle_dstq_desc_cleanup,
				  dst_cleanup_work);
		}
		memset(wq_name, 0, WORKQ_NAME_SIZE);
	}

	return 0;
err_no_dst_q_completion_wq:
	if (chan->srcq_desc_cleanup)
		destroy_workqueue(chan->srcq_desc_cleanup);
err_no_src_q_completion_wq:
	if (chan->sw_intrs_wrkq)
		destroy_workqueue(chan->sw_intrs_wrkq);
err_no_sw_intrs_wq:
	if (chan->maintenance_workq)
		destroy_workqueue(chan->maintenance_workq);
err_no_maintenance_wq:
	if (chan->primary_desc_cleanup)
		destroy_workqueue(chan->primary_desc_cleanup);
err_no_primary_clean_wq:
	if (chan->chan_programming)
		destroy_workqueue(chan->chan_programming);
err_no_desc_program_wq:
	return -ENOMEM;
}

static int xlnx_ps_pcie_alloc_mempool(struct ps_pcie_dma_chan *chan)
{
	chan->transactions_pool =
		mempool_create_kmalloc_pool(chan->total_descriptors,
					    sizeof(struct ps_pcie_tx_segment));

	if (!chan->transactions_pool)
		goto no_transactions_pool;

	chan->tx_elements_pool =
		mempool_create_kmalloc_pool(chan->total_descriptors,
					    sizeof(struct ps_pcie_transfer_elements));

	if (!chan->tx_elements_pool)
		goto no_tx_elements_pool;

	chan->intr_transactions_pool =
	mempool_create_kmalloc_pool(MIN_SW_INTR_TRANSACTIONS,
				    sizeof(struct ps_pcie_intr_segment));

	if (!chan->intr_transactions_pool)
		goto no_intr_transactions_pool;

	return 0;

no_intr_transactions_pool:
	mempool_destroy(chan->tx_elements_pool);
no_tx_elements_pool:
	mempool_destroy(chan->transactions_pool);
no_transactions_pool:
	return -ENOMEM;
}

static int xlnx_ps_pcie_alloc_pkt_contexts(struct ps_pcie_dma_chan *chan)
{
	if (chan->psrc_sgl_bd) {
		chan->ppkt_ctx_srcq =
			kcalloc(chan->total_descriptors,
				sizeof(struct PACKET_TRANSFER_PARAMS),
				GFP_KERNEL);
		if (!chan->ppkt_ctx_srcq) {
			dev_err(chan->dev,
				"Src pkt cxt allocation for chan %d failed\n",
				chan->channel_number);
			goto err_no_src_pkt_ctx;
		}
	}

	if (chan->pdst_sgl_bd) {
		chan->ppkt_ctx_dstq =
			kcalloc(chan->total_descriptors,
				sizeof(struct PACKET_TRANSFER_PARAMS),
				GFP_KERNEL);
		if (!chan->ppkt_ctx_dstq) {
			dev_err(chan->dev,
				"Dst pkt cxt for chan %d failed\n",
				chan->channel_number);
			goto err_no_dst_pkt_ctx;
		}
	}

	return 0;

err_no_dst_pkt_ctx:
	kfree(chan->ppkt_ctx_srcq);

err_no_src_pkt_ctx:
	return -ENOMEM;
}

static int dma_alloc_descriptors_two_queues(struct ps_pcie_dma_chan *chan)
{
	size_t size;

	void *sgl_base;
	void *sta_base;
	dma_addr_t phy_addr_sglbase;
	dma_addr_t phy_addr_stabase;

	size = chan->total_descriptors *
		sizeof(struct SOURCE_DMA_DESCRIPTOR);

	sgl_base = dma_zalloc_coherent(chan->dev, size, &phy_addr_sglbase,
				       GFP_KERNEL);

	if (!sgl_base) {
		dev_err(chan->dev,
			"Sgl bds in two channel mode for chan %d failed\n",
			chan->channel_number);
		goto err_no_sgl_bds;
	}

	size = chan->total_descriptors * sizeof(struct STATUS_DMA_DESCRIPTOR);
	sta_base = dma_zalloc_coherent(chan->dev, size, &phy_addr_stabase,
				       GFP_KERNEL);

	if (!sta_base) {
		dev_err(chan->dev,
			"Sta bds in two channel mode for chan %d failed\n",
			chan->channel_number);
		goto err_no_sta_bds;
	}

	if (chan->direction == DMA_TO_DEVICE) {
		chan->psrc_sgl_bd = sgl_base;
		chan->src_sgl_bd_pa = phy_addr_sglbase;

		chan->psrc_sta_bd = sta_base;
		chan->src_sta_bd_pa = phy_addr_stabase;

		chan->pdst_sgl_bd = NULL;
		chan->dst_sgl_bd_pa = 0;

		chan->pdst_sta_bd = NULL;
		chan->dst_sta_bd_pa = 0;

	} else if (chan->direction == DMA_FROM_DEVICE) {
		chan->psrc_sgl_bd = NULL;
		chan->src_sgl_bd_pa = 0;

		chan->psrc_sta_bd = NULL;
		chan->src_sta_bd_pa = 0;

		chan->pdst_sgl_bd = sgl_base;
		chan->dst_sgl_bd_pa = phy_addr_sglbase;

		chan->pdst_sta_bd = sta_base;
		chan->dst_sta_bd_pa = phy_addr_stabase;

	} else {
		dev_err(chan->dev,
			"%d %s() Unsupported channel direction\n",
			__LINE__, __func__);
		goto unsupported_channel_direction;
	}

	return 0;

unsupported_channel_direction:
	size = chan->total_descriptors *
		sizeof(struct STATUS_DMA_DESCRIPTOR);
	dma_free_coherent(chan->dev, size, sta_base, phy_addr_stabase);
err_no_sta_bds:
	size = chan->total_descriptors *
		sizeof(struct SOURCE_DMA_DESCRIPTOR);
	dma_free_coherent(chan->dev, size, sgl_base, phy_addr_sglbase);
err_no_sgl_bds:

	return -ENOMEM;
}

static int dma_alloc_decriptors_all_queues(struct ps_pcie_dma_chan *chan)
{
	size_t size;

	size = chan->total_descriptors *
		sizeof(struct SOURCE_DMA_DESCRIPTOR);
	chan->psrc_sgl_bd =
		dma_zalloc_coherent(chan->dev, size, &chan->src_sgl_bd_pa,
				    GFP_KERNEL);

	if (!chan->psrc_sgl_bd) {
		dev_err(chan->dev,
			"Alloc fail src q buffer descriptors for chan %d\n",
			chan->channel_number);
		goto err_no_src_sgl_descriptors;
	}

	size = chan->total_descriptors * sizeof(struct DEST_DMA_DESCRIPTOR);
	chan->pdst_sgl_bd =
		dma_zalloc_coherent(chan->dev, size, &chan->dst_sgl_bd_pa,
				    GFP_KERNEL);

	if (!chan->pdst_sgl_bd) {
		dev_err(chan->dev,
			"Alloc fail dst q buffer descriptors for chan %d\n",
			chan->channel_number);
		goto err_no_dst_sgl_descriptors;
	}

	size = chan->total_descriptors * sizeof(struct STATUS_DMA_DESCRIPTOR);
	chan->psrc_sta_bd =
		dma_zalloc_coherent(chan->dev, size, &chan->src_sta_bd_pa,
				    GFP_KERNEL);

	if (!chan->psrc_sta_bd) {
		dev_err(chan->dev,
			"Unable to allocate src q status bds for chan %d\n",
			chan->channel_number);
		goto err_no_src_sta_descriptors;
	}

	chan->pdst_sta_bd =
		dma_zalloc_coherent(chan->dev, size, &chan->dst_sta_bd_pa,
				    GFP_KERNEL);

	if (!chan->pdst_sta_bd) {
		dev_err(chan->dev,
			"Unable to allocate Dst q status bds for chan %d\n",
			chan->channel_number);
		goto err_no_dst_sta_descriptors;
	}

	return 0;

err_no_dst_sta_descriptors:
	size = chan->total_descriptors *
		sizeof(struct STATUS_DMA_DESCRIPTOR);
	dma_free_coherent(chan->dev, size, chan->psrc_sta_bd,
			  chan->src_sta_bd_pa);
err_no_src_sta_descriptors:
	size = chan->total_descriptors *
		sizeof(struct DEST_DMA_DESCRIPTOR);
	dma_free_coherent(chan->dev, size, chan->pdst_sgl_bd,
			  chan->dst_sgl_bd_pa);
err_no_dst_sgl_descriptors:
	size = chan->total_descriptors *
		sizeof(struct SOURCE_DMA_DESCRIPTOR);
	dma_free_coherent(chan->dev, size, chan->psrc_sgl_bd,
			  chan->src_sgl_bd_pa);

err_no_src_sgl_descriptors:
	return -ENOMEM;
}

static void xlnx_ps_pcie_dma_free_chan_resources(struct dma_chan *dchan)
{
	struct ps_pcie_dma_chan *chan;

	if (!dchan)
		return;

	chan = to_xilinx_chan(dchan);

	if (chan->state == CHANNEL_RESOURCE_UNALLOCATED)
		return;

	if (chan->maintenance_workq) {
		if (completion_done(&chan->chan_shutdown_complt))
			reinit_completion(&chan->chan_shutdown_complt);
		queue_work(chan->maintenance_workq,
			   &chan->handle_chan_shutdown);
		wait_for_completion_interruptible(&chan->chan_shutdown_complt);

		xlnx_ps_pcie_free_worker_queues(chan);
		xlnx_ps_pcie_free_pkt_ctxts(chan);
		xlnx_ps_pcie_destroy_mempool(chan);
		xlnx_ps_pcie_free_descriptors(chan);

		spin_lock(&chan->channel_lock);
		chan->state = CHANNEL_RESOURCE_UNALLOCATED;
		spin_unlock(&chan->channel_lock);
	}
}

static int xlnx_ps_pcie_dma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct ps_pcie_dma_chan *chan;

	if (!dchan)
		return PTR_ERR(dchan);

	chan = to_xilinx_chan(dchan);

	if (chan->state != CHANNEL_RESOURCE_UNALLOCATED)
		return 0;

	if (chan->num_queues == DEFAULT_DMA_QUEUES) {
		if (dma_alloc_decriptors_all_queues(chan) != 0) {
			dev_err(chan->dev,
				"Alloc fail bds for channel %d\n",
				chan->channel_number);
			goto err_no_descriptors;
		}
	} else if (chan->num_queues == TWO_DMA_QUEUES) {
		if (dma_alloc_descriptors_two_queues(chan) != 0) {
			dev_err(chan->dev,
				"Alloc fail bds for two queues of channel %d\n",
			chan->channel_number);
			goto err_no_descriptors;
		}
	}

	if (xlnx_ps_pcie_alloc_mempool(chan) != 0) {
		dev_err(chan->dev,
			"Unable to allocate memory pool for channel %d\n",
			chan->channel_number);
		goto err_no_mempools;
	}

	if (xlnx_ps_pcie_alloc_pkt_contexts(chan) != 0) {
		dev_err(chan->dev,
			"Unable to allocate packet contexts for channel %d\n",
			chan->channel_number);
		goto err_no_pkt_ctxts;
	}

	if (xlnx_ps_pcie_alloc_worker_threads(chan) != 0) {
		dev_err(chan->dev,
			"Unable to allocate worker queues for channel %d\n",
			chan->channel_number);
		goto err_no_worker_queues;
	}

	xlnx_ps_pcie_reset_channel(chan);

	dma_cookie_init(dchan);

	return 0;

err_no_worker_queues:
	xlnx_ps_pcie_free_pkt_ctxts(chan);
err_no_pkt_ctxts:
	xlnx_ps_pcie_destroy_mempool(chan);
err_no_mempools:
	xlnx_ps_pcie_free_descriptors(chan);
err_no_descriptors:
	return -ENOMEM;
}

static dma_cookie_t xilinx_intr_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct ps_pcie_intr_segment *intr_seg =
		to_ps_pcie_dma_tx_intr_descriptor(tx);
	struct ps_pcie_dma_chan *chan = to_xilinx_chan(tx->chan);
	dma_cookie_t cookie;

	if (chan->state != CHANNEL_AVAILABLE)
		return -EINVAL;

	spin_lock(&chan->cookie_lock);
	cookie = dma_cookie_assign(tx);
	spin_unlock(&chan->cookie_lock);

	spin_lock(&chan->pending_interrupts_lock);
	list_add_tail(&intr_seg->node, &chan->pending_interrupts_list);
	spin_unlock(&chan->pending_interrupts_lock);

	return cookie;
}

static dma_cookie_t xilinx_dma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct ps_pcie_tx_segment *seg = to_ps_pcie_dma_tx_descriptor(tx);
	struct ps_pcie_dma_chan *chan = to_xilinx_chan(tx->chan);
	dma_cookie_t cookie;

	if (chan->state != CHANNEL_AVAILABLE)
		return -EINVAL;

	spin_lock(&chan->cookie_lock);
	cookie = dma_cookie_assign(tx);
	spin_unlock(&chan->cookie_lock);

	spin_lock(&chan->pending_list_lock);
	list_add_tail(&seg->node, &chan->pending_list);
	spin_unlock(&chan->pending_list_lock);

	return cookie;
}

/**
 * xlnx_ps_pcie_dma_prep_memcpy - prepare descriptors for a memcpy transaction
 * @channel: DMA channel
 * @dma_dst: destination address
 * @dma_src: source address
 * @len: transfer length
 * @flags: transfer ack flags
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *
xlnx_ps_pcie_dma_prep_memcpy(struct dma_chan *channel, dma_addr_t dma_dst,
			     dma_addr_t dma_src, size_t len,
			     unsigned long flags)
{
	struct ps_pcie_dma_chan *chan = to_xilinx_chan(channel);
	struct ps_pcie_tx_segment *seg = NULL;
	struct ps_pcie_transfer_elements *ele = NULL;
	struct ps_pcie_transfer_elements *ele_nxt = NULL;
	u32 i;

	if (chan->state != CHANNEL_AVAILABLE)
		return NULL;

	if (chan->num_queues != DEFAULT_DMA_QUEUES) {
		dev_err(chan->dev, "Only prep_slave_sg for channel %d\n",
			chan->channel_number);
		return NULL;
	}

	seg = mempool_alloc(chan->transactions_pool, GFP_ATOMIC);
	if (!seg) {
		dev_err(chan->dev, "Tx segment alloc for channel %d\n",
			chan->channel_number);
		return NULL;
	}

	memset(seg, 0, sizeof(*seg));
	INIT_LIST_HEAD(&seg->transfer_nodes);

	for (i = 0; i < len / MAX_TRANSFER_LENGTH; i++) {
		ele = mempool_alloc(chan->tx_elements_pool, GFP_ATOMIC);
		if (!ele) {
			dev_err(chan->dev, "Tx element %d for channel %d\n",
				i, chan->channel_number);
			goto err_elements_prep_memcpy;
		}
		ele->src_pa = dma_src + (i * MAX_TRANSFER_LENGTH);
		ele->dst_pa = dma_dst + (i * MAX_TRANSFER_LENGTH);
		ele->transfer_bytes = MAX_TRANSFER_LENGTH;
		list_add_tail(&ele->node, &seg->transfer_nodes);
		seg->src_elements++;
		seg->dst_elements++;
		seg->total_transfer_bytes += ele->transfer_bytes;
		ele = NULL;
	}

	if (len % MAX_TRANSFER_LENGTH) {
		ele = mempool_alloc(chan->tx_elements_pool, GFP_ATOMIC);
		if (!ele) {
			dev_err(chan->dev, "Tx element %d for channel %d\n",
				i, chan->channel_number);
			goto err_elements_prep_memcpy;
		}
		ele->src_pa = dma_src + (i * MAX_TRANSFER_LENGTH);
		ele->dst_pa = dma_dst + (i * MAX_TRANSFER_LENGTH);
		ele->transfer_bytes = len % MAX_TRANSFER_LENGTH;
		list_add_tail(&ele->node, &seg->transfer_nodes);
		seg->src_elements++;
		seg->dst_elements++;
		seg->total_transfer_bytes += ele->transfer_bytes;
	}

	if (seg->src_elements > chan->total_descriptors) {
		dev_err(chan->dev, "Insufficient descriptors in channel %d for dma transaction\n",
			chan->channel_number);
		goto err_elements_prep_memcpy;
	}

	dma_async_tx_descriptor_init(&seg->async_tx, &chan->common);
	seg->async_tx.flags = flags;
	async_tx_ack(&seg->async_tx);
	seg->async_tx.tx_submit = xilinx_dma_tx_submit;

	return &seg->async_tx;

err_elements_prep_memcpy:
	list_for_each_entry_safe(ele, ele_nxt, &seg->transfer_nodes, node) {
		list_del(&ele->node);
		mempool_free(ele, chan->tx_elements_pool);
	}
	mempool_free(seg, chan->transactions_pool);
	return NULL;
}

static struct dma_async_tx_descriptor *xlnx_ps_pcie_dma_prep_slave_sg(
		struct dma_chan *channel, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct ps_pcie_dma_chan *chan = to_xilinx_chan(channel);
	struct ps_pcie_tx_segment *seg = NULL;
	struct scatterlist *sgl_ptr;
	struct ps_pcie_transfer_elements *ele = NULL;
	struct ps_pcie_transfer_elements *ele_nxt = NULL;
	u32 i, j;

	if (chan->state != CHANNEL_AVAILABLE)
		return NULL;

	if (!(is_slave_direction(direction)))
		return NULL;

	if (!sgl || sg_len == 0)
		return NULL;

	if (chan->num_queues != TWO_DMA_QUEUES) {
		dev_err(chan->dev, "Only prep_dma_memcpy is supported channel %d\n",
			chan->channel_number);
		return NULL;
	}

	seg = mempool_alloc(chan->transactions_pool, GFP_ATOMIC);
	if (!seg) {
		dev_err(chan->dev, "Unable to allocate tx segment channel %d\n",
			chan->channel_number);
		return NULL;
	}

	memset(seg, 0, sizeof(*seg));

	for_each_sg(sgl, sgl_ptr, sg_len, j) {
		for (i = 0; i < sg_dma_len(sgl_ptr) / MAX_TRANSFER_LENGTH; i++) {
			ele = mempool_alloc(chan->tx_elements_pool, GFP_ATOMIC);
			if (!ele) {
				dev_err(chan->dev, "Tx element %d for channel %d\n",
					i, chan->channel_number);
				goto err_elements_prep_slave_sg;
			}
			if (chan->direction == DMA_TO_DEVICE) {
				ele->src_pa = sg_dma_address(sgl_ptr) +
						(i * MAX_TRANSFER_LENGTH);
				seg->src_elements++;
			} else {
				ele->dst_pa = sg_dma_address(sgl_ptr) +
						(i * MAX_TRANSFER_LENGTH);
				seg->dst_elements++;
			}
			ele->transfer_bytes = MAX_TRANSFER_LENGTH;
			list_add_tail(&ele->node, &seg->transfer_nodes);
			seg->total_transfer_bytes += ele->transfer_bytes;
			ele = NULL;
		}
		if (sg_dma_len(sgl_ptr) % MAX_TRANSFER_LENGTH) {
			ele = mempool_alloc(chan->tx_elements_pool, GFP_ATOMIC);
			if (!ele) {
				dev_err(chan->dev, "Tx element %d for channel %d\n",
					i, chan->channel_number);
				goto err_elements_prep_slave_sg;
			}
			if (chan->direction == DMA_TO_DEVICE) {
				ele->src_pa = sg_dma_address(sgl_ptr) +
						(i * MAX_TRANSFER_LENGTH);
				seg->src_elements++;
			} else {
				ele->dst_pa = sg_dma_address(sgl_ptr) +
						(i * MAX_TRANSFER_LENGTH);
				seg->dst_elements++;
			}
			ele->transfer_bytes = sg_dma_len(sgl_ptr) %
						MAX_TRANSFER_LENGTH;
			list_add_tail(&ele->node, &seg->transfer_nodes);
			seg->total_transfer_bytes += ele->transfer_bytes;
		}
	}

	if (max(seg->src_elements, seg->dst_elements) >
		chan->total_descriptors) {
		dev_err(chan->dev, "Insufficient descriptors in channel %d for dma transaction\n",
			chan->channel_number);
		goto err_elements_prep_slave_sg;
	}

	dma_async_tx_descriptor_init(&seg->async_tx, &chan->common);
	seg->async_tx.flags = flags;
	async_tx_ack(&seg->async_tx);
	seg->async_tx.tx_submit = xilinx_dma_tx_submit;

	return &seg->async_tx;

err_elements_prep_slave_sg:
	list_for_each_entry_safe(ele, ele_nxt, &seg->transfer_nodes, node) {
		list_del(&ele->node);
		mempool_free(ele, chan->tx_elements_pool);
	}
	mempool_free(seg, chan->transactions_pool);
	return NULL;
}

static void xlnx_ps_pcie_dma_issue_pending(struct dma_chan *channel)
{
	struct ps_pcie_dma_chan *chan;

	if (!channel)
		return;

	chan = to_xilinx_chan(channel);

	if (!list_empty(&chan->pending_list)) {
		spin_lock(&chan->pending_list_lock);
		spin_lock(&chan->active_list_lock);
		list_splice_tail_init(&chan->pending_list,
				      &chan->active_list);
		spin_unlock(&chan->active_list_lock);
		spin_unlock(&chan->pending_list_lock);
	}

	if (!list_empty(&chan->pending_interrupts_list)) {
		spin_lock(&chan->pending_interrupts_lock);
		spin_lock(&chan->active_interrupts_lock);
		list_splice_tail_init(&chan->pending_interrupts_list,
				      &chan->active_interrupts_list);
		spin_unlock(&chan->active_interrupts_lock);
		spin_unlock(&chan->pending_interrupts_lock);
	}

	if (chan->chan_programming)
		queue_work(chan->chan_programming,
			   &chan->handle_chan_programming);
}

static int xlnx_ps_pcie_dma_terminate_all(struct dma_chan *channel)
{
	struct ps_pcie_dma_chan *chan;

	if (!channel)
		return PTR_ERR(channel);

	chan = to_xilinx_chan(channel);

	if (chan->state != CHANNEL_AVAILABLE)
		return 1;

	if (chan->maintenance_workq) {
		if (completion_done(&chan->chan_terminate_complete))
			reinit_completion(&chan->chan_terminate_complete);
		queue_work(chan->maintenance_workq,
			   &chan->handle_chan_terminate);
		wait_for_completion_interruptible(
			   &chan->chan_terminate_complete);
	}

	return 0;
}

static struct dma_async_tx_descriptor *xlnx_ps_pcie_dma_prep_interrupt(
		struct dma_chan *channel, unsigned long flags)
{
	struct ps_pcie_dma_chan *chan;
	struct ps_pcie_intr_segment *intr_segment = NULL;

	if (!channel)
		return NULL;

	chan = to_xilinx_chan(channel);

	if (chan->state != CHANNEL_AVAILABLE)
		return NULL;

	intr_segment = mempool_alloc(chan->intr_transactions_pool, GFP_ATOMIC);

	memset(intr_segment, 0, sizeof(*intr_segment));

	dma_async_tx_descriptor_init(&intr_segment->async_intr_tx,
				     &chan->common);
	intr_segment->async_intr_tx.flags = flags;
	async_tx_ack(&intr_segment->async_intr_tx);
	intr_segment->async_intr_tx.tx_submit = xilinx_intr_tx_submit;

	return &intr_segment->async_intr_tx;
}

static int xlnx_pcie_dma_driver_probe(struct platform_device *platform_dev)
{
	int err, i;
	struct xlnx_pcie_dma_device *xdev;
	static u16 board_number;

	xdev = devm_kzalloc(&platform_dev->dev,
			    sizeof(struct xlnx_pcie_dma_device), GFP_KERNEL);

	if (!xdev)
		return -ENOMEM;

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	xdev->dma_buf_ext_addr = true;
#else
	xdev->dma_buf_ext_addr = false;
#endif

	xdev->is_rootdma = device_property_read_bool(&platform_dev->dev,
						     "rootdma");

	xdev->dev = &platform_dev->dev;
	xdev->board_number = board_number;

	err = device_property_read_u32(&platform_dev->dev, "numchannels",
				       &xdev->num_channels);
	if (err) {
		dev_err(&platform_dev->dev,
			"Unable to find numchannels property\n");
		goto platform_driver_probe_return;
	}

	if (xdev->num_channels == 0 || xdev->num_channels >
		MAX_ALLOWED_CHANNELS_IN_HW) {
		dev_warn(&platform_dev->dev,
			 "Invalid xlnx-num_channels property value\n");
		xdev->num_channels = MAX_ALLOWED_CHANNELS_IN_HW;
	}

	xdev->channels =
	(struct ps_pcie_dma_chan *)devm_kzalloc(&platform_dev->dev,
						sizeof(struct ps_pcie_dma_chan)
							* xdev->num_channels,
						GFP_KERNEL);
	if (!xdev->channels) {
		err = -ENOMEM;
		goto platform_driver_probe_return;
	}

	if (xdev->is_rootdma)
		err = read_rootdma_config(platform_dev, xdev);
	else
		err = read_epdma_config(platform_dev, xdev);

	if (err) {
		dev_err(&platform_dev->dev,
			"Unable to initialize dma configuration\n");
		goto platform_driver_probe_return;
	}

	/* Initialize the DMA engine */
	INIT_LIST_HEAD(&xdev->common.channels);

	dma_cap_set(DMA_SLAVE, xdev->common.cap_mask);
	dma_cap_set(DMA_PRIVATE, xdev->common.cap_mask);
	dma_cap_set(DMA_INTERRUPT, xdev->common.cap_mask);
	dma_cap_set(DMA_MEMCPY, xdev->common.cap_mask);

	xdev->common.src_addr_widths = DMA_SLAVE_BUSWIDTH_UNDEFINED;
	xdev->common.dst_addr_widths = DMA_SLAVE_BUSWIDTH_UNDEFINED;
	xdev->common.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	xdev->common.device_alloc_chan_resources =
		xlnx_ps_pcie_dma_alloc_chan_resources;
	xdev->common.device_free_chan_resources =
		xlnx_ps_pcie_dma_free_chan_resources;
	xdev->common.device_terminate_all = xlnx_ps_pcie_dma_terminate_all;
	xdev->common.device_tx_status =  dma_cookie_status;
	xdev->common.device_issue_pending = xlnx_ps_pcie_dma_issue_pending;
	xdev->common.device_prep_dma_interrupt =
		xlnx_ps_pcie_dma_prep_interrupt;
	xdev->common.device_prep_dma_memcpy = xlnx_ps_pcie_dma_prep_memcpy;
	xdev->common.device_prep_slave_sg = xlnx_ps_pcie_dma_prep_slave_sg;
	xdev->common.residue_granularity = DMA_RESIDUE_GRANULARITY_SEGMENT;

	for (i = 0; i < xdev->num_channels; i++) {
		err = probe_channel_properties(platform_dev, xdev, i);

		if (err != 0) {
			dev_err(xdev->dev,
				"Unable to read channel properties\n");
			goto platform_driver_probe_return;
		}
	}

	if (xdev->is_rootdma)
		err = platform_irq_setup(xdev);
	else
		err = irq_setup(xdev);
	if (err) {
		dev_err(xdev->dev, "Cannot request irq lines for device %d\n",
			xdev->board_number);
		goto platform_driver_probe_return;
	}

	err = dma_async_device_register(&xdev->common);
	if (err) {
		dev_err(xdev->dev,
			"Unable to register board %d with dma framework\n",
			xdev->board_number);
		goto platform_driver_probe_return;
	}

	platform_set_drvdata(platform_dev, xdev);

	board_number++;

	dev_info(&platform_dev->dev, "PS PCIe Platform driver probed\n");
	return 0;

platform_driver_probe_return:
	return err;
}

static int xlnx_pcie_dma_driver_remove(struct platform_device *platform_dev)
{
	struct xlnx_pcie_dma_device *xdev =
		platform_get_drvdata(platform_dev);
	int i;

	for (i = 0; i < xdev->num_channels; i++)
		xlnx_ps_pcie_dma_free_chan_resources(&xdev->channels[i].common);

	dma_async_device_unregister(&xdev->common);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id xlnx_pcie_root_dma_of_ids[] = {
	{ .compatible = "xlnx,ps_pcie_dma-1.00.a", },
	{}
};
MODULE_DEVICE_TABLE(of, xlnx_pcie_root_dma_of_ids);
#endif

static struct platform_driver xlnx_pcie_dma_driver = {
	.driver = {
		.name = XLNX_PLATFORM_DRIVER_NAME,
		.of_match_table = of_match_ptr(xlnx_pcie_root_dma_of_ids),
		.owner = THIS_MODULE,
	},
	.probe =  xlnx_pcie_dma_driver_probe,
	.remove = xlnx_pcie_dma_driver_remove,
};

int dma_platform_driver_register(void)
{
	return platform_driver_register(&xlnx_pcie_dma_driver);
}

void dma_platform_driver_unregister(void)
{
	platform_driver_unregister(&xlnx_pcie_dma_driver);
}
