#ifndef __LINUX_LEDMAN_H__
#define __LINUX_LEDMAN_H__ 1
/****************************************************************************/
/*
 *	ledman.h:  LED manager header,  generic, device indepedant LED stuff
 *
 *	defines for led functionality which may/may not be implemented by the
 *	currently active LED configuration
 *
 *	NOTE: do not change the numbering of the defines below,  tables of
 *	      LED patterns rely on these values
 */

#include <linux/version.h>

#define LEDMAN_ALL			0	/* special case, all LED's */

#define LEDMAN_POWER		1
#define LEDMAN_HEARTBEAT	2
#define LEDMAN_COM1_RX		3
#define LEDMAN_COM1_TX		4
#define LEDMAN_COM2_RX		5
#define LEDMAN_COM2_TX		6
#define LEDMAN_LAN1_RX		7
#define LEDMAN_LAN1_TX		8
#define LEDMAN_LAN2_RX		9
#define LEDMAN_LAN2_TX		10
#define LEDMAN_USB1_RX		11
#define LEDMAN_USB1_TX		12
#define LEDMAN_USB2_RX		13
#define LEDMAN_USB2_TX		14
#define LEDMAN_NVRAM_1		15
#define LEDMAN_NVRAM_2		16
#define LEDMAN_VPN			17
#define LEDMAN_LAN1_DHCP	18
#define LEDMAN_LAN2_DHCP	19
#define LEDMAN_COM1_DCD		20
#define LEDMAN_COM2_DCD		21
#define LEDMAN_ONLINE		22
#define LEDMAN_LAN1_LINK	23
#define LEDMAN_LAN2_LINK	24
#define LEDMAN_VPN_RX		25
#define LEDMAN_VPN_TX		26
#define LEDMAN_RESET		27
#define LEDMAN_STATIC		28
#define LEDMAN_LAN3_RX		29
#define LEDMAN_LAN3_TX		30
#define LEDMAN_LAN3_LINK	31
#define LEDMAN_LAN3_DHCP	32
#define LEDMAN_FAILOVER		33
#define LEDMAN_HIGHAVAIL	34
#define	LEDMAN_MAX			35	/* one more than the highest LED above */

#define LEDMAN_MAX_NAME		16

/****************************************************************************/
/*
 *	ioctl cmds
 */

#define LEDMAN_CMD_SET		0x01	/* turn on briefly to show activity */
#define LEDMAN_CMD_ON		0x02	/* turn LED on permanently */
#define LEDMAN_CMD_OFF		0x03	/* turn LED off permanently */
#define LEDMAN_CMD_FLASH	0x04	/* flash this LED */
#define LEDMAN_CMD_RESET	0x05	/* reset LED to default behaviour */

#define LEDMAN_CMD_ALT_ON	0x06	/* LED is being used for non-std reasons */
#define LEDMAN_CMD_ALT_OFF	0x07	/* LED is being used for std reasons */

#define LEDMAN_CMD_MODE			0x80	/* set LED to named mode (led=char *) */
#define LEDMAN_CMD_STARTTIMER	0x81	/* enable and init the ledman driver */
#define LEDMAN_CMD_KILLTIMER	0x82	/* disable the ledman driver */
 
#define	LEDMAN_CMD_SIGNAL		0x100	/* pid of process to signal on reset */

#define LEDMAN_CMD_ALTBIT		0x8000	/* operate on alternate LED settings */

/****************************************************************************/

#define LEDMAN_MAJOR	126

/****************************************************************************/
#ifdef __KERNEL__

#if LINUX_VERSION_CODE < 0x020100
extern int ledman_init(void);
#endif

extern int  ledman_setup(char *arg);
extern void ledman_killtimer(void);
extern void ledman_starttimer(void);
extern int  ledman_cmd(int cmd, unsigned long led);
extern void ledman_signalreset(void);

#else

#include	<fcntl.h>

#define ledman_cmd(cmd, led) ({ \
	int fd; \
	if ((fd = open("/dev/ledman", O_RDWR)) != -1) { \
		ioctl(fd, cmd, led); \
		close(fd); \
	} \
})

#endif
/****************************************************************************/
#endif /* __LINUX_LEDMAN_H__ */
