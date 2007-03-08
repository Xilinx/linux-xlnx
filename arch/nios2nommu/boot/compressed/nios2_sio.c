
static int putchar(int ch);

static int puts(const char *s)
	{
	while(*s)
		putchar(*s++);
	return 0;
	}

#include <asm/nios.h>
#include <asm/io.h>

#if defined(CONFIG_SERIAL_AJUART_CONSOLE)

#define IORD_ALTERA_AVALON_JTAG_UART_DATA(base)           inl(base) 
#define IOWR_ALTERA_AVALON_JTAG_UART_DATA(base, data)     outl(data, base)
#define IORD_ALTERA_AVALON_JTAG_UART_CONTROL(base)        inl(base+4)
#define IOWR_ALTERA_AVALON_JTAG_UART_CONTROL(base, data)  outl(data, base+4)
#define ALTERA_AVALON_JTAG_UART_CONTROL_WSPACE_MSK        (0xFFFF0000u)
#define ALTERA_AVALON_JTAG_UART_CONTROL_WSPACE_OFST       (16)

static void jtag_putc(int ch)
{
  unsigned base = na_jtag_uart;
  while ((IORD_ALTERA_AVALON_JTAG_UART_CONTROL(base) & ALTERA_AVALON_JTAG_UART_CONTROL_WSPACE_MSK) == 0);
  IOWR_ALTERA_AVALON_JTAG_UART_DATA(base, ch);
}

static int putchar(int ch)
{
   jtag_putc( ch );
   return ch;
}

#elif defined(CONFIG_NIOS_SERIAL_CONSOLE)

static void nr_txchar(int ch)
{
  while ((na_uart0->np_uartstatus & np_uartstatus_trdy_mask) == 0);
  na_uart0->np_uarttxdata = ch;
}

static int putchar(int ch)
{
   nr_txchar( ch ); if (ch=='\n') nr_txchar( '\r' );
   return ch;
}

#else

static int putchar(int ch)
{
   return ch;
}

#endif
