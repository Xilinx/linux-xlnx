/******************************************************************************
*                                                                             *
*                                                                             *
******************************************************************************/

#ifndef __ALTERA_AVALON_SPI_REGS_H__
#define __ALTERA_AVALON_SPI_REGS_H__

#define CONFIG_MTD_EPCS_DEBUG 0 
#define EPCS_DEBUG1 (CONFIG_MTD_EPCS_DEBUG >= 1)
#define EPCS_DEBUG2 (CONFIG_MTD_EPCS_DEBUG >= 2)
#define EPCS_DEBUG3 (CONFIG_MTD_EPCS_DEBUG >= 3)


#define EPCS_SIG_1MBIT	0x10
#define EPCS_SIZE_1MBIT ((1 << 20)/8)

#define EPCS_SIG_4MBIT	0x12
#define EPCS_SIZE_4MBIT (EPCS_SIZE_1MBIT*4)

#define EPCS_SIG_16MBIT 0x14
#define EPCS_SIZE_16MBIT (EPCS_SIZE_4MBIT*4)


#define EPCS_SIG_64MBIT 0x16
#define EPCS_SIZE_64MBIT (EPCS_SIZE_16MBIT*4)

#define EPCS_SECSIZE_64KB ((1<<10)*64)
#define EPCS_SECSIZE_32KB ((1<<10)*32)

#define EPCS_PAGESIZE 256

/*------------------------------------------------------------------------
 * SPI (http://www.altera.com/literature/ds/ds_nios_spi.pdf)
 *----------------------------------------------------------------------*/
typedef volatile struct nios_spi_t {
	u_int	rxdata;		/* Rx data reg */
	u_int	txdata;		/* Tx data reg */
	u_int	status;		/* Status reg */
	u_int	control;	/* Control reg */
	u_int	reserved;	/* (master only) */
	u_int	slaveselect;	/* SPI slave select mask (master only) */
}nios_spi_t;

/* status register */
#define NIOS_SPI_ROE		(1 << 3)	/* rx overrun */
#define NIOS_SPI_TOE		(1 << 4)	/* tx overrun */
#define NIOS_SPI_TMT		(1 << 5)	/* tx empty */
#define NIOS_SPI_TRDY		(1 << 6)	/* tx ready */
#define NIOS_SPI_RRDY		(1 << 7)	/* rx ready */
#define NIOS_SPI_E		(1 << 8)	/* exception */

/* control register */
#define NIOS_SPI_IROE		(1 << 3)	/* rx overrun int ena */
#define NIOS_SPI_ITOE		(1 << 4)	/* tx overrun int ena */
#define NIOS_SPI_ITRDY		(1 << 6)	/* tx ready int ena */
#define NIOS_SPI_IRRDY		(1 << 7)	/* rx ready int ena */
#define NIOS_SPI_IE		(1 << 8)	/* exception int ena */
#define NIOS_SPI_SSO		(1 << 10)	/* override SS_n output */

typedef struct epcs_devinfo_t {
	const char 	*name;		/* Device name */
	u_char	id;		/* Device silicon id */
	u_char	size;		/* Total size log2(bytes)*/
	u_char	num_sects;	/* Number of sectors */
	u_char	sz_sect;	/* Sector size log2(bytes) */
	u_char	sz_page;	/* Page size log2(bytes) */
	u_char   prot_mask;	/* Protection mask */
}epcs_devinfo_t;


u_char epcs_dev_find (void);
int epcs_reset (void);
int epcs_buf_read (u_char *dst, u_int off, u_int cnt);
int epcs_buf_write (const u_char *addr, u_int off, u_int cnt);
u_int epcs_buf_erase (u_int off, u_int len, u_int sz);
void epcs_print_regs(void);

#endif /* __ALTERA_AVALON_SPI_REGS_H__ */
