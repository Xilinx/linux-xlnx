// Modified for uClinux - Vic - Apr 2002
// From:

// File: nios_gdb_stub.c
// Date: 2000 June 20
// Author dvb \ Altera Santa Cruz

#ifndef __KERNEL__
#include "nios.h"
#else
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/nios.h>
#endif

#include "nios_gdb_stub.h"

#define na_debug_peripheral_irq 8

enum
{
  na_BreakpointTrap     = 3,
  na_SingleStepTrap     = 4,
  na_StartGDBTrap       = 5
};


#ifdef __KERNEL__

extern int _etext;

static void puts( unsigned char *s )
{
	while(*s) {
		while (!(nasys_printf_uart->np_uartstatus & np_uartstatus_trdy_mask));
		nasys_printf_uart->np_uarttxdata = *s++;
	}
}

#endif	// __KERNEL__

// --------------------------------
// Local Prototypes

#if GDB_DEBUG_PRINT

static void StringFit(char *s,int w);

// --------------------------------
// Debugging The Debugger

void GDB_RawMessage(char *s)
	{
	StringFit(s,32);
	nr_pio_lcdwritescreen(s);
	}
#else
	#define GDB_RawMessage(a,b,c)	// define away to nothing
#endif

#if GDB_DEBUG_PRINT
void GDB_Print2(char *s,int n1,int n2)
	{
	char st[1000];

	sprintf(st,s,n1,n2);
	GDB_RawMessage(st);
	}
#else
	#define GDB_Print2(a,b,c)	// define away to nothing
#endif

// If string is longer than w, cut out the middle.

#if GDB_DEBUG_PRINT
int StringLen(char *s)
	{
	int l = 0;

	while(*s++)
		l++;
	return l;
	}

static void StringFit(char *s,int w)
	{
	if(StringLen(s) > w)
		{
		int i;


		w = w / 2;

		for(i = 0; i < w; i++)
			{
			s[i + w] = s[StringLen(s) - w + i];
			}
		s[w + w] = 0;
		}
	}
#endif

// ---------------------------------------------
// Generic routines for dealing with
// hex input, output, and parsing
// (Adapted from other stubs.)

NiosGDBGlobals gdb = {0};		// not static: the ISR uses it!

static char dHexChars[16] = "0123456789abcdef";

/*
 * HexCharToValue -- convert a characters
 *                   to its hex value, or -1 if not.
 */
char HexCharToValue(char c)
{
	char result=0;

	if(c >= '0' && c <= '9')
		result = c - '0';
	else if(c >= 'a' && c <= 'f')
		result = c - 'a' + 10;
	else if(c >= 'A' && c <= 'F')
		result = c - 'A' + 10;
	else
		result = -1;
	return result;
}

/*
 * HexStringToValue -- convert a 2*byte_width string of characters
 *                   to its little endian hex value,
 *		     or -1 if not.
 *		This routine is for strings of hex values
 */
unsigned long HexStringToValue(char *c, int byte_width)
{
	unsigned long result=0;
	unsigned char a,b;
	int i=0;

	while (i < byte_width)
	{
		a = HexCharToValue(*c++);
		if (a & 0x80) return a;
		b = HexCharToValue(*c++);
		if (b & 0x80) return b;
		b = (a<<4) | (b&0x0f);
		result |= b << (i*8);
		i++;
	}
	return result;
}

/*
 * Hex2Value -- convert a non-hex char delimited string
 * 		to its big endian hex value.
 *		This routine is for address and byte count values
 */

char *Hex2Value(char *hexIn, int *valueOut)
	{
	char c;
	int digitValue;
	int value = 0;

	while(1)
		{
		c = *hexIn;
		digitValue = HexCharToValue(c);
		if(digitValue < 0)
			{
			*valueOut = value;
			return hexIn;
			}
		hexIn++;
		value = (value << 4) + digitValue;
		}
	}

/*
 * HexToMem -- convert a string to a specified
 *             number of bytes in memory.
 *
 *		JMB -- make this thing a bit smarter so
 *			   that it selects the byte width to
 *			   write based on the number of bytes
 *			   and the destination address alignment.
 *			   This is to support writes to non-byte enabled
 *			   peripheral registers...I don't like it.
 *			   Beware! there are cases where it wont work
 */
