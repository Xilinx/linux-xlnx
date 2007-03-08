/************************************************************************/
/*                                                                      */
/*  mcf_qspi.c - QSPI driver for MCF5272, MCF5235, MCF5282              */
/*                                                                      */
/*  (C) Copyright 2001, Wayne Roberts (wroberts1@home.com)              */
/*                                                                      */
/*  Driver has an 8bit mode, and a 16bit mode.                          */
/*  Transfer size QMR[BITS] is set thru QSPIIOCS_BITS.                  */
/*  When size is 8, driver works normally:                              */
/*        a char is sent for every transfer                             */
/*  When size is 9 to 16bits, driver reads & writes the QDRs with       */
/*  the buffer cast to unsigned shorts.  The QTR & QRR registers can    */
/*  be filled with up to 16bits.  The length passed to read/write must  */
/*  be of the number of chars (2x number of shorts). This has been      */
/*  tested with 10bit a/d and d/a converters.                           */
/*                                                                      */
/*  * QSPIIOCS_READDATA:                                                */
/*    data to send out during read                                      */
/*  * all other ioctls are global                                       */
/* -------------------------------------------------------------------- */
/*  Ported to linux-2.4.x by Ron Fial (ron@fial.com) August 26,2002     */
/*                                                                      */
/*   Added new include files                                            */
/*   Added module_init(),exit(),                                        */
/*   qspi_read(),qspi_write():  Revised qspi_read & write argument      */
/*     processing to handle new *filep argument. Changed i_rdev access  */
/*     to use filep->f_dentry->d_inode->i_rdev  Changed memcpy_fromfs() */
/*     to memcpy().                                                     */
/*   Added '__init' to compiled-in init routine for memory recovery     */
/*   Added '__exit' for loadable-driver module cleanup routine          */
/*   changed  register_chrdev to  devfs_register_chrdev                 */
/*   changed  unregister_chrdev to  devfs_unregister_chrdev             */
/*   Changed various declarations from int to ssize_t or loff_t         */
/* -------------------------------------------------------------------- */
/*   Changed interruptible_sleep_on to sleep_on so the driver has       */
/*           chance to finish the current transfer before application   */
/*           quits when typing '^C'. Otherwise a write collision will   */
/*           most likely occur.                                         */
/*   Added   safe_flags(); cli; and restore_flags() according to        */
/*           gerg@snapgear.com. Otherwise in some cases (higher clock   */
/*           rates) the transfer is finished before the current process */
/*           is put to sleep and therefore never wakes up again.        */
/*           09/12/2002 richard@opentcp.org                             */
/* -------------------------------------------------------------------- */
/*   02/06/2003 josef.baumgartner@telex.de                              */
/*                                                                      */
/*   Renamed cleanup_module() to qspi_exit() to be able to              */
/*     compile as module.                                               */
/*   Removed init_module() because module_init(qspi_init) does all      */
/*     we need.                                                         */
/*   Changed                                                            */
/*     SPI register settings will be saved for each instance to be able */
/*     to use different communication settings for different tasks.     */
/*     An ioctl() does not longer write directly to the SPI registers.  */
/*     It saves the settings which will be copied into the SPI          */
/*     registers on every read()/write().                               */
/*   Added MODULE_LICENSE("GPL") to avoid tainted kernel message.       */
/*     I think it is GPL?? There's no comment about this??              */
/*   Added polling mode                                                 */
/*     Increases performance for small data transfers.                  */
/*   Added odd mode                                                     */
/*     If an odd number of bytes is transfered and 16bit transfers are  */
/*     used, the last byte is transfered in byte mode.                  */
/*   Added dsp mode                                                     */
/*     If dsp mode is set, transfers will be limited to 15 bytes        */
/*     instead of 16. This ensures that DSPs with 24bit words get       */
/*     whole words within one transfer.                                 */
/* -------------------------------------------------------------------- */
/*   16/09/2003 ivan.zanin@bluewin.ch                                   */
/*                                                                      */
/*   Changed init and exit code to support the MCF5249                  */
/* -------------------------------------------------------------------- */
/*   Oct 19, 2004 jsujjavanich@syntech-fuelmaster.com                   */
/*                                                                      */
/*   Adjusted minor number detection to work with one dev per QSPI_CS   */
/* -------------------------------------------------------------------- */
/*   17/11/2004 chris_jones_oz@yahoo.com.au                             */
/*                                                                      */
/*   Changed init and exit code to support the MCF5282                  */
/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */
/*   Feb 3, 2005 ngustavson@emacinc.com                          		*/
/*                                                                      */
/*   Modularized function calls support kernel2kernel calls 			*/		            
/* -------------------------------------------------------------------- */
/************************************************************************/



