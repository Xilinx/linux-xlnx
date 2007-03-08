/*
 * include/asm-arm/arch-s3c24a0/s3c24a0-machine.h
 *
 * $Id: s3c24a0-machine.h,v 1.2 2005/11/28 03:55:11 gerg Exp $
 *
 * vendor/machine specifice ioctl function
 *
 * extended ioctl for MTD
 * change the permission of MTDPART
 */

/*
 *for CONFIG_SA1100_WISMO
 */
#define WS_SET_MODEM_MODE	_IOW(IOC_MAGIC, 0xc0, unsigned long)
#define WS_GET_MODEM_MODE	_IOR(IOC_MAGIC, 0xc1, unsigned long)
#define WS_MODEM_MODE_OFF	0x01
#define WS_MODEM_MODE_READY	0x02	/* i,e,. PowerDown Mode */
#define WS_MODEM_MODE_CALL	0x03	/* i,e,. Normal Mode */

#define WS_KEYPAD_LED		0
#define WS_7COLOR_LED		1

#define WS_SUBLCD_ON		_IO (IOC_MAGIC, 0xc2)
#define WS_SUBLCD_OFF		_IO (IOC_MAGIC, 0xc3)
#define WS_SUBLCD_DRAW		_IO (IOC_MAGIC, 0xc4)
#define WS_SUBLCD_BLITE_ON	_IO (IOC_MAGIC, 0xc5)
#define WS_SUBLCD_BLITE_OFF	_IO (IOC_MAGIC, 0xc6)


typedef struct {
	unsigned char reserved;
	unsigned char sec; 	/* seconds  0 ~ 59 */
	unsigned char min;     /* min 0 - 59 */
	unsigned char hour; 	/* Hour     0 ~ 59 */
	unsigned char day;     /* Day 	    0 ~ 23 */
	unsigned char month; 	/* Month    1 ~ 12 */
} WS_DATE_T;
#define WS_SET_DATE		_IOW(IOC_MAGIC, 0xc7, WS_DATE_T)

typedef struct {
	unsigned char reserved;
	unsigned char batt;
	unsigned char rssi;
	unsigned char msg;
	unsigned char alarm;
	unsigned char alert;
	unsigned char unused[2];
} WS_ICON_T;
#define WS_SET_ICON		_IOW(IOC_MAGIC, 0xc8, WS_ICON_T)

typedef struct {
	unsigned char    hlt;
	unsigned char   llt;
} WS_MOTOR_T;
#define WS_MOTOR_ON		_IOW(IOC_MAGIC, 0xc9, WS_MOTOR_T)
#define WS_MOTOR_OFF		_IO (IOC_MAGIC, 0xca)
#define WS_TOUCH_ON             _IO (IOC_MAGIC, 0xcb)
#define WS_TOUCH_OFF            _IO (IOC_MAGIC, 0xcc)
#define WS_SUBLCD_TIMER         _IO (IOC_MAGIC, 0xce) 

#define WS_LCD_POWER_ON		_IO (IOC_MAGIC, 0xd1)
#define WS_LCD_POWER_OFF	_IO (IOC_MAGIC, 0xd2)
#define WS_LCD_BLITE_ON		_IO (IOC_MAGIC, 0xd3)
#define WS_LCD_BLITE_OFF	_IO (IOC_MAGIC, 0xd4)



/* button definition */
#define WS_SIDE_UP_BUTTON	SCANCODE_SLIDE_UP
#define WS_VOICE_BUTTON		SCANCODE_RECORD
#define WS_SIDE_DOWN_BUTTON	SCANCODE_SLIDE_DOWN

#define WS_SK1_BUTTON		SCANCODE_U1
#define WS_SK2_BUTTON		SCANCODE_U2
#define WS_SK3_BUTTON		SCANCODE_U3

#define WS_WAP_BUTTON		SCANCODE_MENU
#define WS_CLR_BUTTON		SCANCODE_CLR
#define WS_SEND_BUTTON		SCANCODE_SEND
#define WS_END_BUTTON		SCANCODE_END

