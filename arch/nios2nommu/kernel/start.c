/*--------------------------------------------------------------------
 *
 * arch/nios2nommu/kernel/start.c
 *
 * Derived from various works, Alpha, ix86, M68K, Sparc, ...et al
 *
 * Copyright (C) 2004   Microtronix Datacom Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * Jan/20/2004		dgt	    NiosII
 * May/20/2005		dgt	    Altera NiosII Custom shift instr(s)
 *                           possibly assumed by memcpy, etc; ensure
 *                           "correct" core loaded therefore if so.
 *
 ---------------------------------------------------------------------*/


#include <asm/system.h>
#include <asm/nios.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/time.h>

  #ifdef CONFIG_SERIAL_AJUART                       //;dgt;20may05;
    #ifdef CONFIG_SERIAL_AJUART_CONSOLE             //;dgt;20may05;

      #include <linux/console.h>                    //;dgt;20may05;
      #include <asm/altera_juart.h>                 //;dgt;20may05;

      extern struct console juart_console;          //;dgt;20may05;

    #endif    // CONFIG_SERIAL_AJUART               //;dgt;20may05;
  #endif    // CONFIG_SERIAL_AJUART_CONSOLE         //;dgt;20may05;

//;dgt;20may05; #ifdef CONFIG_CRC_CHECK

//  #if defined(CONFIG_NIOS_SERIAL)                 //;dgt;20may05;
//    #if defined(CONFIG_NIOS_SERIAL_CONSOLE)       //;dgt;20may05;
        #if defined(nasys_printf_uart)              //;dgt;20may05;
          static void putsNoNewLine( unsigned char *s )
          {
            while(*s) {
                while (!(nasys_printf_uart->np_uartstatus &
                         np_uartstatus_trdy_mask));
                nasys_printf_uart->np_uarttxdata = *s++;
            }
          }

          #define NL "\r\n"
          static void puts(unsigned char *s)
          {
            putsNoNewLine( s );
            putsNoNewLine( NL );
          }
        #endif  // nasys_printf_uart                //;dgt;20may05;
//    #endif  // CONFIG_NIOS_SERIAL_CONSOLE)        //;dgt;20may05;
//  #endif  // CONFIG_NIOS_SERIAL)                  //;dgt;20may05;

#ifdef CONFIG_CRC_CHECK                             //;dgt;20may05;

#if 1
#define outchar(X) { \
		while (!(nasys_printf_uart->np_uartstatus & np_uartstatus_trdy_mask)); \
		nasys_printf_uart->np_uarttxdata = (X); }
#else
#define outchar(X) putchar(X)
#endif
#define outhex(X,Y) { \
		unsigned long __w; \
		__w = ((X) >> (Y)) & 0xf; \
		__w = __w > 0x9 ? 'A' + __w - 0xa : '0' + __w; \
		outchar(__w); }
#define outhex8(X) { \
		outhex(X,4); \
		outhex(X,0); }
#define outhex16(X) { \
		outhex(X,12); \
		outhex(X,8); \
		outhex(X,4); \
		outhex(X,0); }
#define outhex32(X) { \
		outhex(X,28); \
		outhex(X,24); \
		outhex(X,20); \
		outhex(X,16); \
		outhex(X,12); \
		outhex(X,8); \
		outhex(X,4); \
		outhex(X,0); }
#endif

#if 0
static unsigned long testvar = 0xdeadbeef;
#endif
	
#ifdef CONFIG_CRC_CHECK


/******************************************************/


extern unsigned long __CRC_Table_Begin;

typedef unsigned char  U8;
typedef unsigned long  U32;

/* Table of CRC-32's of all single byte values */
const U32 crc_32_tab[] = {
   0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
   0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
   0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
   0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
   0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
   0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
   0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
   0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
   0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
   0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
   0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
   0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
   0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
   0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
   0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
   0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
   0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
   0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
   0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
   0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
   0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
   0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
   0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
   0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
   0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
   0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
   0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
   0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
   0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
   0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
   0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
   0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
   0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
   0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
   0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
   0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
   0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
   0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
   0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
   0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
   0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
   0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
   0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
   0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
   0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
   0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
   0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
   0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
   0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
   0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
   0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
   0x2d02ef8dL
};

