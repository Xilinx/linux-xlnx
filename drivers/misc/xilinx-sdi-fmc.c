// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx SDI FMC (Fidus/Zetta SDI FMC) mezzanine configuration.
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 *
 * Author: Katta Dhanunjanrao <katta.dhanunjanrao@amd.com>
 *
 * The Fidus SDI FMC mezzanine carries, per SDI channel, a MACOM reclocker
 * (M23145), a cable driver (M23428) and an equalizer/receiver (M23554), all
 * reached over a single AXI Quad-SPI bus.  A DS10CP15A clock crosspoint is
 * reached over AXI I2C, and an AXI GPIO both selects which of the four physical
 * channels is routed onto the SPI bus and raises an "init-done" strobe once
 * configuration completes.
 *
 * Tx/Rx analysis
 * --------------
 * The mezzanine is a single physical card.  One configuration pass programs the
 * equalizer (Rx path), the cable driver (Tx path) and the reclocker (both) for
 * all four channels.  Configuration is therefore required by *both* directions
 * but is a *single common* init, so it lives in a driver of its own rather than
 * inside either endpoint.  That also keeps the V4L2 Rx driver from having to
 * depend on the DRM Tx driver to reach it.
 *
 * Device model
 * ------------
 * The mezzanine is described by a node of its own, bound by the platform driver
 * below, whose probe performs the one-time configuration.  The individual parts
 * are children of their respective controllers and are bound by the i2c_driver
 * and spi_driver registered here, so each is a real driver-owned device rather
 * than an orphan client that some other driver reaches across the bus to poke.
 *
 * SDI endpoints do not configure the card themselves.  They point at the FMC
 * node with an "xlnx,fmc" phandle and call xlnx_sdi_fmc_init() from
 * <linux/xlnx_sdi_fmc.h>, which simply waits for that device to finish
 * probing.  Ordering therefore falls out of the driver model instead of a
 * global "already configured" flag, and a system with two mezzanines works
 * without further changes.
 *
 * That helper is driver-model glue and never calls back into this driver, so
 * nothing here is exported and an endpoint does not have to be built alongside
 * the mezzanine driver to use it.
 */

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

/* Number of physical SDI channels behind the GPIO mux. */
#define SDI_FMC_NCHAN		4

/* SPI transaction framing (bit7 of the address byte): write=0, read=1. */
#define SDI_FMC_SPI_WR		0x00
#define SDI_FMC_SPI_RD		0x80
#define SDI_FMC_SPI_DUMMY	0xFF

/* The AXI Quad-SPI is built for 16-bit words: addr in [15:8], data in [7:0]. */
#define SDI_FMC_SPI_BITS	16
#define SDI_FMC_SPI_ADDR_SHIFT	8
#define SDI_FMC_SPI_ADDR_MASK	GENMASK(6, 0)
#define SDI_FMC_SPI_DATA_MASK	GENMASK(7, 0)

/* Device-ID / version register common to the reclocker and receiver. */
#define SDI_FMC_DEV_ID_REG	0x01

/*
 * Device-ID families.  The reclocker answers in the 0x8x family and the
 * receiver in the 0x0x family, the low nibble being the die revision.
 */
#define SDI_FMC_ID_FAMILY_MASK	GENMASK(7, 4)
#define SDI_FMC_RCLKR_FAMILY	0x80
#define SDI_FMC_RCVR_FAMILY	0x00

/*
 * A bus that is not reaching a part clocks back all-zeroes or all-ones.  The
 * receiver family is 0x0x, so 0x00 would otherwise pass the family check and
 * be mistaken for an unknown die revision.
 */
#define SDI_FMC_ID_ALL_ZEROS	0x00
#define SDI_FMC_ID_ALL_ONES	0xFF

/* DS10CP15A crosspoint: clock routing (IN2->OUT0 148.5, IN3->OUT1 148.35). */
#define SDI_FMC_XBAR_CLK_REG	0x00
#define SDI_FMC_XBAR_CLK_VAL	0x0E

/* GPIO channel-mux is 2 bits [1:0]. */
#define SDI_FMC_CHAN_GPIO_BITS	2

/* Which bus a table entry targets. */
enum sdi_fmc_bus {
	SDI_FMC_BUS_IIC = 0,	/* DS10CP15A crosspoint over I2C */
	SDI_FMC_BUS_SPI,	/* MACOM reclocker/driver/receiver over SPI */
};

