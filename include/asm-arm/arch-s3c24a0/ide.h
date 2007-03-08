/*
 * include/asm-arm/arch-s3c24a0/ide.h
 *
 *
 * Originally based upon linux/include/asm-arm/arch-sa1100/ide.h
 *
 * Changes
 *
 * 2004/06/10 <heechul.yun@samsung.com>  SPJ CPLD IDE support
 * 2004/06/13 <heechul.yun@samsung.com>  CPLD IDE and USB csupport for SPJ
 *
 */

#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>


#ifndef MAX_HWIFS
        #define MAX_HWIFS       1
#else
        #undef MAX_HWIFS
        #define MAX_HWIFS 1
#endif

#define CPLD_IDE_DEBUG  // hcyun

/*
 * Set up a hw structure for a specified data port, control port and IRQ.
 * This should follow whatever the default interface uses.
 */
static __inline__ void
ide_init_hwif_ports(hw_regs_t *hw, int data_port, int ctrl_port, int *irq)
{
        ide_ioreg_t reg;

        memset(hw, 0, sizeof(*hw));

        reg = (ide_ioreg_t)data_port;

        /* increasing 8 */
        hw->io_ports[IDE_DATA_OFFSET] =  reg + 0;
        hw->io_ports[IDE_ERROR_OFFSET] = reg + (1 << 3);
        hw->io_ports[IDE_NSECTOR_OFFSET] = reg + (2 << 3);
        hw->io_ports[IDE_SECTOR_OFFSET] = reg + (3 << 3);
        hw->io_ports[IDE_LCYL_OFFSET] = reg + (4 << 3);
        hw->io_ports[IDE_HCYL_OFFSET] = reg + (5 << 3);
        hw->io_ports[IDE_SELECT_OFFSET] = reg + (6 << 3);
        hw->io_ports[IDE_STATUS_OFFSET] = reg + (7 << 3);

        hw->io_ports[IDE_CONTROL_OFFSET] = (ide_ioreg_t) ctrl_port;

        if (irq)
                *irq = 0;
}


/*
 * CPLD IDE reset. to reset first assert 0 and then assert 1
 */

static __inline__ void ide_set_reset(int on)
{
        volatile unsigned char *usb_reset = (unsigned char *)(SMDK_CPLD_USB_VIO+0x00800000);
        volatile unsigned char *ide_reset = (unsigned char *)(SMDK_CPLD_IDE_VIO+0x00800000);

        if ( on ) {
                /* turn CPLD to IDE mode */
                *ide_reset = 0x02;

                /* turn on IDE */
                *ide_reset = 0x03;

        } else {
                /* turn off IDE */
                *ide_reset = 0x02;

                *ide_reset = 0x02;
        }

/*
        *ide_reset = 0x1;
        *usb_reset = 0x0;
        *usb_reset = 0x1;
*/

}


/*
 * Register the standard ports for this architecture with the IDE driver.
 */
static __inline__ void
ide_init_default_hwifs(void)
{
        /*
                A7      A6      A5      A4      A3      <-- CPLD address line used

                CE2     CE1     A2      A1      A0       IORD   IOWR
                ---------------------------------
                1       0       0       0       0        data port
                ..
                ..

                0       1       1       1       0        control port

                data port = SMDK_CPLD_IDE_VIO + 0x80
                control port = SMDK_CPLD_IDE_VIO + 0x70

         */

        /* Nothing to declare... */

        int ret;

        hw_regs_t hw;

        ide_init_hwif_ports(&hw, SMDK_CPLD_IDE_VIO + 0x80, SMDK_CPLD_IDE_VIO + 0x70, NULL);

        hw.irq = SMDK_CPLD_IDE_IRQ;

        ide_register_hw(&hw, NULL);


#ifdef CPLD_IDE_DEBUG
        printk("SMDK24A0 : IDE initialize - hcyun \n");
        printk("!!FIXME!! IDE and cs8900 are controlled by SROM bank1 and need different timing and bus width\n");
#endif

        bank1_set_state(B1_IDE_PIO4);

        // ide reset
        ide_set_reset(0);

        mdelay(250);

        ide_set_reset(1);

        mdelay(500); // wait 250ms see ATA spec

        printk("riging edge interrupt\n");
        ret = set_external_irq(SMDK_CPLD_IDE_IRQ, EINT_RISING_EDGE, EINT_PULLUP_EN);

        if (ret)
                printk("ERROR: irq set failed\n");

}
