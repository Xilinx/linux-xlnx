/*
 * include/asm-arm/arch-s3c24a0/s3c24a0-key.h
 *
 * $Id: s3c24a0-key.h,v 1.2 2005/11/28 03:55:11 gerg Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 * 
 * Changes 
 * 
 * 2004/06/15 <heechul.yun@samsung.com>   Added SPJ key scancode 
 */

#ifndef _SPJ_KEY_H_
#define _SPJ_KEY_H_
#ifndef __ASSEMBLY__

#define KEY_RELEASED	0
#define KEY_PRESSED	1

/*
 * Definition of Generic Key Scancode
 */
#define SCANCODE_LEFT		0x69
#define SCANCODE_RIGHT		0x6a
#define SCANCODE_UP			0x67
#define SCANCODE_DOWN		0x6c
#define SCANCODE_ENTER		0x1c
#define SCANCODE_PAGE_UP		0x68	/* Page Up */
#define SCANCODE_PAGE_DOWN		0x6d	/* Page Down */
#define SCANCODE_BKSP		0x0e	/* Back Space */

/*
 * Key PAD
 */
#define SCANCODE_PAD_0		0x52
#define SCANCODE_PAD_1		0x4f
#define SCANCODE_PAD_2		0x50
#define SCANCODE_PAD_3		0x51
#define SCANCODE_PAD_4		0x4b
#define SCANCODE_PAD_5		0x4c
#define SCANCODE_PAD_6		0x4d
#define SCANCODE_PAD_7		0x47
#define SCANCODE_PAD_8		0x48
#define SCANCODE_PAD_9		0x49
#define SCANCODE_PAD_MINUS		0x4a
#define SCANCODE_PAD_PLUS		0x4e
#define SCANCODE_PAD_ENTER		0x60
#define SCANCODE_PAD_PERIOD		0x53
#define SCANCODE_PAD_SLASH		0x62
#define SCANCODE_PAD_ASTERISK	0x37

/*
 * Function Key
 */
#define SCANCODE_F5			0x3f
#define SCANCODE_F6			0x40
#define SCANCODE_F7			0x41
#define SCANCODE_F8			0x42
#define SCANCODE_F9			0x43
#define SCANCODE_F10			0x44
#define SCANCODE_F11			0x57
#define SCANCODE_F12			0x58

/*
 * Undefined Region
 */
#define SCANCODE_U1			0x78	/* Unknown */
#define SCANCODE_U2			0x79	/* Unknown */
#define SCANCODE_U3			0x70	/* Unknown */
#define SCANCODE_U4			0x71	/* Unknown */
#define SCANCODE_U5			0x72	/* Unknown */
#define SCANCODE_U6			0x73	/* Unknown */
#define SCANCODE_U7			0x74	/* Unknown */
#define SCANCODE_U8			0x75	/* Unknown */
#define SCANCODE_U9			0x76	/* Unknown */

/*
 * Common key definition for PDA
 */
#define SCANCODE_POWER		0x7a
#define SCANCODE_RECORD		0x7b
#define SCANCODE_ACTION		SCANCODE_ENTER
#define SCANCODE_SLIDE_UP		SCANCODE_PAGE_UP
#define SCANCODE_SLIDE_DOWN		SCANCODE_PAGE_DOWN
#define SCANCODE_SLIDE_CENTER	SCANCODE_PAD_ENTER

/*
 * Common key definition for Phone
 */
#define SCANCODE_ASTERISK		SCANCODE_PAD_ASTERISK
#define SCANCODE_SHARP		SCANCODE_PAD_MINUS
#define SCANCODE_SEND		0x7c
#define SCANCODE_END			0x7d
#define SCANCODE_MENU		0x7e
#define NCODE_CLR			0x7f


/* These are the scancodes for SPJ buttons on the SMDK24a0 */

#define scPOWER    	120 /* sw1 - 0x78 */ 
#define scMENU    	121 /* sw3 - 0x79 */
#define scTOOL     	122 /* sw13 - 0x7a */
#define scRETURN   	123 /* sw11 - 0x7b */ 
#define scVOLUP    	124 /* sw5 - 0x7c */ 
#define scVOLDOWN 	125 /* sw10 - 0x7d */ 
#define scHOLD     	126 /* sw21 - 0x7e*/ 
#define scUP       	103 /* sw2, - 0x67 keycode up*/
#define scRIGHT    	106 /* sw8, - 0x6a keycode right */
#define scLEFT     	105 /* sw6, - 0x69 keycode left */
#define scDOWN     	108 /* sw12, - 0x6c keycode down */
#define scACTION   	96  /* sw7, - 0x60 keycode keypad enter */ 

#endif	/* __ASSEMBLY__ */
#endif /* _SPJ_KEY_H_ */