#define WS_1_BUTTON		SCANCODE_PAD_1
#define WS_2_BUTTON		SCANCODE_PAD_2
#define WS_3_BUTTON		SCANCODE_PAD_3
#define WS_4_BUTTON		SCANCODE_PAD_4
#define WS_5_BUTTON		SCANCODE_PAD_5
#define WS_6_BUTTON		SCANCODE_PAD_6
#define WS_7_BUTTON		SCANCODE_PAD_7
#define WS_8_BUTTON		SCANCODE_PAD_8
#define WS_9_BUTTON		SCANCODE_PAD_9
#define WS_0_BUTTON		SCANCODE_PAD_0
#define WS_ASTERISK_BUTTON	SCANCODE_ASTERISK
#define WS_SHARP_BUTTON		SCANCODE_SHARP

#define WS_UP_BUTTON		SCANCODE_UP
#define WS_DOWN_BUTTON		SCANCODE_DOWN
#define WS_LEFT_BUTTON		SCANCODE_LEFT
#define WS_RIGHT_BUTTON		SCANCODE_RIGHT

/* camera definition */
#define WS_CAM_IOC_MAGIC 'C'

#define WS_CAM_ZOOM_21 0 /* cam : disp = 2 : 1 <== zoom-out */
#define WS_CAM_ZOOM_11 1 /* cam : disp = 1 : 1 <== normal   */
#define WS_CAM_ZOOM_12 2 /* cam : disp = 1 : 2 <== zoom-in  */
#define WS_CAM_SET_ZOOM  _IOW(WS_CAM_IOC_MAGIC, 10, int)

struct ws_cam_set {
    int res;          /* resolution, WS_CAM_SIZE_???  */
#define WS_CAM_SIZE_320x240    0 /* capture only            */
#define WS_CAM_SIZE_240x180    1 /* capture only            */
#define WS_CAM_SIZE_240x320_OV 2 /* overlay(preview) only   */
#define WS_CAM_SIZE_240x180_OV 3 /* overlay(preview) only   */
    int preview_ypos; /* Y position when _res_ is WS_CAM_SIZE_240x180_OV */
};
#define WS_CAM_SET_PARAM _IOW(WS_CAM_IOC_MAGIC, 11, struct ws_cam_set)

#define WS_CAM_SET_X_MIRROR _IOW(WS_CAM_IOC_MAGIC, 12, int)
#define WS_CAM_SET_Y_MIRROR _IOW(WS_CAM_IOC_MAGIC, 13, int)
#define WS_CAM_SET_EXPOSURE _IOW(WS_CAM_IOC_MAGIC, 14, unsigned long)
#define WS_CAM_SET_WHITBLNC _IOW(WS_CAM_IOC_MAGIC, 15, unsigned long)

/*
 * for CONFIG_ARCH_I519
 */
/* Audio Path Control */
#define HN_AUDIO_PATH		_IOW(IOC_MAGIC, 0xc0, unsigned long)
#define MIC_PDA		0x0001
#define PDA_SPK		0x0010
#define MIC_PHONE		0x0002
#define PHONE_RCV		0x0020
#define PHONE_SPK		0x2000

#define HFK_PDA		0x0004
#define PDA_HFK		0x0040
#define HFK_PHONE		0x0008
#define PHONE_HFK		0x0080

#define PHONE_PDA		0x0100
#define PDA_PHONE		0x0200

/* for PXA-ac97 control (debugging only) */
struct hn_ac97 {
     unsigned int reg;
     unsigned int val;
};
#define HN_AC97_REG_WRITE	_IOW(IOC_MAGIC, 0xc3, struct hn_ac97)
#define HN_AC97_REG_READ	_IOR(IOC_MAGIC, 0xc4, struct hn_ac97)

#define HN_ONLY_PDA_SPK		_IO ('h', 0xe3)

/* rescan external perpheral device */
#define HN_RESCAN_ACCESSARY	_IO (IOC_MAGIC, 0xc1)