/* SPI slave role; index into xlnx_sdi_fmc.spi[]. */
enum sdi_fmc_spi_slave {
	SDI_FMC_RCLKR = 0,	/* Reclocker  M23145 */
	SDI_FMC_DRVR,		/* Cable drv  M23428 */
	SDI_FMC_RCVR,		/* Equalizer  M23554 */
	SDI_FMC_SPI_NDEV
};

/* One entry of a register-init / errata table. */
struct sdi_fmc_reg {
	enum sdi_fmc_bus	bus;
	enum sdi_fmc_spi_slave	slave;
	u8			regaddr;
	u8			regdata;
};

/* An errata sequence and the device ID it applies to. */
struct sdi_fmc_errata {
	u8				id;
	const struct sdi_fmc_reg	*tbl;
	size_t				n;
};

/* Bus devices and GPIOs resolved for the duration of one configuration pass. */
struct xlnx_sdi_fmc {
	struct device		*dev;
	struct i2c_client	*xbar;
	struct spi_device	*spi[SDI_FMC_SPI_NDEV];
	struct gpio_desc	*chan_gpio[SDI_FMC_CHAN_GPIO_BITS];
	struct gpio_desc	*done_gpio;
	/*
	 * SPI payloads must be DMA-safe, so the 16-bit words are staged here
	 * rather than on the stack.  One configuration pass runs at a time,
	 * so a single pair is enough.
	 */
	u16			spi_tx;
	u16			spi_rx;
};

/* Programmed once for every channel 0..3. */
static const struct sdi_fmc_reg sdi_fmc_chan_init[] = {
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCVR,  0x03, 0x0F }, /* out swing 800mV */
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCLKR, 0x0C, 0x20 }, /* adaptive BB EQ  */
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCLKR, 0x0D, 0x40 }, /* adaptive boost  */
	{ SDI_FMC_BUS_SPI, SDI_FMC_DRVR,  0x10, 0x1F }, /* input EQ max    */
	{ SDI_FMC_BUS_SPI, SDI_FMC_DRVR,  0x20, 0x71 }, /* mute SDIO1      */
	{ SDI_FMC_BUS_SPI, SDI_FMC_DRVR,  0x21, 0x82 }, /* derate SDIO2    */
};

/* Reclocker errata, selected by device-ID read from DEV_ID_REG. */
static const struct sdi_fmc_reg rclkr_errata_rev0[] = {
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCLKR, 0x43, 0x25 },
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCLKR, 0x11, 0x02 },
};

static const struct sdi_fmc_reg rclkr_errata_rev1[] = {
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCLKR, 0x22, 0x09 },
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCLKR, 0x32, 0x3F },
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCLKR, 0x3F, 0x03 },
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCLKR, 0x42, 0x2C },
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCLKR, 0x11, 0x02 },
};

static const struct sdi_fmc_reg rclkr_errata_rev2[] = {
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCLKR, 0x32, 0x3F },
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCLKR, 0x3F, 0x03 },
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCLKR, 0x11, 0x02 },
};

/* Receiver errata (device-ID 0x01, M23544G-12P). */
static const struct sdi_fmc_reg rcvr_errata_rev0[] = {
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCVR, 0x22, 0x09 },
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCVR, 0x32, 0x3F },
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCVR, 0x3F, 0x03 },
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCVR, 0x42, 0x2C },
	{ SDI_FMC_BUS_SPI, SDI_FMC_RCVR, 0x11, 0x02 },
};

/*
 * Only the die revisions listed here have published errata.  A part answering
 * in the right family but with no entry is a later revision that MACOM has
 * issued no corrections for, and the tables above must not be borrowed for it:
 * they carry VCO trim values for different silicon.
 */
static const struct sdi_fmc_errata rclkr_errata[] = {
	{ 0x80, rclkr_errata_rev0, ARRAY_SIZE(rclkr_errata_rev0) },
	{ 0x81, rclkr_errata_rev1, ARRAY_SIZE(rclkr_errata_rev1) },
	{ 0x82, rclkr_errata_rev2, ARRAY_SIZE(rclkr_errata_rev2) },
};

static const struct sdi_fmc_errata rcvr_errata[] = {
	{ 0x01, rcvr_errata_rev0, ARRAY_SIZE(rcvr_errata_rev0) },
};

/* ------------------------------------------------------------------------- */
/* Mezzanine part drivers                                                    */
/* ------------------------------------------------------------------------- */

