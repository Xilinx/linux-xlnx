/*
 * (C) Copyright 2004, Psyent Corporation <www.psyent.com>
 * Scott McNutt <smcnutt@psyent.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 * 
 *
 * Modified by Jai Dhar, FPS-Tech <contact@fps-tech.net>
 * Modifications made to work with uClinux, linux 2.6.11 kernel
 *
 * TODO: - Add timeouts to while loops; kernel will stall if somethings
 *		goes bad otherwise
 *		 - Possibly add verifying to writes?
 *		 - LED Displays
 *
 */

#include <asm/io.h>
#include "epcs.h"



/*-----------------------------------------------------------------------*/
/* Operation codes for serial configuration devices
 */
#define EPCS_WRITE_ENA		0x06	/* Write enable */
#define EPCS_WRITE_DIS		0x04	/* Write disable */
#define EPCS_READ_STAT		0x05	/* Read status */
#define EPCS_READ_BYTES		0x03	/* Read bytes */
#define EPCS_READ_ID		0xab	/* Read silicon id */
#define EPCS_WRITE_STAT		0x01	/* Write status */
#define EPCS_WRITE_BYTES	0x02	/* Write bytes */
#define EPCS_ERASE_BULK		0xc7	/* Erase entire device */
#define EPCS_ERASE_SECT		0xd8	/* Erase sector */

/* Device status register bits
 */
#define EPCS_STATUS_WIP		(1<<0)	/* Write in progress */
#define EPCS_STATUS_WEL		(1<<1)	/* Write enable latch */


static nios_spi_t *epcs;

void epcs_print_regs(void)
{
	printk(KERN_NOTICE "Printing EPCS Registers\n");
	printk(KERN_NOTICE "rxdata: 0x%X, 0x%X\n",(u_int) &epcs->rxdata,(u_int) readl(&epcs->rxdata));
	printk(KERN_NOTICE "txdata: 0x%X, 0x%X\n",(u_int) &epcs->txdata,(u_int) readl(&epcs->txdata));
	printk(KERN_NOTICE "status: 0x%X, 0x%X\n",(u_int) &epcs->status,(u_int) readl(&epcs->status));
	printk(KERN_NOTICE "control: 0x%X, 0x%X\n",(u_int) &epcs->control,(u_int) readl(&epcs->control));
	printk(KERN_NOTICE "reserved: 0x%X, 0x%X\n",(u_int) &epcs->reserved,(u_int) readl(&epcs->reserved));
	printk(KERN_NOTICE "slaveselect: 0x%X, 0x%X\n",(u_int) &epcs->slaveselect,(u_int) readl(&epcs->slaveselect));

}

/***********************************************************************
 * Device access
 ***********************************************************************/
static int epcs_cs (int assert)
{
	u_int tmp;

	if (assert) {

		#if EPCS_DEBUG3
		printk(KERN_NOTICE "epcs_cs: Asserting CS\n");
		#endif

		writel (NIOS_SPI_SSO, &epcs->control);
	} else {

		#if EPCS_DEBUG3
		printk(KERN_NOTICE "epcs_cs: De-asserting CS\n");
		#endif

		/* Let all bits shift out */
		while ((readl (&epcs->status) & NIOS_SPI_TMT) == 0);

		tmp = readl (&epcs->control);
		writel (0,&epcs->control);
	}

	return 0;


			
}

static int epcs_tx (unsigned char c)
{
	u_int status;

	status = (u_int)readl(&epcs->status);

	#if EPCS_DEBUG3
	printk(KERN_NOTICE "epcs_tx: 0x%X, 0x%X, 0x%X\n",(u_int) &epcs->status,status,NIOS_SPI_TRDY);
	#endif
	
	//start = get_timer (0);
	while ((status & NIOS_SPI_TRDY) == 0)
	{
		status = (u_int)readl(&epcs->status);
	}

		/*if (get_timer (start) > EPCS_TIMEOUT)
			return (-1);*/
	writel (c, &epcs->txdata);
	return (0);
}

static int epcs_rx (void)
{
	u_int status;

	status = (u_int)readl(&epcs->status);

	#if EPCS_DEBUG3
	printk(KERN_NOTICE "epcs_rx: 0x%X, 0x%X, 0x%X\n",(u_int) &epcs->status,status,NIOS_SPI_RRDY);
	#endif

	while ((status & NIOS_SPI_RRDY) == 0)
	{
		status = (u_int)readl(&epcs->status);
	}
	return (readl (&epcs->rxdata));
}

