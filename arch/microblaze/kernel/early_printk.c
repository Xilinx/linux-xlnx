/*
 * arch/microblaze/kernel/early_printk.c
 *
 * Copyright (c) 2003-2006 Yasushi SHOJI <yashi@atmark-techno.com>
 *
 * Early printk support for Microblaze.
 *
 *  - Once we got some system without uart light, we need to refactor.
 */

#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/fcntl.h>
#include <asm/xparameters.h>

#ifdef CONFIG_EARLY_PRINTK_UARTLITE_ADDRESS
#define BASE_ADDR ((unsigned char *)CONFIG_EARLY_PRINTK_UARTLITE_ADDRESS)
#else
#define BASE_ADDR ((unsigned char *)XPAR_RS232_UART_BASEADDR)
#endif
#define RX_FIFO   BASE_ADDR
#define TX_FIFO   ((unsigned long *)(BASE_ADDR + 4))
#define STATUS    ((unsigned long *)(BASE_ADDR + 8))
#define CONTROL   ((unsigned long *)(BASE_ADDR + 12))

static void early_printk_putc(char c)
{
	while (ioread32(STATUS) & (1<<3));
	iowrite32((c & 0xff), TX_FIFO);
}

static void early_printk_write(struct console * unused, const char *s, unsigned n)
{
	while (*s && n-- > 0) {
		early_printk_putc(*s);
		if (*s == '\n')
			early_printk_putc('\r');
		s++;
	}
}

static struct console early_serial_console = {
	.name = "earlyser",
	.write = early_printk_write,
	.flags = CON_PRINTBUFFER,
	.index = -1,
};

/* Direct interface for emergencies */
struct console *early_console = &early_serial_console;
static int early_console_initialized = 0;

void early_printk(const char *fmt, ...)
{
	char buf[512];
	int n;
	va_list ap;

	if (early_console_initialized) {
		va_start(ap, fmt);
		n = vscnprintf(buf, 512, fmt, ap);
		early_console->write(early_console, buf, n);
		va_end(ap);
	}
}

static int __initdata keep_early;

int __init setup_early_printk(char *opt)
{
	char *space;
	char buf[256];

	if (early_console_initialized)
		return 1;

	strlcpy(buf, opt, sizeof(buf));
	space = strchr(buf, ' ');
	if (space)
		*space = 0;

	if (strstr(buf,"keep"))
		keep_early = 1;

	early_console = &early_serial_console;
	early_console_initialized = 1;
	register_console(early_console);
	return 0;
}

void __init disable_early_printk(void)
{
	if (!early_console_initialized || !early_console)
		return;
	if (!keep_early) {
		printk("disabling early console\n");
		unregister_console(early_console);
		early_console_initialized = 0;
	}
	else
		printk("keeping early console\n");
}

__setup("earlyprintk=", setup_early_printk);
