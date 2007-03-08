/*
 *  Copyright  Boris Koprinarov <crumpz@gmail.com>
 *
 *  cobra5272.c ,v 1.14 2005/08/31 13:24:14 
 *  
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#define FLASH_PHYS_ADDR 0xffe00000
#define FLASH_SIZE 0x200000

#define FLASH_PARTITION0_ADDR 0x1000000
#define FLASH_PARTITION0_SIZE 0x1000000

struct map_info flagadm_map = {
		.name =		"Flash chip on COBRA5272",
		.size =		FLASH_SIZE,
		.bankwidth =	2,
};

struct mtd_partition flagadm_parts[] = {
	{
		.name =		"boot (16K)",
		.offset	=	0x0,
		.size =		0x4000

	},
	{
		.name =		"kernel (512K)",
		.offset =	0x80000,
		.size =		0x80000
	},
        {
                .name =         "rootfs (1024K)",
                .offset =       0x100000,
                .size =         0x100000
        },
        {
                .name =         "spare (8K)",
                .offset =       0x4000,
                .size =         0x2000
        },
        {
                .name =         "spare (8K)",
                .offset =       0x6000,
                .size =         0x2000
        },
        {
                .name =         "spare (256K)",
                .offset =       0x40000,
                .size =         0x40000
        },
        {
                .name =         "complete (2048K)",
                .offset =       0x0,
                .size =         0x200000
        },
	{
                .name =         "boot J13 (256K)",
                .offset =       0x100000,
                .size =         0x40000
        },
	{
		.name =		"kernel J13 (512K)",
		.offset =	0x140000,
		.size =		0x80000
	},
	{	
		.name =		"rootfs J13 (256K)",
		.offset =	0x1c0000,
		.size =		0x40000
	}

};

#define PARTITION_COUNT (sizeof(flagadm_parts)/sizeof(struct mtd_partition))

static struct mtd_info *mymtd;

int __init init_flagadm(void)
{	
	printk(KERN_NOTICE "COBRA5272 flash device: %x at %x\n",
			FLASH_SIZE, FLASH_PHYS_ADDR);
	
	flagadm_map.phys = FLASH_PHYS_ADDR;
	flagadm_map.virt = ioremap(FLASH_PHYS_ADDR,
					FLASH_SIZE);

	if (!flagadm_map.virt) {
		printk("Failed to ioremap\n");
		return -EIO;
	}

	simple_map_init(&flagadm_map);

	mymtd = do_map_probe("cfi_probe", &flagadm_map);
	if (mymtd) {
		mymtd->owner = THIS_MODULE;
		add_mtd_partitions(mymtd, flagadm_parts, PARTITION_COUNT);
		printk(KERN_NOTICE "COBRA5272 flash device initialized\n");
		return 0;
	}

	iounmap((void *)flagadm_map.virt);
	return -ENXIO;
}

static void __exit cleanup_flagadm(void)
{
	if (mymtd) {
		del_mtd_partitions(mymtd);
		map_destroy(mymtd);
	}
	if (flagadm_map.virt) {
		iounmap((void *)flagadm_map.virt);
		flagadm_map.virt = 0;
	}
}

module_init(init_flagadm);
module_exit(cleanup_flagadm);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Boris Koprinarov <crumpz@gmail.com>");
MODULE_DESCRIPTION("MTD map driver for COBRA5272 board");
