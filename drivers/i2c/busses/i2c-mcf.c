/*
    i2c-mcf5282.c - Part of lm_sensors, Linux kernel modules for hardware monitoring

    Copyright (c) 2005, Derek CL Cheung <derek.cheung@sympatico.ca>
					<http://www3.sympatico.ca/derek.cheung>

    Copyright (c) 2006, emlix and Freescale
			Sebastian Hess <sh@emlix.com>
			Yaroslav Vinogradov <yaroslav.vinogradov@freescale.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Changes:
    v0.1 	26 March 2005
        	Initial Release - developed on uClinux with 2.6.9 kernel

    v0.2	29 May 2006
		Modified to be more generic and added support for
		i2c_master_xfer

    This I2C adaptor supports the ColdFire 5282 CPU I2C module. Since most Coldfire
    CPUs' I2C module use the same register set (e.g., MCF5249), the code is very
    portable and re-usable to other Coldfire CPUs.

    The transmission frequency is set at about 100KHz for the 5282Lite CPU board with
    8MHz crystal. If the CPU board uses different system clock frequency, you should
    change the following line:
                static int __init i2c_coldfire_init(void)
                {
                                .........
                        // Set transmission frequency 0x15 = ~100kHz
                        *MCF_I2C_I2FDR = 0x15;
                                ........
                }

    Remember to perform a dummy read to set the ColdFire CPU's I2C module for read before
    reading the actual byte from a device

    The I2C_SM_BUS_BLOCK_DATA function are not yet ready but most lm_senors do not care

*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/types.h>
#include "i2c-mcf.h"


static struct i2c_algorithm coldfire_algorithm = {
	/*.name		= "ColdFire I2C algorithm",
	.id		= I2C_ALGO_SMBUS,*/
	.smbus_xfer	= coldfire_i2c_access,
	.master_xfer    = coldfire_i2c_master,
	.functionality	= coldfire_func,
};


static struct i2c_adapter coldfire_adapter = {
	.owner		= THIS_MODULE,
	.class          = I2C_CLASS_HWMON,
	.algo		= &coldfire_algorithm,
	.name		= "ColdFire I2C adapter",
};


__u16 lastaddr;
__u16 lastop;

static inline int coldfire_do_first_start(__u16 addr,__u16 flags)
{
	int err;
	/*
	 * Generate a stop and put the I2C module into slave mode
	 */
	*MCF_I2C_I2CR &= ~MCF_I2C_I2CR_MSTA;

	/*
	 * Generate a new Start signal
	 */
	err = coldfire_i2c_start(flags & I2C_M_RD ? I2C_SMBUS_READ : I2C_SMBUS_WRITE,
				addr, FIRST_START);
	if(err) return err;

	lastaddr = addr;
	lastop = flags & I2C_M_RD;	/* Ensure everything for new start */
	return 0;
}


/*
 *  read one byte data from the I2C bus
 */
static int coldfire_read_data(u8 * const rxData, const enum I2C_ACK_TYPE ackType) {

	int timeout;

	*MCF_I2C_I2CR &= ~MCF_I2C_I2CR_MTX;     	/* master receive mode */

	if (ackType == NACK)
		*MCF_I2C_I2CR |= MCF_I2C_I2CR_TXAK;     /* generate NA */
	else
		*MCF_I2C_I2CR &= ~MCF_I2C_I2CR_TXAK;    /* generate ACK */


	/* read data from the I2C bus */
	*rxData = *MCF_I2C_I2DR;

	/* printk(">>> %s I2DR data is %.2x \n", __FUNCTION__, *rxData); */

	/* wait for data transfer to complete */
	timeout = 500;
	while (timeout-- && !(*MCF_I2C_I2SR & MCF_I2C_I2SR_IIF))
		udelay(1);
	if (timeout <= 0)
		printk("%s - I2C IIF never set. Timeout is %d \n", __FUNCTION__, timeout);


	/* reset the interrupt bit */
	*MCF_I2C_I2SR &= ~MCF_I2C_I2SR_IIF;

	if (timeout <= 0 )
		return -1;
	else
		return 0;

};


/*
 *  write one byte data onto the I2C bus
 */
static int coldfire_write_data(const u8 txData) {

	int timeout;

	timeout = 500;

	*MCF_I2C_I2CR |= MCF_I2C_I2CR_MTX;	/* I2C module into TX mode */
	*MCF_I2C_I2DR = txData;			/* send the data */

	/* wait for data transfer to complete */
	/* rely on the interrupt handling bit */
	timeout = 500;
	while (timeout-- && !(*MCF_I2C_I2SR & MCF_I2C_I2SR_IIF))
		udelay(1);
	if (timeout <=0)
		printk("%s - I2C IIF never set. Timeout is %d \n", __FUNCTION__, timeout);


	/* reset the interrupt bit */
	*MCF_I2C_I2SR &= ~MCF_I2C_I2SR_IIF;

	if (timeout <= 0 )
		return -1;
	else
		return 0;

};




