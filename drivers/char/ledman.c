/****************************************************************************/
/*	vi:set tabstop=4 cindent shiftwidth=4:
 *
 *	ledman.c -- An LED manager,  primarily,  but not limited to SnapGear
 *              devices manages up to 32 seperate LED at once.
 *	            Copyright (C) Lineo, 2000-2001.
 *	            Copyright (C) SnapGear, 2001-2003.
 *
 *	This driver currently supports 4 types of LED modes:
 *
 *	SET      - transient LED's that show activity,  cleared at next poll
 *	ON       - always ON
 *	OFF      - always OFF
 *  FLASHING - a blinking LED with the frequency determined by the poll func
 *
 *	We have two sets of LED's to support non-standard LED usage without
 *	losing previously/during use set of std values.
 *
 *	Hopefully for most cases, adding new HW with new LED patterns will be
 *	as simple as adding two tables, a small function and an entry in
 *	led_modes.  The tables being the map and the defaults while the
 *	function is the XXX_set function.
 *
 *	You can, however, add your own functions for XXX_bits, XXX_tick and
 *	take full control over all aspects of the LED's.
 */
/****************************************************************************/

#include <linux/version.h>
#include <linux/module.h>
#include <linux/utsname.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ledman.h>

#if LINUX_VERSION_CODE < 0x020300
#include <linux/malloc.h>
#else
#include <linux/slab.h>
#endif

#if LINUX_VERSION_CODE < 0x020100
#define INIT_RET_TYPE	int
#define Module_init(a)
#elif LINUX_VERSION_CODE < 0x020300
#include <linux/init.h>
#define INIT_RET_TYPE	int
#define Module_init(a)	module_init(a)
#else
#include <linux/init.h>
#define INIT_RET_TYPE	static int __init
#define Module_init(a)	module_init(a)
#endif

#if LINUX_VERSION_CODE < 0x020100
#define Get_user(a,b)	a = get_user(b)
#else
#include <asm/uaccess.h>
#define Get_user(a,b)	get_user(a,b)
#endif

#if LINUX_VERSION_CODE < 0x020100
static struct symbol_table ledman_syms = {
#include <linux/symtab_begin.h>
	X(ledman_cmd),
#include <linux/symtab_end.h>
};
#else
EXPORT_SYMBOL(ledman_cmd);
#endif

/****************************************************************************/

static void ledman_poll(unsigned long arg);
static int  ledman_ioctl(struct inode * inode, struct file * file,
								unsigned int cmd, unsigned long arg);
#if !defined(CONFIG_SH_KEYWEST) && !defined(CONFIG_SH_BIGSUR)
static int	ledman_bits(unsigned long cmd, unsigned long bits);
static void	ledman_tick(void);
#endif

/****************************************************************************/

static struct timer_list	ledman_timerlist;

/****************************************************************************/

struct file_operations ledman_fops = {
	.ioctl = ledman_ioctl,
};

/****************************************************************************/
/*
 *	some types to make adding new LED modes easier
 *
 *	First the elements for def array specifying default LED behaviour
 */

#define LEDS_SET	0
#define LEDS_ON		1
#define LEDS_OFF	2
#define LEDS_FLASH	3
#define LEDS_MAX	4

typedef unsigned long leddef_t[LEDS_MAX];

/*
 *	A LED map is a mapping from numbers in ledman.h to one or more
 *	physical LED bits.  Currently the typing limits us to 32 LED's
 *	though this would not be hard to change.
 */

typedef unsigned long ledmap_t[LEDMAN_MAX];

/*
 *	A LED mode is a definition of how a set of LED's should behave.
 *
 *	name    - a symbolic name for the LED mode,  used for changing modes
 *	map     - points to a ledmap array,  maps ledman.h defines to real LED bits
 *	def     - default behaviour for the LED bits (ie, on, flashing ...)
 *	bits    - perform command on physical bits,  you may use the default or
 *	          supply your own for more control.
 *	tick    - time based update of LED status,  used to clear SET LED's and
 *	          also for flashing LED's
 *	set     - set the real LED's to match the physical bits
 *	jiffies - how many clock ticks between runs of the tick routine.
 */


typedef struct {
	char	name[LEDMAN_MAX_NAME];
	u_long	*map;
	u_long	*def;
	int		(*bits)(unsigned long cmd, unsigned long led);
	void	(*tick)(void);
	void	(*set)(unsigned long led);
	int		jiffies;
} ledmode_t;

/****************************************************************************/

static int current_mode = 0;				/* the default LED mode */
static int initted = 0;

/*
 * We have two sets of LED's for transient operations like DHCP and so on
 * index 0 is the standard LED's and index 1 is the ALTBIT LED's
 */

static unsigned long leds_alt, leds_alt_cnt[32];
#if !defined(CONFIG_SH_KEYWEST) && !defined(CONFIG_SH_BIGSUR)
static unsigned long leds_set[2];
#endif
static unsigned long leds_on[2], leds_off[2], leds_flash[2];

static pid_t ledman_resetpid = -1;

/****************************************************************************/

/*
 *	Let the system specific defining begin
 */

#if defined(CONFIG_M586)
#define CONFIG_X86 1
#endif

#if defined(CONFIG_X86)
#if defined(CONFIG_MTD_SNAPGEODE)
#define	CONFIG_GEODE		1
#else
#define	CONFIG_AMDSC520		1
#endif
#if defined(CONFIG_SNAPGEAR)
static ledmap_t	nettel_old;
static leddef_t	nettel_def_old;
#endif
static ledmap_t	nettel_std;
static leddef_t	nettel_def;
static void nettel_set(unsigned long bits);
static void ledman_initarch(void);
#endif /* CONFIG_X86 */

#if defined(CONFIG_NETtel) && defined(CONFIG_M5307)
#ifdef ENTERASYS
static ledmap_t enterasys_std;
static leddef_t enterasys_def;
#endif
static ledmap_t	nettel_old;
static ledmap_t	nettel_new;
static leddef_t	nettel_def;
static void nettel_set(unsigned long bits);
#endif

#if defined(CONFIG_NETtel) && defined(CONFIG_M5272)
static ledmap_t	nt5272_std;
static leddef_t	nt5272_def;
static void nt5272_set(unsigned long bits);
#endif

#if defined(CONFIG_SE1100)
static ledmap_t	se1100_std;
static leddef_t	se1100_def;
static void se1100_set(unsigned long bits);
#endif

#if defined(CONFIG_GILBARCONAP) && defined(CONFIG_M5272)
static ledmap_t	nap5272_std;
static leddef_t	nap5272_def;
static void nap5272_set(unsigned long bits);
#endif

#if defined(CONFIG_AVNET5282)
static ledmap_t	ads5282_std;
static leddef_t	ads5282_def;
static void ads5282_set(unsigned long bits);
#endif

#if defined(CONFIG_SH_SECUREEDGE5410)
#include <asm/snapgear.h>
static ledmap_t	se5410_std;
static leddef_t	se5410_def;
static void se5410_set(unsigned long bits);
#endif

#if defined(CONFIG_NETtel) && defined(CONFIG_M5206e)
static ledmap_t	nt1500_std;
static leddef_t	nt1500_def;
static void nt1500_set(unsigned long bits);
#endif

#ifdef CONFIG_eLIA
static ledmap_t	elia_std;
static leddef_t	elia_def;
static void elia_set(unsigned long bits);
#endif

#if defined(CONFIG_SH_KEYWEST) || defined(CONFIG_SH_BIGSUR)
static ledmap_t	keywest_std;
static leddef_t	keywest_def;
static void keywest_set(unsigned long bits);
static void ledman_initkeywest(void);
static int	keywest_bits(unsigned long cmd, unsigned long bits);
static void	keywest_tick(void);
#endif

#if defined(CONFIG_MACH_MONTEJADE) || defined(CONFIG_MACH_IXDPG425) || \
	defined(CONFIG_MACH_SE5100)
static ledmap_t	montejade_std;
static leddef_t	montejade_def;
static void ledman_initarch(void);
static void montejade_set(unsigned long bits);
#endif

#if defined(CONFIG_ARCH_SE4000) || defined(CONFIG_MACH_ESS710) || \
    defined(CONFIG_MACH_SG560) || defined(CONFIG_MACH_SG580) || \
	defined(CONFIG_MACH_SG565) || defined(CONFIG_MACH_SG640) || \
    defined(CONFIG_MACH_SG720) || defined(CONFIG_MACH_SG590) || \
	defined(CONFIG_MACH_SG8100)
static ledmap_t	snapgear425_std;
static leddef_t	snapgear425_def;
static void ledman_initarch(void);
static void snapgear425_set(unsigned long bits);
#endif

#ifdef CONFIG_MACH_IVPN
static ledmap_t	ivpn_std;
static leddef_t	ivpn_def;
static void ledman_initarch(void);
static void ivpn_set(unsigned long bits);
#endif

#ifdef CONFIG_ARCH_KS8695
static ledmap_t	lite3_std;
static leddef_t	lite3_def;
static void ledman_initarch(void);
static void lite3_set(unsigned long bits);
#endif

#ifdef CONFIG_ARCH_EP9312
static ledmap_t	ipd_std;
static leddef_t	ipd_def;
static void ledman_initarch(void);
static void ipd_set(unsigned long bits);
#endif

/****************************************************************************/
/****************************************************************************/

#undef LT
#define LT	(((HZ) + 99) / 100)

/****************************************************************************/

