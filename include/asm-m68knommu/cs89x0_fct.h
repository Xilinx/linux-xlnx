
/* include/asm-m68knommu/cs89x0_fct.h: arch/platformm specific code for CS89x0
 *
 * Copyright (C) 2004  Georges Menie
 *
 */

#ifndef _CS89X0_FCT_H_
#define _CS89X0_FCT_H_

#if defined(CONFIG_UCSIMM)

static inline int cs89x_hw_init_hook(struct net_device *dev, int unit)
{
	extern unsigned char *cs8900a_hwaddr;

	if (unit != 0)
		return 1; /* only one device */

	/* set up the chip select */
	*(volatile unsigned  char *)0xfffff42b |= 0x01; /* output /sleep */
	*(volatile unsigned short *)0xfffff428 |= 0x0101; /* not sleeping */
	*(volatile unsigned  char *)0xfffff42b &= ~0x02; /* input irq5 */
	*(volatile unsigned short *)0xfffff428 &= ~0x0202; /* irq5 fcn on */
	*(volatile unsigned short *)0xfffff102 = 0x8000; /* 0x04000000 */
	*(volatile unsigned short *)0xfffff112 = 0x01e1; /* 128k, 2ws, FLASH, en */

	dev->base_addr = 0x10000301;
	dev->irq = IRQ5_IRQ_NUM;
	memcpy(dev->dev_addr, cs8900a_hwaddr, 6);

	return 0;
}

static inline int cs89x_set_irq(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

	writereg(dev, PP_BusCTL, 0);    /* Disable Interrupts. */
	write_irq(dev, lp->chip_type, dev->irq);
	*(volatile unsigned short *)0xfffff302 |= 0x0080; /* +ve pol irq */
	if (request_irq(dev->irq, &net_interrupt, IRQ_FLG_STD, dev->name, dev)) {
		if (net_debug)
			printk(KERN_DEBUG "cs89x0: request_irq(%d) failed\n", dev->irq);
		return 1;
	}
	writereg(dev, PP_BusCTL, readreg(dev, PP_BusCTL)|ENABLE_IRQ );

	return 0;
}

#elif defined(CONFIG_UCDIMM)

static inline int cs89x_hw_init_hook(struct net_device *dev, int unit)
{
	extern unsigned char *cs8900a_hwaddr;

	if (unit != 0)
		return 1; /* only one device */

	/* set up the chip select */
	*(volatile unsigned  char *)0xfffff430 |= 0x08;
	*(volatile unsigned  char *)0xfffff433 |= 0x08;
	*(volatile unsigned  char *)0xfffff431 |= (0x08); /* sleep */
	*(volatile unsigned  char *)0xfffff42b &= ~0x02; /* input irq5 */
	*(volatile unsigned short *)0xfffff428 &= ~0x0202; /* irq5 fcn on */
	*(volatile unsigned short *)0xfffff102 = 0x8000; /* 0x04000000 */
	*(volatile unsigned short *)0xfffff112 = 0x01e1; /* 128k, 2ws, FLASH, en */

	dev->base_addr = 0x10000301;
	dev->irq = IRQ5_IRQ_NUM;
	memcpy(dev->dev_addr, cs8900a_hwaddr, 6);

	return 0;
}

static inline int cs89x_set_irq(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

	writereg(dev, PP_BusCTL, 0);    /* Disable Interrupts. */
	write_irq(dev, lp->chip_type, dev->irq);
	*(volatile unsigned short *)0xfffff302 |= 0x0080; /* +ve pol irq */
	if (request_irq(dev->irq, &net_interrupt, IRQ_FLG_STD, dev->name, dev)) {
		if (net_debug)
			printk(KERN_DEBUG "cs89x0: request_irq(%d) failed\n", dev->irq);
		return 1;
	}
	writereg(dev, PP_BusCTL, readreg(dev, PP_BusCTL)|ENABLE_IRQ );

	return 0;
}

#elif defined(CONFIG_DRAGEN2)

static inline int cs89x_hw_init_hook(struct net_device *dev, int unit)
{
	if (net_debug)
		printk("cs89x0:cs89x0_hw_init_hook(%d)\n", unit);

	if (unit != 0)
		return 1; /* only one device */

	dev->base_addr = 0x08000041;
	dev->irq = INT1_IRQ_NUM;
	memcpy(dev->dev_addr, (void *) 0x400fffa, 6);

	return 0;
}