#if 0
static unsigned char bitrev[] = {
	0x00, 0x08, 0x04, 0x0c, 0x02, 0x0a, 0x06, 0x0e,
	0x01, 0x09, 0x05, 0x0d, 0x03, 0x0b, 0x07, 0x0f
};
#endif

#if 0
static unsigned char epcs_bitrev (unsigned char c)
{
	unsigned char val;

	val  = bitrev[c>>4];
	val |= bitrev[c & 0x0f]<<4;
	return (val);
}
#endif
static void epcs_rcv (unsigned char *dst, int len)
{
	while (len--) {
		epcs_tx (0);
		*dst++ = epcs_rx ();
	}
}

#if 0
static void epcs_rrcv (unsigned char *dst, int len)
{
	while (len--) {
		epcs_tx (0);
		*dst++ = epcs_bitrev (epcs_rx ());
	}
}
#endif

static void epcs_snd (const unsigned char *src, int len)
{
	while (len--) {
		epcs_tx (*src++);
		epcs_rx ();
	}
}

#if 0
static void epcs_rsnd (unsigned char *src, int len)
{
	while (len--) {
		epcs_tx (epcs_bitrev (*src++));
		epcs_rx ();
	}
}

#endif

static void epcs_wr_enable (void)
{
	epcs_cs (1);
	epcs_tx (EPCS_WRITE_ENA);
	epcs_rx ();
	epcs_cs (0);
}

static unsigned char epcs_status_rd (void)
{
	unsigned char status;

	epcs_cs (1);
	epcs_tx (EPCS_READ_STAT);
	epcs_rx ();
	epcs_tx (0);
	status = epcs_rx ();
	epcs_cs (0);
	return (status);
}

#if 0
static void epcs_status_wr (unsigned char status)
{
	epcs_wr_enable ();
	epcs_cs (1);
	epcs_tx (EPCS_WRITE_STAT);
	epcs_rx ();
	epcs_tx (status);
	epcs_rx ();
	epcs_cs (0);
	return;
}
#endif

/***********************************************************************
 * Device information
 ***********************************************************************/

int epcs_reset (void)
{
	/* When booting from an epcs controller, the epcs bootrom
	 * code may leave the slave select in an asserted state.
	 * This causes two problems: (1) The initial epcs access
	 * will fail -- not a big deal, and (2) a software reset
	 * will cause the bootrom code to hang since it does not
	 * ensure the select is negated prior to first access -- a
	 * big deal. Here we just negate chip select and everything
	 * gets better :-)
	 */

	/* Assign base address */
  epcs = (nios_spi_t *) ((na_epcs_controller | 0x200) | 0x80000000);
	/* Clear status and control registers */

#if 1
	writel(0,&epcs->status);
	writel(0,&epcs->control);
	writel(1,&epcs->slaveselect);
#endif
	
	epcs_cs (0); /* Negate chip select */


	return (0);
}

u_char epcs_dev_find (void)
{
	unsigned char buf[4];
	unsigned char id;

	#if EPCS_DEBUG2
	printk(KERN_NOTICE "epcs_dev_find()\n");
	#endif

	/* Read silicon id requires 3 "dummy bytes" before it's put
	 * on the wire.
	 */
	buf[0] = EPCS_READ_ID;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = 0;

	epcs_cs (1);

	epcs_snd (buf,4);
	epcs_rcv (buf,1);
	if (epcs_cs (0) == -1)
		return (0);
	id = buf[0];


	#if EPCS_DEBUG1
	printk(KERN_NOTICE "epcs_dev_find: Device ID: 0x%X\n",id);
	#endif

	return id;

#if 0

	/* Find the info struct */
	i = 0;
	while (devinfo[i].name) {
		if (id == devinfo[i].id) {
			dev = &devinfo[i];
			break;
		}
		i++;
	}

	return (dev);
#endif
}

/***********************************************************************
 * Misc Utilities
 ***********************************************************************/