char *HexToMem(char *hexIn, char *memOut, int memByteCount)
{
	int i;
	unsigned long x;
	short *memOutS=0;
	long *memOutL=0;
	int byte_width;

	//determine maximum byte width
	if (((memByteCount%2) != 0) || (((unsigned int)memOut%2) != 0))
		byte_width = 1;
	else if (((memByteCount % 4) != 0) || (((unsigned int)memOut % 4) != 0))
	{
		byte_width = 2;
		memOutS = (short *)memOut;
	}
	else
	{
		byte_width = 4;
		memOutL = (long *)memOut;
	}
	for(i = 0; i < memByteCount; i+=byte_width)
	{
		x = HexStringToValue(hexIn,byte_width);
		hexIn += byte_width*2;
		switch (byte_width)
		{
		case 1:
			*memOut++ = (unsigned char) 0x000000ff & x;
			break;
		case 2:
			*memOutS++ = (unsigned short) 0x0000ffff & x;
			break;
		case 4:
			*memOutL++ = x;
			break;
		default:
			//How could this ever happen???
			break;
		}
	}

	return hexIn;
}

char *MemToHex(char *memIn, char *hexOut, int memByteCount)
{
	int i,j;
	int byte_width;
	unsigned long x=0;
	unsigned short *memInS=0;
	unsigned long *memInL=0;

	//determine maximum byte width
	if (((memByteCount % 2) != 0) || (((unsigned int)memIn % 2) != 0))
		byte_width = 1;
	else if (((memByteCount % 4) != 0) || (((unsigned int)memIn % 4) != 0))
	{
		byte_width = 2;
		memInS = (short *)memIn;
	}
	else
	{
		byte_width = 4;
		memInL = (long *)memIn;
	}

	for(i = 0; i < memByteCount; i+=byte_width)
	{
		switch (byte_width)
		{
		case 1:
			x = *memIn++;
			break;
		case 2:
			x = *memInS++;
			break;
		case 4:
			x = *memInL++;
			break;
		default:
			//How would we get here?
			break;
		}

		for (j=0; j<byte_width; j++)
		{
			*hexOut++ = dHexChars[(x&0x000000f0)>>4];
			*hexOut++ = dHexChars[x&0x0000000f];
			x = x>>8;
		}
	}

	*hexOut = 0;

	return hexOut;
}

//Send just the + or - to indicate
//ACK or NACK
void GDBPutAck (char ack)
{
	if (gdb.comlink == ne_gdb_serial)
		GDBPutChar (ack);
#ifdef ETHER_DEBUG
#ifdef ethernet_exists
	else
	{
		if (gdb.host_ip_address != 0)
			nr_plugs_send_to (gdb.gdb_eth_plug, &ack, 1, 0,
				gdb.host_ip_address,
				gdb.host_port_number);
	}
#endif
#endif
}

/*
 * Once a $ comes in, use GetGDBPacket to
 * retrieve a full gdb packet, and verify
 * checksum, and reply + or -.
 */
int GetGDBPacket(char *aBuffer)
{
	int checksum=0;
	int length=0;
	char c;
	int x=0;

	if (gdb.comlink == ne_gdb_serial)
	{
		while ((c = GDBGetChar ()) != '$') ;

startPacket:
		length = 0;
		checksum = 0;
		while(((c = GDBGetChar()) != '#') && (length < kTextBufferSize))
		{
			if(c == '$')
				goto startPacket;
			checksum += c;
			aBuffer[length++] = c;
			aBuffer[length] = 0;
		}

		c = GDBGetChar();
		x = HexCharToValue(c) << 4;
		c = GDBGetChar();
		x += HexCharToValue(c);


		checksum &= 0xff;

		GDB_Print2("GetPacket %d",length,0);
	}
#ifdef ETHER_DEBUG
#ifdef ethernet_exists
	else
	{
		int srcidx;
		// wait till beginning of packet
		while (gdb.textBuffer[0] != '$') nr_plugs_idle();
startEPacket:
		length = 0;
		checksum = 0;
		srcidx = 1;

		//loop until packet terminator
		//leave enough room for the checksum at the end
		while (((c = gdb.textBuffer[srcidx++]) != '#') && (srcidx < kTextBufferSize-2))
		{
			if (c == '$')
				goto startEPacket;

			checksum += c;
			aBuffer[length++] = c;
		}

		c = gdb.textBuffer[srcidx++];
		x = HexCharToValue(c) << 4;
		c = gdb.textBuffer[srcidx++];
		x += HexCharToValue (c);

		aBuffer[length++] = 0;

		checksum &= 0xff;

		GDB_Print2("GetPacket %d",length,0);
	}
#endif
#endif

	if(checksum != x)
	{
		GDBPutAck('-');
		length = 0;
	}
	else
	{
		GDBPutAck('+');
	}
	return length;
}