/* **********************************************************************
Chapter 14. (excerpt) Queued Serial Peripheral Interface (QSPI) Module
   From:  http://e-www.motorola.com/brdata/PDFDB/docs/MCF5272UM.pdf

The following steps are necessary to set up the QSPI 12-bit data transfers
and a QSPI_CLK of 4.125 MHz. The QSPI RAM is set up for a queue of 16
transfers. All four QSPI_CS signals are used in this example.

1. Enable all QSPI_CS pins on the MCF5272. Write PACNT with 0x0080_4000 to
enable QSPI_CS1 and QSPI_CS3.Write PDCNT with 0x0000_0030 to enable QSPI_CS2.

2. Write the QMR with 0xB308 to set up 12-bit data words with the data
shifted on the falling clock edge, and a clock frequency of 4.125 MHz
(assuming a 66-MHz CLKIN).

3. Write QDLYR with the desired delays.

4. Write QIR with 0xD00F to enable write collision, abort bus errors, and
clear any interrupts.

5. Write QAR with 0x0020 to select the first command RAM entry.

6. Write QDR with 0x7E00, 0x7E00, 0x7E00, 0x7E00, 0x7D00, 0x7D00, 0x7D00,
0x7D00, 0x7B00, 0x7B00, 0x7B00, 0x7B00, 0x7700, 0x7700, 0x7700, and 0x7700
to set up four transfers for each chip select. The chip selects are active
low in this example.  NOTE: QDR value auto-increments after each write.

7. Write QAR with 0x0000 to select the first transmit RAM entry.

8. Write QDR with sixteen 12-bit words of data.

9. Write QWR with 0x0F00 to set up a queue beginning at entry 0 and ending
at entry 15.

10. Set QDLYR[SPE] to enable the transfers.

11.Wait until the transfers are complete. QIR[SPIF] is set when the
transfers are complete.

12. Write QAR with 0x0010 to select the first receive RAM entry.

13. Read QDR to get the received data for each transfer.  NOTE: QDR
auto-increments.

14. Repeat steps 5 through 13 to do another transfer.

************************************************************************* */

/**
* Includes
*-------------------------------------------------------------------------------
*/
#define EXPORT_SYMTAB

#include <asm/coldfire.h>               /* gets us MCF_MBAR value */
#include <asm/mcfsim.h>                 /* MCFSIM offsets */
#include <asm/semaphore.h>
#include <asm/system.h>                 /* cli() and friends */
#include <asm/uaccess.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/interrupt.h>

/* Include versioning info, if needed */
#if (defined(MODULE) && defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS))
#define MODVERSIONS
#endif

#if defined(MODVERSIONS)
#include <linux/modversions.h>
#endif

#include <asm/mcf_qspi.h>

int __init qspi_init(void);
static int init(void);
void __exit qspi_exit(void);


/**
* Declarations and global variables
*-------------------------------------------------------------------------------
*/
#define DEVICE_NAME "qspi"

MODULE_LICENSE("GPL");
/* struct wait_queue *wqueue; */
static DECLARE_WAIT_QUEUE_HEAD(wqueue);   /* use ver 2.4 static declaration - ron */
/* or should we use   wait_queue_heat_t *wqueue   ?? see page 141  */

static unsigned char dbuf[1024];

/*  static struct semaphore sem = MUTEX;   */

static DECLARE_MUTEX(sem);



/**
* EXPORTED symbols
*-------------------------------------------------------------------------------
*/
EXPORT_SYMBOL(qspi_mutex_down);
EXPORT_SYMBOL(qspi_mutex_up);
EXPORT_SYMBOL(qspi_create_device);
EXPORT_SYMBOL(qspi_destroy_device);
EXPORT_SYMBOL(qspi_internal_read);
EXPORT_SYMBOL(qspi_internal_write);
EXPORT_SYMBOL(qspi_control);
EXPORT_SYMBOL(qspi_BAUD);

/**
 * Kernel function calls (EXPORTED)
 * These functions provide the interface to the hardware.
 * They may only be called from Kernel space.
 * To use them directly from user space, the standard legacy char driver
 * API has been maintained in wrapper functions. 
 * -----------------------------------------------------------------------------
 */
 
 
 /**
  * qspi_mutex_down.
  * get in line for the qspi mutex
  * the internal kernel calls do not hold the mutex themselves and so down/up
  * must be called manually. This introduces a new level of complexity, 
  * but is required, as it may be necessary for some drivers to 
  * hold the mutex through more than one transaction.
  */
 void qspi_mutex_down(void){
 	 down_interruptible(&sem);
 }
 /**
  * qspi_mutex up
  * signal the qspi mutex.
  * see qspi_mutex_down 
  */
 void qspi_mutex_up(void){
 	 up(&sem);
 }
 
/**
 * qspi_create_device
 * Create a QSPI device. 
 * Configuration information is stored in this device structure, which
 * is used by the read and write calls, to dynamically change the SPI's 
 * configuration(bitrate, CPOL, etc...)
 * The elements of the structure are modifyable via control calls.
 * This function allocates the space for the device with Kmalloc, so if
 * the device is not destroyed(qspi_destroy_device, it will result in a 
 * memory leak.
 * @return a newly allocated and initialized qspi device
 */
