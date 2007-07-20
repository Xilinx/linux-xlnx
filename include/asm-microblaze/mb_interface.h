////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2004 Xilinx, Inc.  All rights reserved. 
// 
// Xilinx, Inc. 
// XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS" AS A 
// COURTESY TO YOU.  BY PROVIDING THIS DESIGN, CODE, OR INFORMATION AS 
// ONE POSSIBLE   IMPLEMENTATION OF THIS FEATURE, APPLICATION OR 
// STANDARD, XILINX IS MAKING NO REPRESENTATION THAT THIS IMPLEMENTATION 
// IS FREE FROM ANY CLAIMS OF INFRINGEMENT, AND YOU ARE RESPONSIBLE 
// FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE FOR YOUR IMPLEMENTATION. 
// XILINX EXPRESSLY DISCLAIMS ANY WARRANTY WHATSOEVER WITH RESPECT TO 
// THE ADEQUACY OF THE IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO 
// ANY WARRANTIES OR REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE 
// FROM CLAIMS OF INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY 
// AND FITNESS FOR A PARTICULAR PURPOSE. 
// 
// File   : mb_interface.h
// Date   : 2002, March 20.
// Company: Xilinx
// Group  : Emerging Software Technologies
//
// Summary:
// Header file for mb_interface
//
// $Id: mb_interface.h,v 1.1.2.1 2007/03/13 15:45:51 akondratenko Exp $
//
////////////////////////////////////////////////////////////////////////////////

#ifndef _MICROBLAZE_INTERFACE_H_
#define _MICROBLAZE_INTERFACE_H_

#include "asm/xbasic_types.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void microblaze_enable_interrupts(void);                 /* Enable Interrupts */
extern void microblaze_disable_interrupts(void);                /* Disable Interrupts */
extern void microblaze_enable_icache(void);                     /* Enable Instruction Cache */
extern void microblaze_disable_icache(void);                    /* Disable Instruction Cache */
extern void microblaze_enable_dcache(void);                     /* Enable Instruction Cache */
extern void microblaze_disable_dcache(void);                    /* Disable Instruction Cache */
extern void microblaze_enable_exceptions(void);                 /* Enable hardware exceptions */
extern void microblaze_disable_exceptions(void);                /* Disable hardware exceptions */
extern void microblaze_register_handler(XInterruptHandler Handler, void *DataPtr);                               /* Register top level interrupt handler */
extern void microblaze_register_exception_handler(u8 ExceptionId, XExceptionHandler Handler, void *DataPtr); /* Register exception handler */
extern void microblaze_update_icache (int , int , int );
extern void microblaze_init_icache_range (int , int );
extern void microblaze_update_dcache (int , int , int );
extern void microblaze_init_dcache_range (int , int );


/* necessary for pre-processor */
#define stringify(s)    tostring(s)
#define tostring(s)     #s

/* FSL Access Macros */

/* Blocking Data Read and Write to FSL no. id */
#define getfsl(val, id)         asm volatile ("get\t%0,rfsl" stringify(id) : "=d" (val))
#define putfsl(val, id)         asm volatile ("put\t%0,rfsl" stringify(id) :: "d" (val))

/* Non-blocking Data Read and Write to FSL no. id */
#define ngetfsl(val, id)        asm volatile ("nget\t%0,rfsl" stringify(id) : "=d" (val))
#define nputfsl(val, id)        asm volatile ("nput\t%0,rfsl" stringify(id) :: "d" (val))

/* Blocking Control Read and Write to FSL no. id */
#define cgetfsl(val, id)        asm volatile ("cget\t%0,rfsl" stringify(id) : "=d" (val))
#define cputfsl(val, id)        asm volatile ("cput\t%0,rfsl" stringify(id) :: "d" (val))

/* Non-blocking Control Read and Write to FSL no. id */
#define ncgetfsl(val, id)       asm volatile ("ncget\t%0,rfsl" stringify(id) : "=d" (val))
#define ncputfsl(val, id)       asm volatile ("ncput\t%0,rfsl" stringify(id) :: "d" (val))