ledmode_t led_mode[] = {

#ifdef ENTERASYS /* first in the list is the default */
	{ "enterasys", enterasys_std, enterasys_def, ledman_bits, ledman_tick, nettel_set, LT },
#endif

#if defined(CONFIG_X86)
	{ "std", nettel_std, nettel_def, ledman_bits, ledman_tick, nettel_set, LT },
#if defined(CONFIG_SNAPGEAR)
	{ "old", nettel_old, nettel_def_old, ledman_bits, ledman_tick, nettel_set, LT },
#endif
#endif

#if defined(CONFIG_NETtel) && defined(CONFIG_M5307)
	/*
	 * by default the first entry is used.  You can select the old-style
	 * LED patterns for acient boards with the command line parameter:
	 *
	 *      ledman=old
	 */
	{ "new", nettel_new, nettel_def, ledman_bits, ledman_tick, nettel_set, LT },
	{ "old", nettel_old, nettel_def, ledman_bits, ledman_tick, nettel_set, LT },
#endif

#if defined(CONFIG_NETtel) && defined(CONFIG_M5272)
	{ "std", nt5272_std, nt5272_def, ledman_bits, ledman_tick, nt5272_set, LT },
#endif

#if defined(CONFIG_NETtel) && defined(CONFIG_M5206e)
	{ "std", nt1500_std, nt1500_def, ledman_bits, ledman_tick, nt1500_set, LT },
#endif

#if defined(CONFIG_SE1100)
	{ "std", se1100_std, se1100_def, ledman_bits, ledman_tick, se1100_set, LT },
#endif

#if defined(CONFIG_GILBARCONAP) && defined(CONFIG_M5272)
	{ "std", nap5272_std, nap5272_def, ledman_bits, ledman_tick, nap5272_set, LT },
#endif

#if defined(CONFIG_SH_SECUREEDGE5410)
	{ "std", se5410_std, se5410_def, ledman_bits, ledman_tick, se5410_set, LT },
#endif

#ifdef CONFIG_eLIA
	{ "std", elia_std, elia_def, ledman_bits, ledman_tick, elia_set, LT },
#endif

#if defined(CONFIG_SH_KEYWEST) || defined(CONFIG_SH_BIGSUR)
	{ "std",keywest_std,keywest_def,keywest_bits,keywest_tick,keywest_set,HZ/10},
#endif

#if defined(CONFIG_MACH_MONTEJADE) || defined(CONFIG_MACH_IXDPG425) || \
	defined(CONFIG_MACH_SE5100)
	{ "std",montejade_std,montejade_def,ledman_bits,ledman_tick,montejade_set,LT},
#endif

#if defined(CONFIG_ARCH_SE4000) || defined(CONFIG_MACH_ESS710) || \
	defined(CONFIG_MACH_SG560) || defined(CONFIG_MACH_SG580) || \
	defined(CONFIG_MACH_SG565) || defined(CONFIG_MACH_SG640) || \
	defined(CONFIG_MACH_SG720) || defined(CONFIG_MACH_SG590) || \
	defined(CONFIG_MACH_SG8100)
	{ "std",snapgear425_std,snapgear425_def,ledman_bits,ledman_tick,snapgear425_set,LT},
#endif

#ifdef CONFIG_MACH_IVPN
	{ "std",ivpn_std,ivpn_def,ledman_bits,ledman_tick,ivpn_set,LT},
#endif

#if defined(CONFIG_ARCH_KS8695)
	{ "std",lite3_std,lite3_def,ledman_bits,ledman_tick,lite3_set,LT},
#endif

#if defined(CONFIG_ARCH_EP9312)
	{ "std", ipd_std, ipd_def, ledman_bits, ledman_tick, ipd_set, LT},
#endif

#if defined(CONFIG_AVNET5282)
	{ "std", ads5282_std, ads5282_def, ledman_bits, ledman_tick, ads5282_set, LT },
#endif

	{ "",  NULL, NULL, NULL }
};

/****************************************************************************/
/*
 *	boot arg processing ledman=mode
 */

#if LINUX_VERSION_CODE >= 0x020100
__setup("ledman=", ledman_setup);
#endif

int
ledman_setup(char *arg)
{
	ledman_cmd(LEDMAN_CMD_MODE, (unsigned long) arg);
	return(0);
}

/****************************************************************************/
/****************************************************************************/

INIT_RET_TYPE ledman_init(void)
{
	int expires;
	printk(KERN_INFO "ledman: Copyright (C) SnapGear, 2000-2003.\n");

	if (register_chrdev(LEDMAN_MAJOR, "nled",  &ledman_fops) < 0) {
		printk("%s(%d): ledman_init() can't get Major %d\n",
				__FILE__, __LINE__, LEDMAN_MAJOR);
		return(-EBUSY);
	} 

#if defined(CONFIG_SH_KEYWEST) || defined(CONFIG_SH_BIGSUR)
	ledman_initkeywest();
#endif

#if defined(CONFIG_X86) || defined(CONFIG_ARM)
	ledman_initarch();
#endif

/*
 *	set the LEDs up correctly at boot
 */
	ledman_cmd(LEDMAN_CMD_RESET, LEDMAN_ALL);
/*
 *	start the timer
 */
	init_timer(&ledman_timerlist);
	if (led_mode[current_mode].tick)
		expires = jiffies + led_mode[current_mode].jiffies;
	else
		expires = jiffies + HZ;
	ledman_timerlist.function = ledman_poll;
	ledman_timerlist.data = 0;
	mod_timer(&ledman_timerlist, expires);

#if LINUX_VERSION_CODE < 0x020100
	register_symtab(&ledman_syms);
#endif

	initted = 1;
	return 0;
}

Module_init(ledman_init);

/****************************************************************************/

void
ledman_killtimer(void)
{
/*
 *	stop the timer
 */
	del_timer(&ledman_timerlist);

/*
 *	set the LEDs up correctly at boot
 */
	ledman_cmd(LEDMAN_CMD_RESET, LEDMAN_ALL);
}

/****************************************************************************/

void
ledman_starttimer(void)
{
/*
 *	start the timer
 */
	mod_timer(&ledman_timerlist, jiffies + 1);

/*
 *	set the LEDs up correctly at boot
 */
	ledman_cmd(LEDMAN_CMD_RESET, LEDMAN_ALL);
}

/****************************************************************************/

static void
ledman_poll(unsigned long arg)
{
	int expires;
	if (led_mode[current_mode].tick) {
		(*led_mode[current_mode].tick)();
		expires = jiffies + led_mode[current_mode].jiffies;
	} else
		expires = jiffies + HZ;
	mod_timer(&ledman_timerlist, expires);
}

/****************************************************************************/

static int
ledman_ioctl(
	struct inode * inode,
	struct file * file,
	unsigned int cmd,
	unsigned long arg)
{
	char	mode[LEDMAN_MAX_NAME];
	int		i;

	if (cmd == LEDMAN_CMD_SIGNAL) {
		ledman_resetpid = current->pid;
		return(0);
	}

	if (cmd == LEDMAN_CMD_MODE) {
		for (i = 0; i < sizeof(mode) - 1; i++) {
			Get_user(mode[i], (char __user *) (arg + i));
			if (!mode[i])
				break;
		}
		mode[i] = '\0';
		arg = (unsigned long) &mode[0];
	}
	return(ledman_cmd(cmd, arg));
}

/****************************************************************************/
/*
 *	cmd - from ledman.h
 *	led - led code from ledman.h
 *
 *	check parameters and then call
 */

int
ledman_cmd(int cmd, unsigned long led)
{
	ledmode_t	*lmp;
	int			i;

	switch (cmd & ~LEDMAN_CMD_ALTBIT) {
	case LEDMAN_CMD_SET:
	case LEDMAN_CMD_ON:
	case LEDMAN_CMD_OFF:
	case LEDMAN_CMD_FLASH:
	case LEDMAN_CMD_RESET:
	case LEDMAN_CMD_ALT_ON:
	case LEDMAN_CMD_ALT_OFF:
		break;
	case LEDMAN_CMD_STARTTIMER:
		ledman_starttimer();
		return(0);
	case LEDMAN_CMD_KILLTIMER:
		ledman_killtimer();
		return(0);
	case LEDMAN_CMD_MODE:
		for (i = 0; led_mode[i].name[0]; i++)
			if (strcmp((char *) led, led_mode[i].name) == 0) {
				current_mode = i;
				if (initted)
					ledman_cmd(LEDMAN_CMD_RESET, LEDMAN_ALL);
				return(0);
			}
		return(-EINVAL);
	default:
		return(-EINVAL);
	}

	if (led < 0 || led >= LEDMAN_MAX)
		return(-EINVAL);

	lmp = &led_mode[current_mode];
	(*lmp->bits)(cmd, lmp->map[led]);
	return(0);
}

/****************************************************************************/
/*
 * Signal the reset pid, if we have one
 */

void
ledman_signalreset(void)
{
	static unsigned long firstjiffies = 0;
	if (ledman_resetpid == -1)
		return;
	if (jiffies > (firstjiffies + (HZ / 4))) {
		firstjiffies = jiffies;
		printk("LED: reset switch interrupt! (sending signal to pid=%d)\n",
					ledman_resetpid);
		kill_proc(ledman_resetpid, SIGUSR2, 1);
	}
}

/****************************************************************************/
#if !defined(CONFIG_SH_KEYWEST) && !defined(CONFIG_SH_BIGSUR)
/****************************************************************************/