qspi_dev *qspi_create_device(void){
qspi_dev *dev;

        if ((dev = kmalloc(sizeof(qspi_dev), GFP_KERNEL)) == NULL) {  
                return(NULL);
        }

        /* set default values */
        dev->read_data.length = 0; 
        dev->read_data.buf = NULL;
        dev->read_data.loop = 0;
        dev->poll_mod = 0;              /* interrupt mode */
        dev->bits = 8;
        dev->baud = qspi_BAUD(DEFAULT_BIT_RATE);
        dev->cpol = 0;
        dev->cpha = 0;
        dev->qcr_cont = 1;
        dev->dsp_mod = 0;               /* no DSP mode */
        dev->odd_mod = 0;               /* no ODD mode */
        dev->qcd = 17;
        dev->dtl = 1;

        return(dev);
}
/**
 * qspi_destroy device
 * free a previously created qspi device
 * @param device the device to destroy
 */
int qspi_destroy_device(qspi_dev *device){
  kfree(device);
return 0;
}

/**
 * qspi_internal_read
 * Read a block of SPI data into an array
 * SPI mode is set at the beginning of the transfer to be whatever is 
 * stored in the device structure
 * This function is not thread safe, qspi_mutex_up/down should be used.
 * @param dev device node containing transfer mode information
 * @buffer kernel space array to transfer the data into
 * @length amount of data to transfer should not be >sizeof(buffer)
 * @off unused
 * @qcr_cs slave select bitmap to use, ie. 3 would toggle SS0 and 1
 */
ssize_t qspi_internal_read(qspi_dev *dev,char *buffer, size_t length,
		loff_t *off,int qcr_cs){
        int total = 0;
        int i = 0;
        int max_trans;
        unsigned char bits;
        unsigned char word = 0;
        //unsigned long flag;
        int rdi = 0;

        /* set the register with default values */
        QMR = QMR_MSTR |
                (dev->dohie << 14) |
                (dev->bits << 10) |
                (dev->cpol << 9) |
                (dev->cpha << 8) |
                (dev->baud);

        QDLYR = (dev->qcd << 8) | dev->dtl;

        if (dev->dsp_mod)
                max_trans = 15;
        else
                max_trans = 16;

	
        bits = dev->bits % 0x10;
        if (bits == 0 || bits > 0x08)
                word = 1; /* 9 to 16bit transfers */

              //printk("\n READ driver -- ioctl xmit data fm dev->read_data.buf array  %x %x %x %x \n",dev->read_data.buf[0],dev->read_data.buf[1],dev->read_data.buf[2],dev->read_data.buf[3]);

        while (i < length) {
                unsigned short *sp = (unsigned short *)&buffer[i];
                unsigned char *cp = &buffer[i];
                unsigned short *rd_sp = (unsigned short *)dev->read_data.buf;
                int x;
                int n;

                QAR = TX_RAM_START;             /* address first QTR */
                QSPIDEBUG("writing from read buffer ");
                for (n = 0; n < max_trans; n++) {
                        if (rdi != -1) {
                                if (word) {
                                        QDR = rd_sp[rdi++];
                                        if (rdi == dev->read_data.length >> 1)
                                                rdi = dev->read_data.loop ? 0 : -1;
                                } else {
                                		QSPIDEBUG("%x ",dev->read_data.buf[rdi]);
                                        QDR = dev->read_data.buf[rdi++];
                                        if (rdi == dev->read_data.length)
                                                rdi = dev->read_data.loop ? 0 : -1;
                                }
                        } else
                                QDR = 0;						

                        i++;
                        if (word)
                                i++;
                        if (i > length)
                                break;
                }
                QSPIDEBUG("\n");
                

                QAR = COMMAND_RAM_START;        /* address first QCR */
                for (x = 0; x < n; x++) {
                        /* QCR write */
                        if (dev->qcr_cont) {
                                if (x == n - 1 && i == length)
                                        QDR = QCR_SETUP | qcr_cs;       /* last transfer */
                                else
                                        QDR = QCR_CONT | QCR_SETUP | qcr_cs;
                        } else
                                QDR = QCR_SETUP | qcr_cs;
                }

                QWR = QWR_CSIV | ((n - 1) << 8);

                /* check if we are using polling mode. Polling increases
                 * performance for small data transfers but is dangerous
                 * if we stay too long here, locking other tasks!!
                 */
                if (dev->poll_mod) {
                        QIR = QIR_SETUP_POLL;
                        QDLYR |= QDLYR_SPE;

                        while ((QIR & QIR_SPIF) != QIR_SPIF)
                                ;
                        QIR = QIR | QIR_SPIF;
                } else {
                        QIR = QIR_SETUP;
                        //save_flags(flag); cli();                // like in write function - don't think we need this
						
                        QDLYR |= QDLYR_SPE;
//                      interruptible_sleep_on(&wqueue);
                        sleep_on(&wqueue);                      // changed richard@opentcp.org
                        //restore_flags(flag);                    // like in write function

                }

                QAR = RX_RAM_START;     /* address: first QRR */
                if (word) {
                        /* 9 to 16bit transfers */
                        for (x = 0; x < n; x++) {
                              	*sp = *(volatile unsigned short *)(MCF_MBAR + MCFSIM_QDR);
                        		sp++;
                        }
                } else {
                        /* 8bit transfers */
                        QSPIDEBUG("8 bit read: ");
                        for (x = 0; x < n; x++){
                                *cp = *(volatile unsigned short *)(MCF_MBAR + MCFSIM_QDR);
                				QSPIDEBUG("%x ",*cp);                				
                				cp++;               				
                }QSPIDEBUG("\n");
                }
                if (word)
                        n <<= 1;

                total += n;
        }

        return(total);
}