/*
 * The MACOM parts have no readable configuration beyond the device-ID register
 * and are programmed as one unit by the init sequence below, so a single driver
 * binds all three roles.  Its job is to own the spi_device and fix the word
 * size; which role a given chip select plays is decided by the FMC node's
 * xlnx,rclkr / xlnx,drvr / xlnx,rcvr phandles.
 */
static int macom_sdi_probe(struct spi_device *spi)
{
	int ret;

	spi->bits_per_word = SDI_FMC_SPI_BITS;
	ret = spi_setup(spi);
	if (ret)
		return dev_err_probe(&spi->dev, ret, "spi_setup failed\n");

	return 0;
}

static const struct of_device_id macom_sdi_of_match[] = {
	{ .compatible = "macom,m23145" },	/* reclocker    */
	{ .compatible = "macom,m23428" },	/* cable driver */
	{ .compatible = "macom,m23554" },	/* equalizer    */
	{ }
};
MODULE_DEVICE_TABLE(of, macom_sdi_of_match);

static const struct spi_device_id macom_sdi_spi_ids[] = {
	{ "m23145" },
	{ "m23428" },
	{ "m23554" },
	{ }
};
MODULE_DEVICE_TABLE(spi, macom_sdi_spi_ids);

static struct spi_driver macom_sdi_driver = {
	.driver = {
		.name		= "macom-sdi-redriver",
		.of_match_table	= macom_sdi_of_match,
	},
	.id_table	= macom_sdi_spi_ids,
	.probe		= macom_sdi_probe,
};

static int ds10cp15a_probe(struct i2c_client *client)
{
	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA))
		return -EOPNOTSUPP;

	return 0;
}

static const struct of_device_id ds10cp15a_of_match[] = {
	{ .compatible = "ti,ds10cp154" },
	{ }
};
MODULE_DEVICE_TABLE(of, ds10cp15a_of_match);

