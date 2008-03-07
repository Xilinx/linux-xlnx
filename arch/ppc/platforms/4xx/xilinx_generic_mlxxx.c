/*
 * Xilinx MLxxx board initialization
 *
 * 2007 (c) Xilinx, Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#include <linux/io.h>
#include <linux/xilinx_devices.h>
#include <linux/platform_device.h>

extern bd_t __res;

int virtex_device_fixup(struct platform_device *dev)
{
	static int temac_count = 0;
	struct xlltemac_platform_data *pdata = dev->dev.platform_data;

#if defined(CONFIG_XILINX_MLxxx)

	if (strcmp(dev->name, "xilinx_lltemac") == 0) {

		/* only copy the mac address into the 1st lltemac if 
		   there are multiple */
	
		if (temac_count++ == 0) {
			printk(KERN_INFO "Fixup MAC address for %s:%d\n",
			       dev->name, dev->id);
			/* Set the MAC address from the iic eeprom info in the board data */
		        memcpy(pdata->mac_addr, ((bd_t *) &__res)->bi_enetaddr, 6);
		}
	}
#endif

	return 0;
}