/* rescan & get battery type */
#define HN_RESCAN_BATTERY_TYPE	_IOR(IOC_MAGIC, 0xc2, unsigned int)
#define HN_BATTERY_TYPE_STD	0x0
#define HN_BATTERY_TYPE_EXT	0x1
#define HN_RESCAN_BATTERY_TYPE2	_IOR(IOC_MAGIC, 0xc5, unsigned int)

/* UART & USB port switching */
#define HN_UART_TO_PHONE	_IO (IOC_MAGIC, 0xc8)
#define HN_UART_TO_PDA		_IO (IOC_MAGIC, 0xc9)
#define HN_USB_TO_PHONE		_IO (IOC_MAGIC, 0xca)
#define HN_USB_TO_PDA		_IO (IOC_MAGIC, 0xcb)
#define HN_USB_UART_STATE	_IO (IOC_MAGIC, 0xce)

#define HN_UART_PATH_PDA	0x0001
#define HN_UART_PATH_PHONE	0x0002
#define HN_USB_PATH_PDA		0x0010
#define HN_USB_PATH_PHONE	0x0020

/* Vibrator Control */
#define HN_MOTOR_ON		_IO (IOC_MAGIC, 0xcc)
#define HN_MOTOR_OFF		_IO (IOC_MAGIC, 0xcd)

/* DPRAM Control for communication between PDA and Phone */

/* DPRAM ioctls for DPRAM tty devices */
#define HN_DPRAM_PHONE_ON		_IO (IOC_MAGIC, 0xd0)
#define HN_DPRAM_PHONE_OFF		_IO (IOC_MAGIC, 0xd1)
#define HN_DPRAM_PHONE_GETSTATUS	_IOR(IOC_MAGIC, 0xd2, unsigned int)
#define HN_DPRAM_PHONE_DOWNLOAD		_IO (IOC_MAGIC, 0xd5)

/* return codes for HN_DPRAM_PHONE_GETSTATUS */
#define HN_DPRAM_PHONE_STATUS_OFF	0x00
#define HN_DPRAM_PHONE_STATUS_ON	0x01

/* DPRAM ioctls for DPRAM ctl device */
#define HN_DPRAM_PPP_ENABLE		_IO (IOC_MAGIC, 0xd3)
#define HN_DPRAM_PPP_DISABLE		_IO (IOC_MAGIC, 0xd4)
#define HN_DPRAM_PPP_AC_ENABLE		_IO (IOC_MAGIC, 0xd6)
#define HN_DPRAM_PPP_AC_DISABLE		_IO (IOC_MAGIC, 0xd7)

/* DPRAM events through /dev/dpram/ctl */
#define HN_DPRAM_EVENT_PPP_ACCESS	0x0001
#define HN_DPRAM_EVENT_PHONE_DN_DONE	0x0002

/* button definition */
#define HN_POWER_BUTTON		SCANCODE_POWER
#define HN_CAMERA_BUTTON	SCANCODE_U1
#define HN_VOICE_BUTTON		SCANCODE_RECORD

#define HN_SIDE_UP_BUTTON	SCANCODE_SLIDE_UP
#define HN_SIDE_DOWN_BUTTON	SCANCODE_SLIDE_DOWN

#define HN_HOME_BUTTON		SCANCODE_MENU
#define HN_BACK_BUTTON		SCANCODE_CLR
#define HN_SEND_BUTTON		SCANCODE_SEND
#define HN_END_BUTTON		SCANCODE_END

#define HN_UP_BUTTON		SCANCODE_UP
#define HN_DOWN_BUTTON		SCANCODE_DOWN
#define HN_LEFT_BUTTON		SCANCODE_LEFT
#define HN_RIGHT_BUTTON		SCANCODE_RIGHT
#define HN_OK_BUTTON		SCANCODE_ENTER

#define HN_EAR_SEND_BUTTON	SCANCODE_U2

/* PXA255 clock control */
#define HN_CLOCK_WRITE		_IOW(IOC_MAGIC, 0xe1, unsigned int)
#define HN_CLOCK_READ		_IOR(IOC_MAGIC, 0xe2, unsigned int)