//Wait for acknowledgement
//Should we have some way of timing out???
//return TRUE if ACK
//return FALSE if NACK
int GDBGetACK (void)
{
	char c;
	if (gdb.comlink == ne_gdb_serial)
	{
		while (1)
		{
			c = GDBGetChar ();
			if (c == '+') return (1);
			else if (c == '-') return (0);
		}

	}
#ifdef ETHER_DEBUG
#ifdef ethernet_exists
	else
	{
		gdb.ACKstatus = ne_gdb_ack_waiting;
		while (1)
		{
			nr_plugs_idle ();
			if (gdb.ACKstatus == ne_gdb_ack_acked)
			{
				gdb.ACKstatus = ne_gdb_ack_notwaiting;
				return (1);
			}
			else if (gdb.ACKstatus == ne_gdb_ack_nacked)
			{
				gdb.ACKstatus = ne_gdb_ack_notwaiting;
				return (0);
			}
		}
	}
#endif
#endif
	return(0);
}

/*
 * Send a packet, preceded by $,
 * and followed by #checksum.
 */
void PutGDBPacket(char *aBuffer)
{
	int checksum;
	char c;
	char *origPtr;
	int cnt=0;

	origPtr = aBuffer; // Remember in case we get a NACK
	if (gdb.comlink == ne_gdb_serial)
	{
startPutSerial:
		GDBPutChar('$');
		checksum = 0;
		while((c = *aBuffer++) != 0)
		{
			checksum += c;
			GDBPutChar(c);
		}
		GDBPutChar('#');
		GDBPutChar(dHexChars[(checksum >> 4) & 15]);
		GDBPutChar(dHexChars[checksum & 15]);

		if (!GDBGetACK ())
		{
		  aBuffer = origPtr;
			if (++cnt < GDB_RETRY_CNT) goto startPutSerial;
		}
	}
#ifdef ETHER_DEBUG
#ifdef ethernet_exists
	else
	{
		if (gdb.host_ip_address != 0)
		{
			int i;
			int result;
			char c1;

			i = 0;
			c = aBuffer[i];
			if (c==0) return; //there is no data in packet, so why bother sending
			aBuffer[i++] = '$';
			checksum = 0;
			do
			{
				checksum += c;
				c1 = aBuffer[i];
				aBuffer[i++] = c;
				c = c1;
			} while (c != 0);

			aBuffer[i++] = '#';
			aBuffer[i++] = dHexChars[(checksum >> 4) & 15];
			aBuffer[i++] = dHexChars[checksum & 15];
			aBuffer[i++] = 0;
startPutEth:
			result = nr_plugs_send_to (gdb.gdb_eth_plug, aBuffer, i, 0,
				gdb.host_ip_address,
				gdb.host_port_number);

			if (!GDBGetACK ())
			{
				if (++cnt < GDB_RETRY_CNT) goto startPutEth;
			}
			aBuffer[0] = 0; //clear packet to
		}
	}
#endif
#endif
}

int PutTracePacket(char *aBuffer, int size)
{
	int checksum;
#ifdef ethernet_exists
	char c;
#endif
	int i;
	int cnt=0;

	if (gdb.comlink == ne_gdb_serial)
	{
startPutSerial:
		GDBPutChar('$');
		checksum = 0;
		for (i=0; i<size; i++)
		{
			checksum += aBuffer[i];
			GDBPutChar (aBuffer[i]);
		}
		GDBPutChar('#');
		GDBPutChar(dHexChars[(checksum >> 4) & 15]);
		GDBPutChar(dHexChars[checksum & 15]);

		if (!GDBGetACK ())
		{
			if (++cnt < GDB_RETRY_CNT) goto startPutSerial;
		}
	}
#ifdef ETHER_DEBUG
#ifdef ethernet_exists
	else
	{
		int result;
		char c1;

		checksum = 0;
		c = '$';
		for (i=0; i<size; i++)
		{
			checksum += aBuffer[i];
			c1 = aBuffer[i];
			aBuffer[i] = c;
			c = c1;
		}
		aBuffer[i++] = c;

		aBuffer[i++] = '#';
		aBuffer[i++] = dHexChars[(checksum >> 4) & 15];
		aBuffer[i++] = dHexChars[checksum & 15];
		aBuffer[i++] = 0;
ethResend:
		if (gdb.host_ip_address != 0)
		{
			result = nr_plugs_send_to (gdb.gdb_eth_plug, aBuffer, i, 0,
				gdb.host_ip_address,
				gdb.host_port_number);
		}
		if (!GDBGetACK ())
		{
			if (++cnt < GDB_RETRY_CNT) goto ethResend;
		}
		aBuffer[0]=0;
	}
#endif
#endif
	if (cnt < GDB_RETRY_CNT) return 1;
	else return 0;
}

void PutGDBOKPacket(char *aBuffer)
	{
	aBuffer[0] = 'O';
	aBuffer[1] = 'K';
	aBuffer[2] = 0;
	PutGDBPacket(aBuffer);
	}

#if nasys_debug_core

//some defines used exclusively for TRACE data xfer
//stepsize is the ascii hex step value i.e. twice the binary length
#define stepsize (2*(2*sizeof(int) + sizeof (char)))
#define MAX_TRACE_BYTES	(((int)((2*MAX_DATA_SIZE-2)/stepsize))*stepsize)