/*
 *  Generate I2C start or repeat start signal
 *  Combine the 7 bit target_address and the R/W bit and put it onto the I2C bus
 */
static int coldfire_i2c_start(const char read_write, const u16 target_address, const enum I2C_START_TYPE start_type) {

	int timeout;

	// printk(">>> %s START TYPE %s \n", __FUNCTION__, start_type == FIRST_START ? "FIRST_START" : "REPEAT_START");

	*MCF_I2C_I2CR |= MCF_I2C_I2CR_IEN;

	if (start_type == FIRST_START) {
		/* Make sure the I2C bus is idle */
		timeout = 500;		/* 500us timeout */
		while (timeout-- && (*MCF_I2C_I2SR & MCF_I2C_I2SR_IBB))
			udelay(1);
		if (timeout <= 0) {
			printk("%s - I2C bus always busy in the past 500us timeout is %d \n", __FUNCTION__, timeout);
			goto check_rc;
		}
		/* generate a START and put the I2C module into MASTER TX mode*/
		*MCF_I2C_I2CR |= (MCF_I2C_I2CR_MSTA | MCF_I2C_I2CR_MTX);

		/* wait for bus busy to be set */
		timeout = 500;
		while (timeout-- && !(*MCF_I2C_I2SR & MCF_I2C_I2SR_IBB))
			udelay(1);
		if (timeout <= 0) {
			printk("%s - I2C bus is never busy after START. Timeout is %d \n", __FUNCTION__, timeout);
			goto check_rc;
		}

	} else {
		/* this is repeat START */
		udelay(500);	/* need some delay before repeat start */
		*MCF_I2C_I2CR |= (MCF_I2C_I2CR_MSTA | MCF_I2C_I2CR_RSTA);
	}


	/* combine the R/W bit and the 7 bit target address and put it onto the I2C bus */
	*MCF_I2C_I2DR = ((target_address & 0x7F) << 1) | (read_write == I2C_SMBUS_WRITE ? 0x00 : 0x01);

	/* wait for bus transfer to complete */
	/* when one byte transfer is completed, IIF set at the faling edge of the 9th clock */
	timeout = 500;
	while (timeout-- && !(*MCF_I2C_I2SR & MCF_I2C_I2SR_IIF))
		udelay(1);
	if (timeout <= 0)
		printk("%s - I2C IIF never set. Timeout is %d \n", __FUNCTION__, timeout);


check_rc:
	/* reset the interrupt bit */
	*MCF_I2C_I2SR &= ~MCF_I2C_I2SR_IIF;

	if (timeout <= 0)
		return -1;
	else
		return 0;
};


/*
 *  5282 SMBUS supporting functions
 */

