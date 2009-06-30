#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/slab.h>
#include <linux/vt_kern.h>
#include <linux/vt_buffer.h>
#include <linux/selection.h>
#include <linux/spinlock.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/xparameters.h>

/* 
Character color mapping used by this driver
-------------------------------------------
'a' red
'b' green
'c' yellow
'd' blue
'e' magenta
'f' cyan
'g' white
'h' black
*/

#define XIo_Out32(OutputPtr, Value) \
    (*(volatile unsigned int *)((OutputPtr)) = (Value))


#define SCR_BUF_BASEADDR          (XPAR_OPB_COLOR_VIDEO_CTRL_0_BASEADDR)
#define SCR_CTRL_REG_BASEADDR     (XPAR_OPB_COLOR_VIDEO_CTRL_0_BASEADDR + 0xA000)
#define SCR_CHAR_MAP_BASEADDR     (XPAR_OPB_COLOR_VIDEO_CTRL_0_BASEADDR + 0xC000)
#define xy2scroffset(x, y)        (((y * SCR_X) + x) << 2)
#define out32                     XIo_Out32
#define pack_scr_char(c, clr)     ((((unsigned int)clr) << 8) | (c & 0xff))
#define XIL_ADDR(x,y)			  ((void *)(SCR_BUF_BASEADDR + xy2scroffset(x,y)))

