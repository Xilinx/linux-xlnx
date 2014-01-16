/*
 * pmodclp.c - Digilent PmodCLP driver
 *
 * Copyright (c) 2012 Digilent. All right reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <asm/uaccess.h>

#define DRIVER_NAME "pmodclp"
#define MAX_PMODCLP_DEV_NUM 16
#define TXT_BUF_SIZE 1024
#define MAX_NO_ROWS 2   /* The device has 2 rows */
#define MAX_NO_COLS 40  /* The device has max 40 columns */

#define CMD_LCDFNCINIT 0x38             /* function set command, (8-bit interface, 2 lines, and 5x8 dots) */
#define CMD_LCDCTLINIT 0x08             /* display control set command */
#define CMD_LCDCLEAR 0x01               /* clear display command */
#define CMD_LCDRETHOME 0x02             /* return home command */
#define CMD_LCDDISPLAYSHIFT 0x18        /* shift display command */
#define CMD_LCDCURSORSHIFT 0x10         /* shift cursor command */
#define CMD_LCDSETDDRAMPOS 0x80         /* set DDRAM position command */
#define CMD_LCDSETCGRAMPOS 0x40         /* set CGRAM position command */

#define MSK_BSTATUS 0x80                /* bit busy */
#define MSK_SHIFTRL 0x04                /* shift direction mask */
#define OPT_DISPLAYON 0x4               /* Set Display On option */
#define OPT_CURSORON 0x2                /* Set Cursor On option */
#define OPT_BLINKON 0x1                 /* Set Blink On option */

static dev_t pmodclp_dev_id;
static unsigned int device_num;
static unsigned int cur_minor;
static struct class *pmodclp_class;

/* structure that keeps the parallel port related information */
struct par_device {
	/* Pin Assignment */

	unsigned long iRS;
	unsigned long iRW;
	unsigned long iE;
	unsigned long iBK;
	unsigned long iData[8];
};

struct pmodclp_device {
	char *name;
	/* R/W Mutex Lock */
	struct mutex mutex;
	/* Text Buffer */
	char *txt_buf;          /* Device Text buffer */
	unsigned long cur_row;  /* Maintain current row */
	int exceeded_rows;      /* Flag for situation where maximum number of rows is exceeded */

	int display_on;
	int cursor_on;
	int blink_on;
	int bk_on;

	/* Pin Assignment */
	struct par_device par_dev;

	/* Char Device */
	struct cdev cdev;
	dev_t dev_id;
};

/* Forward definitions */
void parse_text(char *txt_buf, int cnt, struct pmodclp_device *dev);
void pmodclp_write_command(struct par_device *par_dev, unsigned char cmd);
static int pmodclp_init_gpio(struct pmodclp_device *pmodclp_dev);

/**
 * A basic open function.
 */
static int pmodclp_open(struct inode *inode, struct file *fp)
{
	struct pmodclp_device *dev;

	dev = container_of(inode->i_cdev, struct pmodclp_device, cdev);
	fp->private_data = dev;

	return 0;
}

/**
 * A basic close function, do nothing.
 */

static int pmodclp_close(struct inode *inode, struct file *fp)
{
	return 0;
}

/*
 * Driver write function
 *
 * This function uses a generic SPI write to send values to the Pmod device.
 * It takes a string from the app in the buffer.
 * It interprets the string of characters, and sends the commands and the text to PmodCLP over the parallel interface.
 */

static ssize_t pmodclp_write(struct file *fp, const char __user *buffer, size_t length, loff_t *offset)
{
	ssize_t retval = 0;
	int cnt;
	struct pmodclp_device *dev;

	dev = fp->private_data;

	if (mutex_lock_interruptible(&dev->mutex)) {
		retval = -ERESTARTSYS;
		goto write_lock_err;
	}

	cnt = length;

	if (copy_from_user(dev->txt_buf, buffer, cnt)) {
		retval = -EFAULT;
		goto quit_write;
	}
	retval = cnt;

	dev->txt_buf[cnt] = '\0';

	parse_text(dev->txt_buf, cnt, dev);

quit_write:
	mutex_unlock(&dev->mutex);
write_lock_err:
	return retval;
}

static void write_display_control_cmd(struct pmodclp_device *dev)
{
	unsigned char cmd = CMD_LCDCTLINIT +
			    (dev->display_on ? OPT_DISPLAYON : 0) +
			    (dev->cursor_on ? OPT_CURSORON : 0) +
			    (dev->blink_on ? OPT_BLINKON : 0);

	pmodclp_write_command(&(dev->par_dev), cmd);

}

/* Begin of parallel interface functions */

/**
 * gpio_par_define_data_direction - Configure the gpio data pins as input or output.
 *
 * Parameters:
 * @struct par_device *par_dev:	pointer to the structure containing parallel interface information
 * @bool fOutput:		true if the pins are configured as output
				false if the pins are configured as input
 * @unsigned char bOutputVal	the 8 bit value corresponding to the initial value set for the 8 lines if they are defined as output
 *
 *
 * This function configures the gpio data pins as input or output, as required by read or write operations.
 */
