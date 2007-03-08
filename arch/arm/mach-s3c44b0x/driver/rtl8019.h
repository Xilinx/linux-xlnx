
#define SHIFT(x)	(x<<1)

#define	BaseAddr	0x6000000
#define	RWPORT	(BaseAddr+SHIFT(0x10))	/* dma read write address, form 0x10 - 0x17 */
#define	RstAddr (BaseAddr+SHIFT(0x18))	/* reset register, 0x18, 0x1a, 0x1c, 0x1e even address is recommanded */

/* page 0 */
#define	Pstart	(BaseAddr+SHIFT(1))	/* page start */
#define	Pstop	(BaseAddr+SHIFT(2))	/* page stop */
#define	BNRY	(BaseAddr+SHIFT(3))	
#define	TPSR	(BaseAddr+SHIFT(4))	/* transmit page start */
#define	TBCR0	(BaseAddr+SHIFT(5))
#define	TBCR1	(BaseAddr+SHIFT(6))
#define	ISR	(BaseAddr+SHIFT(7))	/* interrupt status register */

#define	RSAR0	(BaseAddr+SHIFT(8))	/* dma read address */
#define	RSAR1	(BaseAddr+SHIFT(9))
#define	RBCR0	(BaseAddr+SHIFT(10))	/* dma read byte count */
#define	RBCR1	(BaseAddr+SHIFT(11))

#define	RCR	(BaseAddr+SHIFT(12))	/* receive config */
#define	TCR	(BaseAddr+SHIFT(13))	/* transmit config */
#define	DCR	(BaseAddr+SHIFT(14))	/* data config */
#define	IMR	(BaseAddr+SHIFT(15))	/* interrupt mask */

#define	ID8019L	(BaseAddr+SHIFT(10))
#define	ID8019H	(BaseAddr+SHIFT(11))

/* page 1 */
#define	PAR0	(BaseAddr+SHIFT(1))
#define	PAR1	(BaseAddr+SHIFT(2))
#define	PAR2	(BaseAddr+SHIFT(3))
#define	PAR3	(BaseAddr+SHIFT(4))
#define	PAR4	(BaseAddr+SHIFT(5))
#define	PAR6	(BaseAddr+SHIFT(6))

#define	CURR	(BaseAddr+SHIFT(7))		
#define	MAR0	(BaseAddr+SHIFT(8))
#define	MAR1	(BaseAddr+SHIFT(9))
#define	MAR2	(BaseAddr+SHIFT(10))
#define	MAR3	(BaseAddr+SHIFT(11))
#define	MAR4	(BaseAddr+SHIFT(12))
#define	MAR5	(BaseAddr+SHIFT(13))
#define	MAR6	(BaseAddr+SHIFT(14))
#define	MAR7	(BaseAddr+SHIFT(15))

/* page 2 */

/* page 3 */
#define	CR9346	(BaseAddr+SHIFT(1))
#define	CONFIG0	(BaseAddr+SHIFT(3))
#define	CONFIG1	(BaseAddr+SHIFT(4))
#define	CONFIG2	(BaseAddr+SHIFT(5))
#define	CONFIG3	(BaseAddr+SHIFT(6))
