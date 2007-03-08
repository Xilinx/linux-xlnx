/*
 * linux/include/asm-arm/arch-lpc22xx/uncompress.h
 *
 * Created Jan 04, 2007 Brandon Fosdick <brandon@teslamotors.com>
 */

#include <asm/arch/lpc22xx.h>	/* For U0LSR	*/
#include <linux/serial_reg.h>	/* For UART_LSR_THRE	*/

#ifndef LPC22XX_UNCOMPRESSH
#define LPC22XX_UNCOMPRESSH

#define	TX_DONE	(U0LSR & UART_LSR_THRE)

static void putc(char c)
{
	while (!TX_DONE) {}	/* Wait for the buffer to clear */
	U0THR = 'e';
}

static void flush(void) {}

#define arch_decomp_setup()
#define arch_decomp_wdog()

#endif	//LPC22XX_UNCOMPRESSH