static int gpio_par_define_data_direction(struct par_device *par_dev, bool fOutput, unsigned char bOutputVal)
{
	int i;
	int status = 0;

	for (i = 0; !status && (i < 8); i++) {
		if (fOutput)
			status = gpio_direction_output(par_dev->iData[i], ((bOutputVal & (1 << i)) ? 1 : 0));
		else
			status = gpio_direction_input(par_dev->iData[i]);
	}
	udelay(20);
	return status;
}

/**
 * gpio_par_read_byte - Read one byte function.
 *
 * Parameters:
 * @struct par_device *par_dev:	pointer to the structure containing parallel interface information
 *
 * Return value			the byte read
 *
 * This function implements the parallel read cycle on the gpio pins. It is the basic read function.
 */
static unsigned char gpio_par_read_byte(struct par_device *par_dev)
{
	int i;
	unsigned char bData = 0;

	/* Set RW */
	gpio_set_value(par_dev->iRW, 1);
	udelay(20);
	/* Set Enable */
	gpio_set_value(par_dev->iE, 1);
	udelay(20);
	for (i = 0; i < 8; i++)
		bData += ((unsigned char)gpio_get_value(par_dev->iData[i]) << i);

	/* Clear Enable */
	gpio_set_value(par_dev->iE, 0);
	udelay(20);
	/* Clear RW */
	gpio_set_value(par_dev->iRW, 0);
	udelay(20);
	return bData;
}

/**
 * gpio_par_write_byte - Write one byte function.
 *
 * Parameters:
 * @struct par_device *par_dev:	pointer to the structure containing parallel interface information
 * @unsigned char bData:	the byte to be written over the parallel interface.
 *
 *
 * This function implements the parallel write cycle on the gpio pins. It is the basic write function,
 * it writes one byte over the parallel interface.
 */
static void gpio_par_write_byte(struct par_device *par_dev, unsigned char bData)
{
	int i;

	/* Clear RW */
	gpio_set_value(par_dev->iRW, 0);
	udelay(20);
	/* Set Enable */
	gpio_set_value(par_dev->iE, 1);
	udelay(20);
	for (i = 0; i < 8; i++)
		gpio_set_value(par_dev->iData[i], (bData >> i) & 0x01);

	/* Clear Enable */
	gpio_set_value(par_dev->iE, 0);
	udelay(20);
	/* Set RW */
	gpio_set_value(par_dev->iRW, 1);
	udelay(20);
}

/**
 * pmodclp_read_status - Read Status function
 *
 * Parameters:
 * @struct par_device *par_dev:	pointer to the structure containing parallel interface information
 *
 *
 * This function reads the status of the PmodCLP device.
 */
static unsigned char pmodclp_read_status(struct par_device *par_dev)
{
	unsigned char bStatus;

	/* define data pins as input */
	gpio_par_define_data_direction(par_dev, false, 0);

	/* clear RS, meaning instruction register */
	gpio_set_value(par_dev->iRS, 0);
	udelay(20);

	bStatus = gpio_par_read_byte(par_dev);

	return bStatus;
}

/**
 * pmodclp_wait_until_not_busy - Wait until device is ready function
 *
 * Parameters:
 * @struct par_device *par_dev:	pointer to the structure containing parallel interface information
 *
 *
 * This function loops until the device reports to be not busy.
 */
static void pmodclp_wait_until_not_busy(struct par_device *par_dev)
{
	unsigned char bStatus;

	/* read status */
	bStatus = pmodclp_read_status(par_dev);
	while (bStatus & MSK_BSTATUS) {
		mdelay(10);
		bStatus = pmodclp_read_status(par_dev);
	}
}

/**
 * gpio_par_write_byte - Write one command byte function.
 *
 * Parameters:
 * @struct par_device *par_dev:	pointer to the structure containing parallel interface information
 * @unsigned char bData:	the byte containing the command to be written over the parallel interface.
 *
 *
 * This function writes a command byte over the parallel interface.
 */
void pmodclp_write_command(struct par_device *par_dev, unsigned char cmd)
{
	/* wait until LCD is not busy */
	pmodclp_wait_until_not_busy(par_dev);

	/* clear RS, meaning instruction register */
	gpio_set_value(par_dev->iRS, 0);
	udelay(20);

	/* Write command byte */
	/* define data pins as output, and provide initial output value */
	gpio_par_define_data_direction(par_dev, true, cmd);

	/* implement write command */
	gpio_par_write_byte(par_dev, cmd);
}

/**
 * gpio_par_write_byte - Write array of caracters as data function.
 *
 * Parameters:
 * @struct par_device *par_dev:	pointer to the structure containing parallel interface information
 * @char *txt_buf: the text array to be written
 * @int cnt the number of charcaters in the text array
 *
 * This function writes a number of characters as data over the parallel interface.
 */