static const struct i2c_device_id ds10cp15a_i2c_ids[] = {
	{ "ds10cp154" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ds10cp15a_i2c_ids);

static struct i2c_driver ds10cp15a_driver = {
	.driver = {
		.name		= "ds10cp15a",
		.of_match_table	= ds10cp15a_of_match,
	},
	.id_table	= ds10cp15a_i2c_ids,
	.probe		= ds10cp15a_probe,
};

/* ------------------------------------------------------------------------- */
/* Low-level bus helpers                                                     */
/* ------------------------------------------------------------------------- */

/* Drive the 2-bit channel mux (GPIO[1:0]); channel 0..3. */
static int sdi_fmc_set_channel(struct xlnx_sdi_fmc *fmc, u8 channel)
{
	unsigned int i;

	if (channel >= SDI_FMC_NCHAN)
		return -EINVAL;

	for (i = 0; i < SDI_FMC_CHAN_GPIO_BITS; i++)
		gpiod_set_value_cansleep(fmc->chan_gpio[i], (channel >> i) & 1);

	return 0;
}

/* DS10CP15A crosspoint register write over I2C. */
static int sdi_fmc_iic_write(struct xlnx_sdi_fmc *fmc, u8 reg, u8 data)
{
	int ret = i2c_smbus_write_byte_data(fmc->xbar, reg, data);

	if (ret)
		dev_err(fmc->dev, "xbar write [0x%02x]=0x%02x failed: %d\n",
			reg, data, ret);
	return ret;
}

/* DS10CP15A crosspoint register read over I2C. */
static int sdi_fmc_iic_read(struct xlnx_sdi_fmc *fmc, u8 reg)
{
	int ret = i2c_smbus_read_byte_data(fmc->xbar, reg);

	if (ret < 0)
		dev_err(fmc->dev, "xbar read [0x%02x] failed: %d\n", reg, ret);
	return ret;
}

/*
 * MACOM SPI register write.  This AXI Quad-SPI is configured for 16-bit
 * transfers (num-transfer-bits = 16), so address+data form a single 16-bit
 * word sent MSB-first: bits[15:8] = (WR | 7-bit addr), bits[7:0] = data.
 */
static int sdi_fmc_spi_write(struct xlnx_sdi_fmc *fmc,
			     enum sdi_fmc_spi_slave slave, u8 reg, u8 data)
{
	struct spi_transfer xfer = {
		.tx_buf		= &fmc->spi_tx,
		.len		= sizeof(fmc->spi_tx),
		.bits_per_word	= SDI_FMC_SPI_BITS,
	};

	if (slave >= SDI_FMC_SPI_NDEV || !fmc->spi[slave])
		return -ENODEV;

	fmc->spi_tx = ((u16)(SDI_FMC_SPI_WR | (reg & SDI_FMC_SPI_ADDR_MASK))
		       << SDI_FMC_SPI_ADDR_SHIFT) | data;

	return spi_sync_transfer(fmc->spi[slave], &xfer, 1);
}

/*
 * MACOM SPI register read.  The part needs two 16-bit words: the first latches
 * (RD | addr) and the second clocks the register contents out on MISO.  A
 * single word returns whatever MISO held during the latching phase - normally
 * 0x00 - which silently defeats the device-ID checks that select the errata
 * tables.
 *
 * Both words must be framed as separate chip-select cycles.  Issuing them as
 * one spi_message would hold CS low throughout and the part returns nothing,
 * so send two messages.
 */
static int sdi_fmc_spi_read(struct xlnx_sdi_fmc *fmc,
			    enum sdi_fmc_spi_slave slave, u8 reg)
{
	struct spi_transfer latch = {
		.tx_buf		= &fmc->spi_tx,
		.len		= sizeof(fmc->spi_tx),
		.bits_per_word	= SDI_FMC_SPI_BITS,
	};
	struct spi_transfer fetch = {
		.tx_buf		= &fmc->spi_tx,
		.rx_buf		= &fmc->spi_rx,
		.len		= sizeof(fmc->spi_tx),
		.bits_per_word	= SDI_FMC_SPI_BITS,
	};
	int ret;

	if (slave >= SDI_FMC_SPI_NDEV || !fmc->spi[slave])
		return -ENODEV;

	fmc->spi_tx = ((u16)(SDI_FMC_SPI_RD | (reg & SDI_FMC_SPI_ADDR_MASK))
		       << SDI_FMC_SPI_ADDR_SHIFT) | SDI_FMC_SPI_DUMMY;
	fmc->spi_rx = 0;

	ret = spi_sync_transfer(fmc->spi[slave], &latch, 1);
	if (ret)
		return ret;

	ret = spi_sync_transfer(fmc->spi[slave], &fetch, 1);
	if (ret)
		return ret;

	return fmc->spi_rx & SDI_FMC_SPI_DATA_MASK;
}

static int sdi_fmc_apply(struct xlnx_sdi_fmc *fmc, const struct sdi_fmc_reg *r)
{
	if (r->bus == SDI_FMC_BUS_IIC)
		return sdi_fmc_iic_write(fmc, r->regaddr, r->regdata);

	return sdi_fmc_spi_write(fmc, r->slave, r->regaddr, r->regdata);
}

static int sdi_fmc_apply_table(struct xlnx_sdi_fmc *fmc,
			       const struct sdi_fmc_reg *tbl, size_t n)
{
	size_t i;
	int ret;

	for (i = 0; i < n; i++) {
		ret = sdi_fmc_apply(fmc, &tbl[i]);
		if (ret)
			return ret;
	}
	return 0;
}

/* ------------------------------------------------------------------------- */
/* Init sequence                                                             */
/* ------------------------------------------------------------------------- */

static int sdi_fmc_dev_init(struct xlnx_sdi_fmc *fmc)
{
	int ret;
	u8 ch;

	/* Clock crosspoint routing first (channel independent). */
	ret = sdi_fmc_iic_write(fmc, SDI_FMC_XBAR_CLK_REG, SDI_FMC_XBAR_CLK_VAL);
	if (ret)
		return ret;

	for (ch = 0; ch < SDI_FMC_NCHAN; ch++) {
		ret = sdi_fmc_set_channel(fmc, ch);
		if (ret)
			return ret;

		ret = sdi_fmc_apply_table(fmc, sdi_fmc_chan_init,
					  ARRAY_SIZE(sdi_fmc_chan_init));
		if (ret) {
			dev_err(fmc->dev, "ch%u base init failed: %d\n", ch,
				ret);
			return ret;
		}
	}

	dev_dbg(fmc->dev, "base init done\n");
	return 0;
}

static const struct sdi_fmc_errata *sdi_fmc_find_errata(const struct sdi_fmc_errata *tbl,
							size_t n, u8 id)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (tbl[i].id == id)
			return &tbl[i];

	return NULL;
}