static unsigned int null_char[8]  =         { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static unsigned int solid_square_char[8]  = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static unsigned int horiz_line[8] =         { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static unsigned int horiz_barred_line[8] =  { 0xff, 0xff, 0x03, 0x03, 0x03, 0x00, 0x00, 0x00 };
static unsigned int vert_line[8]  =         { 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18 };

static void xilscr_write_char (int x, int y, char c, char color);
static void xilscr_redefine_char (unsigned char c, unsigned int *defptr);



/* console information */

static int	xil_first_vc = 1;
static int	xil_last_vc  = 16;

static struct vc_data	*xil_display_fg = NULL;

module_param(xil_first_vc, int, 0);
module_param(xil_last_vc, int, 0);

/* XILINX register values */

#define CLR_R                     1
#define CLR_G                     2
#define CLR_B                     4

#define SCR_X_PIXELS              800
#define SCR_Y_PIXELS              600
#define SCR_X                     100
#define SCR_Y                     75

#define BLANK_CHAR                0
#define HORIZ_LINE_CHAR           128
#define VERT_LINE_CHAR            129
#define HORIZ_BARRED_LINE_CHAR    130
#define SOLID_SQUARE_CHAR         131



#ifndef MODULE

static int __init xilcon_setup(char *str)
{
	int ints[3];

	str = get_options(str, ARRAY_SIZE(ints), ints);

	if (ints[0] < 2)
		return 0;

	if (ints[1] < 1 || ints[1] > MAX_NR_CONSOLES || 
	    ints[2] < 1 || ints[2] > MAX_NR_CONSOLES)
		return 0;

	xil_first_vc = ints[1];
	xil_last_vc  = ints[2];
	return 1;
}

__setup("xilcon=", xilcon_setup);

#endif


static void xilscr_redefine_char (unsigned char c, unsigned int *defptr)
{
    unsigned int *charp = (unsigned int*)(SCR_CHAR_MAP_BASEADDR + (unsigned int)(((unsigned int)c)<<5));

    *charp++ = *defptr++;
    *charp++ = *defptr++;
    *charp++ = *defptr++;
    *charp++ = *defptr++;
    *charp++ = *defptr++;
    *charp++ = *defptr++;
    *charp++ = *defptr++;
    *charp++ = *defptr++;
}



static const char __init *xilcon_startup(void)
{
	char video_mode;
	video_mode = 0;
   // Enable the character mode in the control register of the videocontroller 
    out32 (SCR_CTRL_REG_BASEADDR, (video_mode<<8) | 0x02); 
    xilscr_redefine_char (BLANK_CHAR,  null_char);
    xilscr_redefine_char (HORIZ_LINE_CHAR, horiz_line);
    xilscr_redefine_char (VERT_LINE_CHAR, vert_line);
    xilscr_redefine_char (HORIZ_BARRED_LINE_CHAR, horiz_barred_line);
    xilscr_redefine_char (SOLID_SQUARE_CHAR, solid_square_char);

	return "XILINX_OPB_CHAR";
}

static void xilcon_init(struct vc_data *c, int init)
{
	c->vc_can_do_color = 1;
	c->vc_complement_mask = 0x0800;	 /* reverse video */
	c->vc_display_fg = &xil_display_fg;

	if (init) {
		c->vc_cols = 100;
		c->vc_rows = 75;
	} else
		vc_resize(c, 100, 75);

	/* make the first XIL console visible */

	if (xil_display_fg == NULL)
		xil_display_fg = c;
}

static void xilcon_deinit(struct vc_data *c)
{
	if (xil_display_fg == c)
		xil_display_fg = NULL;
}


static u8 xilcon_build_attr(struct vc_data *c, u8 color, u8 intensity, 
			    u8 blink, u8 underline, u8 reverse)
{

	return color;
}


static void xilscr_write_char (int x, int y, char c, char color)
{
    XIo_Out32((SCR_BUF_BASEADDR + xy2scroffset (x,y)), pack_scr_char (c, color));
}


static void xilcon_putc(struct vc_data *c, int ch, int y, int x)
{
	xilscr_write_char (x, y, (ch)&0xFF, (ch>>8)&0xFF);
}

static void xilcon_putcs(struct vc_data *c, const unsigned short *s,
		         int count, int y, int x)
{
	for (; count > 0; count--) {
		xilcon_putc(c, *(s++), y, x++);
	}
}


static void xilcon_clear(struct vc_data *c, int y, int x, 
			  int height, int width)
{
    unsigned int *scr_buf_start = (unsigned int*)(SCR_BUF_BASEADDR);
    unsigned int *scr_buf_end   = (unsigned int*)(SCR_BUF_BASEADDR + xy2scroffset (SCR_X, SCR_Y));
    unsigned int *bufp;

    bufp = scr_buf_start;
    while (bufp <= scr_buf_end)
	*bufp++ = 0x0;
}
                        

static int xilcon_switch(struct vc_data *c)
{
	return 1;	/* redrawing needed */
}

static int xilcon_set_palette(struct vc_data *c, unsigned char *table)
{
	return -EINVAL;
}

static int xilcon_blank(struct vc_data *c, int blank, int mode_switch)
{
	if(blank)
	    out32 (SCR_CTRL_REG_BASEADDR, 0x0);
	else
	    out32 (SCR_CTRL_REG_BASEADDR, 0x2);

	return 0;
}

static int xilcon_scrolldelta(struct vc_data *c, int lines)
{
	return 0;
}

static void xilcon_cursor(struct vc_data *c, int mode)
{
    unsigned short car1;

    car1 = c->vc_screenbuf[c->vc_x + c->vc_y * c->vc_cols];
    switch (mode) {
    case CM_ERASE:
		xilcon_putc(c, car1, c->vc_y, c->vc_x);
	break;
    case CM_MOVE:
    case CM_DRAW:
	switch (c->vc_cursor_type & 0x0f) {
	case CUR_UNDERLINE:
	case CUR_LOWER_THIRD:
	case CUR_LOWER_HALF:
	case CUR_TWO_THIRDS:
	case CUR_BLOCK:
	    xilcon_putc(c, (7<<8) | 131, c->vc_y, c->vc_x);
	    break;
	}
	break;
    }

}

static int xilcon_scroll(struct vc_data *c, int t, int b, int dir, int lines)
{
	if (!lines)
		return 0;

	if (lines > c->vc_rows)
		lines = c->vc_rows;

	switch (dir) {

	case SM_UP:
			scr_memmovew((void *)(SCR_BUF_BASEADDR + xy2scroffset(0,t)),
						 (void *)(SCR_BUF_BASEADDR + xy2scroffset(0,t + lines)),
						 (b-t-lines)*75*8);
			scr_memsetw((void *)(SCR_BUF_BASEADDR + xy2scroffset(0,b-lines)), (('h'<<8) || 0x00),
						 lines*75*8);
		break;
	case SM_DOWN:
			scr_memmovew((void *)(SCR_BUF_BASEADDR + xy2scroffset(0,t + lines)),
						 (void *)(SCR_BUF_BASEADDR + xy2scroffset(0,t)),
						 (b-t-lines)*75*8);
			scr_memsetw((void *)(SCR_BUF_BASEADDR + xy2scroffset(0,t)), (('h'<<8) || 0x00), lines*75*8);
		break;
	}

	return 0;
}

static void xilcon_bmove(struct vc_data *c, int sy, int sx, 
			 int dy, int dx, int height, int width)
{
	u16 *src, *dest;

	if (width <= 0 || height <= 0)
		return;
		
	if (sx==0 && dx==0 && width==100) {
		scr_memmovew(XIL_ADDR(0,dy), XIL_ADDR(0,sy), height*width*2);

	} else if (dy < sy || (dy == sy && dx < sx)) {
		src  = XIL_ADDR(sx, sy);
		dest = XIL_ADDR(dx, dy);

		for (; height > 0; height--) {
			scr_memmovew(dest, src, width*2);
			src  += 100;
			dest += 100;
		}
	} else {
		src  = XIL_ADDR(sx, sy+height-1);
		dest = XIL_ADDR(dx, dy+height-1);

		for (; height > 0; height--) {
			scr_memmovew(dest, src, width*2);
			src  -= 100;
			dest -= 100;
		}
	}

	return;
}

/*
 *  The console `switch' structure for the XILINX based console
 */

const struct consw xil_con = {
	.owner =		THIS_MODULE,
	.con_startup =		xilcon_startup,
	.con_init =			xilcon_init,
	.con_deinit =		xilcon_deinit,
	.con_clear =		xilcon_clear,
	.con_putc =			xilcon_putc,
	.con_putcs =		xilcon_putcs,
	.con_switch =		xilcon_switch,
	.con_blank =		xilcon_blank,
	.con_set_palette =	xilcon_set_palette,
	.con_scrolldelta =	xilcon_scrolldelta,
	.con_build_attr =	xilcon_build_attr,
	.con_cursor =		xilcon_cursor,
	.con_scroll =		xilcon_scroll,
	.con_bmove =		xilcon_bmove,
};


int __init xilinx_console_init(void)
{
	if (xil_first_vc > xil_last_vc)
		return 1;
	return take_over_console(&xil_con, xil_first_vc-1, xil_last_vc-1, 1);
}

static void __exit xilinx_console_exit(void)
{
	give_up_console(&xil_con);
}

module_init(xilinx_console_init);
module_exit(xilinx_console_exit);

MODULE_LICENSE("GPL");