static int
ledman_bits(unsigned long cmd, unsigned long bits)
{
	ledmode_t		*lmp = &led_mode[current_mode];
	int				 alt, i;
	unsigned long	 new_alt;

	alt = (cmd & LEDMAN_CMD_ALTBIT) ? 1 : 0;

	switch (cmd & ~LEDMAN_CMD_ALTBIT) {
	case LEDMAN_CMD_SET:
		leds_set[alt]   |= bits;
		break;
	case LEDMAN_CMD_ON:
		leds_on[alt]    |= bits;
		leds_off[alt]   &= ~bits;
		leds_flash[alt] &= ~bits;
		(*lmp->tick)();
		break;
	case LEDMAN_CMD_OFF:
		leds_on[alt]    &= ~bits;
		leds_off[alt]   |= bits;
		leds_flash[alt] &= ~bits;
		(*lmp->tick)();
		break;
	case LEDMAN_CMD_FLASH:
		leds_on[alt]    &= ~bits;
		leds_off[alt]   &= ~bits;
		leds_flash[alt] |= bits;
		break;
	case LEDMAN_CMD_RESET:
		leds_set[alt]   = (leds_set[alt]  &~bits) | (bits&lmp->def[LEDS_SET]);
		leds_on[alt]    = (leds_on[alt]   &~bits) | (bits&lmp->def[LEDS_ON]);
		leds_off[alt]   = (leds_off[alt]  &~bits) | (bits&lmp->def[LEDS_OFF]);
		leds_flash[alt] = (leds_flash[alt]&~bits) | (bits&lmp->def[LEDS_FLASH]);
		break;
	case LEDMAN_CMD_ALT_ON:
		new_alt = (bits & ~leds_alt);
		leds_alt |= bits;
		/*
		 * put any newly alt'd bits into a default state
		 */
		(*lmp->bits)(LEDMAN_CMD_RESET | LEDMAN_CMD_ALTBIT, new_alt);
		for (i = 0; i < 32; i++)
			if (bits & (1 << i))
				leds_alt_cnt[i]++;
		break;
	case LEDMAN_CMD_ALT_OFF:
		for (i = 0; i < 32; i++)
			if ((bits & (1 << i)) && leds_alt_cnt[i]) {
				leds_alt_cnt[i]--;
				if (leds_alt_cnt[i] == 0)
					leds_alt &= ~(1 << i);
			}
		break;
	default:
		return(-EINVAL);
	}
	return(0);
}

/****************************************************************************/

static void
ledman_tick(void)
{
	ledmode_t	*lmp = &led_mode[current_mode];
	int			new_value;
	static int	flash_on = 0;
/*
 *	work out which LED's should be on
 */
	new_value = 0;
	new_value |= (((leds_set[0] | leds_on[0]) & ~leds_off[0]) & ~leds_alt);
	new_value |= (((leds_set[1] | leds_on[1]) & ~leds_off[1]) & leds_alt);
/*
 *	flashing LED's run on their own devices,  ie,  according to the
 *	value fo flash_on
 */
	if ((flash_on++ % 60) >= 30)
		new_value |= ((leds_flash[0]&~leds_alt) | (leds_flash[1]&leds_alt));
	else
		new_value &= ~((leds_flash[0]&~leds_alt) | (leds_flash[1]&leds_alt));
/*
 *	set the HW
 */
 	(*lmp->set)(new_value);
	leds_set[0] = leds_set[1] = 0;
}

/****************************************************************************/
#endif /* !defined(CONFIG_SH_KEYWEST) && !defined(CONFIG_SH_BIGSUR) */
/****************************************************************************/
#if defined(CONFIG_NETtel) && defined(CONFIG_M5307)
/****************************************************************************/
/*
 *	Here it the definition of the LED's on the NETtel circuit board
 *	as per the labels next to them.  The two parallel port LED's steal
 *	some high bits so we can map it more easily onto the HW
 *
 *	LED - D1   D2   D3   D4   D5   D6   D7   D8   D11  D12  
 *	HEX - 100  200  004  008  010  020  040  080  002  001
 *
 */

#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/nettel.h>

static ledmap_t	nettel_old = {
	0x3ff, 0x200, 0x100, 0x008, 0x004, 0x020, 0x010, 0x080, 0x080, 0x080,
	0x080, 0x040, 0x040, 0x002, 0x002, 0x024, 0x018, 0x001, 0x0ff, 0x0ff,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x100, 0x200, 0x000,
	0x000, 0x000, 0x000, 0x000
};

#if defined(CONFIG_SNAPGEAR)

/*
 * all snapgear 5307 based boards have a SW link status on the front
 */

static ledmap_t nettel_new = {
	0x3ff, 0x200, 0x100, 0x040, 0x040, 0x002, 0x002, 0x008, 0x008, 0x020,
	0x020, 0x000, 0x000, 0x000, 0x000, 0x024, 0x018, 0x001, 0x0ff, 0x080,
	0x000, 0x000, 0x080, 0x004, 0x010, 0x000, 0x000, 0x100, 0x200, 0x000,
	0x000, 0x000, 0x000, 0x000
};

#else

static ledmap_t nettel_new = {
	0x3ff, 0x200, 0x100, 0x040, 0x040, 0x002, 0x002, 0x008, 0x004, 0x020,
	0x010, 0x000, 0x000, 0x000, 0x000, 0x024, 0x018, 0x001, 0x0ff, 0x080,
	0x000, 0x000, 0x080, 0x000, 0x000, 0x000, 0x000, 0x100, 0x200, 0x000,
	0x000, 0x000, 0x000, 0x000
};

#endif

static leddef_t	nettel_def = {
	0x000, 0x200, 0x000, 0x100,
};

#ifdef ENTERASYS
static ledmap_t enterasys_std = {
	0x3ff, 0x200, 0x100, 0x040, 0x040, 0x002, 0x002, 0x008, 0x004, 0x020,
	0x010, 0x000, 0x000, 0x000, 0x000, 0x024, 0x018, 0x001, 0x00c, 0x030,
	0x000, 0x000, 0x080, 0x000, 0x000, 0x000, 0x000, 0x100, 0x200, 0x000,
	0x000, 0x000, 0x000, 0x000
};
  
static leddef_t enterasys_def = {
	0x000, 0x200, 0x000, 0x100,
};
#endif

static void
nettel_set(unsigned long bits)
{
	unsigned long		flags;

	save_flags(flags); cli();
	* (volatile char *) NETtel_LEDADDR = (~bits & 0xff);
	mcf_setppleds(0x60, ~(bits >> 3) & 0x60);
	restore_flags(flags);
}

/****************************************************************************/
#endif /* defined(CONFIG_NETtel) && defined(CONFIG_M5307) */
/****************************************************************************/
#if defined(CONFIG_NETtel) && defined(CONFIG_M5272)
/****************************************************************************/

/*
 *	For the SecureEdge Firewall (5272), 5 operational LED's.
 *
 *	LED -   POWER HEARTBEAT TX     RX     VPN
 * 	HEX -    001     002    004    008    010
 */

#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/nettel.h>

static ledmap_t nt5272_std = {
	0x01f, 0x001, 0x002, 0x008, 0x004, 0x008, 0x004, 0x000, 0x000, 0x008,
	0x004, 0x000, 0x000, 0x000, 0x000, 0x014, 0x008, 0x010, 0x01c, 0x010,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x001, 0x002, 0x000,
	0x000, 0x000, 0x000, 0x000
};

static leddef_t	nt5272_def = {
	0x000, 0x001, 0x000, 0x002,
};

static void nt5272_set(unsigned long bits)
{
	*((volatile unsigned short *) (MCF_MBAR + MCFSIM_PADAT)) = (~bits & 0x1f);
}

/****************************************************************************/
#endif /* defined(CONFIG_NETtel) && defined(CONFIG_M5272) */
/****************************************************************************/
#if defined(CONFIG_SE1100)
/****************************************************************************/

/*
 *	For the SecureEdge SE1100 (5272), 3 operational LED's.
 *
 *	LED -   RUNNING INTERNAL1 INTERNAL2
 * 	HEX -     001     200       002
 */

#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/se1100.h>

static ledmap_t se1100_std = {
	0x203, 0x000, 0x001, 0x200, 0x200, 0x200, 0x200, 0x000, 0x000, 0x000,
	0x000, 0x002, 0x002, 0x002, 0x002, 0x200, 0x002, 0x000, 0x202, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x001, 0x002, 0x000,
	0x000, 0x000, 0x000, 0x000
};

static leddef_t	se1100_def = {
	0x000, 0x000, 0x000, 0x001
};

static void se1100_set(unsigned long bits)
{
	mcf_setpa(0x203, bits & 0x203);
}

/****************************************************************************/
#endif /* defined(CONFIG_SE1100) */
/****************************************************************************/
#if defined(CONFIG_GILBARCONAP) && defined(CONFIG_M5272)
/****************************************************************************/

/*
 *	For the Gilbarco/NAP (5272), 2 operational LED's.
 *
 *	LED -   RUNNING DIAG
 * 	HEX -     001    002
 */

#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/nap.h>

static ledmap_t nap5272_std = {
	0x003, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x002, 0x001, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x001, 0x002, 0x000,
	0x000, 0x000, 0x000, 0x000
};

static leddef_t	nap5272_def = {
	0x000, 0x001, 0x000, 0x002,
};

static void nap5272_set(unsigned long bits)
{
	mcf_setpa(0x3, ~bits & 0x3);
}

/****************************************************************************/
#endif /* defined(CONFIG_SE1100) */
/****************************************************************************/
#if defined(CONFIG_AVNET5282)
/****************************************************************************/

#include <asm/coldfire.h>
#include <asm/mcfsim.h>

#define GPTASYSCR1  (*(volatile unsigned char *)0x401a0006)
#define GPTBSYSCR1  (*(volatile unsigned char *)0x401b0006)
#define GPTADR      (*(volatile unsigned char *)0x401a001d)
#define GPTBDR      (*(volatile unsigned char *)0x401b001d)
#define GPTADDR     (*(volatile unsigned char *)0x401a001e)
#define GPTBDDR     (*(volatile unsigned char *)0x401b001e)
#define PORT_TC     (*(volatile unsigned char *)0x4010000f)
#define PORT_TD     (*(volatile unsigned char *)0x40100010)
#define DDR_TC      (*(volatile unsigned char *)0x40100023)
#define DDR_TD      (*(volatile unsigned char *)0x40100024)
#define DR_TC       (*(volatile unsigned char *)0x40100037)
#define DR_TD       (*(volatile unsigned char *)0x40100038)


static ledmap_t ads5282_std = {
	0x003, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x002, 0x001, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x001, 0x002, 0x000,
	0x000, 0x000, 0x000, 0x000
};

