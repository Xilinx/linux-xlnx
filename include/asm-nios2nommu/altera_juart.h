/*------------------------------------------------------------------------
 *
 *  linux/drivers/serial/altera_juart.h
 *
 *  Driver for Altera JTAG UART core with Avalon interface
 *
 * Copyright (C) 2004 Microtronix Datacom Ltd
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
 * History:
 *    Jun/20/2005   DGT Microtronix Datacom NiosII
 *
 -----------------------------------------------------------------------*/

#ifndef _ALTERA_JUART_H_
    #define _ALTERA_JUART_H_

    /* jtag uart details needed outside of the driver itself:           */
    /*  by: arch/kernel/start.c - boot time error message(s)            */

    void jtaguart_console_write
            (       struct console  *co,
             const  char            *s,
                    unsigned int     count);

#endif  /* _ALTERA_JUART_H_ */