/*
 * Apply the errata sequence matching a part's device ID.  An ID in the expected
 * family with no table is a later die revision with nothing published against
 * it; an ID outside the family means the SPI bus is not reaching the part.
 */
static int sdi_fmc_apply_errata(struct xlnx_sdi_fmc *fmc,
				enum sdi_fmc_spi_slave slave,
				const struct sdi_fmc_errata *tbl, size_t n,
				u8 family, const char *what, u8 ch)
{
	const struct sdi_fmc_errata *e;
	int id;

	id = sdi_fmc_spi_read(fmc, slave, SDI_FMC_DEV_ID_REG);
	if (id < 0)
		return id;

	if (id == SDI_FMC_ID_ALL_ZEROS || id == SDI_FMC_ID_ALL_ONES) {
		dev_warn(fmc->dev,
			 "ch%u %s reads back 0x%02x: the SPI bus is not reaching the mezzanine, so no errata were applied\n",
			 ch, what, id);
		return 0;
	}

	if ((id & SDI_FMC_ID_FAMILY_MASK) != family) {
		dev_warn(fmc->dev,
			 "ch%u unrecognised %s id 0x%02x: no errata applied, and the SPI bus may not be reaching the mezzanine\n",
			 ch, what, id);
		return 0;
	}

	e = sdi_fmc_find_errata(tbl, n, id);
	if (!e) {
		dev_info_once(fmc->dev,
			      "%s id 0x%02x: later revision with no published errata, none applied\n",
			      what, id);
		return 0;
	}

	return sdi_fmc_apply_table(fmc, e->tbl, e->n);
}

static int sdi_fmc_dev_errata_init(struct xlnx_sdi_fmc *fmc)
{
	int ret;
	u8 ch;

	for (ch = 0; ch < SDI_FMC_NCHAN; ch++) {
		ret = sdi_fmc_set_channel(fmc, ch);
		if (ret)
			return ret;

		ret = sdi_fmc_apply_errata(fmc, SDI_FMC_RCLKR, rclkr_errata,
					   ARRAY_SIZE(rclkr_errata),
					   SDI_FMC_RCLKR_FAMILY, "reclocker",
					   ch);
		if (ret)
			return ret;

		ret = sdi_fmc_apply_errata(fmc, SDI_FMC_RCVR, rcvr_errata,
					   ARRAY_SIZE(rcvr_errata),
					   SDI_FMC_RCVR_FAMILY, "receiver", ch);
		if (ret)
			return ret;
	}

	dev_dbg(fmc->dev, "errata init done\n");
	return 0;
}

/*
 * Report the mezzanine's identity registers.
 *
 * sdi_fmc_spi_read() only fails when the SPI transfer itself errors, so a bus
 * that never reaches the card still reads back a plausible 0x00 and every
 * errata table is then skipped while init reports success.  Those tables carry
 * the MACOM "VCO out of range" fixes, and without them the reclocker only
 * degrades at the top line rate: 12G is unstable while 6G and below work.
 * All-0x00 or all-0xFF therefore means the bus is not reaching the mezzanine.
 */
static void sdi_fmc_report_ids(struct xlnx_sdi_fmc *fmc)
{
	unsigned int responded = 0;
	int xbar;
	u8 ch;

	xbar = sdi_fmc_iic_read(fmc, SDI_FMC_XBAR_CLK_REG);
	if (xbar >= 0)
		dev_dbg(fmc->dev, "xbar [0x%02x] = 0x%02x (expected 0x%02x)\n",
			SDI_FMC_XBAR_CLK_REG, xbar, SDI_FMC_XBAR_CLK_VAL);

	for (ch = 0; ch < SDI_FMC_NCHAN; ch++) {
		int rclkr, drvr, rcvr;

		if (sdi_fmc_set_channel(fmc, ch))
			continue;

		rclkr = sdi_fmc_spi_read(fmc, SDI_FMC_RCLKR, SDI_FMC_DEV_ID_REG);
		drvr  = sdi_fmc_spi_read(fmc, SDI_FMC_DRVR, SDI_FMC_DEV_ID_REG);
		rcvr  = sdi_fmc_spi_read(fmc, SDI_FMC_RCVR, SDI_FMC_DEV_ID_REG);

		if (rclkr < 0 || drvr < 0 || rcvr < 0) {
			dev_warn(fmc->dev,
				 "ch%u SPI read failed (rclkr %d drvr %d rcvr %d)\n",
				 ch, rclkr, drvr, rcvr);
			continue;
		}

		dev_dbg(fmc->dev,
			"ch%u id: reclocker 0x%02x driver 0x%02x receiver 0x%02x\n",
			ch, rclkr, drvr, rcvr);

		if ((rclkr & SDI_FMC_ID_FAMILY_MASK) == SDI_FMC_RCLKR_FAMILY)
			responded++;
	}

	if (!responded)
		dev_warn(fmc->dev,
			 "no reclocker returned a known device ID - the SPI bus is not reaching the mezzanine\n");
}