/**
 * qspi_internal_write
 * write an array of data to the queued SPI bus
 * like qspi_internal_read, hardware configuration 
 * is done at the beginning of this routine.
 * This function is not thread safe, qspi_mutex_up/down should be used.
 * @param dev device node containing transfer mode information
 * @buffer kernel space array containing the data to send
 * @length amount of data to transfer should not be >sizeof(buffer)
 * @off unused
 * @qcr_cs slave select bitmap to use, ie. 3 would toggle SS0 and 1
 * @return 0 on success
 */
ssize_t qspi_internal_write(qspi_dev *dev, const char *buffer, size_t length,
                loff_t *off,int qcr_cs){
        int i = 0;
        int total = 0;
        int z;
        int max_trans;
        unsigned char bits;
        unsigned char word = 0;
        //unsigned long flag;


        QMR = QMR_MSTR |
                (dev->dohie << 14) |
                (dev->bits << 10) |
                (dev->cpol << 9) |
                (dev->cpha << 8) |
                (dev->baud);

        QDLYR = (dev->qcd << 8) | dev->dtl;

        bits = (QMR >> 10) % 0x10;
        if (bits == 0 || bits > 0x08)
                word = 1;       /* 9 to 16 bit transfers */

	
                                  /* next line was memcpy_fromfs()  */        

	 QSPIDEBUG("length = %x  \n",length);
      //printk("data to write is %x  %x  %x  %X  \n",dbuf[0],dbuf[1],dbuf[2],dbuf[3]);

        if (dev->odd_mod)
                z = QCR_SETUP8;
        else
                z = QCR_SETUP;

        if (dev->dsp_mod)
                max_trans = 15;
        else
                max_trans = 16;

        while (i < length) {
                int x;
                int n;

                QAR = TX_RAM_START;             /* address: first QTR */
                if (word) {
                        for (n = 0; n < max_trans; ) {
                                /* in odd mode last byte will be transfered in byte mode */
                                if (dev->odd_mod && (i + 1 == length)) {
                                        QDR = buffer[i];  /* tx data: QDR write */
                                         QSPIDEBUG("0x%X ", dbuf[i]);
                                        n++;
                                        i++;
                                        break;
                                }
                                else {
                                        QDR = (buffer[i] << 8) + buffer[i+1]; /* tx data: QDR write */
                                        QSPIDEBUG("0x%X 0x%X ", dbuf[i], dbuf[i+1]);
                                        n++;
                                        i += 2;
                                        if (i >= length)
                                                break;
                                }
                        }
                } else {
                        /* 8bit transfers */
                        QSPIDEBUG("8 bit write : ");
                        for (n = 0; n < max_trans; ) {                                
                                QSPIDEBUG("%x ",buffer[i]); 
                                QDR = buffer[i];  /* tx data: QTR write */
                                n++;
                                i++;
                                if (i == length)
                                        break;
                        }
                        QSPIDEBUG("\n");
                }

                QAR = COMMAND_RAM_START;        /* address: first QCR */
                for (x = 0; x < n; x++) {
                        /* QCR write */
                        if (dev->qcr_cont) {
                                if (x == n-1 && i == length)
                                        if ((i % 2)!= 0)
                                                QDR = z | qcr_cs; /* last transfer and odd number of chars */
                                        else
                                                QDR = QCR_SETUP | qcr_cs;       /* last transfer */
                                else
                                        QDR = QCR_CONT | QCR_SETUP | qcr_cs;
                        } else {
                                if (x == n - 1 && i == length)
                                        QDR = z | qcr_cs; /* last transfer */
                                else
                                        QDR = QCR_SETUP | qcr_cs;
                        }
                }

                QWR = QWR_CSIV | ((n - 1) << 8);  /* QWR[ENDQP] = n << 8 */

                /* check if we are using polling mode. Polling increases
                 * performance for small data transfers but is dangerous
                 * if we stay too long here, locking other tasks!!
                 */
                if (dev->poll_mod) {
                        QIR = QIR_SETUP_POLL;
                        QDLYR |= QDLYR_SPE;

                        while ((QIR & QIR_SPIF) != QIR_SPIF)
                                ;
                        QIR = QIR | QIR_SPIF;
                } else {
                        QIR = QIR_SETUP;
                       // save_flags(flag); cli();                // added according to gerg@snapgear.com - handled by mutex?
                        QDLYR |= QDLYR_SPE;

//                      interruptible_sleep_on(&wqueue);
                        sleep_on(&wqueue);                      // changed richard@opentcp.org

                        //restore_flags(flag);                    // added according to gerg@snapgear.com
                }


                if (word)
                        n <<= 1;

                total += n;
        }

        return(total);	               	
  		}

