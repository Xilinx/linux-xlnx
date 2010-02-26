/*
 * XILINX FSL PS2 IP core keyboard driver for Linux
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <asm/xparameters.h>
#include <asm/mb_interface.h>
#include <asm/irq.h>

MODULE_AUTHOR("LynxWorks");
MODULE_DESCRIPTION("XILINX fsl_ps2 keyboard driver");
MODULE_LICENSE("GPL");


static unsigned char xilkbd_keycode[512] = {

	  0, 67, 65, 63, 61, 59, 60, 88,  0, 68, 66, 64, 62, 15, 41,117,
	  0, 56, 42, 93, 29, 16,  2,  0,  0,  0, 44, 31, 30, 17,  3,  0,
	  0, 46, 45, 32, 18,  5,  4, 95,  0, 57, 47, 33, 20, 19,  6,183,
	  0, 49, 48, 35, 34, 21,  7,184,  0,  0, 50, 36, 22,  8,  9,185,
	  0, 51, 37, 23, 24, 11, 10,  0,  0, 52, 53, 38, 39, 25, 12,  0,
	  0, 89, 40,  0, 26, 13,  0,  0, 58, 54, 28, 27,  0, 43,  0, 85,
	  0, 86, 91, 90, 92,  0, 14, 94,  0, 79,124, 75, 71,121,  0,  0,
	 82, 83, 80, 76, 77, 72,  1, 69, 87, 78, 81, 74, 55, 73, 70, 99,

	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	217,100,255,  0, 97,165,  0,  0,156,  0,  0,  0,  0,  0,  0,125,
	173,114,  0,113,  0,  0,  0,126,128,  0,  0,140,  0,  0,  0,127,
	159,  0,115,  0,164,  0,  0,116,158,  0,150,166,  0,  0,  0,142,
	157,  0,  0,  0,  0,  0,  0,  0,155,  0, 98,  0,  0,163,  0,  0,
	226,  0,  0,  0,  0,  0,  0,  0,  0,255, 96,  0,  0,  0,143,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,107,  0,105,102,  0,  0,112,
	110,111,108,112,106,103,  0,119,  0,118,109,  0, 99,104,119,  0,

	  0,  0,  0, 65, 99,
};


static unsigned char inbyte(void);
static unsigned int inword(void);
/*
unsigned int cur_scancode = 0;
unsigned int mark_scancode = 0 ;


static unsigned char inbyte(void)
{
	int break_scancode = 0, done = 0 ;
	unsigned int data  = 1 ;


	while(!done){ 		
	    microblaze_nbread_datafsl(data,0);
		data = data >> 24;
		if( !mark_scancode && (data != cur_scancode) ) {
			    mark_scancode = 1 ;
			    cur_scancode = data ;
	    }else if( mark_scancode && (data == 0xF0) ) {
		    break_scancode = 1 ;
	    }else if( break_scancode ) {
		    if( data == cur_scancode ) 
			    mark_scancode = 0 ;
		    else 
			    cur_scancode = data ;
			    done = 1 ;
	    }
	}

	return data;
}
*/


static unsigned int inword(void)
{

	unsigned int data  = 0 ;
	static unsigned int prev_data = 0;
	unsigned int msr;

    microblaze_nbread_datafsl(data,0);
//	msr = mfmsr();
//	if(msr&4) return 0;
	data = data >> 24;

	if (data == 0xF0) {
		prev_data = data;
		return 0;
	}

	if (prev_data == 0xF0) {
		prev_data = data;
		return 0x00F00000 | data;
	}

	prev_data = data;
	return data;
}

static struct input_dev xilkbd_dev;

static char *xilkbd_name = "PS2 keyboard on XILINX FSL PS2";
static char *xilkbd_phys = "xilkbd/input0";


static irqreturn_t xilkbd_interrupt(int irq, void *dummy, struct pt_regs *fp)
{
	unsigned int scancode;

	scancode = inword();
	
	if (!scancode) return IRQ_HANDLED;

	input_regs(&xilkbd_dev, fp);

	if ((scancode&0x00F00000) == 0x00F00000)
			input_report_key(&xilkbd_dev, xilkbd_keycode[scancode&0xFF], 0);
	else
			input_report_key(&xilkbd_dev, xilkbd_keycode[scancode&0xFF], 1);

	input_sync(&xilkbd_dev);

	if (
		xilkbd_keycode[scancode&0xFF] != KEY_LEFTSHIFT &&
		xilkbd_keycode[scancode&0xFF] != KEY_RIGHTSHIFT &&
		xilkbd_keycode[scancode&0xFF] != KEY_CAPSLOCK &&
		xilkbd_keycode[scancode&0xFF] != KEY_LEFTCTRL &&
		xilkbd_keycode[scancode&0xFF] != KEY_LEFTALT &&
		xilkbd_keycode[scancode&0xFF] != KEY_RIGHTALT &&
		xilkbd_keycode[scancode&0xFF] != KEY_LEFTMETA &&
		xilkbd_keycode[scancode&0xFF] != KEY_RIGHTMETA 
	   ) {
		input_report_key(&xilkbd_dev, xilkbd_keycode[scancode&0xFF], 0);
		input_sync(&xilkbd_dev);
	}

	return IRQ_HANDLED;
}

/*
static irqreturn_t xilkbd_interrupt(int irq, void *dummy, struct pt_regs *fp)
{
	unsigned int scancode;

	scancode = inbyte();
	
	input_regs(&xilkbd_dev, fp);

	input_report_key(&xilkbd_dev, xilkbd_keycode[scancode], 1);
	input_sync(&xilkbd_dev);
	input_report_key(&xilkbd_dev, xilkbd_keycode[scancode], 0);
	input_sync(&xilkbd_dev);


	return IRQ_HANDLED;
}
*/

static int __init xilkbd_init(void)
{
	int i;

	init_input_dev(&xilkbd_dev);

	xilkbd_dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REP);
	xilkbd_dev.keycode = xilkbd_keycode;
	xilkbd_dev.keycodesize = sizeof(unsigned char);
	xilkbd_dev.keycodemax = ARRAY_SIZE(xilkbd_keycode);

	for (i = 0; i < 512; i++)
		if (xilkbd_keycode[i])
			set_bit(xilkbd_keycode[i], xilkbd_dev.keybit);


	request_irq(XPAR_FSL_PS2_IRQ, xilkbd_interrupt, 0, "xilkbd", xilkbd_interrupt);

	xilkbd_dev.name = xilkbd_name;
	xilkbd_dev.phys = xilkbd_phys;
	xilkbd_dev.id.bustype = 0;
	xilkbd_dev.id.vendor = 0x0001;
	xilkbd_dev.id.product = 0x0001;
	xilkbd_dev.id.version = 0x0100;

	input_register_device(&xilkbd_dev);

	printk(KERN_INFO "input: %s\n", xilkbd_name);

	return 0;
}

static void __exit xilkbd_exit(void)
{
	input_unregister_device(&xilkbd_dev);
	free_irq(XPAR_FSL_PS2_IRQ, xilkbd_interrupt);
}

module_init(xilkbd_init);
module_exit(xilkbd_exit);
