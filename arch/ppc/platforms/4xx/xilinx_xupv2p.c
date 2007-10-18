/*
 * Xilinx XUPV2P board initialization
 *
 * Author: Stephen.Neuendorffer@xilinx.com
 *
 * 2007 (c) Xilinx, Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#include <linux/io.h>
#include <linux/xilinx_devices.h>
#include <platforms/4xx/xparameters/xparameters.h>

int virtex_device_fixup(struct platform_device *dev)
{
#ifdef XPAR_ONEWIRE_0_BASEADDR
	int i;
	// Use the Silicon Serial ID attached on the onewire bus to
	// generate sensible MAC addresses.
	unsigned char *p_onewire = ioremap(XPAR_ONEWIRE_0_BASEADDR, 6);
	struct xemac_platform_data *pdata = dev->dev.platform_data;
	if (strcmp(dev->name, "xilinx_emac") == 0) {
		printk(KERN_INFO "Fixup MAC address for %s:%d\n",
		       dev->name, dev->id);
		// FIXME.. this doesn't seem to return data that is consistent
		// with the self test... why not?
		pdata->mac_addr[0] = 0x00;
		pdata->mac_addr[1] = 0x0A;
		pdata->mac_addr[2] = 0x35;
		pdata->mac_addr[3] = dev->id;
		pdata->mac_addr[4] = p_onewire[4];
		pdata->mac_addr[5] = p_onewire[5];
		pr_debug(KERN_INFO
			 "MAC address is now %2x:%2x:%2x:%2x:%2x:%2x\n",
			 pdata->mac_addr[0], pdata->mac_addr[1],
			 pdata->mac_addr[2], pdata->mac_addr[3],
			 pdata->mac_addr[4], pdata->mac_addr[5]);
	}
	iounmap(p_onewire);
#endif
	return 0;
}
