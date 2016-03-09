/*
 * IOCTL constants for the FSL FIFO device/driver
 *
 * Copyright (C) 2004 John Williams <jwilliams@itee.uq.edu.au>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 */

#ifndef _FSLFIFO_IOCTL_H
#define _FSLFIFO_IOCTL_H

#include <linux/ioctl.h>

#define FSLFIFO_MAGIC 'f'

/* Reset the SW buffers */
#define FSLFIFO_IOCRESET _IO(FSLFIFO_MAGIC, 0)

/*
 * S means "Set" through a ptr
 * T means "Tell" directly with the argument value
 * G means "Get" reply by setting through a ptr
 * Q means "Query" response is on the return value
 */

/* Write a control value to the FSL channel */
#define FSLFIFO_IOCTCONTROL _IOW(FSLFIFO_MAGIC, 1, unsigned)

/* Read a control value from the FSL channel */
#define FSLFIFO_IOCQCONTROL _IOR(FSLFIFO_MAGIC, 2, unsigned)

/* Set the READ data width.  This forces a reset of the SW buffers */
#define FSLFIFO_IOCTRWIDTH  _IOW(FSLFIFO_MAGIC, 3, unsigned)

/* Read the READ data width */
#define FSLFIFO_IOCQRWIDTH  _IOR(FSLFIFO_MAGIC, 4, unsigned)

/* Set the WRITE data width.  This forces a reset of the SW buffers */
#define FSLFIFO_IOCTWWIDTH  _IOW(FSLFIFO_MAGIC, 5, unsigned)

/* Read the WRITE data width */
#define FSLFIFO_IOCQWWIDTH  _IOR(FSLFIFO_MAGIC, 6, unsigned)


#define FSLFIFO_IOC_MAXNR 6

#endif