static leddef_t	ads5282_def = {
	0x000, 0x001, 0x000, 0x002,
};

static void ads5282_set(unsigned long bits)
{
	static int first_call = 1;

	if (first_call) {
		GPTASYSCR1 = 0x00;
		GPTBSYSCR1 = 0x00;
		GPTADDR = 0x00;
		GPTBDDR = 0x0f;
		DDR_TC = 0x05;
		DDR_TD = 0x05;
		first_call = 0;
	}

	if (bits & 0x01) PORT_TC &= ~0x01; else PORT_TC |= 0x01;
	if (bits & 0x02) PORT_TC &= ~0x04; else PORT_TC |= 0x04;
	if (bits & 0x04) PORT_TD &= ~0x01; else PORT_TD |= 0x01;
	if (bits & 0x08) PORT_TD &= ~0x04; else PORT_TD |= 0x04;
	if (bits & 0x10) GPTBDR  &= ~0x01; else GPTBDR  |= 0x01;
	if (bits & 0x20) GPTBDR  &= ~0x02; else GPTBDR  |= 0x02;
	if (bits & 0x40) GPTBDR  &= ~0x04; else GPTBDR  |= 0x04;
	if (bits & 0x80) GPTBDR  &= ~0x08; else GPTBDR  |= 0x08;
}

/****************************************************************************/
#endif /* defined(CONFIG_AVNET5282) */
/****************************************************************************/
#if defined(CONFIG_SH_SECUREEDGE5410)
/****************************************************************************/

/*
 *	For the SecureEdge5410 7 (or 8 for eth2/DMZ port) operational LED's.
 *
 *	LED -   POWR  HBEAT  LAN1  LAN2 | LAN3 | COM ONLINE  VPN 
 *	POS -    D2    D3    D4    D5   |  ??  | D6    D7    D8    DTR
 * 	HEX -    01    02    04    08   |0x2000| 10    20    40    80 
 */

#include <asm/io.h>
#if defined(CONFIG_LEDMAP_TAMS_SOHO)
/*
 *	LED -   POWR  HBEAT  LAN1  LAN2    COM   ONLINE VPN      
 *	POS -    D2    D3    D4    D5      ??    D6     D7    DTR
 * 	HEX -    01    02    04    08    0x2000  10     20    80
 */
static ledmap_t se5410_std = {
	0x203f,0x0001,0x0002,0x2000,0x2000,0x2000,0x2000,0x0004,0x0004,0x0008,
	0x0008,0x0000,0x0000,0x0000,0x0000,0x2024,0x0018,0x0020,0x203c,0x0000,
	0x0000,0x0000,0x0010,0x0000,0x0000,0x0000,0x0000,0x0001,0x0002,0x0000,
	0x0000,0x0000,0x0000,0x0000
};
#else
static ledmap_t se5410_std = {
	0x207f,0x0001,0x0002,0x0010,0x0010,0x0010,0x0010,0x0004,0x0004,0x0008,
	0x0008,0x0000,0x0000,0x0000,0x0000,0x2054,0x0028,0x0040,0x207c,0x0000,
	0x0000,0x0000,0x0020,0x0000,0x0000,0x0000,0x0000,0x0001,0x0002,0x2000,
	0x2000,0x0000,0x0000,0x0000
};
#endif

static leddef_t	se5410_def = {
	0x0000, 0x0001, 0x0000, 0x0002,
};

static void se5410_set(unsigned long bits)
{
	unsigned long flags;

	save_and_cli(flags);
	SECUREEDGE_WRITE_IOPORT(~bits, 0x207f);
	restore_flags(flags);
}

/****************************************************************************/
#endif /* defined(CONFIG_GILBARCONAP) && defined(CONFIG_M5272) */
/****************************************************************************/
#if defined(CONFIG_NETtel) && defined(CONFIG_M5206e)
/****************************************************************************/
/*
 *	For the WebWhale/NETtel1500,  3 LED's (was 2)
 *
 *	LED - HEARTBEAT  DCD    DATA
 * 	HEX -    001     002    004
 */

#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/nettel.h>

static ledmap_t nt1500_std = {
	0x007, 0x000, 0x001, 0x004, 0x004, 0x004, 0x004, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x004, 0x002, 0x000, 0x007, 0x000,
	0x002, 0x002, 0x000, 0x000, 0x000, 0x000, 0x000, 0x001, 0x002, 0x000,
	0x000, 0x000, 0x000, 0x000
};

static leddef_t	nt1500_def = {
	0x000, 0x000, 0x000, 0x001,
};

static void
nt1500_set(unsigned long bits)
{
	* (volatile char *) NETtel_LEDADDR = (~bits & 0x7);
}

/****************************************************************************/
#endif /* defined(CONFIG_NETtel) && defined(CONFIG_M5206e) */
/****************************************************************************/
#ifdef CONFIG_eLIA
/****************************************************************************/
/*
 *	For the eLIA, only 2 LED's.
 *
 *	LED - HEARTBEAT  USER
 *	HEX -    2        1
 */

#ifdef CONFIG_COLDFIRE
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#endif
#include <asm/elia.h>

static ledmap_t elia_std = {
	0x003, 0x000, 0x002, 0x001, 0x001, 0x001, 0x001, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x002, 0x001, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x001, 0x002, 0x000,
	0x000, 0x000, 0x000, 0x000
};

static leddef_t	elia_def = {
	0x000, 0x000, 0x000, 0x002,
};

static void
elia_set(unsigned long bits)
{
	unsigned long		flags;

	save_flags(flags); cli();
	mcf_setppleds(0x3000, ~(bits << 12) & 0x3000);
	restore_flags(flags);
}

/****************************************************************************/
#endif /* CONFIG_eLIA */
/****************************************************************************/
/****************************************************************************/
#if defined(CONFIG_AMDSC520)
/****************************************************************************/

#include <linux/smp_lock.h>
#include <linux/sched.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <asm/io.h>

#if defined(CONFIG_CHINOOK)
/*
 *	Here is the definition of the LED's on the 6wind Chinook circuit board
 *	as per the LED order from left to right. These probably don't
 *  correspond to any known enclosure.
 *
 *	LED -  D1    D2    D3    D4    D5    D6    D7    D8    D9    D10
 *	HEX - 0001  0400  0008  0010  0800  0020  0040  0080  0100  0200
 *
 *  Sync LEDs - Activity  Link
 *  HEX       - 04000000  08000000
 */
static ledmap_t	nettel_std = {
	0x0c000ff9, 0x00000001, 0x00000400, 0x00000040, 0x00000040,
	0x04000000, 0x04000000, 0x00000010, 0x00000008, 0x00000020,
	0x00000800, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000028, 0x00000810, 0x00000200, 0x00000bf8, 0x00000820,
	0x00000000, 0x08000000, 0x00000100, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000
};

static leddef_t	nettel_def = {
	0x0000, 0x0001, 0x0000, 0x0400,
};

#elif defined(CONFIG_SNAPGEAR)
/*
 *	Here is the definition of the LED's on the SnapGear x86 circuit board
 *	as per the labels next to them.
 *
 *	LED - D1   D2   D3   D4   D5   D6   D7   D8   D9   D10
 *	HEX - 001  002  004  008  010  020  040  100  080  200
 */
static ledmap_t	nettel_std = {
	0x3ff, 0x001, 0x002, 0x080, 0x080, 0x040, 0x040, 0x010, 0x010, 0x020,
	0x020, 0x000, 0x000, 0x000, 0x000, 0x048, 0x030, 0x200, 0x3fc, 0x004,
	0x000, 0x000, 0x100, 0x008, 0x004, 0x000, 0x000, 0x001, 0x002, 0x000,
	0x000, 0x000, 0x000, 0x000
};

static leddef_t	nettel_def = {
	0x0000, 0x0001, 0x0000, 0x0002,
};

/*
 *	Here is the definition of the LED's on the SnapGear x86 circuit board
 *	as per the labels next to them.  This is for the old enclosure.
 *
 *	LED - D1   D2   D3   D4   D5   D6   D7   D8   D9   D10
 *	HEX - 001  002  004  008  010  020  040  100  080  200
 */
static ledmap_t	nettel_old = {
	0x3ff, 0x002, 0x001, 0x080, 0x080, 0x040, 0x040, 0x010, 0x010, 0x020,
	0x020, 0x000, 0x000, 0x000, 0x000, 0x048, 0x030, 0x200, 0x3fc, 0x004,
	0x000, 0x000, 0x100, 0x008, 0x004, 0x000, 0x000, 0x001, 0x002, 0x000,
	0x000, 0x000, 0x000, 0x000
};

static leddef_t	nettel_def_old = {
	0x0000, 0x0002, 0x0000, 0x0001,
};

#elif defined(CONFIG_SITECTRLER)
/*
 *	Here it the definition of the LED's on the SiteController circuit board
 *	as per the labels next to them. (D9 and D10 are not software controlled)
 *
 *	LED -  D1   D2   D3   D4   D5   D6   D7   D8 
 *	HEX - 0001 0002 0004 0008 0010 0020 0040 0080 
 */
static ledmap_t	nettel_std = {
	0x10fd,0x0001,0x1000,0x0004,0x0004,0x0008,0x0008,0x0040,0x0040,0x0080,
	0x0080,0x0000,0x0000,0x0000,0x0000,0x00cc,0x0030,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x1000,
	0x0000,0x0000,0x0000,0x0000
};

static leddef_t	nettel_def = {
	0x0000, 0x0001, 0x0000, 0x1000,
};

#elif defined(CONFIG_ADTRAN_ADVANTA)
/*
 *	Here is the definition of the LED's on the Adtran Advanta3110 circuit
 *	board as per the labels next to them.
 *	The lower 8 bits are for IO port 0x300.
 *	The upper 4 bits are for PIO31-28.
 *
 *	LED - D1   D2   D3   D4   D7   D8   D15green D15red   D16green D16red
 *	HEX - 01   02   04   08   40   80   10000000 40000000 20000000 80000000
 */