static inline int cs89x_set_irq(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

	writereg(dev, PP_BusCTL, 0);    /* Disable Interrupts. */
	write_irq(dev, lp->chip_type, dev->irq);
	if (request_irq(dev->irq, &net_interrupt, IRQ_FLG_STD, dev->name, dev)) {
		if (net_debug)
			printk(KERN_DEBUG "cs89x0: request_irq(%d) failed\n", dev->irq);
		return 1;
	}
	writereg(dev, PP_BusCTL, readreg(dev, PP_BusCTL)|ENABLE_IRQ );

	return 0;
}

#elif defined(CONFIG_EZ328LCD) || defined(CONFIG_VZ328LCD)

static inline int cs89x_hw_init_hook(struct net_device *dev, int unit)
{
	if (unit != 0)
		return 1; /* only one device */

	dev->base_addr = 0x2000301;
	dev->irq = IRQ5_IRQ_NUM;
	dev->dev_addr[0] = 0x00;
	dev->dev_addr[1] = 0x10;
	dev->dev_addr[2] = 0x8b;
	dev->dev_addr[3] = 0xf1;
	dev->dev_addr[4] = 0xda;
	dev->dev_addr[5] = 0x01;

	return 0;
}

static inline int cs89x_set_irq(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

	writereg(dev, PP_BusCTL, 0);    /* Disable Interrupts. */
	write_irq(dev, lp->chip_type, dev->irq);
	*(volatile unsigned short *)0xfffff302 |= 0x0080; /* +ve pol irq */
	if (request_irq(dev->irq, &net_interrupt, IRQ_FLG_STD, dev->name, dev)) {
		if (net_debug)
			printk(KERN_DEBUG "cs89x0: request_irq(%d) failed\n", dev->irq);
		return 1;
	}
	writereg(dev, PP_BusCTL, readreg(dev, PP_BusCTL)|ENABLE_IRQ );

	return 0;
}

#elif defined(CONFIG_ARCH_TA7S)

static inline int cs89x_hw_init_hook(struct net_device *dev, int unit)
{
	if (unit != 0)
		return 1; /* only one device */

	dev->base_addr = 0x10000001;
	dev->irq = IRQ_CSL_USER_0;

	return 0;
}

static inline int cs89x_set_irq(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

	writereg(dev, PP_BusCTL, 0);    /* Disable Interrupts. */
	write_irq(dev, lp->chip_type, dev->irq);
	if (request_irq(dev->irq, &net_interrupt, SA_INTERRUPT, dev->name, dev)) {
		if (net_debug)
			printk(KERN_DEBUG "cs89x0: request_irq(%d) failed\n", dev->irq);
		return 1;
	}
	writereg(dev, PP_BusCTL, readreg(dev, PP_BusCTL)|ENABLE_IRQ );

	return 0;
}

#elif defined(CONFIG_DRAGONIXVZ)

static inline int cs89x_hw_init_hook(struct net_device *dev, int unit)
{
	extern unsigned char cs8900a_hwaddr1[6];

	if (unit != 0)
		return 1; /* only one device */

	/* set up the chip select */
	*(volatile unsigned  char *)0xfffff41b &= ~0x80; /* input irq6 */
	*(volatile unsigned  char *)0x04000105= 0x01; /* nSleep=1 */

	dev->base_addr = 0x4000001;
	dev->irq = IRQ6_IRQ_NUM;
	memcpy(dev->dev_addr, cs8900a_hwaddr1, 6);

	return 0;
}

static inline int cs89x_set_irq(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	
	writereg(dev, PP_BusCTL, 0);    /* Disable Interrupts. */
	write_irq(dev, lp->chip_type, dev->irq);
	*(volatile unsigned short *)0xfffff302 &= ~0x1100; /* -ve pol, level sensitive irq */
	if (request_irq(dev->irq, &net_interrupt, IRQ_FLG_STD, dev->name, dev)) {
		if (net_debug)
			printk(KERN_DEBUG "cs89x0: request_irq(%d) failed\n", dev->irq);
		return 1;
	}
	writereg(dev, PP_BusCTL, readreg(dev, PP_BusCTL)|ENABLE_IRQ );

	return 0;
}

#elif defined(CONFIG_CWVZ328)