int Trace_Read_Intercept (char *aBuffer)
{
	int cnt=0;
	unsigned int data;
	unsigned char code;
	int byteCount;
	unsigned char *w;
	unsigned short dataAccumulate;
	int status;

	w = aBuffer;
	w++;	//skip past the m
	if (*w++ == 't')  //see if this is a special "memory trace" packet
	{
		w = Hex2Value(w,&byteCount); //get the number of bytes to transfer

		//turn byteCount to a multiple of stepsize
		byteCount = ((int)(byteCount/stepsize))*stepsize;

		//wait until fifo empties
		nm_debug_get_reg(status, np_debug_write_status);
		while (status&np_debug_write_status_writing_mask) nm_debug_get_reg(status,np_debug_write_status);

		// loop through total size
		while (byteCount > 0)
		{
			w=aBuffer;	//reset w to beginning of buffer

			//calculate the number of bytes in this packet
			if (byteCount > MAX_TRACE_BYTES) dataAccumulate = MAX_TRACE_BYTES;
			else dataAccumulate = byteCount;

			//insert data size at beginning of packet
			w = MemToHex((char *)&dataAccumulate, w, sizeof (dataAccumulate));

			byteCount -= dataAccumulate; //decrement byteCount

			// accumulate a full buffer
			for (cnt=0; cnt<dataAccumulate; cnt+=stepsize)
			{
				int valid;
				nm_debug_set_reg (1, np_debug_read_sample); //begin transaction

				//wait until data is ready
				nm_debug_get_reg (valid, np_debug_data_valid);
				while (!valid) nm_debug_get_reg(valid,np_debug_data_valid) ;

				nm_debug_get_reg (data, np_debug_trace_address);
				w = MemToHex ((char *)&data, w, sizeof (int));

				nm_debug_get_reg (data, np_debug_trace_data);
				w = MemToHex ((char *)&data, w, sizeof (int));

				nm_debug_get_reg (data, np_debug_trace_code);
				w = MemToHex ((char *)&data, w, sizeof (char));
			}

			//if one of our data packets doesn't make it, stop sending them
			//if (PutTracePacket (aBuffer,dataAccumulate+4) != 1) //+4 for size filed
			//	byteCount = 0;
			/* kenw - My module can't handle the incoming data fast enough.  So
			 * send this one packet, and wait for another mt command.
			 */
			PutTracePacket (aBuffer,dataAccumulate+4);
			byteCount = 0;
		}
		return 1;
	}
	return 0;
}

/*
#undef stepsize
#undef MAX_TRACE_BYTES
*/

#endif

void DoGDBCommand_m(char *aBuffer)
	{
	char *w;
	int startAddr,byteCount;

#if nasys_debug_core
	/* intercept some access to the dbg peripheral */
	if (Trace_Read_Intercept (aBuffer)) return;
#endif

	w = aBuffer;
	w++;				// past 'm'
	w = Hex2Value(w,&startAddr);
	w++;				// past ','
	w = Hex2Value(w,&byteCount);

	if (byteCount > MAX_DATA_SIZE) byteCount = MAX_DATA_SIZE;

	// mA,L -- request memory
	w = aBuffer;
	w = MemToHex((char *)startAddr,w,byteCount);
	PutGDBPacket(aBuffer);
	}

void DoGDBCommand_M(char *aBuffer)
	{
	char *w;
	int startAddr,byteCount;

	w = aBuffer;
	w++;				// past 'M'
	w = Hex2Value(w,&startAddr);
	w++;				// past ','
	w = Hex2Value(w,&byteCount);
	w++;				// past ':'

	GDB_Print2("M from %x to %x",startAddr,byteCount);

	// MA,L:values -- write to memory

	w = HexToMem(w,(char *)startAddr,byteCount);

	// Send "OK"
	PutGDBOKPacket(aBuffer);
	}

int Debug_Read_Intercept (char *aBuffer)
{
	unsigned int data;
	int index;
	unsigned char *w;

	w = aBuffer;
	w++;	//skip past the g
	if (*w++ == 'g')  //see if this is a special "register read" packet
	{
	    w = Hex2Value(w,&index); //get the index of the register to be read

	    nm_debug_get_reg (data, index);

	    //assemble the output packet
	    w=aBuffer;	//reset w to beginning of buffer
	    w = MemToHex((char *)&data, w, sizeof (data));
	    *w++ = 0;

	    //now send it
	    PutTracePacket (aBuffer,sizeof (data) * 2);

	    return 1;
	}
	return 0;
}

