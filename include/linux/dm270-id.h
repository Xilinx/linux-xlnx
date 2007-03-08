/* FIXME: this temporarily, until these are included in linux/video_decoder.h */
struct video_decoder_reg
{
	unsigned short addr;
	unsigned char val;
};

#define DM270V4L_IMGSIZE_SUBQCIF_WIDTH	128
#define DM270V4L_IMGSIZE_SUBQCIF_HEIGHT	 96
#define DM270V4L_IMGSIZE_QQVGA_WIDTH	160
#define DM270V4L_IMGSIZE_QQVGA_HEIGHT	120
#define DM270V4L_IMGSIZE_QCIF_WIDTH	176
#define DM270V4L_IMGSIZE_QCIF_HEIGHT	144
#define DM270V4L_IMGSIZE_QVGA_WIDTH	320
#define DM270V4L_IMGSIZE_QVGA_HEIGHT	240
#define DM270V4L_IMGSIZE_CIF_WIDTH	352
#define DM270V4L_IMGSIZE_CIF_HEIGHT	288
#define DM270V4L_IMGSIZE_VGA_WIDTH	640
#define DM270V4L_IMGSIZE_VGA_HEIGHT	480
#define DM270V4L_IMGSIZE_NTSC_WIDTH	720
#define DM270V4L_IMGSIZE_NTSC_HEIGHT	480

/* FIXME: this temporarily, until these are included in linux/fb.h */
#define FBIOPUT_DM270_COLORIMG	0x4680
#define FBCMD_DM270_PRINT_FBUF	0x468e
#define FBCMD_DM270_PRINT_REG	0x468f

/* FIXME: this temporarily, until these are included in linux/i2c-id.h */
/* sensor/video decoder chips */
#ifndef  I2C_DRIVERID_STV0974
# define I2C_DRIVERID_STV0974		I2C_DRIVERID_EXP0
#endif
#ifndef  I2C_DRIVERID_MT9V111
# define I2C_DRIVERID_MT9V111		I2C_DRIVERID_EXP1
#endif
#ifndef  I2C_DRIVERID_TCM8210MDA
# define I2C_DRIVERID_TCM8210MDA	I2C_DRIVERID_EXP2
#endif
#ifndef  I2C_DRIVERID_SAA7113
# define I2C_DRIVERID_SAA7113		I2C_DRIVERID_EXP3
#endif
#ifndef  I2C_DRIVERID_TVP5150A
# define I2C_DRIVERID_TVP5150A		(I2C_DRIVERID_EXP0 + 4)
#endif

#ifndef  GPIO_DRIVERID_4T103X3M
# define GPIO_DRIVERID_4T103X3M		(I2C_DRIVERID_EXP0 + 5)
#endif

/* video encoder chips */
#ifndef MMIO_DRIVERID_DM270VENC
# define MMIO_DRIVERID_DM270VENC	(I2C_DRIVERID_EXP0 + 8)
#endif

/* FIXME: this temporarily, until these are included in linux/videodev2.h */
#ifndef V4L2_PIX_FMT_MPEG4
# define V4L2_PIX_FMT_MPEG4		v4l2_fourcc('M','P','G','4') /* MPEG4         */
#endif

/* FIXME: this temporarily, until these are included in linux/videodev2.h */
#ifndef V4L2_PIX_FMT_H263
# define V4L2_PIX_FMT_H263		v4l2_fourcc('H','2','6','3') /* H.263         */
#endif

/* FIXME: this temporarily, until these are included in linux/video_decoder.h */
#ifndef DECODER_GET_FIELD
# define DECODER_GET_FIELD		_IOR('d', 128, int)
#endif
#ifndef DECODER_SET_CROP
# define DECODER_SET_CROP		_IOW('d', 129, struct v4l2_crop)
#endif
#ifndef DECODER_SET_FMT
# define DECODER_SET_FMT		_IOW('d', 130, struct v4l2_format)
#endif
#ifndef DECODER_INIT
# define DECODER_INIT			_IOW('d', 131, int)
#endif
#ifndef DECODER_REG_READ
# define DECODER_REG_READ		_IOWR('d', 132, struct video_decoder_reg)
#endif
#ifndef DECODER_REG_WRITE
# define DECODER_REG_WRITE		_IOW('d', 133, struct video_decoder_reg)
#endif

/* FIXME: this temporarily, until these are included in linux/videodev2.h */
#ifndef VIDIOC_S_CAMERAPWR
# define VIDIOC_S_CAMERAPWR		_IOW('v', BASE_VIDIOCPRIVATE+4, int)
#endif
#ifndef VIDIOC_DBGPRINT
# define VIDIOC_DBGPRINT		_IOW('v', BASE_VIDIOCPRIVATE+7, int)
#endif
#ifndef VIDIOC_PRTCAPTURE
# define VIDIOC_PRTCAPTURE		_IOW('v', BASE_VIDIOCPRIVATE+8, int)
#endif
#ifndef VIDIOC_PRTOUTPUT
# define VIDIOC_PRTOUTPUT		_IOW('v', BASE_VIDIOCPRIVATE+9, int)
#endif
#ifndef VIDIOC_PRTOVERLAY
# define VIDIOC_PRTOVERLAY		_IOW('v', BASE_VIDIOCPRIVATE+10, int)
#endif
#ifndef VIDIOC_G_DECREG
# define VIDIOC_G_DECREG		_IOWR('v', BASE_VIDIOCPRIVATE+11, struct video_decoder_reg)
#endif
#ifndef VIDIOC_S_DECREG
# define VIDIOC_S_DECREG		_IOW('v', BASE_VIDIOCPRIVATE+12, struct video_decoder_reg)
#endif