/* ------------------------------------------------------------------------- */
/* Bus device resolution                                                     */
/* ------------------------------------------------------------------------- */

/*
 * Resolve a phandle to a bound device on @bus.  The device must be bound and
 * not merely instantiated: until its driver has probed, the SPI word size has
 * not been set up and the part cannot be talked to.  Returns NULL when the
 * phandle is absent or the target is not ready, which the caller turns into
 * -EPROBE_DEFER.
 *
 * bus_find_device_by_of_node() is the same lookup the SPI and I2C cores use
 * internally, and works for either bus.  of_find_spi_device_by_node() is not an
 * alternative: it is static to drivers/spi/spi.c and built only under
 * CONFIG_OF_DYNAMIC, unlike its exported I2C counterpart.
 */
static struct device *sdi_fmc_get_part(struct device_node *np, const char *prop,
				       const struct bus_type *bus)
{
	struct device_node *node;
	struct device *dev;

	node = of_parse_phandle(np, prop, 0);
	if (!node)
		return NULL;

	dev = bus_find_device_by_of_node(bus, node);
	of_node_put(node);

	if (!dev)
		return NULL;

	if (!device_is_bound(dev)) {
		put_device(dev);
		return NULL;
	}

	return dev;
}

static void sdi_fmc_put(struct xlnx_sdi_fmc *fmc)
{
	unsigned int i;

	for (i = 0; i < SDI_FMC_SPI_NDEV; i++)
		if (fmc->spi[i])
			put_device(&fmc->spi[i]->dev);
	if (fmc->xbar)
		put_device(&fmc->xbar->dev);
}

static int sdi_fmc_resolve(struct xlnx_sdi_fmc *fmc)
{
	static const char * const spi_props[SDI_FMC_SPI_NDEV] = {
		[SDI_FMC_RCLKR]	= "xlnx,rclkr",
		[SDI_FMC_DRVR]	= "xlnx,drvr",
		[SDI_FMC_RCVR]	= "xlnx,rcvr",
	};
	struct device_node *np = fmc->dev->of_node;
	struct device *dev;
	unsigned int i;

	dev = sdi_fmc_get_part(np, "xlnx,xbar", &i2c_bus_type);
	if (dev)
		fmc->xbar = to_i2c_client(dev);

	for (i = 0; i < SDI_FMC_SPI_NDEV; i++) {
		dev = sdi_fmc_get_part(np, spi_props[i], &spi_bus_type);
		if (dev)
			fmc->spi[i] = to_spi_device(dev);
	}

	if (!fmc->xbar || !fmc->spi[SDI_FMC_RCLKR] ||
	    !fmc->spi[SDI_FMC_DRVR] || !fmc->spi[SDI_FMC_RCVR]) {
		sdi_fmc_put(fmc);
		return -EPROBE_DEFER;
	}

	return 0;
}

/*
 * The channel-select lines are best-effort.  The I2C/SPI parts resolved above
 * are the only hard dependency, and by the time they exist every AXI GPIO
 * controller has long been probed.  A line that cannot be acquired must not
 * fail the probe: sdi_fmc_set_channel() is a no-op on a NULL descriptor, so a
 * single-channel card still comes up.
 */
static void sdi_fmc_get_gpios(struct xlnx_sdi_fmc *fmc)
{
	struct device *dev = fmc->dev;
	unsigned int i;

	for (i = 0; i < SDI_FMC_CHAN_GPIO_BITS; i++) {
		struct gpio_desc *desc;

		desc = devm_gpiod_get_index_optional(dev, "channel", i,
						     GPIOD_OUT_LOW);
		if (IS_ERR(desc)) {
			dev_warn(dev,
				 "channel gpio %u unavailable (%pe); continuing without it\n",
				 i, desc);
			desc = NULL;
		}
		fmc->chan_gpio[i] = desc;
	}

	fmc->done_gpio = devm_gpiod_get_optional(dev, "init-done",
						 GPIOD_OUT_LOW);
	if (IS_ERR(fmc->done_gpio))
		fmc->done_gpio = NULL;
}