static inline int cs89x_hw_init_hook(struct net_device *dev, int unit)
{
	if (unit != 0)
		return 1; /* only one device */

	*(volatile unsigned  char *)0xfffff42b |= 0x01; /* output /sleep */
	*(volatile unsigned short *)0xfffff428 |= 0x0101; /* not sleeping */
	*(volatile unsigned  char *)0xfffff42b &= ~0x02; /* input irq5 */
	*(volatile unsigned short *)0xfffff428 &= ~0x0202; /* irq5 fcn on */
	*(volatile unsigned short *)0xfffff102 = 0x2000; /* 0x4000000 */
	*(volatile unsigned short *)0xfffff112 = 0x01e1; /* 128k, 2ws, FLASH, en */

	dev->base_addr = 0x4000001;
	dev->irq = IRQ5_IRQ_NUM;
	dev->dev_addr[0] = 0x00;
	dev->dev_addr[1] = 0x10;
	dev->dev_addr[2] = 0x8b;
	dev->dev_addr[3] = 0xf1;
	dev->dev_addr[4] = 0xda;
	dev->dev_addr[5] = 0x01;

	return 0;
}

static inline int cs89x_set_irq(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

	writereg(dev, PP_BusCTL, 0);    /* Disable Interrupts. */
	write_irq(dev, lp->chip_type, dev->irq);
	*(volatile unsigned short *)0xfffff302 |= 0x0080; /* +ve pol irq */
	if (request_irq(dev->irq, &net_interrupt, IRQ_FLG_STD, dev->name, dev)) {
		if (net_debug)
			printk(KERN_DEBUG "cs89x0: request_irq(%d) failed\n", dev->irq);
		return 1;
	}
	writereg(dev, PP_BusCTL, readreg(dev, PP_BusCTL)|ENABLE_IRQ );

	return 0;
}

#elif defined(CONFIG_EXCALIBUR)

static inline int cs89x_hw_init_hook(struct net_device *dev, int unit)
{
	extern unsigned char *cs8900a_hwaddr;

	if (unit != 0)
		return 1; /* only one device */

	*(char *)na_enet = 0;			/* Reset the chip to a usable state. */
#if 0 /* this is done in cs89x0_probe1 if (ioaddr & 1) flag is set */
	dev->base_addr = ioaddr;
	if (readreg(dev, PP_ChipID) != CHIP_EISA_ID_SIG) {
		return -ENODEV;
	}
#endif
#ifdef na_enet_reset_n
	*(volatile unsigned char*)na_enet_reset_n=3;
#endif

	dev->base_addr = na_enet+1;
	dev->irq = na_enet_irq;
	memcpy(dev->dev_addr, cs8900a_hwaddr, 6);

	return 0;
}

static inline int cs89x_set_irq(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

	writereg(dev, PP_BusCTL, 0);    /* Disable Interrupts. */
	write_irq(dev, lp->chip_type, dev->irq);
	if (request_irq(dev->irq, &net_interrupt, SA_INTERRUPT, dev->name, dev)) {
		if (net_debug)
			printk(KERN_DEBUG "cs89x0: request_irq(%d) failed\n", dev->irq);
		return 1;
	}
	writereg(dev, PP_BusCTL, readreg(dev, PP_BusCTL)|ENABLE_IRQ );

	return 0;
}

#elif defined(CONFIG_HYPERSTONE_CS89X0)

static inline int cs89x_hw_init_hook(struct net_device *dev, int unit)
{
	if (unit != 0)
		return 1; /* only one device */

	dev->base_addr = 0x01000301;
	dev->irq = CONFIG_HYPERSTONE_CS89X0_IRQ-1;
	memcpy(dev->dev_addr, "\x48\x79\x4c\x6e\x78\x30", 6); /* FIXME */

	return 0;
}

static inline int cs89x_set_irq(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

	writereg(dev, PP_BusCTL, 0);    /* Disable Interrupts. */
	write_irq(dev, lp->chip_type, dev->irq);
	if (request_irq(dev->irq, &net_interrupt, SA_INTERRUPT, dev->name, dev)) {
		if (net_debug)
			printk(KERN_DEBUG "cs89x0: request_irq(%d) failed\n", dev->irq);
		return 1;
	}
	writereg(dev, PP_BusCTL, readreg(dev, PP_BusCTL)|ENABLE_IRQ );

	return 0;
}

#endif

#endif
