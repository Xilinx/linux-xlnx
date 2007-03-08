#include <linux/init.h>
#include <linux/console.h>
#include <asm/arch/hardware.h>

void s3c44b0x_uart_putc(const char c)
{
	while(!(SYSREG_GET(S3C44B0X_UTRSTAT0) & 0x2));
	SYSREG_SETB(S3C44B0X_UTXH0, c);
}

void s3c44b0x_console_write(struct console *co, const char *b, unsigned count)
{
	while(count) {
		s3c44b0x_uart_putc(*b);
		if (*b == '\n')
			s3c44b0x_uart_putc('\r');
		++b;
		--count;
	}
}

static int __init s3c44b0x_console_setup(struct console *co, char *options)
{       
	return 0;
}

struct console s3c44b0x_con_driver = {
	.name           = "S3C44B0X",
	.write          = s3c44b0x_console_write,
	.setup          = s3c44b0x_console_setup,
	.flags          = CON_PRINTBUFFER,
	.index          = -1,
};      

static int __init s3c44b0x_console_init(void) 
{                               
	register_console(&s3c44b0x_con_driver);
	return 0;       
}                               

console_initcall(s3c44b0x_console_init);