/* ------------------------------------------------------------------------- */
/* FMC platform driver                                                       */
/* ------------------------------------------------------------------------- */

static int sdi_fmc_configure(struct xlnx_sdi_fmc *fmc)
{
	int ret;

	ret = sdi_fmc_dev_init(fmc);
	if (ret)
		return ret;

	sdi_fmc_report_ids(fmc);

	ret = sdi_fmc_dev_errata_init(fmc);
	if (ret)
		return ret;

	/*
	 * Park the mux back on channel 0 and raise init-done.  Leaving it on
	 * the last configured channel can disturb the live Rx/Tx paths.
	 */
	sdi_fmc_set_channel(fmc, 0);
	gpiod_set_value_cansleep(fmc->done_gpio, 1);

	return 0;
}

static int xlnx_sdi_fmc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct xlnx_sdi_fmc *fmc;
	int ret;

	fmc = devm_kzalloc(dev, sizeof(*fmc), GFP_KERNEL);
	if (!fmc)
		return -ENOMEM;

	fmc->dev = dev;

	ret = sdi_fmc_resolve(fmc);
	if (ret)
		return dev_err_probe(dev, ret,
				     "mezzanine bus devices not ready\n");

	sdi_fmc_get_gpios(fmc);

	ret = sdi_fmc_configure(fmc);

	/*
	 * The card holds its configuration once programmed, so the bus devices
	 * are only needed for the duration of that pass.  The GPIOs are devm
	 * and stay claimed, since the mux lines belong to this device.
	 */
	sdi_fmc_put(fmc);

	/*
	 * The mezzanine only conditions the signal.  A card that will not
	 * program costs link margin at the top line rates, but failing the
	 * probe over it would leave the SDI endpoints deferring forever and
	 * take down the whole video pipeline, so bind anyway and leave the
	 * reason in the log.
	 */
	if (ret)
		dev_err(dev, "configuration failed (%d); SDI will run with an unconditioned signal\n",
			ret);
	else
		dev_dbg(dev, "configured\n");

	return 0;
}

static const struct of_device_id xlnx_sdi_fmc_of_match[] = {
	{ .compatible = "xlnx,sdi-fmc" },
	{ }
};
MODULE_DEVICE_TABLE(of, xlnx_sdi_fmc_of_match);

/*
 * No remove callback: the probe keeps no state and registers nothing that
 * outlives it.  The bus references are dropped before it returns and the
 * GPIOs and the private data are devm, so unbinding has nothing left to undo.
 */
static struct platform_driver xlnx_sdi_fmc_driver = {
	.driver = {
		.name		= "xlnx-sdi-fmc",
		.of_match_table	= xlnx_sdi_fmc_of_match,
	},
	.probe	= xlnx_sdi_fmc_probe,
};

static int __init xlnx_sdi_fmc_module_init(void)
{
	int ret;

	ret = i2c_add_driver(&ds10cp15a_driver);
	if (ret)
		return ret;

	ret = spi_register_driver(&macom_sdi_driver);
	if (ret)
		goto del_i2c;

	ret = platform_driver_register(&xlnx_sdi_fmc_driver);
	if (ret)
		goto del_spi;

	return 0;

del_spi:
	spi_unregister_driver(&macom_sdi_driver);
del_i2c:
	i2c_del_driver(&ds10cp15a_driver);
	return ret;
}
module_init(xlnx_sdi_fmc_module_init);

static void __exit xlnx_sdi_fmc_module_exit(void)
{
	platform_driver_unregister(&xlnx_sdi_fmc_driver);
	spi_unregister_driver(&macom_sdi_driver);
	i2c_del_driver(&ds10cp15a_driver);
}
module_exit(xlnx_sdi_fmc_module_exit);

MODULE_AUTHOR("Katta Dhanunjanrao <katta.dhanunjanrao@amd.com>");
MODULE_AUTHOR("Advanced Micro Devices, Inc.");
MODULE_DESCRIPTION("Xilinx SDI FMC (Fidus SDI FMC) mezzanine configuration");
MODULE_LICENSE("GPL");