// Return the values of all the registers
void DoGDBCommand_g(NiosGDBGlobals *g)
	{
	char *w;

	if (Debug_Read_Intercept (g->textBuffer)) return;

	w = g->textBuffer;

	w = MemToHex((char *)(&g->registers),w,sizeof(g->registers));
	PutGDBPacket(g->textBuffer);
	GDB_Print2("Sent            Registers",0,0);
	}

int Debug_Write_Intercept (char *aBuffer)
{
	unsigned int data;
	int index;
	unsigned char *w;

	w = aBuffer;
	w++;	//skip past the g
	if (*w++ == 'g')  //see if this is a special "register read" packet
	{
	    w = Hex2Value(w,&index); //get the index of the register to be written
	    w++;				// past ','
	    w = Hex2Value(w,&data);

	    nm_debug_set_reg (data, index);

	    //now send it
	    // Send "OK"
	    PutGDBOKPacket(aBuffer);

	    return 1;
	}
	return 0;
}

void DoGDBCommand_G(NiosGDBGlobals *g)
	{
	char *w;

	if (Debug_Write_Intercept (g->textBuffer)) return;

	w = g->textBuffer;
	w++;	// skip past 'G'
	w = HexToMem(w,(char *)(&g->registers), sizeof(g->registers) );

	// Send "OK"
	PutGDBOKPacket(g->textBuffer);

	GDB_Print2("Received        Registers",0,0);
	}

// Return last signal value
void DoGDBCommand_qm(NiosGDBGlobals *g)
	{
	char *w;

	w = g->textBuffer;

	*w++ = 'S';
	*w++ = '2';
	*w++ = '3';	// make up a signal for now...
	*w++ = 0;
	PutGDBPacket(g->textBuffer);
	}

void DoGDBCommand_q(NiosGDBGlobals *g)
{
#ifdef na_ssram_detect_in
	short int* ssram_exists;
#endif
	char *w;
	w = g->textBuffer;

	w++;	/* skip past the q */
	switch (*w) {
		case ('A'):
			w = g->textBuffer;

			/* handle intialization information */
			/* is nios_ocd available? */
#ifdef nasys_debug_core
			*w++ = nasys_debug_core + '0';
#else
			*w++ = '0';
#endif
			*w++ = ',';

			/* determine if the SSRAM debugger board is
			 * physically present */
#ifdef na_ssram_detect_in
			ssram_exists = (short int*) na_ssram_detect_in;
			*w++ = !(*ssram_exists) + '0';
#else
			*w++ = '0';
#endif
			*w++ = ',';

			/* print out the max size of a trace packet */
#if nasys_debug_core
			sprintf (w, "%04x", MAX_TRACE_BYTES);
#else
			sprintf (w, "0000");
#endif

			break;
		case ('B'):
			w = g->textBuffer;

			/* returns 1 if it was an OCD interrupt
			 * returns 0 if it was software breakpoint */
			if (gdb.trapNumber == nasys_debug_core_irq) {
				*w++ = '1';
			} else {
				*w++ = '0';
			}

			*w++ = 0;
			break;
		default:
			w = g->textBuffer;

			*w = 0;
			break;
	}

	PutGDBPacket(g->textBuffer);
}


void GDBInsertBreakpoint(NiosGDBGlobals *g,short *address)
	{
	NiosGDBBreakpoint *b;

	GDB_Print2("breakpoint 0x%x",(int)address,0);
	if(g->breakpointCount < kMaximumBreakpoints)
		{
		b = &g->breakpoint[g->breakpointCount++];
		b->address = address;
		b->oldContents = *b->address;
		*b->address = 0x7904;
		}
	}

void GDBRemoveBreakpoints(NiosGDBGlobals *g)
	{
	NiosGDBBreakpoint *b;
	int i;

	for(i = 0; i < g->breakpointCount; i++)
		{
		b = &g->breakpoint[i];
		*b->address = b->oldContents;
		b->address = 0;
		}

	g->breakpointCount = 0;
	}

int NiosInstructionIsTrap5(unsigned short instruction)
	{
	return instruction == 0x7905;
	}

int NiosInstructionIsPrefix(unsigned short instruction)
	{
	return (instruction >> 11) == 0x13;
	}

int NiosInstructionIsSkip(unsigned short instruction)
	{
	int op6;
	int op11;

	op6 = (instruction >> 10);
	op11 = (instruction >> 5);

	return (op6 == 0x14		// SKP0
		|| op6 == 0x15		// SKP1
		|| op11 == 0x3f6	// SKPRz
		|| op11 == 0x3f7	// SKPS
		|| op11 == 0x3fa);	// SKPRnz
	}