#if 1
u_int epcs_buf_erase (u_int off, u_int len, u_int sz)
{
	u_char buf[4];

	/* Erase the requested sectors. An address is required
	 * that lies within the requested sector -- we'll just
	 * use the first address in the sector.
	 */

	#if EPCS_DEBUG2
	/* Check if address is a sector */
	printk(KERN_NOTICE "epcs_erase(): off: 0x%X, len: 0x%X, sz: 0x%X\n",off,len,sz);
	#endif

	if ((off % sz) || (len % sz))
	{
		printk(KERN_NOTICE "epcs_erase: Address is not sector-aligned, halting!\n");
		while(1);
	}

	while (len) {


		#if EPCS_DEBUG3
		printk(KERN_NOTICE "epcs_erase: Erasing 0x%X\n",off);
		#endif

		buf[0] = EPCS_ERASE_SECT;
		buf[1] = off >> 16;
		buf[2] = off >> 8;
		buf[3] = off;

		epcs_wr_enable ();
		epcs_cs (1);
		epcs_snd (buf,4);
		epcs_cs (0);

		/* Wait for erase to complete */
		while (epcs_status_rd() & EPCS_STATUS_WIP);

		len -= sz;
		off += sz;
	}
	return (0);
}
#endif

#if 1
int epcs_buf_read (u_char *dst, u_int off, u_int cnt)
{
	u_char buf[4];


	buf[0] = EPCS_READ_BYTES;
	buf[1] = off >> 16;
	buf[2] = off >> 8;
	buf[3] = off;

	epcs_cs (1);
	epcs_snd (buf,4);
	//epcs_rrcv ((u_char *)addr, cnt);
	epcs_rcv (dst, cnt);
	epcs_cs (0);

	return (0);
}

#endif

#if 1
int epcs_buf_write (const u_char *addr, u_int off, u_int cnt)
{
	u_int wrcnt;
	u_int pgsz;
	u_char buf[4];

	pgsz = EPCS_PAGESIZE;

	#if EPCS_DEBUG2
	printk(KERN_NOTICE "epcs_buf_write(): 0x%X, 0x%X\n",cnt,off);
	#endif

	while (cnt) {
		if (off % pgsz)
			wrcnt = pgsz - (off % pgsz);
		else
			wrcnt = pgsz;
		wrcnt = (wrcnt > cnt) ? cnt : wrcnt;

		buf[0] = EPCS_WRITE_BYTES;
		buf[1] = off >> 16;
		buf[2] = off >> 8;
		buf[3] = off;

		#if EPCS_DEBUG3
		printk(KERN_NOTICE "epcs_buf_write: wrcnt: 0x%X, offset: 0x%X\n",wrcnt,off);
		#endif

		epcs_wr_enable ();
		epcs_cs (1);
		epcs_snd (buf,4);
		//epcs_rsnd ((unsigned char *)addr, wrcnt);
		epcs_snd (addr, wrcnt);
		epcs_cs (0);

		/* Wait for write to complete */
		while (epcs_status_rd() & EPCS_STATUS_WIP);

		cnt -= wrcnt;
		off += wrcnt;
		addr += wrcnt;
	}

	return (0);
}

#endif

#if 0
int epcs_verify (ulong addr, ulong off, ulong cnt, ulong *err)
{
	ulong rdcnt;
	unsigned char buf[256];
	unsigned char *start,*end;
	int i;

	start = end = (unsigned char *)addr;
	while (cnt) {
		rdcnt = (cnt>sizeof(buf)) ? sizeof(buf) : cnt;
		epcs_read ((ulong)buf, off, rdcnt);
		for (i=0; i<rdcnt; i++) {
			if (*end != buf[i]) {
				*err = end - start;
				return(-1);
			}
			end++;
		}
		cnt -= rdcnt;
		off += rdcnt;
	}
	return (0);
}

static int epcs_sect_erased (int sect, unsigned *offset,
		struct epcs_devinfo_t *dev)
{
	unsigned char buf[128];
	unsigned off, end;
	unsigned sectsz;
	int i;

	sectsz = (1 << dev->sz_sect);
	off = sectsz * sect;
	end = off + sectsz;

	while (off < end) {
		epcs_read ((ulong)buf, off, sizeof(buf));
		for (i=0; i < sizeof(buf); i++) {
			if (buf[i] != 0xff) {
				*offset = off + i;
				return (0);
			}
		}
		off += sizeof(buf);
	}
	return (1);
}

#endif