U32 Calc_CRC( const U8 *p, U32 len )
{
    U32 crc = (U32)~0L;
    while (len--)
       crc = crc_32_tab[0xFF & (crc ^ *p++)] ^ (crc >> 8);
   
    return crc ^ (U32)~0L;
}



/******************************************************/


/* hjz: Following time stuff is hacked and modified from uC-libc (various files), which in turn was... */
/* This is adapted from glibc */
/* Copyright (C) 1991, 1993 Free Software Foundation, Inc */

#define SECS_PER_HOUR 3600L
#define SECS_PER_DAY  86400L
typedef unsigned long time_t;


static const unsigned short int __mon_lengths[2][12] = {
	/* Normal years.  */
	{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
	/* Leap years.  */
	{31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
};
/* This global is exported to the wide world in keeping
 * with the interface in time.h */
long int timezone = 0;

static const char *dayOfWeek[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *month[]     = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

/* Nonzero if YEAR is a leap year (every 4 years,
   except every 100th isn't, and every 400th is).  */
# define __isleap(year)	((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))

struct tm
{
  int tm_sec;			/* Seconds.	[0-60] (1 leap second) */
  int tm_min;			/* Minutes.	[0-59] */
  int tm_hour;			/* Hours.	[0-23] */
  int tm_mday;			/* Day.		[1-31] */
  int tm_mon;			/* Month.	[0-11] */
  int tm_year;			/* Year	- 1900.  */
  int tm_wday;			/* Day of week.	[0-6] */
  int tm_yday;			/* Days in year.[0-365]	*/
  int tm_isdst;			/* DST.		[-1/0/1]*/

# ifdef	__USE_BSD
  long int tm_gmtoff;		/* Seconds east of UTC.  */
  __const char *tm_zone;	/* Timezone abbreviation.  */
# else
  long int __tm_gmtoff;		/* Seconds east of UTC.  */
  __const char *__tm_zone;	/* Timezone abbreviation.  */
# endif
};

void  __tm_conv(struct tm *tmbuf, time_t *t, time_t offset)
{
	long days, rem;
	register int y;
	register const unsigned short int *ip;

	timezone = -offset;

	days = *t / SECS_PER_DAY;
	rem = *t % SECS_PER_DAY;
	rem += offset;
	while (rem < 0)
        {
	    rem += SECS_PER_DAY;
	    days--;
	}
	while (rem >= SECS_PER_DAY)
        {
	    rem -= SECS_PER_DAY;
	    days++;
	}

	tmbuf->tm_hour = rem / SECS_PER_HOUR;
	rem           %= SECS_PER_HOUR;
	tmbuf->tm_min  = rem / 60;
	tmbuf->tm_sec  = rem % 60;

	/* January 1, 1970 was a Thursday.  */
	tmbuf->tm_wday = (4 + days) % 7;
	if (tmbuf->tm_wday < 0)
	    tmbuf->tm_wday += 7;

	y = 1970;
	while (days >= (rem = __isleap(y) ? 366 : 365))
        {
	    y++;
	    days -= rem;
	}

	while (days < 0)
        {
	    y--;
	    days += __isleap(y) ? 366 : 365;
	}

	tmbuf->tm_year = y - 1900;
	tmbuf->tm_yday = days;

	ip = __mon_lengths[__isleap(y)];
	for (y = 0; days >= ip[y]; ++y)
	    days -= ip[y];

	tmbuf->tm_mon   = y;
	tmbuf->tm_mday  = days + 1;
	tmbuf->tm_isdst = -1;
}



/* hjz: NOT your traditional ctime: This one includes timezone */
/* (UTC) and excludes the traditional trailing newline.        */
char *CTime( time_t *t )
{
    static char theTime[29];
    struct tm tm;

    __tm_conv( &tm, t, 0 );
    sprintf( theTime, "%s %s %02d %02d:%02d:%02d UTC %04d",
             dayOfWeek[tm.tm_wday], month[tm.tm_mon], tm.tm_mday,
	     tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_year + 1900 );

    return theTime;
}

/******************************************************/


/* hjz: polled-I/O: Get a char if one is ready, or return -1 */
int getc( void )
{
    if ( nasys_printf_uart->np_uartstatus & np_uartstatus_rrdy_mask )
        return nasys_printf_uart->np_uartrxdata;
    else
      return -1;
}


typedef unsigned long off_t;
typedef struct
{
    U8    *startAddr;
    U8    *endAddr;
    U32    CRC;
    time_t mtime;
    off_t  size;    // File size
    char id[44];    // Filename. If path exceeds available size, name is "..." + last 40 chars of given filename
    char host[32];  // hostname. If name exceeds available size name is first 28 chars of hostname + "..."
} FLASH_REGION_DESC;


int  Test_Flash_Regions(void)
{
    FLASH_REGION_DESC *pRegion = (FLASH_REGION_DESC *)&__CRC_Table_Begin;
    U32  crc;
    char cBuff[256];
    int  nrFailedRegions = 0;
    int  regionStatus;
    int  i;
    unsigned int startAddr = (int) pRegion->startAddr;
    unsigned int endAddr = (int) pRegion->endAddr;

    puts( "***Checking flash CRC's" );
    if ( (startAddr == -1) || (startAddr >= endAddr) 
     || !( ((startAddr >= (int) NIOS_FLASH_START) && (endAddr < (int) NIOS_FLASH_END))
           || ((startAddr >= (int) na_flash)     && (endAddr < (int) na_flash_end)) ) )
    {
	puts( "   No Flash regions defined." );
        return -1;
    }


    for ( i = 0; pRegion->startAddr && pRegion->startAddr != (U8 *)~0L; pRegion++, i++ )
    {
        crc = Calc_CRC( pRegion->startAddr, pRegion->endAddr - pRegion->startAddr );
	if ( crc != pRegion->CRC )
	{
	    regionStatus = 1;
	    nrFailedRegions++;
	}
	else
	    regionStatus = 0;

        sprintf( cBuff, "   Region %d: 0x%08lX - 0x%08lX, CRC = 0x%08lX --> %s" NL
                        "        From file `%s' on host `%s'" NL
		        "        Dated %s, size = %lu bytes",
                 i, (U32)pRegion->startAddr, (U32)pRegion->endAddr, pRegion->CRC,
		 regionStatus ? "***Failed" : "Passed",
                 pRegion->id, pRegion->host, CTime( &pRegion->mtime ), pRegion->size
               );
	puts( cBuff );
    }

    return nrFailedRegions;
}
#endif /* CONFIG_CRC_CHECK */


int main(void) {
	extern void start_kernel(void);

#if 0
	//	extern unsigned long __data_rom_start;
	extern unsigned long __data_start;
	extern unsigned long __data_end;
	extern unsigned long __bss_start;
	extern unsigned long __bss_end;

	unsigned long *src;
	unsigned long *dest;
	unsigned long tmp;
#endif


#ifdef DEBUG
	puts("MAIN: starting c\n");
#endif

#ifdef CONFIG_KGDB				/* builtin GDB stub */

/* Set up GDB stub, and make the first trap into it */
	nios_gdb_install(1); 
#ifdef CONFIG_BREAK_ON_START
	puts( "MAIN: trapping to debugger - make sure nios-elf-gdb is running on host." );
	nios_gdb_breakpoint(); 
	nop();
#endif

#endif	/* CONFIG_KGDB */


#if 0
	puts("Testing RAM\n");

	puts("Write...\n");
	for (dest = (unsigned long *)0x40000000; dest < (unsigned long *)0x40080000; dest++) {
		*dest = (unsigned long)0x5a5a5a5a ^ (unsigned long)dest;
	}
 
	puts("Read...\n");
	for (dest = (unsigned long *)0x40000000; dest < (unsigned long *)0x40080000; dest++) {
		tmp = (unsigned long)0x5a5a5a5a ^ (unsigned long)dest;
		if (*dest != tmp) {
			puts("Failed.");
			outhex32((unsigned long)dest);
			puts("is");
			outhex32(*dest);
			puts("wrote");
			outhex32(tmp);
			while(1);
		}
	}
 
	puts("512k RAM\n");
	if (testvar == 0xdeadbeef) puts("Found my key\n");
	else puts("My keys are missing!\n");

	//	src = &__data_rom_start;
	src = &__data_start;
	dest = &__data_start;
	while (dest < &__data_end) *(dest++) = *(src++);

	dest = &__bss_start;
	while (dest < &__bss_end) *(dest++) = 0;

	puts("Moved .data\n");
	if (testvar == 0xdeadbeef) puts("Found my key\n");
	else puts("My keys are missing!\n");

	testvar = 0;
#endif


#ifdef CONFIG_CRC_CHECK
    #ifdef CONFIG_PROMPT_ON_MISSING_CRC_TABLES
	if ( Test_Flash_Regions() )
    #else
	if ( Test_Flash_Regions() > 0 )
    #endif
	{
	    int  c;
            char tmp[3];
	    while ( getc() != -1 ) // flush input
	        ;

	    putsNoNewLine( "   Do you wish to continue (Y/N) ?  " );
	    while ( 1 )
	    {
	        c = getc();
		if ( c == -1 )
		    continue;

		if ( !isprint( c ) )
		    c = '?';

	        sprintf( tmp, "\b%c", c );
	        putsNoNewLine( tmp );
		c = toupper( c );
                if ( c == 'Y' )
		{
		    puts( "" );
		    break;
                }

		if ( c == 'N' )
		{
		    puts( NL "***Trapping to monitor..." );
		    return -1;
		}
	    }
        }
	puts( "***Starting kernel..." );

#endif

  // Altera NiosII Custom shift instr(s) possibly           //;dgt;
  //  assumed by memcpy, etc; ensure "correct" core         //;dgt;
  //  loaded therefore if so.                               //;dgt;

  #if defined(ALT_CI_ALIGN_32_N)                            //;dgt;
    if(ALT_CI_ALIGN_32(1, 0xA9876543,                       //;dgt;
                          0xB210FEDC)   != 0x10FEDCA9)      //;dgt;
      {                                                     //;dgt;
        goto badshiftci_label;                              //;dgt;
      }                                                     //;dgt;
    if(ALT_CI_ALIGN_32(2, 0xA9876543,                       //;dgt;
                          0xB210FEDC)   != 0xFEDCA987)      //;dgt;
      {                                                     //;dgt;
        goto badshiftci_label;                              //;dgt;
      }                                                     //;dgt;
    if(ALT_CI_ALIGN_32(3, 0xA9876543,                       //;dgt;
                          0xB210FEDC)   != 0xDCA98765)      //;dgt;
      {                                                     //;dgt;
        goto badshiftci_label;                              //;dgt;
      }                                                     //;dgt;
  #endif                                                    //;dgt;
    goto gudshiftci_label;                                  //;dgt;
badshiftci_label:                                           //;dgt;
    {                                                       //;dgt;
      unsigned char BadCImsg[]      =                       //;dgt;
            "?...ALT_CI_ALIGNn_321() NOT expected"          //;dgt;
            " NiosII custom instruction\n";                 //;dgt;
      unsigned char CIabortMsg[]    =                       //;dgt;
            " ...aborting uClinux startup...";              //;dgt;

      #ifdef CONFIG_SERIAL_AJUART                           //;dgt;
        #ifdef CONFIG_SERIAL_AJUART_CONSOLE                 //;dgt;
          juart_console.index = 0;                          //;dgt;
          jtaguart_console_write(&(juart_console),          //;dgt;
                                 BadCImsg,                  //;dgt;
                                 strlen(BadCImsg));         //;dgt;
          jtaguart_console_write(&(juart_console),          //;dgt;
                                 CIabortMsg,                //;dgt;
                                 strlen(CIabortMsg));       //;dgt;
        #endif    // CONFIG_SERIAL_AJUART                   //;dgt;
      #endif    // CONFIG_SERIAL_AJUART_CONSOLE             //;dgt;

//    #if defined(CONFIG_NIOS_SERIAL)                       //;dgt;
//      #if defined(CONFIG_NIOS_SERIAL_CONSOLE)             //;dgt;
          #if defined(nasys_printf_uart)                    //;dgt;
            puts(BadCImsg);                                 //;dgt;
            puts(CIabortMsg);                               //;dgt;
          #endif  // nasys_printf_uart                      //;dgt;
//      #endif  // CONFIG_NIOS_SERIAL_CONSOLE)              //;dgt;
//    #endif  // CONFIG_NIOS_SERIAL)                        //;dgt;

	  panic(" ...wrong fpga core?...");                     //;dgt;
    }                                                       //;dgt;

gudshiftci_label:                                           //;dgt;

	start_kernel();
	return 0;
}