/**
 * qspi_control
 * Set configuration bits in the device structure or create an array
 * of data to transfer during the next tranceive operation.
 * This function does not actually interface with any hardware, it is used
 * only for modifying the device structure. From kernel space this could
 * be done more efficiently by directly modifying the structure.
 * This function is not thread safe, qspi_mutex_up/down should be used.
 * @param dev device node containing transfer mode information
 * @param cmd IOCTL command see mcf_qspi.h
 * @param arg IOCTL arg
 * @return 0 on success
 */
int qspi_control(qspi_dev *dev, unsigned int cmd, unsigned long arg){
        int ret = 0;
        struct qspi_read_data *read_data;


        switch (cmd) {
                /* Set QMR[DOHIE] (high-z Dout between transfers) */
                case QSPIIOCS_DOUT_HIZ:
                        dev->dohie = (arg ? 1 : 0);
                        break;

                /* Set QMR[BITS] */
                case QSPIIOCS_BITS:
                        if (((arg > 0) && (arg < 8)) || (arg > 16)) {
                                ret = -EINVAL;
                                break;
                        }

                        dev->bits = (u8)arg;
                        break;

                /* Get QMR[BITS] */
                case QSPIIOCG_BITS:
                        *((int *)arg) = dev->bits;
                        break;

                /* Set QMR[CPOL] (QSPI_CLK inactive state) */
                case QSPIIOCS_CPOL:
                        dev->cpol = (arg ? 1 : 0);
                        break;

                /* Set QMR[CPHA] (QSPI_CLK phase, 1 = rising edge) */
                case QSPIIOCS_CPHA:
                        dev->cpha = (arg ? 1 : 0);
                        break;

                /* Set QMR[BAUD] (QSPI_CLK baud rate divisor) */
                case QSPIIOCS_BAUD:
                        if (arg > 255) {
                                ret = -EINVAL;
                                break;
                        }

                        dev->baud = (u8)arg;
                        break;

                /* Set QDR[QCD] (QSPI_CS to QSPI_CLK setup) */
                case QSPIIOCS_QCD:
                        if (arg > 127) {
                                ret = -EINVAL;
                                break;
                        }

                        dev->qcd = (u8)arg;
                        break;

                /* Set QDR[DTL] (QSPI_CLK to QSPI_CS hold) */
                case QSPIIOCS_DTL:
                        if (arg > 255) {
                                ret = -EINVAL;
                                break;
                        }

                        dev->dtl = (u8)arg;
                        break;

                /* Set QCRn[CONT] (QSPI_CS continuous mode, 1 = remain
                 * asserted after transfer of 16 data words) */
                case QSPIIOCS_CONT:
                        dev->qcr_cont = (arg ? 1 : 0);
                        break;

                /* Set DSP mode, used to limit transfers to 15 bytes for
                 * 24-bit DSPs */
                case QSPIIOCS_DSP_MOD:
                        dev->dsp_mod  = (arg ? 1 : 0);
                        break;

                /* If an odd count of bytes is transferred, force the transfer
                 * of the last byte to byte mode, even if word mode is used */
                case QSPIIOCS_ODD_MOD:
                        dev->odd_mod = (arg ? 1 : 0);
                        break;               

                /* Set driver to use polling mode, which may increase
                 * performance for small transfers */
                case QSPIIOCS_POLL_MOD:
                        dev->poll_mod = (arg ? 1 : 0);
                        break;
                                                 
		
		 /* Kernel space copy function
          * Set data buffer to be used as "send data" during reads 
          * */
               	case QSPIIOCS_READDATA:
                        read_data = (struct qspi_read_data *)arg;                     
                        dev->read_data.length = read_data->length;
                        dev->read_data.buf = read_data->buf;
                        dev->read_data.loop = read_data->loop;                    
                        break;                              
		
		
		default:
			ret = -EINVAL;
			break;        
                                                
        }

return ret;	
}