int NiosInstructionIsBranch(unsigned short instruction,short *pc,short **branchTargetOut)
	{
	int op4;
	int op7;
	int op10;
	short *branchTarget = 0;
	int result = 0;

	op4 = (instruction >> 12);
	op7 = (instruction >> 9);
	op10 = (instruction >> 6);

	if(op4 == 0x08)		// BR, BSR
		{
		int offset;

		result = 1;
		offset = instruction & 0x07ff;
		if(offset & 0x400)	// sign extend
			offset |= 0xffffF800;
		branchTarget = pc + offset + 1;	// short * gets x2 scaling automatically
		}
	else if(op10 == 0x1ff)	// JMP, CALL
		{
		result = 1;
		branchTarget = (short *)(gdb.registers.r[instruction & 31] * 2);
		}
	else if(op7 == 0x3d)	// JMPC, CALLC
		{
		result = 1;
		branchTarget = pc + 1 + (instruction & 0x0ffff);
#ifdef __nios32__
		branchTarget = (short *)((int)branchTarget & 0xffffFFFc);	// align 32...
#else
		branchTarget = (short *)((int)branchTarget & 0xFFFe);		// align 16...
#endif
		branchTarget = (short *)(*(int *)branchTarget);
		}

	if(branchTargetOut)
		*branchTargetOut = branchTarget;

	return result;
	}

// -------------------------
// Step at address
//
// "stepping" involves inserting a
// breakpoint at some reasonable
// spot later than the current program
// counter
//
// On the Nios processor, this is
// nontrivial. For example, we should
// not break up a PFX instruction.

void DoGDBCommand_s(NiosGDBGlobals *g)
	{
	char *w;
	int x;
	short *pc;
	short *branchTarget;
	unsigned short instruction;
	int stepType;

	/*
	 * First, if there's an argument to the packet,
	 * set the new program-counter value
	 */

	w = g->textBuffer;
	w++;
	if(HexCharToValue(*w) >= 0)
		{
		w = Hex2Value(w,&x);
		g->registers.pc = x;
		}

	/*
	 * Scan forward to see what the
	 * most appropriate location(s) for
	 * a breakpoint will be.
	 *
	 * The rules are:
	 *  1. If *pc == PFX, break after modified instruction.
	 *  2. If *pc == BR,BSR,JMP,CALL, break at destination
	 *  3. If *pc == SKIP, break right after SKIP AND after optional instruction,
	                 which might, of course, be prefixed.
	 *  4. Anything else, just drop in the breakpoint.
	 */

	pc = (short *)(int)g->registers.pc;

	instruction = *pc;
	stepType = 0;

	if(NiosInstructionIsPrefix(instruction))
		{
		/*
		 * PFX instruction: skip til after it
		 */
		while(NiosInstructionIsPrefix(instruction))
			{
			pc++;
			instruction = *pc;
			}

		GDBInsertBreakpoint(g,pc + 1);
		stepType = 1;
		}
	else if(NiosInstructionIsBranch(instruction,pc,&branchTarget))
		{
		GDBInsertBreakpoint(g,branchTarget);
		stepType = 2;
		}
	else if(NiosInstructionIsSkip(instruction))
		{
		short *pc2;
		stepType = 3;

		/*
		 * Skip gets to breaks: one after the skippable instruction,
		 * and the skippable instruction itself.
		 *
		 * Since Skips know how to skip over PFX's, we have to, too.
		 */
		pc2 = pc;	// the Skip instruction
		do
			{
			pc2++;
			} while(NiosInstructionIsPrefix(*pc2));
		// pc2 now points to first non-PFX after Skip
		GDBInsertBreakpoint(g,pc2+1);
		GDBInsertBreakpoint(g,pc+1);
		}
	else
		GDBInsertBreakpoint(g,pc+1);		// the genericest case

	GDB_Print2("Program Steppingat 0x%x (%d)",g->registers.pc,stepType);
	}

// -----------------------------
// Continue at address

void DoGDBCommand_c(NiosGDBGlobals *g)
	{
	char *w;
	int x;
	w = g->textBuffer;

	w++;		// past command

	// Anything in the packet? if so,
	// use it to set the PC value

	if(HexCharToValue(*w) >= 0)
		{
		w = Hex2Value(w,&x);
		g->registers.pc = x;
		}

	GDB_Print2("Program Running at 0x%x",g->registers.pc,0);
	}

// ----------------------
// Kill

void DoGDBCommand_k(NiosGDBGlobals *g)
	{
	return;
	}


/*
 * If we've somehow skidded
 * to a stop just after a PFX instruction
 * back up the program counter by one.
 *
 * That way, we can't end up with an accidentally-unprefixed
 * instruction.
 *
 * We do this just before we begin running
 * again, so that when the host queries our
 * registers, we report the place we actually
 * stopped.
 */

void MaybeAdjustProgramCounter(NiosGDBGlobals *g)
	{
	short instruction;
	if(g->registers.pc)
		{
		instruction = *(short *)(int)(g->registers.pc - 2);
		if(NiosInstructionIsPrefix(instruction))
			g->registers.pc -= 2;
		else
			{
			// If the *current* instruction is Trap5, we must skip it!
			instruction = *(short *)(int)(g->registers.pc);
			if(NiosInstructionIsTrap5(instruction))
				g->registers.pc += 2;
			}
		}
	}