static ledmap_t	nettel_std = {
	0xf00000cf, 0x00000000, 0x20000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000002, 0x00000001, 0x00000008,
	0x00000004,	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000009, 0x00000006, 0x10000000, 0x100000cf, 0x0000000c,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000080, 0x00000040, 0x00000001, 0x00000002, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000
};

static leddef_t	nettel_def = {
	0, 0, 0, 0x20000000,
};

#else
/*
 *	Here is the definition of the LED's on the x86 NETtel circuit board
 *	as per the labels next to them.
 *
 *	LED - D1   D2   D3   D4   D5   D6   D7   D8   D9   D10
 *	HEX - 001  002  004  008  010  020  040  100  080  200
 */
static ledmap_t	nettel_std = {
	0x3ff, 0x002, 0x001, 0x100, 0x100, 0x080, 0x080, 0x010, 0x008, 0x040,
	0x020, 0x000, 0x000, 0x000, 0x000, 0x048, 0x030, 0x200, 0x3fc, 0x004,
	0x000, 0x000, 0x004, 0x000, 0x000, 0x000, 0x000, 0x001, 0x002, 0x000,
	0x000, 0x000, 0x000, 0x000
};

static leddef_t	nettel_def = {
	0x0000, 0x0002, 0x0000, 0x0001,
};

#endif

static volatile unsigned long	*ledman_ledp;

static void nettel_set(unsigned long bits)
{
#ifdef CONFIG_ADTRAN_ADVANTA
	outb(~bits, 0x300);
	*ledman_ledp = (*ledman_ledp & 0x0fffffff) | (~bits & 0xf0000000);
#else
	*ledman_ledp = (*ledman_ledp & ~ nettel_std[LEDMAN_ALL])
			| (~bits & nettel_std[LEDMAN_ALL]);
#endif
}

static irqreturn_t ledman_interrupt(int irq, void *dev_id)
{
	ledman_signalreset();
	return IRQ_HANDLED;
}

static void ledman_initarch(void)
{
	volatile unsigned char	*mmcrp;

	/* Map the CPU MMCR register for access */
	mmcrp = (volatile unsigned char *) ioremap(0xfffef000, 4096);

#ifdef CONFIG_ADTRAN_ADVANTA
	/* Enable the PIO for the PWR and VPN LEDs */
	*(volatile unsigned short *)(mmcrp + 0xc22) &= 0x0fff;
	*(volatile unsigned short *)(mmcrp + 0xc2c) |= 0xf000;

	/* Enable GPIRQ2, low-to-high transitions, map to IRQ12 */
	*(volatile unsigned short *)(mmcrp + 0xc22) |= 0x0020;
	*(volatile unsigned long *)(mmcrp + 0xd10) |= 0x0004;
	*(mmcrp + 0xd52) = 0x07;
#endif

	ledman_ledp = (volatile unsigned long *) (mmcrp + 0xc30);

	/* Setup extern "factory default" switch on IRQ12 */
	if (request_irq(12, ledman_interrupt, SA_INTERRUPT, "Erase", NULL))
		printk("LED: failed to register IRQ12 for ERASE witch\n");
	else
		printk("LED: registered ERASE switch on IRQ12\n");
}

/****************************************************************************/
#endif /* CONFIG_AMDSC520 */
/****************************************************************************/
/****************************************************************************/
#if defined(CONFIG_GEODE)
/****************************************************************************/

#include <asm/io.h>

/*
 *	Construct a mapping from virtual LED to gpio bit and bank.
 */
struct gpiomap {
		unsigned int	bank;
		unsigned long	bit;
};

#if defined(CONFIG_REEFEDGE) || defined(CONFIG_SE5000)
/*
 *	Definition of the LED's on the GEODE SC1100 ReefEdfe circuit board,
 *	as per the labels next to them.
 *
 *	LED      -  D20  D19  D18  D16
 *	GPIO BIT -   38   18   40   37
 *  VIRTNUM  -  0x8  0x4  0x2  0x1
 */
#define	GPIO0_OFF	0x00040000
#define	GPIO1_OFF	0x00000160

static ledmap_t	nettel_std = {
	0x00f, 0x001, 0x002, 0x000, 0x000, 0x000, 0x000, 0x004,
	0x004, 0x008, 0x008, 0x000, 0x000, 0x000, 0x000, 0x004,
	0x008, 0x000, 0x00e, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000
};

static struct gpiomap iomap[] = {
	/* VIRT 0x1 */	{ 1, 0x00000020 },
	/* VIRT 0x2 */	{ 1, 0x00000100 },
	/* VIRT 0x4 */	{ 0, 0x00040000 },
	/* VIRT 0x8 */	{ 1, 0x00000040 },
};

#elif defined(CONFIG_SE2910)
/*
 *	Definition of the LED's on the SnapGear SE2910 boards.
 *
 *	LED      -   D2   D4   D6  D11  D13  D18  D20  D22   D25   D28
 *	GPIO BIT -    2    3   17    0   36   47   39   40    18    38
 *  VIRTNUM  -  0x1  0x2  0x4  0x8 0x10 0x20 0x40 0x80 0x100 0x200
 */
#define	GPIO0_OFF	0x0006000d
#define	GPIO1_OFF	0x000081d0

static ledmap_t	nettel_std = {
	0x3ff, 0x001, 0x002, 0x080, 0x080, 0x080, 0x080, 0x010,
	0x008, 0x040, 0x020, 0x200, 0x200, 0x200, 0x200, 0x30c,
	0x0f0, 0x000, 0x3fc, 0x004, 0x000, 0x000, 0x004, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
   	0x000, 0x000
};

static struct gpiomap iomap[] = {
	/* VIRT 0x1 */   { 0, (1 << (02 - 00)) },
	/* VIRT 0x2 */   { 0, (1 << (03 - 00)) },
	/* VIRT 0x4 */   { 0, (1 << (17 - 00)) },
	/* VIRT 0x8 */   { 0, (1 << (00 - 00)) },
	/* VIRT 0x10 */  { 1, (1 << (36 - 32)) },
	/* VIRT 0x20 */  { 1, (1 << (47 - 32)) },
	/* VIRT 0x40 */  { 1, (1 << (39 - 32)) },
	/* VIRT 0x80 */  { 1, (1 << (40 - 32)) },
	/* VIRT 0x100 */ { 0, (1 << (18 - 00)) },
	/* VIRT 0x200 */ { 1, (1 << (38 - 32)) },
};

#else
/*
 *	Definition of the LED's on the SnapGear GEODE board.
 *
 *	LED      -  D11  D12  D13  D14  D15  D16  D17  D18   D19   D20
 *	GPIO BIT -   32   33   34   35   36   37   39   40    18    38
 *  VIRTNUM  -  0x1  0x2  0x4  0x8 0x10 0x20 0x40 0x80 0x100 0x200
 */
#define	GPIO0_OFF	0x00040000
#define	GPIO1_OFF	0x000001ff

static ledmap_t	nettel_std = {
	0x3ff, 0x001, 0x002, 0x080, 0x080, 0x080, 0x080, 0x010,
	0x008, 0x040, 0x020, 0x200, 0x200, 0x200, 0x200, 0x30c,
	0x0f0, 0x000, 0x3fc, 0x004, 0x000, 0x000, 0x004, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
   	0x000, 0x000
};

static struct gpiomap iomap[] = {
	/* VIRT 0x1 */   { 1, 0x00000001 },
	/* VIRT 0x2 */   { 1, 0x00000002 },
	/* VIRT 0x4 */   { 1, 0x00000004 },
	/* VIRT 0x8 */   { 1, 0x00000008 },
	/* VIRT 0x10 */  { 1, 0x00000010 },
	/* VIRT 0x20 */  { 1, 0x00000020 },
	/* VIRT 0x40 */  { 1, 0x00000080 },
	/* VIRT 0x80 */  { 1, 0x00000100 },
	/* VIRT 0x100 */ { 0, 0x00040000 },
	/* VIRT 0x200 */ { 1, 0x00000040 },
};

#endif /* !CONFIG_REEFEDGE && !CONFIG_SE5000 */

#define	GPIO_SIZE	(sizeof(iomap) / sizeof(*iomap))

static leddef_t	nettel_def = {
	0x0000, 0x0001, 0x0000, 0x0002,
};


static void nettel_set(unsigned long bits)
{
	unsigned int gpio[2];
	unsigned int i, mask;

	gpio[0] = GPIO0_OFF;
	gpio[1] = GPIO1_OFF;

	for (i = 0, mask = 0x1; (i < GPIO_SIZE); i++, mask <<= 1) {
		if (bits & mask)
			gpio[iomap[i].bank] &= ~iomap[i].bit;
	}

	outl(gpio[0], 0x6400);
	outl(gpio[1], 0x6410);
}

static int ledman_button;
static struct timer_list ledman_timer;

static void ledman_buttonpoll(unsigned long arg)
{
	if (inl(0x6404) & 0x0002) {
		if (ledman_button == 0) {
			printk("LEDMAN: reset button pushed!\n");
			ledman_signalreset();
		}
		ledman_button = 1;
	} else {
		ledman_button = 0;
	}

	/* Re-arm timer interrupt. */
	mod_timer(&ledman_timer, jiffies + HZ/25);
}

static void ledman_initarch(void)
{
	init_timer(&ledman_timer);
	ledman_timer.function = ledman_buttonpoll;
	ledman_timer.data = 0;
	mod_timer(&ledman_timer, jiffies + HZ/25);
}