static s32 coldfire_i2c_access(struct i2c_adapter *adap, u16 addr,
			      unsigned short flags, char read_write,
			      u8 command, int size, union i2c_smbus_data *data)
{
	int i, len, rc = 0;
	u8 rxData, tempRxData[2];

	switch (size) {
		case I2C_SMBUS_QUICK:
			rc = coldfire_i2c_start(read_write, addr, FIRST_START); 	/* generate START */
                        break;
		case I2C_SMBUS_BYTE:
			rc = coldfire_i2c_start(read_write, addr, FIRST_START);
			*MCF_I2C_I2CR |= MCF_I2C_I2CR_TXAK;     	/* generate NA */
			if (read_write == I2C_SMBUS_WRITE)
				rc += coldfire_write_data(command);
			else {
				coldfire_read_data(&rxData, NACK);		/* dummy read */
				rc += coldfire_read_data(&rxData, NACK);
				data->byte = rxData;
			}
			*MCF_I2C_I2CR &= ~MCF_I2C_I2CR_TXAK; /* reset ACK bit */
			break;
		case I2C_SMBUS_BYTE_DATA:
			rc = coldfire_i2c_start(I2C_SMBUS_WRITE, addr, FIRST_START);
			rc += coldfire_write_data(command);
			if (read_write == I2C_SMBUS_WRITE)
				rc += coldfire_write_data(data->byte);
			else {
				/* This is SMBus READ Byte Data Request. Perform REPEAT START */
				rc += coldfire_i2c_start(I2C_SMBUS_READ, addr, REPEAT_START);
				coldfire_read_data(&rxData, ACK);                /* dummy read */
				/* Disable Acknowledge, generate STOP after next byte transfer */
				rc += coldfire_read_data(&rxData, NACK);
				data->byte = rxData;
			}
			*MCF_I2C_I2CR &= ~MCF_I2C_I2CR_TXAK;		/* reset to normal ACk */
			break;
		case I2C_SMBUS_PROC_CALL:
        	case I2C_SMBUS_WORD_DATA:
			dev_info(&adap->dev, "size = I2C_SMBUS_WORD_DATA \n");
			rc = coldfire_i2c_start(I2C_SMBUS_WRITE, addr, FIRST_START);
			rc += coldfire_write_data(command);
			if (read_write == I2C_SMBUS_WRITE) {
				rc += coldfire_write_data(data->word & 0x00FF);
				rc += coldfire_write_data((data->word & 0x00FF) >> 8);
			} else {
				/* This is SMBUS READ WORD request. Peform REPEAT START */
				rc += coldfire_i2c_start(I2C_SMBUS_READ, addr, REPEAT_START);
				coldfire_read_data(&rxData, ACK);        /* dummy read */
				/* Disable Acknowledge, generate STOP after next byte transfer */
				/* read the MS byte from the device */
				rc += coldfire_read_data(&rxData, NACK);
				tempRxData[1] = rxData;
				/* read the LS byte from the device */
				rc += coldfire_read_data(&rxData, NACK);
				tempRxData[0] = rxData;
				/* the host driver expect little endian convention. Swap the byte */
				data->word = (tempRxData[0] << 8) | tempRxData[1];
			}
			*MCF_I2C_I2CR &= ~MCF_I2C_I2CR_TXAK;
			break;
		case I2C_SMBUS_BLOCK_DATA:
			/* this is not ready yet!!!
			if (read_write == I2C_SMBUS_WRITE) {
				dev_info(&adap->dev, "data = %.4x\n", data->word);
				len = data->block[0];
				if (len < 0)
					len = 0;
				if (len > 32)
					len = 32;
				for (i = 1; i <= len; i++)
					dev_info(&adap->dev, "data->block[%d] = %.2x\n", i, data->block[i]);
			} */
			break;
		default:
			printk("Unsupported I2C size \n");
			rc = -1;
			break;
	};

	/* Generate a STOP and put I2C module into slave mode */
	*MCF_I2C_I2CR &= ~MCF_I2C_I2CR_MSTA;

	/* restore interrupt */
	*MCF_I2C_I2CR |= MCF_I2C_I2CR_IIEN;

	if (rc < 0)
		return -1;
	else
		return 0;
};


/*
 *  List the SMBUS functions supported by this I2C adaptor
 *  Also tell the I2C Subsystem that we are able of master_xfer()
 */
static u32 coldfire_func(struct i2c_adapter *adapter)
{
	return(I2C_FUNC_SMBUS_QUICK |
	       I2C_FUNC_SMBUS_BYTE |
	       I2C_FUNC_SMBUS_PROC_CALL |
	       I2C_FUNC_SMBUS_BYTE_DATA |
	       I2C_FUNC_SMBUS_WORD_DATA |
	       I2C_FUNC_I2C |
	       I2C_FUNC_SMBUS_BLOCK_DATA);
};

static int coldfire_i2c_master(struct i2c_adapter *adap,struct i2c_msg *msgs,
	                       int num)
{
	u8 dummyRead;
	struct i2c_msg *p;
	int i, err = 0;
	int ic=0;

	lastaddr = 0;
	lastop = 8;

	/* disable the IRQ, we are doing polling */
	*MCF_I2C_I2CR &= ~MCF_I2C_I2CR_IIEN;

	dev_dbg(&adap->dev,"Num of actions: %d\n", num);

	for (i = 0; !err && i < num; i++) {
		p = &msgs[i];


		if (!p->len)
		{
			dev_dbg(&adap->dev,"p->len == 0!\n");
			continue;
		}
		/*
		 * Generate a new Start, if the target address differs from the last target,
		 * generate a stop in this case first
		 */
		if(p->addr != lastaddr)
		{
			err = coldfire_do_first_start(p->addr,p->flags);
			if(err)
			{
				dev_dbg(&adap->dev,"First Init failed!\n");
				break;
			}
		}

		else if((p->flags & I2C_M_RD)  != lastop)
		{
			/*
			 * If the Operational Mode changed, we need to do this here ...
			 */
			dev_dbg(&adap->dev,"%s(): Direction changed, was: %d; is now: %d\n",
				__FUNCTION__,lastop,p->flags & I2C_M_RD);

			/* Last op was an read, now it's write: complete stop and reinit */
			if (lastop & I2C_M_RD)
			{
				dev_dbg(&adap->dev,"%s(): The device is in read state, we must reset!\n",
					__FUNCTION__);
				if((err = coldfire_do_first_start(p->addr,p->flags)))
					break;
			}
			else
			{
				dev_dbg(&adap->dev,"%s(): We switchted to read mode\n",__FUNCTION__);
				if((err = coldfire_i2c_start((p->flags & I2C_M_RD) ? I2C_SMBUS_READ : I2C_SMBUS_WRITE,
				          p->addr, REPEAT_START)))
					break;
			}

			lastop = p->flags & I2C_M_RD;	/* Save the last op */
		}

		if (p->flags & I2C_M_RD)
		{
			/*
			 * When ever we get here, a new session was activated, so
			 * read a dummy byte
			 */
			coldfire_read_data(&dummyRead, ACK);
			/*
			 * read p->len -1 bytes with ACK to the slave,
			 * read the last byte without the ACK, to inform him about the
			 * stop afterwards
			 */
			ic = 0;
			while(!err && (ic < p->len-1 ))
			{
				err = coldfire_read_data(p->buf+ic, ACK );
				ic++;
			}
			if(!err)
				err = coldfire_read_data(p->buf+ic, NACK);
			dev_dbg(&coldfire_adapter.dev,"read: %2x\n", p->buf[ic]);
		}
		else
		{
			if(p->len == 2)
				dev_dbg(&coldfire_adapter.dev,"writing: 0x %2x %2x\n", p->buf[0], p->buf[1]);

			/*
			 * Write data to the slave
			 */
			for(ic=0; !err && ic < p->len; ic++)
			{
				err = coldfire_write_data(p->buf[ic]);
				if(err)
				{
					dev_dbg(&coldfire_adapter.dev,"Failed to write data\n");
				}
			}
		}
	}

	/*
	 * Put the device into slave mode to enable the STOP Generation (the RTC needs this)
	 */
	*MCF_I2C_I2CR &= ~MCF_I2C_I2CR_MSTA;

	*MCF_I2C_I2CR &= ~MCF_I2C_I2CR_TXAK;	/* reset the ACK bit */

	/* restore interrupt */
	*MCF_I2C_I2CR |= MCF_I2C_I2CR_IIEN;

	/* Return the number of messages processed, or the error code. */
	if (err == 0)
		err = num;
	return err;
}


