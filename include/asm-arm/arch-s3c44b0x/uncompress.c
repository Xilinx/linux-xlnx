/*
 * linux/include/asm/arch-samsung/uncompress.c
 */

#include <asm/hardware.h>

static int s3c44b0x_decomp_setup()
{
}

static int s3c44b0x_putc(char c)
{
	while (!(SYSREG_GET(S3C44B0X_UTRSTAT0) & 0x2));
	SYSREG_SETB(S3C44B0X_UTXH0, c);
	if(c == '\n')
		s3c44b0x_putc('\r');
}

static void s3c44b0x_puts(const char *s)
{
	while(*s != '\0')
		s3c44b0x_putc(*s++);
}