/*
 * GDBMainLoop - this is the main processing loop
 * for the GDB stub.
 */
void GDBMainLoop (void)
{
	while(1)
	{
		if (GetGDBPacket(gdb.textBuffer) > 0)
		{

			GDB_Print2(gdb.textBuffer,0,0);
			switch(gdb.textBuffer[0])
			{
			case 's':
				DoGDBCommand_s(&gdb);
				goto startRunning;
				break;

			case 'c':	// continue
				DoGDBCommand_c(&gdb);

				// if the PC is something other than 0, it's
				// probably ok to exit and go there

			startRunning:
				if(gdb.registers.pc)
				{
					MaybeAdjustProgramCounter(&gdb);
					return;
				}
				break;

			case 'm':	// memory read
				DoGDBCommand_m(gdb.textBuffer);
				break;

			case 'M':	// memory set
				DoGDBCommand_M(gdb.textBuffer);
				break;

			case 'g':	// registers read
				DoGDBCommand_g(&gdb);
				break;

			case 'G':	//registers set
				DoGDBCommand_G(&gdb);
				break;

			case 'k':	//kill process
				DoGDBCommand_k(&gdb);
				break;

			case '?':	// last exception value
				DoGDBCommand_qm(&gdb);
				break;

			case 'q':
				DoGDBCommand_q(&gdb);
				break;

			default:	// return empty packet, means "yeah yeah".
				gdb.textBuffer[0] = 0;
				PutGDBPacket(gdb.textBuffer);
			break;
			}
		}
	}

}

// ----------main------------
void GDBMain(void)
{
	int i;

	for(i = 0; i < kTextBufferSize; i++)
		gdb.textBuffer[i] = i;

	GDBRemoveBreakpoints(&gdb);

#ifdef __KERNEL__
/*
 * Inform the user that they need to add the symbol file for the application
 * that is just starting up.  Display the  .text  .data  .bss  regions.
 */
	if (gdb.trapNumber == 5) {
		extern struct task_struct *_current_task;
		sprintf(gdb.textBuffer,
				"\r\n\nGDB: trap 5 at 0x%08lX", gdb.registers.pc);
		puts(gdb.textBuffer);
		if (_current_task) {
			if ( _current_task->mm->start_code > _etext )
				sprintf(gdb.textBuffer,
					"\r\nGDB: Enter the following command in the nios-elf-gdb Console Window:"
					"\r\nGDB:    add-symbol-file %s.abself 0x%08lX 0x%08lX 0x%08lX\r\n\n",
					_current_task->comm,
					(unsigned long)_current_task->mm->start_code,
					(unsigned long)_current_task->mm->start_data,
					(unsigned long)_current_task->mm->end_data );
			else
				sprintf(gdb.textBuffer,
					", kernel process: %s\r\n", _current_task->comm );
		} else
			sprintf(gdb.textBuffer,
				", kernel process unknown\r\n" );
		puts(gdb.textBuffer);
	}
#endif

	// Send trapnumber for breakpoint encountered. No other signals.

	gdb.textBuffer[0] = 'S';
	gdb.textBuffer[1] = '0';

#if nasys_debug_core
	if (gdb.trapNumber == nasys_debug_core_irq)
	{
	    /* gdb.textBuffer[2] = '8'; */
	    gdb.textBuffer[2] = '5';
	}
	else
	{
	    gdb.textBuffer[2] = '5';
	}
#else
	gdb.textBuffer[2] = '5';
#endif
	gdb.textBuffer[3] = 0;
	PutGDBPacket(gdb.textBuffer);

	GDB_Print2("Trap %2d         At 0x%x",
		gdb.trapNumber,gdb.registers.pc);
//	printf ("Trap %d at 0x%x\n",gdb.trapNumber,gdb.registers.pc);
//	for (i=0;i<32;i++) printf ("	register[%d] = 0x%x\n",i,gdb.registers.r[i]);

	GDBMainLoop ();
}

