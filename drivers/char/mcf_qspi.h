#if !defined(MCF_QSPI_H)
#define MCF_QSPI_H

#include <asm/mcfqspi.h>
#include <linux/types.h>


#define QSPI_MAJOR              126
#if defined(CONFIG_M5249)
#define MCFQSPI_IRQ_VECTOR      27
#define QSPIMOD_OFFSET          0x400
#elif defined(CONFIG_M523x)
#define MCF5235ICM_INTC0	0xC00
#define MCFINTC0_ICR		0x40
#define MCFQSPI_IRQ_VECTOR  82
#define QSPIMOD_OFFSET      0x340
#define IRQ_SOURCE			18
#define MCF5235INTC_IMRL	0x0C
#elif (defined(CONFIG_M5282) || defined(CONFIG_M5280))
#define MCFQSPI_IRQ_VECTOR      (64 + 18)
#define QSPIMOD_OFFSET          0x340
#else
#define MCFQSPI_IRQ_VECTOR      89
#define QSPIMOD_OFFSET          0xa0
#endif


/* QSPI registers */
#define MCFSIM_QMR              (0x00 + QSPIMOD_OFFSET) /* mode */
#define MCFSIM_QDLYR            (0x04 + QSPIMOD_OFFSET) /* delay */
#define MCFSIM_QWR              (0x08 + QSPIMOD_OFFSET) /* wrap */
#define MCFSIM_QIR              (0x0c + QSPIMOD_OFFSET) /* interrupt */
#define MCFSIM_QAR              (0x10 + QSPIMOD_OFFSET) /* address */
#define MCFSIM_QDR              (0x14 + QSPIMOD_OFFSET) /* address */

#define TX_RAM_START            0x00
#define RX_RAM_START            0x10
#define COMMAND_RAM_START       0x20

#define QMR                     *(volatile u16 *)(MCF_MBAR + MCFSIM_QMR)
#define QAR                     *(volatile u16 *)(MCF_MBAR + MCFSIM_QAR)
#define QDR                     *(volatile u16 *)(MCF_MBAR + MCFSIM_QDR)
#define QWR                     *(volatile u16 *)(MCF_MBAR + MCFSIM_QWR)
#define QDLYR                   *(volatile u16 *)(MCF_MBAR + MCFSIM_QDLYR)
#define QIR                     *(volatile u16 *)(MCF_MBAR + MCFSIM_QIR)


/* bits */
#define QMR_MSTR                0x8000  /* master mode enable: must always be set */
#define QMR_DOHIE               0x4000  /* shut off (hi-z) Dout between transfers */
#define QMR_BITS                0x3c00  /* bits per transfer (size) */
#define QMR_CPOL                0x0200  /* clock state when inactive */
#define QMR_CPHA                0x0100  /* clock phase: 1 = data taken at rising edge */
#define QMR_BAUD                0x00ff  /* clock rate divider */

#define QIR_WCEF                0x0008  /* write collison */
#define QIR_ABRT                0x0004  /* abort */
#define QIR_SPIF                0x0001  /* finished */
#define QIR_SETUP               0xdd0f  /* setup QIR for tranfer start */
#define QIR_SETUP_POLL          0xdc0d  /* setup QIR for tranfer start */

#define QWR_CSIV                0x1000  /* 1 = active low chip selects */

#define QDLYR_SPE               0x8000  /* initiates transfer when set */
#define QDLYR_QCD               0x7f00  /* start delay between CS and first clock */
#define QDLYR_DTL               0x00ff  /* delay after CS release */

/* QCR: chip selects return to inactive, bits set in QMR[BITS],
 * after delay set in QDLYR[DTL], pre-delay set in QDLYR[QCD] */
#define QCR_SETUP               0x7000
#define QCR_CONT                0x8000  /* 1=continuous CS after transfer */
#define QCR_SETUP8              0x3000  /* sets BITSE to 0 => eigth bits per transfer */


typedef struct qspi_dev {
        qspi_read_data read_data;
        u8 bits;                        /* transfer size, number of bits to transfer for each entry */
        u8 baud;                        /* baud rate */
        u8 qcd;                         /* QSPILCK delay */
        u8 dtl;                         /* delay after transfer */
        unsigned int qcr_cont   : 1;    /* keep CS active throughout transfer */
        unsigned int odd_mod    : 1;    /* if length of buffer is a odd number, 16-bit transfers
                                           are finalized with a 8-bit transfer */
        unsigned int dsp_mod    : 1;    /* transfers are bounded to 15/30 bytes
                                           (= a multiple of 3 bytes = 1 word) */
        unsigned int poll_mod   : 1;    /* mode polling or interrupt */
        unsigned int cpol       : 1;    /* SPI clock polarity */
        unsigned int cpha       : 1;    /* SPI clock phase */
        unsigned int dohie      : 1;    /* data output high impedance enable */
} qspi_dev;


#endif  /* MCF_QSPI_H */