static void pmodclp_write_data(struct par_device *par_dev, char *txt_buf, int cnt)
{
	int i;

	/* set RS, meaning data */
	gpio_set_value(par_dev->iRS, 1);
	udelay(20);

	/* define data pins as output, and provide initial output value */
	gpio_par_define_data_direction(par_dev, true, txt_buf[0]);
	for (i = 0; i < cnt; i++)
		/* implement write command */
		gpio_par_write_byte(par_dev, txt_buf[i]);
}

/**
 * pmodclp_init - Required initialization sequence for PmodCLP.
 *
 * Parameters:
 * @struct par_device *par_dev: pointer to the structure containing parallel interface information
 *
 *
 * This function performs the required initialization sequence for PmodCLP. See the reference manual for more information.
 */
static void pmodclp_init(struct par_device *par_dev)
{

	/* perform initialization sequence, according to datasheet */

	/*	wait 20 ms */
	mdelay(20);
	/* Set function */
	pmodclp_write_command(par_dev, CMD_LCDFNCINIT);
	/* Wait 37 us */
	udelay(37);

	/* display on, no cursor, no blinking */
	pmodclp_write_command(par_dev, CMD_LCDCTLINIT);

	/* Wait 37 us */
	udelay(37);

	/* Display Clear */
	pmodclp_write_command(par_dev, CMD_LCDCLEAR);
	/* Wait 1.52 ms */
	udelay(1520);

}
/* Begin of parse functions */

/**
 * is_decimal_digit - This function returns true if the specified character is among decimal characters.
 *
 * Parameters
 * @char c: character that is searched to be among decimal characters
 *
 * Return value
 *	true if the specified character is among decimal characters
 *	false if the character character is not among decimal characters
 *
 * This function returns true if the specified character is among hexa characters ('0', '1', ...'9').
 */
static bool is_decimal_digit(char c)
{
	return c >= '0' && c <= '9';
}

/**
 * is_hexa_digit - This function returns true if the specified character is among hexa characters.
 *
 * Parameters
 * @char c: character that is searched to be among hexa characters
 *
 * Return value
 *	true if the specified character is among hexa characters
 *	false if the character character is not among hexa characters
 *
 * This function returns true if the specified character is among hexa characters ('0', '1', ...'9', 'A', 'B', ... , 'F', 'a', 'b', ..., 'f').
 */