// +----------------------------------
// | gdb_eth_proc -- gets called for udp packets
// | from the host bound for gdb stub
#ifdef ETHER_DEBUG
#ifdef ethernet_exists
int gdb_eth_proc(int plug_handle,
		void *context,
		ns_plugs_packet *p,
		void *payload,
		int payload_length)
{
	int i;
	char *buf = (char *)payload;
	// if this is a stop request, set a flag to stop after nr_plugs_idle
	// leave it up to the host to prevent stops from being sent while stub is running???

	if (*buf == 3) gdb.stop = 1;

	// if we're waiting for an ack, check that here
	if (gdb.ACKstatus == ne_gdb_ack_waiting)
	{
		if (buf[0] == '+')
		{
			gdb.ACKstatus = ne_gdb_ack_acked;
			return 0;
		}
		else if (buf[0] == '-')
		{
			gdb.ACKstatus = ne_gdb_ack_nacked;
			return 0;
		}
	}
	strcpy (gdb.textBuffer, buf);	//all commands should be zero terminated strings

	gdb.textBuffer[payload_length] = 0;	//terminate string

	gdb.host_ip_address=((ns_plugs_ip_packet *)(p[ne_plugs_ip].header))->source_ip_address;
	gdb.host_port_number=((ns_plugs_udp_packet *)(p[ne_plugs_udp].header))->source_port;

	return 0;
}

int nr_dbg_plugs_idle (void)
{
	int result;

	result = nr_plugs_idle ();
	if (gdb.stop)
	{
		gdb.stop = 0;
//;dgt2;tmp;		asm ("TRAP #5");
	}
	return result;
}
#endif
#endif


/*
 * int main(void)
 *
 * All we really do here is install our trap # 3,
 * and call it once, so that we're living down in
 * the GDBMain, trap handler.
 */

extern int StubBreakpointHandler;
extern int StubHarmlessHandler;
#if nasys_debug_core
extern int StubHWBreakpointHandler;
#endif
#ifdef nasys_debug_uart
extern int StubUartHandler;
#endif

void gdb_local_install(int active)
{
	unsigned int *vectorTable;
	unsigned int stubBreakpointHandler;
	unsigned int stubHarmlessHandler;
#if nasys_debug_core
	unsigned int stubHWBreakpointHandler;
#endif

	gdb.breakpointCount = 0;
	gdb.textBuffer[0] = 0;

	vectorTable = (int *)nasys_vector_table;
	stubBreakpointHandler = ( (unsigned int)(&StubBreakpointHandler) ) >> 1;
	stubHarmlessHandler = ( (unsigned int)(&StubHarmlessHandler) ) >> 1;
#if nasys_debug_core
	stubHWBreakpointHandler = ( (unsigned int)(&StubHWBreakpointHandler) ) >> 1;
#endif

	/*
	 * Breakpoint & single step both go here
	 */
	vectorTable[na_BreakpointTrap] = stubBreakpointHandler;
	vectorTable[na_SingleStepTrap] = stubBreakpointHandler;
	vectorTable[na_StartGDBTrap] = active ? stubBreakpointHandler : stubHarmlessHandler;
	/*
	 * If it exists, Hardware Breakpoint has a different entry point
	 */
#if nasys_debug_core
	vectorTable[na_debug_peripheral_irq] = stubHWBreakpointHandler;
#endif

#ifndef __KERNEL__
#ifdef nasys_debug_uart
	if (gdb.comlink == ne_gdb_serial)
	{
		np_uart *uart = (np_uart *)nasys_debug_uart;
		unsigned int stubUartHandler = ((unsigned int)(&StubUartHandler)) >> 1;

		vectorTable[nasys_debug_uart_irq] = stubUartHandler;	  //set Uart int vector
		uart->np_uartcontrol = np_uartcontrol_irrdy_mask; //enable Rx intr
	}
#endif
#endif
}

void nios_gdb_install(int active)
{
	gdb.comlink = ne_gdb_serial;
	gdb_local_install (active);
}

#ifdef ETHER_DEBUG
#ifdef ethernet_exists
void nios_gdb_install_ethernet (int active)
{
	int result;
	host_16	host_port = GDB_ETH_PORT;

	gdb.comlink = ne_gdb_ethernet;
	gdb_local_install (active);

	result = nr_plugs_create (&gdb.gdb_eth_plug, ne_plugs_udp, host_port, gdb_eth_proc, 0, 0);
	//if unabled to open ethernet plug, switch back to default serial interface
	if (result)
	{
		printf ("nr_plugs_create failed %d\n",result);
		gdb.comlink = ne_gdb_serial;
		return;
	}
	result = nr_plugs_connect (gdb.gdb_eth_plug, 0, -1, -1);
	if (result)
	{
		printf ("nr_plugs_connect fialed %d\n",result);
		gdb.comlink = ne_gdb_serial;
		return;
	}
}
#endif
#endif

#ifdef nios_gdb_breakpoint
	#undef nios_gdb_breakpoint
#endif

void nios_gdb_breakpoint(void)
	{
	/*
	 * If you arrived here, you didn't include
	 * the file "nios_peripherals.h", which
	 * defines nios_gdb_breakpoint as a
	 * macro that expands to TRAP 5.
	 *
	 * (No problem, you can step out
	 * of this routine.)
	 */
//;dgt2;tmp;	asm("TRAP 5");
	}

// end of file