/**
* Perform baud rate calculation for QMR register
*@param desired integer setting for desired baud rate
*@returns divider setting for QMR, -1 for invalid settings
*/
u16 qspi_BAUD(int desired){
   u16 baud =  MCF_CLK/(2*desired);
   if((baud>QMR_BAUD)||(baud<2))
      return -1;//invalid setting
   return baud;
}

/*------------------------------------------------------------------------------
 * End Kernal function calls
 */



/**
 * qspi_interrupt
 * hooked at initialization.
 * This logs error messages with printk and wakes up a process on 
 * the waitqueue.
 */
#ifdef LINUX26
static irqreturn_t  qspi_interrupt(int irq, void *dev_id, struct pt_regs *regs)
#else
static void qspi_interrupt(int irq, void *dev_id, struct pt_regs *regs)
#endif
{
        u16 qir = (QIR & (QIR_WCEF | QIR_ABRT | QIR_SPIF));

        /* Check write collision and transfer abort flags.  Report any
         * goofiness. */
        if (qir & QIR_WCEF)
                printk(KERN_INFO "%s: WCEF\n", __FILE__);

        if (qir & QIR_ABRT)
                printk(KERN_INFO "%s: ABRT\n", __FILE__);

        /* Check for completed transfer.  Wake any tasks sleeping on our
         * global wait queue. */
        if (qir & QIR_SPIF)
                wake_up(&wqueue);

        /* Clear any set flags. */
        QIR |= qir;
        
#ifdef LINUX26
return IRQ_RETVAL(1);
#endif
}

/**
 * qspi_ioctl
 * Standard ioctl system call for qspi character devices
 * calls qspi_control protected by the qspi mutex.
 */
static int qspi_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
                unsigned long arg){
        struct qspi_dev *dev = filp->private_data;
        qspi_read_data *read_data;
        int error;
        int ret;        
        int length;
        __u8 *kbuffer; 
        __u8 *ubuffer;
        int i;
        
        QSPIDEBUG("qspi ioctl\n");
        
         /* User space copy function
          * Set data buffer to be used as "send data" during reads 
          * */
                if(cmd==QSPIIOCS_READDATA){
                	QSPIDEBUG("READDATA set\n");
                        read_data = (struct qspi_read_data *)arg;
                       /*
                        error = verify_area(VERIFY_READ, read_data,
                                        sizeof(struct qspi_read_data));
                        */
                        error = access_ok(VERIFY_READ, (void *)arg ,
                                        sizeof(struct qspi_read_data));
                        
                        if (!error) {
                          		printk("couldn't read from %lx\n",(unsigned long) read_data);
                                return error;
                        }                       
                        //store old data buffer location
                        kbuffer = dev->read_data.buf;
                        //copy in user structure
                        copy_from_user(&dev->read_data, read_data,sizeof(struct qspi_read_data));
						//get user data location
						ubuffer = dev->read_data.buf;
						length =  dev->read_data.length;						
						dev->read_data.buf = kbuffer;//reset kernel pointer
						
						/*make sure user buffer is <= kbuffer size*/
						if(length>QSPIBSZ)
							return(-EINVAL);
						/*get user data*/	
						copy_from_user(kbuffer, ubuffer,length);
						QSPIDEBUG("copied over : ");
						for(i=0;i<length;i++)
							QSPIDEBUG("%x ",dev->read_data.buf[i]);
						QSPIDEBUG("\n");	
                        return 0;
                }
            //use qspi_control if we don't have to map anything from user space   
        	down_interruptible(&sem);
        	ret = qspi_control(dev,cmd,arg);
        	up(&sem);
        	return ret;
}

/**
 * qspi_open
 * Standard open system call for character qspi devices
 * creates a qspi device in private data
 */
static int qspi_open(struct inode *inode, struct file *file){
        
        qspi_dev *device = qspi_create_device();        
        if(device==NULL)
                return(-ENOMEM);
                
        if ((device->read_data.buf = kmalloc(QSPIBSZ, GFP_KERNEL)) == NULL) {
                qspi_destroy_device(device);
                return(-ENOMEM);
        }
        file->private_data = device;        
                
        return(0);
}


/**
 * qspi_release
 * Standard close system call for character qspi devices
 * destroys the qspi device in private data
 */
static int qspi_release(struct inode *inode, struct file *file)
{    
        qspi_destroy_device(file->private_data);
        file->private_data = NULL;
        return(0);
}

/**
 * qspi_read
 * Standard read system call for character qspi devices
 * gets cs number from device minor.
 * doesn't implement the offset arguement
 * calls qspi_internal_read protected by the qspi mutex
 */
static ssize_t qspi_read(struct file *filep, char *buffer, size_t length,
                loff_t *off){
                /* CS for QCR */
                int qcr_cs = (~MINOR(filep->f_dentry->d_inode->i_rdev) << 8) & 0xf00;                  
                int ret;
                qspi_dev *dev = filep->private_data;
                down_interruptible(&sem);                                                           
                ret = qspi_internal_read(dev, dbuf, length,off,qcr_cs); 
				up(&sem);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
                       	memcpy_tofs(buffer, dbuf,length);
#else
                        copy_to_user(buffer, dbuf,length);
#endif
                return ret;          	               	
                }