/*
 *  Initalize the 5282 I2C module
 *  Disable the 5282 I2C interrupt capability. Just use callback
 */

static int __init i2c_coldfire_init(void)
{
	int retval;
	u8  dummyRead;

#if defined(CONFIG_M532x)
	/*
	 * Initialize the GPIOs for I2C
	 */
	MCF_GPIO_PAR_FECI2C |= (0
    			    | MCF_GPIO_PAR_FECI2C_PAR_SDA(3)
    			    | MCF_GPIO_PAR_FECI2C_PAR_SCL(3));
#else
	/* Initialize PASP0 and PASP1 to I2C functions, 5282 user guide 26-19 */
	/* Port AS Pin Assignment Register (PASPAR)		*/
	/*		PASPA1 = 11 = AS1 pin is I2C SDA	*/
	/*		PASPA0 = 11 = AS0 pin is I2C SCL	*/
	*MCF_GPIO_PASPAR |= 0x000F;		/* u16 declaration */
#endif


    	/* Set transmission frequency 0x15 = ~100kHz */
    	*MCF_I2C_I2FDR = 0x15;

	/* set the 5282 I2C slave address thought we never use it */
    	*MCF_I2C_I2ADR = 0x6A;

    	/* Enable I2C module and if IBB is set, do the special initialzation */
	/* procedures as are documented at the 5282 User Guide page 24-11 */
    	*MCF_I2C_I2CR |= MCF_I2C_I2CR_IEN;
	if ((*MCF_I2C_I2SR & MCF_I2C_I2SR_IBB) == 1) {
		printk("%s - do special 5282 I2C init procedures \n", __FUNCTION__);
		*MCF_I2C_I2CR = 0x00;
		*MCF_I2C_I2CR = 0xA0;
		dummyRead = *MCF_I2C_I2DR;
		*MCF_I2C_I2SR = 0x00;
		*MCF_I2C_I2CR = 0x00;
	}

	/* default I2C mode is - slave and receive */
	*MCF_I2C_I2CR &= ~(MCF_I2C_I2CR_MSTA | MCF_I2C_I2CR_MTX);

	retval = i2c_add_adapter(&coldfire_adapter);

	if (retval < 0)
		printk("%s - return code is: %d \n", __FUNCTION__, retval);

	return retval;
};


/*
 *  I2C module exit function
 */

static void __exit i2c_coldfire_exit(void)
{
	/* disable I2C and Interrupt */
	*MCF_I2C_I2CR &= ~(MCF_I2C_I2CR_IEN | MCF_I2C_I2CR_IIEN);
	i2c_del_adapter(&coldfire_adapter);

};


MODULE_AUTHOR("Derek CL Cheung <derek.cheung@sympatico.ca>");
MODULE_DESCRIPTION("MCF5282 I2C adaptor");
MODULE_LICENSE("GPL");

module_init(i2c_coldfire_init);
module_exit(i2c_coldfire_exit);