static bool is_hexa_digit(char c)
{
	return is_decimal_digit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

/**
 * is_binary_digit - This function returns true if the specified character is among binary characters.
 *
 * Parameters
 * @char c: character that is searched to be among binary characters
 *
 * Return value
 *	true if the specified character is among binary characters
 *	false if the character character is not among binary characters
 *
 * This function returns true if the specified character is among binary characters ('0' and '1').
 */
static bool is_binary_digit(char c)
{
	return c == '0' || c == '1';
}

/**
 * parse_cmd - This function builds the commands to be sent for each recognized escape sequence.
 *
 * Parameters
 * @char c: character that is searched to be among command codes
 * @unsigned char *pars parameters array, built in parse_text function
 * @int idx_par			index of last parameter in parameters array, built in parse_text function
 * @int par_typ			the type of the last parameter
 *					0 - decimal
 *					1 - hexa
 *					2 - binary
 * @struct pmodclp_device *dev	pointer to device structure
 *
 * Return value
 *	1 if the character was recognized as a command code and the parameters configuration is correct for that command
 *	0 if the character is not a command code
 *
 * This function tries to mach the specified character and the parameters configuration to a command.
 *	If it does, the command is built and sent to PmodCLP on parallel interface and 1 is returned.
 * If no command is recognized for the character, 0 is returned.
 */

static int parse_cmd(char c, unsigned char *pars, int idx_par, int par_type, struct pmodclp_device *dev)
{
	int is_consumed_char = 0; /* this will be returned */
	unsigned char cmd;

	switch (c) {
	case 'e':
		/* enable/disable display */
		if (idx_par >= 0 && par_type == 0 && pars[0] >= 0 && pars[0] <= 3) {
			/* set display */
			int display, bk;
			display = (pars[0] & 1) != 0;
			if (display != dev->display_on) {
				dev->display_on = display;
				write_display_control_cmd(dev);
			}

			/* set background light, if the pin is defined. */
			if (dev->par_dev.iBK != -1) {
				bk = (pars[0] & 2) >> 1;
				if (bk != dev->bk_on) {
					dev->bk_on = bk;
					gpio_set_value(dev->par_dev.iBK, bk);
				}
			}
			is_consumed_char = 1; /* mark char as consumed. */
		}
		break;
	case 'H': /* set cursor position */
		if (idx_par == 1 && pars[0] >= 0 && pars[0] < MAX_NO_ROWS && pars[1] >= 0 && pars[1] < MAX_NO_COLS) {
			/* allow only decimal parameter */
			dev->cur_row = pars[0];
			dev->exceeded_rows = 0;
			cmd = 0x40 * pars[0] + pars[1];
			cmd |= CMD_LCDSETDDRAMPOS;
			pmodclp_write_command(&(dev->par_dev), cmd);
			is_consumed_char = 1; /* mark char as consumed. */
		}
		break;
	case 'j': /* clear display and home cursor */
		dev->cur_row = 0;
		dev->exceeded_rows = 0;
		pmodclp_write_command(&(dev->par_dev), CMD_LCDCLEAR);
		is_consumed_char = 1;   /* mark char as consumed. */
		break;
	case '@':                       /* scroll left */
		if (idx_par == 0 && pars[0] >= 0 && pars[0] < MAX_NO_COLS) {
			/* allow only decimal parameter */
			int i;
			cmd = CMD_LCDDISPLAYSHIFT;
			for (i = 0; i < pars[0]; i++)
				/* scroll one position */
				pmodclp_write_command(&(dev->par_dev), cmd);
			is_consumed_char = 1; /* mark char as consumed. */
		}
		break;
	case 'A': /* scroll right */
		if (idx_par == 0 && pars[0] >= 0 && pars[0] < MAX_NO_COLS) {
			/* allow only decimal parameter */
			int i;
			cmd = CMD_LCDDISPLAYSHIFT | MSK_SHIFTRL;
			for (i = 0; i < pars[0]; i++)
				/* scroll one position */
				pmodclp_write_command(&(dev->par_dev), cmd);
			is_consumed_char = 1; /* mark char as consumed. */
		}
		break;
	case 'c': /* set cursor mode */
		if (idx_par == 0 && par_type == 0 && pars[0] >= 0 && pars[0] <= 2) {
			/* allow only decimal parameter */
			int cursor, blink;

			/* set cursor */
			cursor = pars[0] >= 1; /* 1 and 2 */
			if (cursor != dev->cursor_on) {
				dev->cursor_on = cursor;
				write_display_control_cmd(dev);
			}
			/* set blink */
			blink = pars[0] <= 1; /* 0 and 1 */

			if (blink != dev->blink_on) {
				dev->blink_on = blink;
				write_display_control_cmd(dev);
			}
			is_consumed_char = 1; /* mark char as consumed. */
		}
		break;
	case 'd': /* define user programmable character */
	{
		/* define user char */
		if (idx_par == 8 && par_type == 0) {
			/* 9 params
				- first 8 definition bytes and
				- last one is the character number, allow only ecimal character
			 */

			/* set CGRAM pos */
			unsigned char cmd = CMD_LCDSETCGRAMPOS | (pars[8] << 3);
			pmodclp_write_command(&(dev->par_dev), cmd);

			/* write 8 character definition bytes as data */
			pmodclp_write_data(&(dev->par_dev), pars, 8);
			is_consumed_char = 1; /* mark} char as consumed. */
		}
	}
	break;
	default:
	break;
		/* no command was recognized */
	}
	return is_consumed_char;
}

/**
 * parse_text - This function builds the commands to be sent for each recognized escape sequence.
 *
 * Parameters
 * @char *txt_buf: the text array to be parsed
 * @int cnt the number of charcaters to be parsed in the text array
 * @struct pmodclp_device *dev	pointer to device structure
 *
 *
 * This function parses a text array, containing a sequence of one or more text or commands sent to PmodCLP. Its purpose is:
 * - recognize, interpret the text sent to the device:
 * - split the separate commands / text and process them individually.
 * - recognize escape code commands and translate them into PmodCLP commands on parallel interface
 * - send text data to PmodCLP device on parallel interface
 * - maintain a shadow value of the current row (this is because the cursor position cannot be read)
 * - recognize LF character ('\n') inside a text to be sent to the device
 * - if current line is the first, move the cursor to the beginning of the next line
 * - if current line is the second, there is no room for new line. Text characters after LF are ignored, commands are still interpreted.
 *
 */
void parse_text(char *txt_buf, int cnt, struct pmodclp_device *dev)
{
	int is_ignore_txt;
	int is_cmd = 0;
	int is_inside_par = -1;
	int is_consumed_char;
	int par_type = 0;

	char *parse_ptr, *processed_ptr, *par_ptr;
	int idx_par = -1;
	unsigned char pars[10];

	par_ptr = NULL;
	parse_ptr = txt_buf;
	processed_ptr = txt_buf - 1;
	is_cmd = 0;
	par_type = -1;
	is_ignore_txt = dev->exceeded_rows;
	while (parse_ptr < (txt_buf + cnt)) {
		is_consumed_char = 0; /* waiting to be consumed */
		/* recognize command - look for ESC code, followed by '[' */
		if ((!is_cmd) && ((*parse_ptr) == 0x1B) && (parse_ptr[1] == '[')) {
			/* enter command mode */
			is_cmd = 1;
			is_inside_par = 0; /* able to receive the parameter */
			/* send previous text (before the ESC sequence) */
			if ((parse_ptr - processed_ptr) > 1)
				pmodclp_write_data(&(dev->par_dev), processed_ptr + 1, parse_ptr - 1 - processed_ptr);
			parse_ptr++; /* skip '[' char */
		} else {
			if (is_cmd) {
				/* look for commands */
				if (!(par_type == 1 && (parse_ptr - par_ptr) <= 2))
					/* do not look for commands when current parameter is hexa and less than 2 chars are parsed */
					is_consumed_char = parse_cmd(*parse_ptr, pars, idx_par, par_type, dev);
				is_ignore_txt = dev->exceeded_rows; /* because command parsing may change this parameter */
				if (is_consumed_char) {
					/* mark text as processed including the command char */
					if ((parse_ptr - processed_ptr) > 0) {
						processed_ptr = parse_ptr;
						is_cmd = 0; /* comand mode is abandonned */
					}
				}
				if (!is_inside_par) {
					/* look for begining of a parameter */
					if (is_decimal_digit(*parse_ptr)) {
						par_type = -1;
						if (*parse_ptr == '0') {
							if (parse_ptr[1] == 'x' || parse_ptr[1] == 'X') {
								/* 0x or 0X sequence detected, start a hexa parameter */
								par_type = 1;
								is_consumed_char = 1;   /* char was consumed */
								parse_ptr++;            /* skip 'x' or 'X' char */
							} else {
								if (parse_ptr[1] == 'b' || parse_ptr[1] == 'B') {
									/* 0B or 0b sequence detected, start a binary parameter */
									par_type = 2;
									is_consumed_char = 1;   /* char was consumed */
									parse_ptr++;            /* skip 'b' or 'B' char */
								}
							}
						}
						idx_par++;
						if (!is_consumed_char) {
							/* 0x or 0b were not detected, start a decimal parameter */
							par_type = 0;
							pars[idx_par] = (*parse_ptr - '0');
							is_consumed_char = 1; /* char was consumed */
						} else {
							pars[idx_par] = 0;
						}
						par_ptr = parse_ptr;
						is_inside_par = 1;
					}
				} else {
					/* inside parameter, look for ';' separator and parameter digits */
					if (!is_consumed_char) {
						if ((*parse_ptr) == ';') {      /* parameters separator */
							par_ptr = NULL;
							is_inside_par = 0;      /* look for a new parameter */
							is_consumed_char = 1;   /* char was consumed */
						}
					}
					if (!is_consumed_char) {
						switch (par_type) {
						/* interpret parameter digit */
						case 0: /* decimal */
							if (is_decimal_digit(*parse_ptr)) {
								pars[idx_par] = 10 * pars[idx_par] + (*parse_ptr - '0');
								is_consumed_char = 1; /* char was consumed */
							}
							/* else wrong char, expecting decimal digit, do not consume char */
							break;
						case 1: /* hexa */
							if (is_hexa_digit(*parse_ptr)) {
								pars[idx_par] = pars[idx_par] << 4;
								if (*parse_ptr >= '0' && *parse_ptr <= '9')
									pars[idx_par] += (*parse_ptr - '0');
								else{
									if (*parse_ptr >= 'A' && *parse_ptr <= 'F')
										pars[idx_par] += 10 + (*parse_ptr - 'A');
									else
										pars[idx_par] += 10 + (*parse_ptr - 'a');
								}
								is_consumed_char = 1; /* char was consumed */
							}
							/* else wrong char, expecting decimal digit, do not consume char */
							break;
						case 2: /* binary */
							if (is_binary_digit(*parse_ptr)) {
								pars[idx_par] = (pars[idx_par] << 1) + (*parse_ptr - '0');
								is_consumed_char = 1; /* char was consumed */
							}
							/* else wrong char, expecting binary digit, leave command mode */
							break;
						}
					}       /* end of interpret parameter digit */
				}               /* end of parameter} */
				if (!is_consumed_char) {
					/* inside command mode, if character is not consumed, leave command mode */
					is_cmd = 0;
					/* consume unrecongnized command characters, makes no sense to display them on LCD */
					pr_info(" Wrong command: %.*s\n", (int)(parse_ptr - processed_ptr + 1), processed_ptr);
					processed_ptr = parse_ptr;
				}
				/* end of inside command */
			} else {
				/* free text, not inside a command */
				if (is_ignore_txt) {
					/* if text is ignored, processed_ptr advances together with parse_ptr */
					processed_ptr = parse_ptr;
				} else {
					if ((*parse_ptr) == '\n') { /* LF */
						/* mark processed before the LF char */
						if ((parse_ptr - processed_ptr) > 0)
							pmodclp_write_data(&(dev->par_dev), processed_ptr + 1, parse_ptr - 1 - processed_ptr);
						/* position the cursor on the beginning of the next line */
						if (dev->cur_row < (MAX_NO_ROWS - 1)) {
							dev->cur_row++;
							pmodclp_write_command(&(dev->par_dev), CMD_LCDSETDDRAMPOS + 0x40 * dev->cur_row);
						} else {
							/* there is no room to place a third line. Enter ignore text (still look for the comands) */
							is_ignore_txt = 1;
						}
						/* advance the pointers so that LF char is skipped next time when chars are sent */
						processed_ptr = parse_ptr;
					}
				}
			}
		}
		parse_ptr++; /* advance one character */
	}
	parse_ptr--;
	/* send remaining chars */

	if (((parse_ptr - processed_ptr) > 0)) {
		if (!is_cmd)
			pmodclp_write_data(&(dev->par_dev), processed_ptr + 1, parse_ptr - processed_ptr);
		else
			pr_info(" Wrong command: %.*s\n", (int)(parse_ptr - processed_ptr + 2), processed_ptr + 1);
	}

	dev->exceeded_rows = is_ignore_txt;
}

/**
 * Driver Read Function
 *
 * This function does not actually read the PmodCLP as it is a write-only device.
 */
static ssize_t pmodclp_read(struct file *fp, char __user *buffer, size_t length, loff_t *offset)
{
	ssize_t retval = 0;

	return retval;
}

static const struct file_operations pmodclp_cdev_fops = {
	.owner		= THIS_MODULE,
	.write		= pmodclp_write,
	.read		= pmodclp_read,
	.open		= pmodclp_open,
	.release	= pmodclp_close,
};

/**
 * pmodclp_setup_cdev - Setup Char Device for ZED PmodCLP device.
 * @dev: pointer to device tree node
 * @dev_id: pointer to device major and minor number
 * @spi: pointer to spi_device structure
 *
 * This function initializes char device for PmodCLP device, and add it into
 * kernel device structure. It returns 0, if the cdev is successfully
 * initialized, or a negative value if there is an error.
 */
static int pmodclp_setup_cdev(struct pmodclp_device *dev, dev_t *dev_id)
{
	int status = 0;
	struct device *device;

	cdev_init(&dev->cdev, &pmodclp_cdev_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &pmodclp_cdev_fops;

	*dev_id = MKDEV(MAJOR(pmodclp_dev_id), cur_minor++);
	status = cdev_add(&dev->cdev, *dev_id, 1);
	if (status < 0) {
		pr_info(" cdev_add failed ...\n");
		return status;
	}

	/* Add Device node in system */
	device = device_create(pmodclp_class, NULL,
			       *dev_id, NULL,
			       "%s", dev->name);
	if (IS_ERR(device)) {
		status = PTR_ERR(device);
		pr_info("failed to create device node %s, err %d\n",
			 dev->name, status);
		cdev_del(&dev->cdev);
	}

	return status;
}

static const struct of_device_id pmodclp_of_match[] = {
	{ .compatible = "dglnt,pmodclp", },
	{},
};
MODULE_DEVICE_TABLE(of, pmodclp_of_match);

/**
 * pmodclp_of_probe - Probe method for PmodCLP device (over GPIO).
 * @pdev: pointer to platform devices
 *
 * This function probes the PmodCLP device in the device tree. It initializes the
 * PmodCLP driver data structure. It returns 0, if the driver is bound to the PmodCLP
 * device, or a negative value if there is an error.
 */
static int pmodclp_of_probe(struct platform_device *pdev)
{
	struct pmodclp_device *pmodclp_dev;

	struct device_node *np = pdev->dev.of_node;

	int status = 0;

	/* Alloc Space for platform device structure */
	pmodclp_dev = kzalloc(sizeof(*pmodclp_dev), GFP_KERNEL);
	if (!pmodclp_dev) {
		status = -ENOMEM;
		goto dev_alloc_err;
	}

	/* Alloc Text Buffer for device */
	pmodclp_dev->txt_buf = kmalloc(TXT_BUF_SIZE, GFP_KERNEL);
	if (!pmodclp_dev->txt_buf) {
		status = -ENOMEM;
		dev_err(&pdev->dev, "Device Display data buffer allocation failed: %d\n", status);
		goto txt_buf_alloc_err;
	}

	/* Get the GPIO Pins */
	pmodclp_dev->par_dev.iRS = of_get_named_gpio(np, "rs-gpio", 0);
	pmodclp_dev->par_dev.iRW = of_get_named_gpio(np, "rw-gpio", 0);
	pmodclp_dev->par_dev.iE = of_get_named_gpio(np, "e-gpio", 0);
	status = of_get_named_gpio(np, "bk-gpio", 0);
	pmodclp_dev->par_dev.iBK = (status < 0) ? -1 : status;

	pmodclp_dev->par_dev.iData[0] = of_get_named_gpio(np, "data0-gpio", 0);
	pmodclp_dev->par_dev.iData[1] = of_get_named_gpio(np, "data1-gpio", 0);
	pmodclp_dev->par_dev.iData[2] = of_get_named_gpio(np, "data2-gpio", 0);
	pmodclp_dev->par_dev.iData[3] = of_get_named_gpio(np, "data3-gpio", 0);
	pmodclp_dev->par_dev.iData[4] = of_get_named_gpio(np, "data4-gpio", 0);
	pmodclp_dev->par_dev.iData[5] = of_get_named_gpio(np, "data5-gpio", 0);
	pmodclp_dev->par_dev.iData[6] = of_get_named_gpio(np, "data6-gpio", 0);
	pmodclp_dev->par_dev.iData[7] = of_get_named_gpio(np, "data7-gpio", 0);

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s: iRS: 0x%lx\n", np->name, pmodclp_dev->par_dev.iRS);
	pr_info(DRIVER_NAME " %s: iRW: 0x%lx\n", np->name, pmodclp_dev->par_dev.iRW);
	pr_info(DRIVER_NAME " %s: iE : 0x%lx\n", np->name, pmodclp_dev->par_dev.iE);
	pr_info(DRIVER_NAME " %s: iBK : 0x%lx\n", np->name, pmodclp_dev->par_dev.iBK);

	pr_info(DRIVER_NAME " %s: iData[0] : 0x%lx\n", np->name, pmodclp_dev->par_dev.iData[0]);
	pr_info(DRIVER_NAME " %s: iData[1] : 0x%lx\n", np->name, pmodclp_dev->par_dev.iData[1]);
	pr_info(DRIVER_NAME " %s: iData[2] : 0x%lx\n", np->name, pmodclp_dev->par_dev.iData[2]);
	pr_info(DRIVER_NAME " %s: iData[3] : 0x%lx\n", np->name, pmodclp_dev->par_dev.iData[3]);
	pr_info(DRIVER_NAME " %s: iData[4] : 0x%lx\n", np->name, pmodclp_dev->par_dev.iData[4]);
	pr_info(DRIVER_NAME " %s: iData[5] : 0x%lx\n", np->name, pmodclp_dev->par_dev.iData[5]);
	pr_info(DRIVER_NAME " %s: iData[6] : 0x%lx\n", np->name, pmodclp_dev->par_dev.iData[6]);
	pr_info(DRIVER_NAME " %s: iData[7] : 0x%lx\n", np->name, pmodclp_dev->par_dev.iData[7]);
#endif
	pmodclp_dev->name = (char *)np->name;

	/* initialize device data */
	pmodclp_dev->cur_row = 0;
	pmodclp_dev->exceeded_rows = 0;

	pmodclp_dev->display_on = 0;
	pmodclp_dev->cursor_on = 0;
	pmodclp_dev->blink_on = 0;
	pmodclp_dev->bk_on = 0;

	/* Point device node data to pmodclp_device structure */
	if (np->data == NULL)
		np->data = pmodclp_dev;

	if (pmodclp_dev_id == 0) {
		/* Alloc Major & Minor number for char device */
		status = alloc_chrdev_region(&pmodclp_dev_id, 0, MAX_PMODCLP_DEV_NUM, DRIVER_NAME);
		if (status) {
			dev_err(&pdev->dev, "Character device region not allocated correctly: %d\n", status);
			goto err_alloc_chrdev_region;
		}
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Char Device Region Registered, with Major: %d.\n",
			MAJOR(pmodclp_dev_id));
#endif
	}

	if (pmodclp_class == NULL) {
		/* Create Pmodclp Device Class */
		pmodclp_class = class_create(THIS_MODULE, DRIVER_NAME);
		if (IS_ERR(pmodclp_class)) {
			status = PTR_ERR(pmodclp_class);
			goto err_create_class;
		}
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : pmodclp device class registered.\n");
#endif
	}

	/* Setup char driver */
	status = pmodclp_setup_cdev(pmodclp_dev, &(pmodclp_dev->dev_id));
	if (status) {
		pr_info(" pmodclp_probe: Error adding %s device: %d\n", DRIVER_NAME, status);
		goto cdev_add_err;
	}

	device_num++;

	/* Initialize Mutex */
	mutex_init(&pmodclp_dev->mutex);

	status = pmodclp_init_gpio(pmodclp_dev);
	if (status) {
		pr_info(" spi_probe: Error init gpio: %d\n", status);
		goto cdev_add_err;
	}

	pmodclp_init(&pmodclp_dev->par_dev);

	return status;

err_create_class:
	unregister_chrdev_region(pmodclp_dev_id, MAX_PMODCLP_DEV_NUM);
	pmodclp_dev_id = 0;
err_alloc_chrdev_region:
cdev_add_err:

	pr_info(DRIVER_NAME " Free text buffer.\n");

	kfree(pmodclp_dev->txt_buf);
txt_buf_alloc_err:
	kfree(pmodclp_dev);
dev_alloc_err:
	return status;
}

/**
 * pmodclp_init_gpio - Initialize GPIO for ZED PmodCLP device
 * @dev - pmodclp_device
 *
 * Initializes PmodCLP GPIO Control Pins.
 * It returns 0, if the gpio pins are successfully
 * initialized, or a negative value if there is an error.
 */
static int pmodclp_init_gpio(struct pmodclp_device *pmodclp_dev)
{
	struct gpio pmodclp_ctrl[] = {
		{ pmodclp_dev->par_dev.iRS,      GPIOF_OUT_INIT_HIGH, "CLP RS"      },
		{ pmodclp_dev->par_dev.iRW,      GPIOF_OUT_INIT_HIGH, "CLP RW"      },
		{ pmodclp_dev->par_dev.iE,       GPIOF_OUT_INIT_HIGH, "CLP E"       },
		{ pmodclp_dev->par_dev.iData[0], GPIOF_OUT_INIT_HIGH, "CLP DATA[0]" },
		{ pmodclp_dev->par_dev.iData[1], GPIOF_OUT_INIT_HIGH, "CLP DATA[1]" },
		{ pmodclp_dev->par_dev.iData[2], GPIOF_OUT_INIT_HIGH, "CLP DATA[2]" },
		{ pmodclp_dev->par_dev.iData[3], GPIOF_OUT_INIT_HIGH, "CLP DATA[3]" },
		{ pmodclp_dev->par_dev.iData[4], GPIOF_OUT_INIT_HIGH, "CLP DATA[4]" },
		{ pmodclp_dev->par_dev.iData[5], GPIOF_OUT_INIT_HIGH, "CLP DATA[5]" },
		{ pmodclp_dev->par_dev.iData[6], GPIOF_OUT_INIT_HIGH, "CLP DATA[6]" },
		{ pmodclp_dev->par_dev.iData[7], GPIOF_OUT_INIT_HIGH, "CLP DATA[7]" },
		{ pmodclp_dev->par_dev.iBK,      GPIOF_OUT_INIT_HIGH, "CLP BK"      }
	};
	int status;
	int i;
	int array_size = pmodclp_dev->par_dev.iBK == -1 ? (ARRAY_SIZE(pmodclp_ctrl) - 1) : ARRAY_SIZE(pmodclp_ctrl);

	for (i = 0; i < array_size; i++) {
		status = gpio_is_valid(pmodclp_ctrl[i].gpio);
		if (!status) {
			pr_info("!! gpio_is_valid for GPIO %d, %s FAILED!, status: %d\n",
				pmodclp_ctrl[i].gpio, pmodclp_ctrl[i].label, status);
			goto gpio_invalid;
		}
	}

	pr_info("** gpio_request_array array_size = %d, ARRAY_SIZE = %d\n", array_size, (int)ARRAY_SIZE(pmodclp_ctrl));

	status = gpio_request_array(pmodclp_ctrl, ARRAY_SIZE(pmodclp_ctrl));
	if (status) {
		pr_info("!! gpio_request_array FAILED!\n");
		pr_info(" status is: %d, array_size = %d, ARRAY_SIZE = %d\n", status, array_size, (int)ARRAY_SIZE(pmodclp_ctrl));
		gpio_free_array(pmodclp_ctrl, 4);
		goto gpio_invalid;
	}

gpio_invalid:
/* gpio_direction_output_invalid: */
	return status;
}

/**
 * pmodclp_of_remove - Remove method for ZED PmodCLP device.
 * @np: pointer to device tree node
 *
 * This function removes the PmodCLP device in the device tree. It frees the
 * PmodCLP driver data structure. It returns 0, if the driver is successfully
 * removed, or a negative value if there is an error.
 */
static int pmodclp_of_remove(struct platform_device *pdev)
{
	struct pmodclp_device *pmodclp_dev;
	struct device_node *np = pdev->dev.of_node;

	if (np->data == NULL) {
		dev_err(&pdev->dev, "pmodclp %s: ERROR: No pmodclp_device structure found!\n", np->name);
		return -ENOSYS;
	}
	pmodclp_dev = (struct pmodclp_device *)(np->data);

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s : Free text buffer.\n", np->name);
#endif

	if (pmodclp_dev->txt_buf != NULL)
		kfree(pmodclp_dev->txt_buf);

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s : Unregister gpio_spi Platform Devices.\n", np->name);
#endif

	np->data = NULL;
	device_num--;

	/* Destroy pmodclp class, Release device id Region after
	 * all pmodclp devices have been removed.
	 */
	if (device_num == 0) {
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Destroy pmodclp_gpio Class.\n");
#endif

		if (pmodclp_class)
			class_destroy(pmodclp_class);
		pmodclp_class = NULL;

#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Release Char Device Region.\n");
#endif

		unregister_chrdev_region(pmodclp_dev_id, MAX_PMODCLP_DEV_NUM);
		pmodclp_dev_id = 0;
	}

	return 0;
}

static struct platform_driver pmodclp_driver = {
	.driver			= {
		.name		= DRIVER_NAME,
		.owner		= THIS_MODULE,
		.of_match_table = pmodclp_of_match,
	},
	.probe			= pmodclp_of_probe,
	.remove			= pmodclp_of_remove,
};

module_platform_driver(pmodclp_driver);

MODULE_AUTHOR("Digilent, Inc.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_NAME ": PmodCLP display driver");
MODULE_ALIAS(DRIVER_NAME);