/* Polling versions of FSL access macros. This makes the FSL access interruptible */
#define getfsl_interruptible(val, id)       asm volatile ("\n1:\n\tnget\t%0,rfsl" stringify(id) "\n\t"   \
                                                          "addic\tr18,r0,0\n\t"                \
                                                          "bnei\tr18,1b\n"                     \
                                                           : "=d" (val) :: "r18")

#define putfsl_interruptible(val, id)       asm volatile ("\n1:\n\tnput\t%0,rfsl" stringify(id) "\n\t"   \
                                                          "addic\tr18,r0,0\n\t"                \
                                                          "bnei\tr18,1b\n"                     \
                                                          :: "d" (val) : "r18")

#define cgetfsl_interruptible(val, id)      asm volatile ("\n1:\n\tncget\t%0,rfsl" stringify(id) "\n\t"  \
                                                          "addic\tr18,r0,0\n\t"                \
                                                          "bnei\tr18,1b\n"                     \
                                                          : "=d" (val) :: "r18")

#define cputfsl_interruptible(val, id)      asm volatile ("\n1:\n\tncput\t%0,rfsl" stringify(id) "\n\t"  \
                                                          "addic\tr18,r0,0\n\t"                \
                                                          "bnei\tr18,1b\n"                     \
                                                          :: "d" (val) : "r18")
/* FSL valid and error check macros. */
#define fsl_isinvalid(result)               asm volatile ("addic\t%0,r0,0"  : "=d" (result))
#define fsl_iserror(error)                  asm volatile ("mfs\t%0,rmsr\n\t"  \
                                                              "andi\t%0,%0,0x10" : "=d" (error))

/* Pseudo assembler instructions */
#define mfgpr(rn)       ({  unsigned int _rval;         \
                            __asm__ __volatile__ (      \
                                "or\t%0,r0," stringify(rn) "\n" : "=d"(_rval) \
                            );                          \
                            _rval;                      \
                        })

#define mfmsr()         ({  unsigned int _rval;         \
                            __asm__ __volatile__ (      \
                                "mfs\t%0,rmsr\n" : "=d"(_rval) \
                            );                          \
                            _rval;                      \
                        })

#define mfear()         ({  unsigned int _rval;         \
                            __asm__ __volatile__ (      \
                                "mfs\t%0,rear\n" : "=d"(_rval) \
                            );                          \
                            _rval;                      \
                        })

#define mfesr()         ({  unsigned int _rval;         \
                            __asm__ __volatile__ (      \
                                "mfs\t%0,resr\n" : "=d"(_rval) \
                            );                          \
                            _rval;                      \
                        })

#define mffsr()         ({  unsigned int _rval;         \
                            __asm__ __volatile__ (      \
                                "mfs\t%0,rfsr\n" : "=d"(_rval) \
                            );                          \
                            _rval;                      \
                        })

#define mtgpr(rn, v)    ({  __asm__ __volatile__ (      \
                            "or\t" stringify(rn) ",r0,%0\n" :: "d" (v)    \
                            );                          \
                        })

#define mtmsr(v)        ({  __asm__ __volatile__ (      \
                            "mts\trmsr,%0\n\tnop\n" ::"d" (v) \
                            );                          \
                        })

#define microblaze_getfpex_operand_a()     ({          \
                                    extern unsigned int mb_fpex_op_a;   \
                                    mb_fpex_op_a;                       \
                                })

#define microblaze_getfpex_operand_b()     ({          \
                                    extern unsigned int mb_fpex_op_b;   \
                                    mb_fpex_op_b;                       \
                                })

/* Deprecated MicroBlaze FSL macros */
#define microblaze_bread_datafsl(val, id)       getfsl(val,id)
#define microblaze_bwrite_datafsl(val, id)      putfsl(val,id)
#define microblaze_nbread_datafsl(val, id)      ngetfsl(val,id)
#define microblaze_nbwrite_datafsl(val, id)     nputfsl(val,id)
#define microblaze_bread_cntlfsl(val, id)       cgetfsl(val,id)
#define microblaze_bwrite_cntlfsl(val, id)      cputfsl(val,id)
#define microblaze_nbread_cntlfsl(val, id)      ncgetfsl(val,id)
#define microblaze_nbwrite_cntlfsl(val, id)     ncputfsl(val,id)

#ifdef __cplusplus
}
#endif
#endif // _MICROBLAZE_INTERFACE_H_
