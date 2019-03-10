/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx FPGA Xilinx RDMA NIC driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 */
struct mr {
	phys_addr_t paddr;
	u64 vaddr;
	int len;
	unsigned int access;
	struct ernic_pd *pd;
	int lkey;
	int rkey;
	struct list_head list;
};

struct ernic_pd {
	atomic_t id;
};

struct ernic_mtt {
	unsigned long pd;
#define	ERNIC_PD_OFFSET		0
	u64 iova;
#define	ERNIC_IOVA_OFFSET	4
	u64 pa;
#define	ERNIC_PA_OFFSET		12
	int rkey;
#define	ERNIC_RKEY_OFFSET	20
	int len;
#define	ERNIC_LEN_OFFSET	24
	unsigned int access;
#define	ERNIC_ACCESS_OFFSET	28
};

phys_addr_t alloc_mem(struct ernic_pd *pd, int len);
void free_mem(phys_addr_t paddr);
struct mr *query_mr(struct ernic_pd *pd);
struct ernic_pd *alloc_pd(void);
void dealloc_pd(struct ernic_pd *pd);
void dump_free_list(void);
void dump_alloc_list(void);
int init_mr(phys_addr_t addr, int len);
int free_pool_insert(struct mr *chunk);
void dereg_mr(struct mr *mr);
u64 get_virt_addr(phys_addr_t phys_addr);
struct mr *reg_phys_mr(struct ernic_pd *pd, phys_addr_t phys_addr,
		       int len, int access, void *va_reg_base);
int alloc_pool_remove(struct mr *chunk);

extern void __iomem *mtt_va;
/* TODO: Get the Base address and Length from DTS, instead of Macro.
 * Currently, the design is only for Microblaze with a fixed memory
 * in the design.
 *
 * MEMORY_REGION_BASE is a carve-out memory which will be ioremapped
 * when required for ERNIC Configuration and Queue Pairs.
 */
#define	MEMORY_REGION_BASE	0xC4000000
#define	MEMORY_REGION_LEN	0x3BFFFFFF
/* TODO: Get MTT_BASE from DTS instead of Macro. */
#define	MTT_BASE		0x84000000
#define	MR_ACCESS_READ		0
#define	MR_ACCESS_WRITE		1
#define	MR_ACCESS_RDWR		2
#define	MR_ACCESS_RESVD		3