/**
 * qspi_write
 * Standard write system call for character qspi devices
 * gets cs number from device number
 * doesn't implement the offset arguement
 * calls qspi_internal_write protected by the qspi mutex
 */
static ssize_t qspi_write(struct file *filep, const char *buffer, size_t length,
                loff_t *off){
                int ret;	
               	qspi_dev *dev= filep->private_data;
               	int qcr_cs = (~MINOR(filep->f_dentry->d_inode->i_rdev) << 8) & 0xf00;  /* CS for QCR */
               	if(length>sizeof(dbuf))
               		return -ENOMEM;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
                        memcpy_fromfs(dbuf, buffer,length); 
#else                                     		
     		          	copy_from_user (dbuf, buffer, length);
#endif         
				down_interruptible(&sem);       	
               	ret = qspi_internal_write(dev,dbuf,length, off,qcr_cs);
                up(&sem);
                return ret;
                }

/**
 * qspi character file operations structure
 */
static struct file_operations Fops = {
        owner:          THIS_MODULE,
        read:           qspi_read,
        write:          qspi_write,
        ioctl:          qspi_ioctl,
        open:           qspi_open,
        release:        qspi_release  /* a.k.a. close */
};


/**
 * init
 * module initialization function.
 * hooks the interrupt and initializes the hardware
 */
static int init(void)
{
        volatile u32 *lp;
        volatile u8 *cp;

        /* common init: driver or module: */

        if (request_irq(MCFQSPI_IRQ_VECTOR, qspi_interrupt, SA_INTERRUPT, "ColdFire QSPI", NULL)) {
                printk("QSPI: Unable to attach ColdFire QSPI interrupt "
                        "vector=%d\n", MCFQSPI_IRQ_VECTOR);
                return(-EINVAL);
        }

#if defined(CONFIG_M5249)
        cp = (volatile u8 *)(MCF_MBAR + MCFSIM_ICR10);
        *cp = 0x8f;             /* autovector on, il=3, ip=3 */

        lp = (volatile u32 *)(MCF_MBAR2 + 0x180);
        *lp |= 0x00000800;      /* activate qspi_in and qspi_clk */

        lp = (volatile u32 *)(MCF_MBAR2 + MCFSIM2_GPIOFUNC);
        *lp &= 0xdc9FFFFF;      /* activate qspi_cs0 .. 3, qspi_dout */

        lp = (volatile u32 *)(MCF_MBAR + MCFSIM_IMR);
        *lp &= 0xFFFbFFFF;      /* enable qspi interrupt */
#elif defined(CONFIG_M5235)
	// interrupts mask here
	{
	 volatile unsigned char  *icrp;
	 icrp = (volatile unsigned char *)(MCF_MBAR + MCF5235ICM_INTC0 + MCFINTC0_ICR);
	 icrp[IRQ_SOURCE] = (( 3/*IL*/ & 0x3 ) << 3 ) | (3 /*IP*/ & 0x3);
	}
	{
	 volatile unsigned int *imrl;
	 imrl = (volatile unsigned int *)(MCF_MBAR + MCF5235ICM_INTC0 + MCF5235INTC_IMRL);
	 *imrl &= ~(1 << IRQ_SOURCE);
	}
	// GPIO here
	{
		volatile unsigned char *parp;
		parp = (volatile unsigned char *)(MCF_MBAR + 0x10004A);
		*parp = 0xFF;
	}
#elif (defined(CONFIG_M5282) || defined(CONFIG_M5280)|| defined(CONFIG_M528x))
       cp = (volatile u8 *) (MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_ICR0 +
                              MCFINT_QSPI);
 
        *cp = (5 << 3) + 3;     /* level 5, priority 3 */


#ifdef CONFIG_SOM5282EM
        cp = (volatile u8 *) (MCF_IPSBAR + MCF5282_GPIO_PQSPAR);
        *cp = 0x3f;             /* activate din, dout, clk and cs[0..2] // RTC has reverse polarity */
#else
		cp = (volatile u8 *) (MCF_IPSBAR + MCF5282_GPIO_PQSPAR);
        *cp = 0x7f;             /* activate din, dout, clk and cs[0..3] */        
#endif
        lp = (volatile u32 *) (MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_IMRL);
        *lp &= ~(1 + (1 << MCFINT_QSPI));      /* enable qspi interrupt */
#else
        /* set our IPL */
        lp = (volatile u32 *)(MCF_MBAR + MCFSIM_ICR4);
        *lp = (*lp & 0x07777777) | 0xd0000000;

        /* 1) CS pin setup 17.2.x
         *      Dout, clk, cs0 always enabled. Din, cs[3:1] must be enabled.
         *      CS1: PACNT[23:22] = 10
         *      CS1: PBCNT[23:22] = 10 ?
         *      CS2: PDCNT[05:04] = 11
         *      CS3: PACNT[15:14] = 01
         */
        lp = (volatile u32 *)(MCF_MBAR + MCFSIM_PACNT);
        *lp = (*lp & 0xFF3F3FFF) | 0x00804000;  /* 17.2.1 QSPI CS1 & CS3 */
        lp = (volatile u32 *)(MCF_MBAR + MCFSIM_PDCNT);
        *lp = (*lp & 0xFFFFFFCF) | 0x00000030;  /* QSPI_CS2 */
#endif

        /*
         * These values have to be setup according to the applications
         * using the qspi driver. Maybe some #defines at the beginning
         * would be more appropriate. Especially the transfer size
         * and speed settings
         */
        QMR = 0xA1A2; // default mode setup: 8 bits, baud, 160kHz clk.
//      QMR = 0x81A2; // default mode setup: 16 bits, baud, 160kHz clk.
        QDLYR = 0x0202; // default start & end delays

        init_waitqueue_head(&wqueue);    /* was init_waitqueue() --Ron */

#if defined(CONFIG_M5249)
        printk("MCF5249 QSPI driver ok\n");
#elif defined(CONFIG_M5235)
	printk("MCF5235 QSPI driver ok\n");
#elif (defined(CONFIG_M5282) || defined(CONFIG_M5280)|| defined(CONFIG_M528x))
        printk("MCF5282 QSPI driver ok\n");
#else
        printk("MCF5272 QSPI driver ok\n");
#endif

        return(0);
}