/****************************************************************************/
#endif /* CONFIG_GEODE */
/****************************************************************************/
/****************************************************************************/
#if defined(CONFIG_SH_KEYWEST) || defined(CONFIG_SH_BIGSUR)
/****************************************************************************/
/*
 *	Here it the definition of the how we use the 8 segment LED display on
 *	the Hitachi Keywest
 *
 *	LED - LD0  LD1  LD2  LD3  LD4  LD5  LD6  LD7
 *	HEX - 001  002  004  008  010  020  040  080
 *        HB   CNT  L1R  L1T  L2R  L2T  COM  VPN
 *
 */

#include <linux/kernel_stat.h>

#define KEYWEST_NUM_LEDS 8

#if defined(CONFIG_SH_BIGSUR)
#define LED_BASE 0xb1fffe00
#define LED_ADDR(x) (LED_BASE+((x)<<2))
#else
#define LED_BASE 0xb1ffe000
#define LED_ADDR(x) (LED_BASE+(x))
#endif

static ledmap_t	keywest_std = {
	0x0ff, 0x000, 0x001, 0x040, 0x040, 0x040, 0x040, 0x004, 0x008, 0x010,
	0x020, 0x000, 0x000, 0x000, 0x000, 0x054, 0x02a, 0x080, 0x07e, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x001, 0x002, 0x000,
	0x000, 0x000, 0x000, 0x000
};

static leddef_t	keywest_def = {
	0x000, 0x000, 0x000, 0x001,
};

static struct keywest_led_value {
	int				count;
	int				max;
	int				prev;
	unsigned char	disp;
} keywest_led_values[KEYWEST_NUM_LEDS][2];


struct keywest_font_s {
	unsigned char row[7];
} keywest_font[] = {
	{{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }}, /* bar 0 */
	{{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f }}, /* bar 1 */
	{{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x1f }}, /* bar 2 */
	{{ 0x00, 0x00, 0x00, 0x00, 0x1f, 0x1f, 0x1f }}, /* bar 3 */
	{{ 0x00, 0x00, 0x00, 0x1f, 0x1f, 0x1f, 0x1f }}, /* bar 4 */
	{{ 0x00, 0x00, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f }}, /* bar 5 */
	{{ 0x00, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f }}, /* bar 6 */
	{{ 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f }}, /* bar 7 */
	{{ 0x00, 0x0a, 0x1f, 0x1f, 0x0e, 0x04, 0x00 }}, /* heart */
	{{ 0x08, 0x14, 0x14, 0x1c, 0x1c, 0x1c, 0x1c }}, /* vpn locked */
	{{ 0x02, 0x05, 0x05, 0x1c, 0x1c, 0x1c, 0x1c }}, /* vpn unlocked */
};

static unsigned int keywest_old_cntx = 0;

/*
 * program up some display bars
 */

static void ledman_initkeywest()
{
	int i, j;

	for (i = 0; i < sizeof(keywest_font) / sizeof(struct keywest_font_s); i++) {
		* (unsigned char *)(LED_ADDR(0x20)) = i;
		for (j = 0; j < 7; j++)
			* (unsigned char *)(LED_ADDR(0x28+j)) = keywest_font[i].row[j];
	}
	keywest_old_cntx = kstat.context_swtch;
}

/*
 *	We just rip through and write all LED 'disp' chars each tick.
 */

static void keywest_set(unsigned long bits)
{
	int i, alt;
	for (i = 0; i < KEYWEST_NUM_LEDS; i++) {
		alt = (leds_alt & (1 << i)) ? 1 : 0;
		* (unsigned char *)(LED_ADDR(0x38+i)) = keywest_led_values[i][alt].disp;
	}
}

static int
keywest_bits(unsigned long cmd, unsigned long bits)
{
	ledmode_t		*lmp = &led_mode[current_mode];
	int				 alt, i;
	unsigned long	 new_alt;

	alt = (cmd & LEDMAN_CMD_ALTBIT) ? 1 : 0;

	switch (cmd & ~LEDMAN_CMD_ALTBIT) {
	case LEDMAN_CMD_SET:
		bits   &= ~(leds_flash[alt]|leds_on[alt]|leds_off[alt]);
		for (i = 0; i < KEYWEST_NUM_LEDS; i++)
			if (bits & (1 << i))
				keywest_led_values[i][alt].count++;
		break;
	case LEDMAN_CMD_ON:
		leds_on[alt]    |= bits;
		leds_off[alt]   &= ~bits;
		leds_flash[alt] &= ~bits;
		(*lmp->tick)();
		break;
	case LEDMAN_CMD_OFF:
		leds_on[alt]    &= ~bits;
		leds_off[alt]   |= bits;
		leds_flash[alt] &= ~bits;
		(*lmp->tick)();
		break;
	case LEDMAN_CMD_FLASH:
		leds_on[alt]    &= ~bits;
		leds_off[alt]   &= ~bits;
		leds_flash[alt] |= bits;
		break;
	case LEDMAN_CMD_RESET:
		leds_on[alt]    = (leds_on[alt]   &~bits) | (bits&lmp->def[LEDS_ON]);
		leds_off[alt]   = (leds_off[alt]  &~bits) | (bits&lmp->def[LEDS_OFF]);
		leds_flash[alt] = (leds_flash[alt]&~bits) | (bits&lmp->def[LEDS_FLASH]);
		memset(keywest_led_values, 0, sizeof(keywest_led_values));
		break;
	case LEDMAN_CMD_ALT_ON:
		new_alt = (bits & ~leds_alt);
		leds_alt |= bits;
		/*
		 * put any newly alt'd bits into a default state
		 */
		(*lmp->bits)(LEDMAN_CMD_RESET | LEDMAN_CMD_ALTBIT, new_alt);
		for (i = 0; i < 32; i++)
			if (bits & (1 << i))
				leds_alt_cnt[i]++;
		break;
	case LEDMAN_CMD_ALT_OFF:
		for (i = 0; i < 32; i++)
			if ((bits & (1 << i)) && leds_alt_cnt[i]) {
				leds_alt_cnt[i]--;
				if (leds_alt_cnt[i] == 0)
					leds_alt &= ~(1 << i);
			}
		break;
	default:
		return(-EINVAL);
	}
	return(0);
}

static void
keywest_tick(void)
{
	ledmode_t	*lmp = &led_mode[current_mode];
	int			alt, i;
	static int	flash_on = 0;
	struct keywest_led_value *led_value;

	/*
	 * we take over the second LED as a context switch indicator
	 */
	keywest_led_values[1][0].count = kstat.context_swtch - keywest_old_cntx;
	keywest_old_cntx = kstat.context_swtch;

	for (i = 0; i < KEYWEST_NUM_LEDS; i++) {
		alt = (leds_alt >> i) & 1;
		led_value = &keywest_led_values[i][alt];
		if (leds_off[alt] & (1 << i)) {
			if ((1 << i) == 0x080) /* VPN unlock */
				led_value->disp = 0x8a;
			else
				led_value->disp = 0x20;
		} else if (leds_on[alt] & (1 << i)) {
			if ((1 << i) == 0x080) /* VPN lock */
				led_value->disp = 0x89;
			else
				led_value->disp = 0x87;
		} else if (leds_flash[alt] & (1 << i)) {
			if ((flash_on % 6) >= 3) {
				if ((1 << i) == 0x001) /* heart beat */
					led_value->disp = 0x88;
				else
					led_value->disp = 0x87;
			} else
				led_value->disp = 0x20;
		} else {
			int val;

			if (led_value->count > led_value->max)
				led_value->max = led_value->count;
			
			val = (led_value->prev + led_value->count) / 2;
			led_value->prev = val;

			val = (val * 7) / led_value->max;
			if (val == 0 && led_value->count)
				val = 1;
			led_value->disp = 0x80 + (val & 0x7);
			led_value->count = 0;
			/* degrade the maximum over time (except load) */
			if (i != 1)
				led_value->max = (led_value->max * 9)/10;
		}
	}
	flash_on++;
 	(*lmp->set)(0);
}

/****************************************************************************/
#endif /* CONFIG_SH_KEYWEST */
/****************************************************************************/
/****************************************************************************/
#if defined(CONFIG_MACH_MONTEJADE) || defined(CONFIG_MACH_IXDPG425) || \
	defined(CONFIG_MACH_SE5100)
/****************************************************************************/

#include <linux/interrupt.h>
#include <asm/hardware.h>
#include <asm/io.h>

/*
 *	Here is the definition of the LED's on the Intel/MonteJade platform.
 *	LED D7 is not visible on the front panel, so not much point using it...
 *
 *	LED - D1  D2  D3  D4  D16  D17  D18  D23
 *	HEX - 80  40  20  10   08   04   02   01
 */
static ledmap_t	montejade_std = {
	0xff, 0x00, 0x04, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x10, 0x20, 0xfc, 0x10,
	0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00
};

static leddef_t	montejade_def = {
	0x0000, 0x0000, 0x0000, 0x0001,
};


static volatile unsigned char *ledman_cs2;

static void ledman_interrupt(int irq, void *dev_id)
{
	ledman_signalreset();
}

static void montejade_set(unsigned long bits)
{
	*ledman_cs2 = ~bits;
}

static struct timer_list montejade_wdt;

static void montejade_wdtpoll(unsigned long arg)
{
	*IXP4XX_GPIO_GPOUTR ^= 0x400;

	/* Re-arm timer interrupt. */
	mod_timer(&montejade_wdt, jiffies + HZ/10);
}

static void montejade_wdtinit(void)
{
	/* Configure GPIO10 as an output to kick watchdog */
	gpio_line_config(10, IXP4XX_GPIO_OUT);

	/* Setup timer poll, 10 times a second should be good enough */
	init_timer(&montejade_wdt);
	montejade_wdt.function = montejade_wdtpoll;
	montejade_wdt.data = 0;
	mod_timer(&montejade_wdt, jiffies + HZ/10);
}

