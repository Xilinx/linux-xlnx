/*
 * linux/include/asm/arch-samsung/uncompress.c
 * 2001 Mac Wang <mac@os.nctu.edu.tw>
 *
 * linux-2.6.7/include/asm-armnommu/arch-espd_4510b/uncompress.c
 * 2004 JS H. <asky@syncom.com.tw
 */

#include "hardware.h"

#define VPint   *(volatile unsigned int *)

#ifndef CSR_WRITE
#   define CSR_WRITE(addr,data) (VPint(addr) = (data))
#endif

#ifndef CSR_READ
#   define CSR_READ(addr)       (VPint(addr))
#endif

/** Console UART Port */
#define DEBUG_CONSOLE   (0)

#if (DEBUG_CONSOLE == 0)
	#define DEBUG_TX_BUFF_BASE	REG_UART0_TXB
	#define DEBUG_RX_BUFF_BASE      REG_UART0_RXB
	#define DEBUG_UARTLCON_BASE     REG_UART0_LCON
	#define DEBUG_UARTCONT_BASE     REG_UART0_CTRL
	#define DEBUG_UARTBRD_BASE      REG_UART0_BAUD_DIV
	#define DEBUG_CHK_STAT_BASE     REG_UART0_STAT
#else /* DEBUG_CONSOLE == 1 */
	#define DEBUG_TX_BUFF_BASE      REG_UART1_TXB
	#define DEBUG_RX_BUFF_BASE      REG_UART1_RXB
	#define DEBUG_UARTLCON_BASE     REG_UART1_LCON
	#define DEBUG_UARTCONT_BASE     REG_UART1_CTRL
	#define DEBUG_UARTBRD_BASE      REG_UART1_BAUD_DIV
	#define DEBUG_CHK_STAT_BASE     REG_UART1_STAT
#endif

#define DEBUG_ULCON_REG_VAL     (0x3)
#define DEBUG_UCON_REG_VAL      (0x9)
#define DEBUG_UBRDIV_REG_VAL    (0x500)
#define DEBUG_RX_CHECK_BIT      (0X20)
#define DEBUG_TX_CAN_CHECK_BIT  (0X40)
#define DEBUG_TX_DONE_CHECK_BIT (0X80)

/** Setup console UART as 19200 bps */
static void s3c4510b_decomp_setup(void)
{
	CSR_WRITE(DEBUG_UARTLCON_BASE, DEBUG_ULCON_REG_VAL);
	CSR_WRITE(DEBUG_UARTCONT_BASE, DEBUG_UCON_REG_VAL);
	CSR_WRITE(DEBUG_UARTBRD_BASE,  DEBUG_UBRDIV_REG_VAL);
}

static void s3c4510b_putc(char c)
{
	CSR_WRITE(DEBUG_TX_BUFF_BASE, c);
	while(!(CSR_READ(DEBUG_CHK_STAT_BASE) & DEBUG_TX_DONE_CHECK_BIT));

	if(c == '\n')
		s3c4510b_putc('\r');
}

static void s3c4510b_puts(const char *s)
{
	while(*s != '\0')
		s3c4510b_putc(*s++);
}

