/*
 * keyboard.h
 *
 * $Id: keyboard.h,v 1.2 2005/11/28 03:55:11 gerg Exp $
 *
 */

#ifndef _S3C24A0_KEYBOARD_H
#define _S3C24A0_KEYBOARD_H

#define kbd_disable_irq()       do { } while(0);
#define kbd_enable_irq()        do { } while(0);

#define k_leds(x...)
#define k_setkeycode(x...)
#define k_getkeycode(x...)
#define k_unexpected_up(x...)   (1)

/* S3C24A0 SPI */

#if 1 // hcyun
extern int elfin_kbd_init (void);
#define kbd_init_hw()           elfin_kbd_init()
#else
#define kbd_init_hw()           do {} while(0);
#endif

/* Generic Keyboard Scan Codes */
#define KK_NONE         0x00
#define KK_ESC          0x01
#define KK_F1           0x3b
#define KK_F2           0x3c
#define KK_F3           0x3d
#define KK_F4           0x3e
#define KK_F5           0x3f
#define KK_F6           0x40
#define KK_F7           0x41
#define KK_F8           0x42
#define KK_F9           0x43
#define KK_F10          0x44
#define KK_F11          0x57
#define KK_F12          0x58
#define KK_PRNT         0x63    /* PrintScreen */
#define KK_SCRL         0x46    /* Scroll Lock */
#define KK_BRK          0x77    /* Break */
#define KK_AGR          0x29    /* ` */
#define KK_1            0x02
#define KK_2            0x03
#define KK_3            0x04
#define KK_4            0x05
#define KK_5            0x06
#define KK_6            0x07
#define KK_7            0x08
#define KK_8            0x09
#define KK_9            0x0a
#define KK_0            0x0b
#define KK_MINS         0x0c    /* - */
#define KK_EQLS         0x0d    /* = */
#define KK_BKSP         0x0e    /* BKSP */
#define KK_INS          0x6e    /* Insert */
#define KK_HOME         0x66
#define KK_PGUP         0x68
#define KK_NUML         0x45
#define KP_SLH          0x62    /* KP / */
#define KP_STR          0x37    /* KP * */
#define KP_MNS          0x4a    /* KP - */
#define KK_TAB          0x0f
#define KK_Q            0x10
#define KK_W            0x11
#define KK_E            0x12
#define KK_R            0x13
#define KK_T            0x14
#define KK_Y            0x15
#define KK_U            0x16
#define KK_I            0x17
#define KK_O            0x18
#define KK_P            0x19
#define KK_LSBK         0x1a    /* [ */
#define KK_RSBK         0x1b    /* ] */
#define KK_ENTR         0x1c
#define KK_DEL          0x6f
#define KK_END          0x6b
#define KK_PGDN         0x6d
#define KP_7            0x47
#define KP_8            0x48
#define KP_9            0x49
#define KP_PLS          0x37    /* KP + */
#define KK_CAPS         0x3a
#define KK_A            0x1e
#define KK_S            0x1f
#define KK_D            0x20
#define KK_F            0x21
#define KK_G            0x22
#define KK_H            0x23
#define KK_J            0x24
#define KK_K            0x25
#define KK_L            0x26
#define KK_SEMI         0x27    /* ; */
#define KK_SQOT         0x28    /* ' */
#define KK_HASH         0x29    /* ` */
#define KP_4            0x4b
#define KP_5            0x4c
#define KP_6            0x4d
#define KK_LSFT         0x2a    /* L SHIFT */
#define KK_BSLH         0x2b    /* \ */
#define KK_Z            0x2c
#define KK_X            0x2d
#define KK_C            0x2e
#define KK_V            0x2f
#define KK_B            0x30
#define KK_N            0x31
#define KK_M            0x32
#define KK_COMA         0x33    /* , */
#define KK_DOT          0x34    /* . */
#define KK_FSLH         0x35    /* / */
#define KK_RSFT         0x36    /* R SHIFT */
#define KK_UP           0x67
#define KP_1            0x4f
#define KP_2            0x50
#define KP_3            0x51
#define KP_ENT          0x60    /* KP Enter */
#define KK_LCTL         0x1d    /* L CTRL */
#define KK_LALT         0x38    /* L ALT */
#define KK_SPCE         0x39    /* SPACE */
#define KK_RALT         0x64    /* R ALT */
#define KK_RCTL         0x61    /* R CTRL */
#define KK_LEFT         0x69
#define KK_DOWN         0x6c
#define KK_RGHT         0x6a
#define KP_0            0x52
#define KP_DOT          0x53    /* KP . */
#define KK_21           0x21

#endif  /* _S3C24A0_KEYBOARD_H */