static void ledman_initarch(void)
{
	/* Configure CS2 for operation, 8bit and writable will do */
	*IXP4XX_EXP_CS2 = 0xbfff0003;

	/* Map the LED chip select address space */
	ledman_cs2 = (volatile unsigned char *) ioremap(IXP4XX_EXP_BUS_CS2_BASE_PHYS, 512);
	*ledman_cs2 = 0xffffffff;

	/* Configure GPIO9 as interrupt input (ERASE switch) */
	gpio_line_config(9, (IXP4XX_GPIO_IN | IXP4XX_GPIO_FALLING_EDGE));

	if (request_irq(26, ledman_interrupt, SA_INTERRUPT, "Erase", NULL))
		printk("LED: failed to register IRQ26 for ERASE witch\n");
	else
		printk("LED: registered ERASE switch on IRQ26\n");

	montejade_wdtinit();
}

/****************************************************************************/
#endif /* CONFIG_MACH_MONTEJADE || CONFIG_MACH_IXDPG425 || CONFIG_MACH_SE5100 */
/****************************************************************************/
/****************************************************************************/
#if defined(CONFIG_ARCH_SE4000) || defined(CONFIG_MACH_ESS710) || \
    defined(CONFIG_MACH_SG560) || defined(CONFIG_MACH_SG580) || \
	defined(CONFIG_MACH_SG565) || defined(CONFIG_MACH_SG640) || \
    defined(CONFIG_MACH_SG720) || defined(CONFIG_MACH_SG590) || \
	defined(CONFIG_MACH_SG8100)
/****************************************************************************/

#include <linux/interrupt.h>
#include <asm/hardware.h>
#include <asm/io.h>

#if defined(CONFIG_MACH_ESS710) || defined(CONFIG_MACH_SG720)
/*
 *	Here is the definition of the LED's on the SnapGear/ESS710 circuit board
 *	as per the labels next to them.
 *
 *	LED - D1      D3   D4      D5 
 *	HEX - 004     008  010     020 
 *	      F/OVER  H/A  ONLINE  H/B
 */
static ledmap_t	snapgear425_std = {
	0x03c, 0x000, 0x020, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x024, 0x018, 0x000, 0x03c, 0x000,
	0x000, 0x000, 0x010, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x004, 0x008
};

static leddef_t	snapgear425_def = {
	0x0000, 0x0000, 0x0000, 0x0020,
};

#define	LEDMASK		0x3c

#elif defined(CONFIG_MACH_SG640)
/*
 *	Here is the definition of the LED's on the SnapGear/SG640 circuit board
 *	as per the labels next to them.
 *
 */

#define LED_D1_UPPER 0x08
#define LED_D1_LOWER 0x10
#define LED_D2_UPPER 0x20
#define LED_D2_LOWER 0x04
#define	LEDMASK	     0x3c

static ledmap_t	snapgear425_std = {
	[LEDMAN_ALL]       = LEDMASK,
	[LEDMAN_POWER]     = LED_D1_LOWER,
	[LEDMAN_HEARTBEAT] = LED_D1_UPPER,
	[LEDMAN_VPN]       = LED_D2_LOWER,
	[LEDMAN_ONLINE]    = LED_D2_UPPER,
	[LEDMAN_NVRAM_1]   = LED_D1_LOWER | LED_D2_UPPER,
	[LEDMAN_NVRAM_2]   = LED_D2_LOWER | LED_D1_UPPER,
	[LEDMAN_LAN1_DHCP] = LED_D2_LOWER | LED_D2_UPPER,
	[LEDMAN_LAN2_DHCP] = LED_D2_LOWER | LED_D2_UPPER,
};

static leddef_t	snapgear425_def = {
	[LEDS_ON]    = LED_D1_LOWER,
	[LEDS_FLASH] = LED_D1_UPPER,
};

#elif defined(CONFIG_MACH_SG565)

/*
 *	Here is the definition of the LEDs on the CyberGuard/SG565,
 *	as per the labels next to them.
 *
 *	LED -  D2   D3   D4   D5   D6   D7   D8
 *	HEX - 0004 0008 0010 0020 0040 0080 0400
 */
static ledmap_t	snapgear425_std = {
	[LEDMAN_ALL] = 0x4fc,
	[LEDMAN_HEARTBEAT] = 0x004,
	[LEDMAN_COM1_RX] = 0x040,
	[LEDMAN_COM1_TX] = 0x040,
	[LEDMAN_LAN1_RX] = 0x008,
	[LEDMAN_LAN1_TX] = 0x008,
	[LEDMAN_LAN2_RX] = 0x008,
	[LEDMAN_LAN2_TX] = 0x008,
	[LEDMAN_USB1_RX] = 0x010,
	[LEDMAN_USB1_TX] = 0x010,
	[LEDMAN_USB2_RX] = 0x010,
	[LEDMAN_USB2_TX] = 0x010,
	[LEDMAN_NVRAM_1] = 0x48c,
	[LEDMAN_NVRAM_2] = 0x070,
	[LEDMAN_VPN] = 0x400,
	[LEDMAN_LAN1_DHCP] = 0x4fc,
	[LEDMAN_ONLINE] = 0x080,
	[LEDMAN_LAN3_RX] = 0x020,
	[LEDMAN_LAN3_TX] = 0x020,
};

static leddef_t	snapgear425_def = {
	[LEDS_FLASH] = 0x004,
};

#define	LEDMASK		0x04fc

#elif defined(CONFIG_MACH_SG560) || defined(CONFIG_MACH_SG580)
/*
 *	Here is the definition of the LEDs on the CyberGuard/SG560
 *	and CyberGuard/SG580, as per the labels next to them.
 *
 *	LED -  D2   D3   D4   D5   D6   D7   D8
 *	HEX - 0004 0008 0010 0400 0040 0020 0080
 */
static ledmap_t	snapgear425_std = {
	0x4fc, 0x000, 0x004, 0x040, 0x040, 0x040, 0x040, 0x008, 0x008, 0x010,
	0x010, 0x000, 0x000, 0x000, 0x000, 0x0ac, 0x450, 0x080, 0x4fc, 0x000,
	0x000, 0x000, 0x020, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x400
};

static leddef_t	snapgear425_def = {
	0x0000, 0x0000, 0x0000, 0x0004,
};

#define	LEDMASK		0x04fc

#elif defined(CONFIG_MACH_SG590)
/*
 *	Here is the definition of the LEDs on the TAMS/TVR.
 */
#define LED_D2  0x80
#define LED_D3  0x40
#define LED_D4  0x20
#define LED_D5  0x10
#define LED_D6  0x08
#define LED_D7  0x04
#define LEDMASK 0xfc

static ledmap_t	snapgear425_std = {
	[LEDMAN_ALL]       = LEDMASK,
	[LEDMAN_HEARTBEAT] = LED_D2,
	[LEDMAN_LAN1_RX]   = LED_D3,
	[LEDMAN_LAN1_TX]   = LED_D3,
	[LEDMAN_LAN2_RX]   = LED_D4,
	[LEDMAN_LAN2_TX]   = LED_D4,
	[LEDMAN_VPN_RX]    = LED_D5,
	[LEDMAN_VPN_TX]    = LED_D5,
	[LEDMAN_ONLINE]    = LED_D6,
	[LEDMAN_VPN]       = LED_D7,
	[LEDMAN_NVRAM_1]   = LED_D4 | LED_D5,
	[LEDMAN_NVRAM_2]   = LED_D2 | LED_D3 | LED_D6 | LED_D7,
	[LEDMAN_LAN1_DHCP] = LEDMASK,
};

static leddef_t	snapgear425_def = {
	[LEDS_FLASH] = LED_D2,
};

#elif defined(CONFIG_MACH_SG8100)

/*
 *	Here is the definition of the LEDs on the SG8100
 *	as per the labels next to them.
 *
 *	LED -  D1   D2   D3   D4   D5   D6   D7   D8
 *	HEX - 0004 0008 0010 0020 0040 0080 0400 0800
 */
static ledmap_t	snapgear425_std = {
	[LEDMAN_ALL] = 0xcfc,
	[LEDMAN_HEARTBEAT] = 0x004,
	[LEDMAN_COM1_RX] = 0x040,
	[LEDMAN_COM1_TX] = 0x040,
	[LEDMAN_LAN1_RX] = 0x008,
	[LEDMAN_LAN1_TX] = 0x008,
	[LEDMAN_LAN2_RX] = 0x008,
	[LEDMAN_LAN2_TX] = 0x008,
	[LEDMAN_USB1_RX] = 0x010,
	[LEDMAN_USB1_TX] = 0x010,
	[LEDMAN_USB2_RX] = 0x010,
	[LEDMAN_USB2_TX] = 0x010,
	[LEDMAN_NVRAM_1] = 0xc0c,
	[LEDMAN_NVRAM_2] = 0x0f0,
	[LEDMAN_VPN] = 0x400,
	[LEDMAN_LAN1_DHCP] = 0xcfc,
	[LEDMAN_ONLINE] = 0x080,
	[LEDMAN_LAN3_RX] = 0x020,
	[LEDMAN_LAN3_TX] = 0x020,
};

static leddef_t	snapgear425_def = {
	[LEDS_FLASH] = 0x004,
};

#define	LEDMASK		0x0cfc

#else

/*
 *	Here is the definition of the LEDs on the SnapGear/SE4000, as per the
 *	labels next to them.  LED D7 is not visible on the front panel, so not
 *	much point using it...
 *
 *	LED - D1   D3   D4   D5   D6   D7
 *	HEX - 004  008  010  020  040  080
 */
static ledmap_t	snapgear425_std = {
	0x0fc, 0x000, 0x004, 0x008, 0x008, 0x008, 0x008, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x028, 0x010, 0x020, 0x0fc, 0x010,
	0x000, 0x000, 0x010, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000
};

static leddef_t	snapgear425_def = {
	0x0000, 0x0000, 0x0000, 0x0004,
};

#define	LEDMASK		0xfc

#endif

