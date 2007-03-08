
// UART Registers
typedef volatile struct
	{
	int np_uartrxdata;      // Read-only, 8-bit
	int np_uarttxdata;      // Write-only, 8-bit
	int np_uartstatus;      // Read-only, 8-bit
	int np_uartcontrol;     // Read/Write, 9-bit
	int np_uartdivisor;     // Read/Write, 16-bit, optional
	int np_uartendofpacket; // Read/Write, end-of-packet character
	} np_uart;

// UART Status Register Bits
enum
	{
	np_uartstatus_eop_bit  = 12,
	np_uartstatus_cts_bit  = 11,
	np_uartstatus_dcts_bit = 10,
	np_uartstatus_e_bit    = 8,
	np_uartstatus_rrdy_bit = 7,
	np_uartstatus_trdy_bit = 6,
	np_uartstatus_tmt_bit  = 5,
	np_uartstatus_toe_bit  = 4,
	np_uartstatus_roe_bit  = 3,
	np_uartstatus_brk_bit  = 2,
	np_uartstatus_fe_bit   = 1,
	np_uartstatus_pe_bit   = 0,

	np_uartstatus_eop_mask  = (1<<12),
	np_uartstatus_cts_mask  = (1<<11),
	np_uartstatus_dcts_mask = (1<<10),
	np_uartstatus_e_mask    = (1<<8),
	np_uartstatus_rrdy_mask = (1<<7),
	np_uartstatus_trdy_mask = (1<<6),
	np_uartstatus_tmt_mask  = (1<<5),
	np_uartstatus_toe_mask  = (1<<4),
	np_uartstatus_roe_mask  = (1<<3),
	np_uartstatus_brk_mask  = (1<<2),
	np_uartstatus_fe_mask   = (1<<1),
	np_uartstatus_pe_mask   = (1<<0)
	};

// UART Control Register Bits
enum
	{
	np_uartcontrol_ieop_bit  = 12,
	np_uartcontrol_rts_bit   = 11,
	np_uartcontrol_idcts_bit = 10,
	np_uartcontrol_tbrk_bit  = 9,
	np_uartcontrol_ie_bit    = 8,
	np_uartcontrol_irrdy_bit = 7,
	np_uartcontrol_itrdy_bit = 6,
	np_uartcontrol_itmt_bit  = 5,
	np_uartcontrol_itoe_bit  = 4,
	np_uartcontrol_iroe_bit  = 3,
	np_uartcontrol_ibrk_bit  = 2,
	np_uartcontrol_ife_bit   = 1,
	np_uartcontrol_ipe_bit   = 0,

	np_uartcontrol_ieop_mask  = (1<<12),
	np_uartcontrol_rts_mask   = (1<<11),
	np_uartcontrol_idcts_mask = (1<<10),
	np_uartcontrol_tbrk_mask  = (1<<9),
	np_uartcontrol_ie_mask    = (1<<8),
	np_uartcontrol_irrdy_mask = (1<<7),
	np_uartcontrol_itrdy_mask = (1<<6),
	np_uartcontrol_itmt_mask  = (1<<5),
	np_uartcontrol_itoe_mask  = (1<<4),
	np_uartcontrol_iroe_mask  = (1<<3),
	np_uartcontrol_ibrk_mask  = (1<<2),
	np_uartcontrol_ife_mask   = (1<<1),
	np_uartcontrol_ipe_mask   = (1<<0)
	};

// UART Routines
int nr_uart_rxchar(np_uart *uartBase);        // 0 for default UART
void nr_uart_txcr(void);
void nr_uart_txchar(int c,np_uart *uartBase); // 0 for default UART
void nr_uart_txhex(int x);                     // 16 or 32 bits
void nr_uart_txhex16(short x);
void nr_uart_txhex32(long x);
void nr_uart_txstring(char *s);

