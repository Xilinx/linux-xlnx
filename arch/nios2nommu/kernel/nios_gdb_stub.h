// file: nios_gdb_stub.h
// Author: Altera Santa Cruz \ 2000
//
// You can modify this header file to
// enable some features useful for
// debugging the debugger. They're
// good features also to just show
// signs of life on your Nios board.
// But they consume valuable peripherals!
//
// The 'GDB_DEBUG_PRINT' option ties
// up the LCD living on the 5v port,
// showing useful internals of the stub.
//
// dvb@altera.com
//

#ifdef ETHER_DEBUG
#ifdef na_enet
#define ethernet_exists
#endif
#endif

#ifdef ETHER_DEBUG
#ifdef ethernet_exists
#include "plugs.h"
#endif
#endif

#define MAX_DATA_SIZE		650
#define kTextBufferSize		((2*MAX_DATA_SIZE)+4)
#define kMaximumBreakpoints	4
#define GDB_ETH_PORT		7070
#define	GDB_WHOLE_PACKET	0
#define	GDB_SKIP_FIRST		1
#define GDB_RETRY_CNT		3

/*
 * This register structure must match
 * its counterpart in the GDB host, since
 * it is blasted across in byte notation.
 */
typedef struct
	{
	int r[32];
	long pc;
	short ctl0;
	short ctl1;
	short ctl2;
	short ctl3;
	} NiosGDBRegisters;

typedef struct
	{
	short *address;
	short oldContents;
	} NiosGDBBreakpoint;

typedef struct
	{
	NiosGDBRegisters registers;
	int trapNumber;				// stashed by ISR, to distinguish types
	char textBuffer[kTextBufferSize];
	int breakpointCount;			// breakpoints used for stepping
	int comlink;
	int stop;
	int gdb_eth_plug;
	NiosGDBBreakpoint breakpoint[kMaximumBreakpoints];
#ifdef ETHER_DEBUG
#ifdef ethernet_exists
	volatile int ACKstatus;
	net_32 host_ip_address;
	net_16 host_port_number;
#endif
#endif
	} NiosGDBGlobals;

#ifdef ETHER_DEBUG
#ifdef ethernet_exists
enum
{
	ne_gdb_ack_notwaiting,
	ne_gdb_ack_waiting,
	ne_gdb_ack_acked,
	ne_gdb_ack_nacked
};
#endif
#endif

enum 
{
	ne_gdb_serial,
	ne_gdb_ethernet
};

#ifndef GDB_DEBUG_PRINT
	#define GDB_DEBUG_PRINT 0
#endif

void GDB_Main(void);	// initialize gdb and begin.

char GDBGetChar(void);
void GDBPutChar(char c);
void GDB_Print2(char *s,int v1,int v2);