/*
 * And the bus type.
 */
 /*
struct bus_type qspi_bus_type = {
	.name = "mcfqspi",
	.match = qspi_match,
};
*/

/* init for qspi module */
int __init qspi_init(void)                                  /* the __init added by ron  */
{
        int ret;
#ifdef DEVFS
        if ((ret = devfs_register_chrdev(QSPI_MAJOR, DEVICE_NAME, &Fops) < 0)) {
                printk ("%s device failed with %d\n",
                        "Sorry, registering the character", ret);
                return(ret);
        }
#else 

/*register mcf_qspi driver as a bus*/
//if(ret = bus_register(&qspi_bus_type))
//		return ret;

 if ((ret = register_chrdev(QSPI_MAJOR, DEVICE_NAME, &Fops) < 0)) {
                printk ("%s device failed with %d\n",
                        "Sorry, registering the character", ret);
                return(ret);
        }
#endif


        printk ("QSPI device driver installed OK\n");
        return(init());
}

/* Cleanup - undid whatever init_module did */
void __exit qspi_exit(void)      /* the __exit added by ron  */
{
        int ret;

        free_irq(MCFQSPI_IRQ_VECTOR, NULL);

#if defined(CONFIG_M5249)
        /* autovector on, il=0, ip=0 */
        *(volatile u8 *)(MCF_MBAR + MCFSIM_ICR10) = 0x80;
        /* disable qspi interrupt */
        *(volatile u32 *)(MCF_MBAR + MCFSIM_IMR) |= 0x00040000;
#elif defined(CONFIG_M5235)
	{
	 volatile unsigned char  *icrp;
	 icrp = (volatile unsigned char *)(MCF_MBAR + MCF5235ICM_INTC0 + MCFINTC0_ICR);
	 icrp[IRQ_SOURCE] = 0;
	}
	// GPIO here
	{
		volatile unsigned char *parp;
		parp = (volatile unsigned char *)(MCF_MBAR + 0x10004A);
		*parp = 0x00;
	}
#elif (defined(CONFIG_M5282) || defined(CONFIG_M5280) || defined(CONFIG_M528x))
        /* interrupt level 0, priority 0 */
        *(volatile u8 *) (MCF_IPSBAR + MCFICM_INTC0 +
                          MCFINTC_ICR0 + MCFINT_QSPI) = 0;
        /* disable qspi interrupt */
        *(volatile u32 *) (MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_IMRL)
                                                        |= (1 << MCFINT_QSPI);
#else
        /* zero our IPL */
        *((volatile u32 *)(MCF_MBAR + MCFSIM_ICR4)) = 0x80000000;
#endif

        /* Unregister the device */
//bus_unregister(&qspi_bus_type);

#ifdef DEVFS
        if ((ret = devfs_unregister_chrdev(QSPI_MAJOR, DEVICE_NAME)) < 0)
                printk("Error in unregister_chrdev: %d\n", ret);
}
#else
 if ((ret = unregister_chrdev(QSPI_MAJOR, DEVICE_NAME)) < 0)
                printk("Error in unregister_chrdev: %d\n", ret);
}
#endif



module_init(qspi_init);      /* added by ron so driver can compile directly into kernel */
module_exit(qspi_exit);      /* added by ron so driver can compile directly into kernel */