#if defined(CONFIG_MACH_SG720) || defined(CONFIG_MACH_SG590)
#define	ERASEGPIO	10
#define ERASEIRQ    IRQ_IXP4XX_GPIO10
#else
#define	ERASEGPIO	9
#define ERASEIRQ    IRQ_IXP4XX_GPIO9
#endif


static irqreturn_t ledman_interrupt(int irq, void *dev_id)
{
	ledman_signalreset();
	return IRQ_HANDLED;
}

static void snapgear425_set(unsigned long bits)
{
	*IXP4XX_GPIO_GPOUTR = (*IXP4XX_GPIO_GPOUTR & ~LEDMASK) | (~bits & LEDMASK);
}

static void ledman_initarch(void)
{
	/* Enable LED lines as outputs - do them all in one go */
	*IXP4XX_GPIO_GPOER &= ~LEDMASK;

	/* Configure GPIO9 as interrupt input (ERASE switch) */
	gpio_line_config(ERASEGPIO, IXP4XX_GPIO_IN);
	set_irq_type(ERASEIRQ, IRQT_FALLING);
	gpio_line_isr_clear(ERASEGPIO);

#if !defined(CONFIG_MACH_SG720) && !defined(CONFIG_MACH_SG590)
	/* De-assert reset for the hub/switch - just in case... */
	gpio_line_config(13, IXP4XX_GPIO_OUT);
	gpio_line_set(13, 1);
#endif

	if (request_irq(ERASEIRQ, ledman_interrupt, SA_INTERRUPT, "Erase", NULL))
		printk("LED: failed to register IRQ%d for ERASE witch\n", ERASEIRQ);
	else
		printk("LED: registered ERASE switch on IRQ%d\n", ERASEIRQ);
}

/****************************************************************************/
#endif /* CONFIG_ARCH_SE4000 || CONFIG_MACH_ESS710 || CONFIG_MACH_SG560 || CONFIG_MACH_SG565 || CONFIG_MACH_SG580 || CONFIG_MACH_SG640 || CONFIG_MACH_SG720 || CONFIG_MACH_SG590 || CONFIG_MACH_SG8100 */
/****************************************************************************/
/****************************************************************************/
#if defined(CONFIG_MACH_IVPN)
/****************************************************************************/

#include <linux/interrupt.h>
#include <asm/hardware.h>
#include <asm/io.h>

/*
 *	There is 2 LED banks on the iVPN. But really there is 4 LEDs, 2 green
 *	and 2 red. They are connected to gpio lines, with the mapping:
 *
 *	GPIO -   12  13   2  11
 *	LED  -   G0  R0  G1  R1
 *
 *	But...
 *
 *	I have virtualized the bits in the ledmap, since the iVPN led
 *	functionality is a little weird, and cannot be displayed by just
 *	setting the bits.
 */

#define	BIT_HEARTBEAT	0x1
#define	BIT_LANLINK		0x2
#define	BIT_LANACTIVITY	0x4
#define	BIT_WANLINK		0x8
#define	BIT_WANACTIVITY	0x10
#define	BIT_WIFLINK		0x20
#define	BIT_WIFACTIVITY	0x40
#define	BIT_VPNLINK		0x80
#define	BIT_VPNACTIVITY	0x100

static ledmap_t	ivpn_std = {
	0x1ff, 0x000, 0x001, 0x000, 0x000, 0x000, 0x000, 0x004, 0x004, 0x010,
	0x010, 0x000, 0x000, 0x000, 0x000, 0x00a, 0x088, 0x080, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x002, 0x008, 0x100, 0x100, 0x000, 0x000, 0x040,
	0x040, 0x020, 0x000, 0x000
};

static leddef_t	ivpn_def = {
	0x0000, 0x0002, 0x0000, 0x0001,
};


static irqreturn_t ledman_interrupt(int irq, void *dev_id)
{
	ledman_signalreset();
	return IRQ_HANDLED;
}

static void ivpn_set(unsigned long bits)
{
	unsigned long flags;
	unsigned int wanbit, val = 0;
	static unsigned int lancnt = 0;
	static unsigned int wancnt = 0;

	if (bits & BIT_LANLINK) {
		if ((bits & BIT_LANACTIVITY) || lancnt) {
			if (lancnt++ > 4) {
				val |= 0x1;
				if (lancnt > 8)
					lancnt = 0;
			}
		} else {
			val |= 0x1;
		}
	} else {
		val |= 0x2;
	}
	if (bits & (BIT_WANLINK | BIT_WIFLINK)) {
		wanbit = (bits & BIT_VPNLINK) ? 0x4 : 0x8;
		if ((bits & (BIT_WANACTIVITY | BIT_WIFACTIVITY)) || wancnt) {
			if (wancnt++ > 4) {
				val |= wanbit;
				if (wancnt > 8)
					wancnt = 0;
			}
		} else {
			val |= wanbit;
		}
	}

	save_flags(flags); cli();
	gpio_line_set(2, val & 0x4 ? 0 : 1);
	gpio_line_set(11, val & 0x8 ? 0 : 1);
	gpio_line_set(12, val & 0x1 ? 0 : 1);
	gpio_line_set(13, val & 0x2 ? 0 : 1);
	restore_flags(flags);
}

static void ledman_initarch(void)
{
#if 0
	/* Enabled second ethernet port */
	gpio_line_config(3, IXP4XX_GPIO_OUT);
	gpio_line_set(3, 0);
#endif
	/* Set up GPIO lines to allow access to LEDs. */
	gpio_line_set(2, 1);
	gpio_line_set(11, 1);
	gpio_line_set(12, 1);
	gpio_line_set(13, 1);
	gpio_line_config(2, IXP4XX_GPIO_OUT);
	gpio_line_config(11, IXP4XX_GPIO_OUT);
	gpio_line_config(12, IXP4XX_GPIO_OUT);
	gpio_line_config(13, IXP4XX_GPIO_OUT);
	ivpnss_hwsetup();
	ivpnss_memmap();

	/* Configure GPIO9 as interrupt input (ERASE switch) */
	gpio_line_config(9, (IXP4XX_GPIO_IN | IXP4XX_GPIO_FALLING_EDGE));

	if (request_irq(26, ledman_interrupt, SA_INTERRUPT, "Erase", NULL))
		printk("LED: failed to register IRQ26 for ERASE witch\n");
	else
		printk("LED: registered ERASE switch on IRQ26\n");
}

/****************************************************************************/
#endif /* CONFIG_MACH_IVPN */
/****************************************************************************/
/****************************************************************************/
#if defined(CONFIG_ARCH_KS8695)
/****************************************************************************/

#include <linux/interrupt.h>
#include <asm/hardware.h>
#include <asm/io.h>

/*
 *	Here is the definition of the LED's on the SnapGear/LITE3 circuit board,
 *	as per the labels next to them. There is only 2 software programmable
 *	LEDs, so this is pretty easy :-)
 *
 *	LED - D1   D2
 *	HEX - 002  004
 */
static ledmap_t	lite3_std = {
	0x006, 0x004, 0x002, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x002, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000
};

static leddef_t	lite3_def = {
	0x0000, 0x0004, 0x0000, 0x0002,
};

static volatile unsigned int *ledman_gpio;

static irqreturn_t ledman_interrupt(int irq, void *dev_id)
{
	volatile unsigned int *intstatp;

	intstatp = (volatile unsigned int __force *) (KS8695_REG(KS8695_INT_STATUS));
	*intstatp |= 0x4;

	ledman_signalreset();
	return IRQ_HANDLED;
}

static void lite3_set(unsigned long bits)
{
	*ledman_gpio = (*ledman_gpio & ~0x6) | (~bits & 0x6);
}

static void ledman_initarch(void)
{
	volatile unsigned int *gpiop, *intenp;

	/* Enable LED lines as outputs */
	gpiop = (volatile unsigned int __force *) (KS8695_REG(KS8695_GPIO_MODE));
	*gpiop = (*gpiop | 0x6) & ~0x1;

	/* Turn LEDs off */
	ledman_gpio = (volatile unsigned int __force *) (KS8695_REG(KS8695_GPIO_DATA));
	*ledman_gpio = (*ledman_gpio & ~0x6);

	/* Configure GPIO0 as interrupt input (ERASE switch) */
	gpiop = (volatile unsigned int __force *) (KS8695_REG(KS8695_GPIO_CTRL));
	*gpiop = (*gpiop & ~0x7) | 0xc;

	intenp = (volatile unsigned int __force *) (KS8695_REG(KS8695_INT_ENABLE));
	*intenp |= 0x4;

	if (request_irq(2, ledman_interrupt, SA_INTERRUPT, "Erase", NULL))
		printk("LED: failed to register IRQ2 for ERASE witch\n");
	else
		printk("LED: registered ERASE switch on IRQ2\n");
}

/****************************************************************************/
#endif /* CONFIG_ARCH_KS8695 */
/****************************************************************************/
/****************************************************************************/
#if defined(CONFIG_ARCH_EP9312)
/****************************************************************************/

#include <linux/interrupt.h>
#include <asm/hardware.h>
#include <asm/io.h>

/*
 *	Here is the definition of the LED's on the Trusonic IPD Board
 *
 *	It has no visible LED's, though it has provision for deug LED's,
 *	however, we need flatfsd support ;-)
 */
static ledmap_t	ipd_std;
static leddef_t	ipd_def;

static void ipd_set(unsigned long bits)
{
}

static irqreturn_r ledman_interrupt(int irq, void *dev_id)
{
	while (inl(VIC1RAWINTR) & 0x1)
		;
	ledman_signalreset();
	return IRQ_HANDLED;
}

static void ledman_initarch(void)
{
	if (request_irq(32, ledman_interrupt, SA_INTERRUPT, "Erase", NULL))
		printk("LED: failed to register IRQ32 for ERASE witch\n");
	else
		printk("LED: registered ERASE switch on IRQ32\n");
}

/****************************************************************************/
#endif /* CONFIG_ARCH_EP9312 */
/****************************************************************************/
