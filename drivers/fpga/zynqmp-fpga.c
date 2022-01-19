// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Xilinx, Inc.
 */

#include <linux/dma-mapping.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/firmware/xlnx-zynqmp.h>

/* Constant Definitions */
#define IXR_FPGA_DONE_MASK	BIT(3)

#define READ_DMA_SIZE		0x200
#define DUMMY_FRAMES_SIZE	0x64

/* Error Register */
#define IXR_FPGA_ERR_CRC_ERR		BIT(0)
#define IXR_FPGA_ERR_SECURITY_ERR	BIT(16)

/* Signal Status Register */
#define IXR_FPGA_END_OF_STARTUP		BIT(4)
#define IXR_FPGA_GST_CFG_B		BIT(5)
#define IXR_FPGA_INIT_B_INTERNAL	BIT(11)
#define IXR_FPGA_DONE_INTERNAL_SIGNAL	BIT(13)

#define IXR_FPGA_CONFIG_STAT_OFFSET	7U
#define IXR_FPGA_READ_CONFIG_TYPE	0U

#define XILINX_ZYNQMP_PM_FPGA_READ_BACK		BIT(6)
#define XILINX_ZYNQMP_PM_FPGA_REG_READ_BACK	BIT(7)

#define DEFAULT_FEATURE_LIST	(XILINX_ZYNQMP_PM_FPGA_FULL | \
				 XILINX_ZYNQMP_PM_FPGA_PARTIAL | \
				 XILINX_ZYNQMP_PM_FPGA_AUTHENTICATION_DDR | \
				 XILINX_ZYNQMP_PM_FPGA_AUTHENTICATION_OCM | \
				 XILINX_ZYNQMP_PM_FPGA_ENCRYPTION_USERKEY | \
				 XILINX_ZYNQMP_PM_FPGA_ENCRYPTION_DEVKEY | \
				 XILINX_ZYNQMP_PM_FPGA_READ_BACK | \
				 XILINX_ZYNQMP_PM_FPGA_REG_READ_BACK)

static bool readback_type;
module_param(readback_type, bool, 0644);
MODULE_PARM_DESC(readback_type,
		 "readback_type 0-configuration register read "
		 "1- configuration data read (default: 0)");

/**
 * struct zynqmp_configreg - Configuration register offsets
 * @reg:	Name of the configuration register.
 * @offset:	Register offset.
 */
struct zynqmp_configreg {
	char *reg;
	u32 offset;
};

static struct zynqmp_configreg cfgreg[] = {
	{.reg = "CRC",		.offset = 0},
	{.reg = "FAR",		.offset = 1},
	{.reg = "FDRI",		.offset = 2},
	{.reg = "FDRO",		.offset = 3},
	{.reg = "CMD",		.offset = 4},
	{.reg = "CTRL0",	.offset = 5},
	{.reg = "MASK",		.offset = 6},
	{.reg = "STAT",		.offset = 7},
	{.reg = "LOUT",		.offset = 8},
	{.reg = "COR0",		.offset = 9},
	{.reg = "MFWR",		.offset = 10},
	{.reg = "CBC",		.offset = 11},
	{.reg = "IDCODE",	.offset = 12},
	{.reg = "AXSS",		.offset = 13},
	{.reg = "COR1",		.offset = 14},
	{.reg = "WBSTR",	.offset = 16},
	{.reg = "TIMER",	.offset = 17},
	{.reg = "BOOTSTS",	.offset = 22},
	{.reg = "CTRL1",	.offset = 24},
	{}
};

/**
 * struct zynqmp_fpga_priv - Private data structure
 * @dev:	Device data structure
 * @feature_list: Firmware supported feature list
 * @version: Firmware version info. The higher 16 bytes belong to
 *           the major version number and the lower 16 bytes belong
 *           to a minor version number.
 * @flags:	flags which is used to identify the bitfile type
 * @size:	Size of the Bit-stream used for readback
 */
struct zynqmp_fpga_priv {
	struct device *dev;
	u32 feature_list;
	u32 version;
	u32 flags;
	u32 size;
};

static int zynqmp_fpga_ops_write_init(struct fpga_manager *mgr,
				      struct fpga_image_info *info,
				      const char *buf, size_t size)
{
	struct zynqmp_fpga_priv *priv;
	int  eemi_flags = 0;

	priv = mgr->priv;
	priv->flags = info->flags;

	/* Update firmware flags */
	if (priv->flags & FPGA_MGR_USERKEY_ENCRYPTED_BITSTREAM)
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_ENCRYPTION_USERKEY;
	else if (priv->flags & FPGA_MGR_ENCRYPTED_BITSTREAM)
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_ENCRYPTION_DEVKEY;
	if (priv->flags & FPGA_MGR_DDR_MEM_AUTH_BITSTREAM)
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_AUTHENTICATION_DDR;
	else if (priv->flags & FPGA_MGR_SECURE_MEM_AUTH_BITSTREAM)
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_AUTHENTICATION_OCM;
	if (priv->flags & FPGA_MGR_PARTIAL_RECONFIG)
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_PARTIAL;

	/* Validate user flgas with firmware feature list */
	if ((priv->feature_list & eemi_flags) != eemi_flags)
		return -EINVAL;

	return 0;
}

static int zynqmp_fpga_ops_write(struct fpga_manager *mgr,
				 const char *buf, size_t size)
{
	struct zynqmp_fpga_priv *priv;
	dma_addr_t dma_addr = 0;
	u32 eemi_flags = 0;
	size_t dma_size;
	u32 status;
	char *kbuf;
	int ret;

	priv = mgr->priv;
	priv->size = size;

	if (priv->flags & FPGA_MGR_USERKEY_ENCRYPTED_BITSTREAM)
		dma_size = size + ENCRYPTED_KEY_LEN;
	else
		dma_size = size;

	kbuf = dma_alloc_coherent(priv->dev, dma_size, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	memcpy(kbuf, buf, size);

	if (priv->flags & FPGA_MGR_USERKEY_ENCRYPTED_BITSTREAM) {
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_ENCRYPTION_USERKEY;
		memcpy(kbuf + size, mgr->key, ENCRYPTED_KEY_LEN);
	} else if (priv->flags & FPGA_MGR_ENCRYPTED_BITSTREAM) {
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_ENCRYPTION_DEVKEY;
	}

	wmb(); /* ensure all writes are done before initiate FW call */

	if (priv->flags & FPGA_MGR_DDR_MEM_AUTH_BITSTREAM)
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_AUTHENTICATION_DDR;
	else if (priv->flags & FPGA_MGR_SECURE_MEM_AUTH_BITSTREAM)
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_AUTHENTICATION_OCM;

	if (priv->flags & FPGA_MGR_PARTIAL_RECONFIG)
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_PARTIAL;

	if (priv->flags & FPGA_MGR_USERKEY_ENCRYPTED_BITSTREAM)
		ret = zynqmp_pm_fpga_load(dma_addr, dma_addr + size,
					  eemi_flags, &status);
	else
		ret = zynqmp_pm_fpga_load(dma_addr, size,
					  eemi_flags, &status);

	dma_free_coherent(priv->dev, dma_size, kbuf, dma_addr);

	if (status)
		return status;

	return ret;
}

static unsigned long zynqmp_fpga_get_contiguous_size(struct sg_table *sgt)
{
	dma_addr_t expected = sg_dma_address(sgt->sgl);
	unsigned long size = 0;
	struct scatterlist *s;
	unsigned int i;

	for_each_sg(sgt->sgl, s, sgt->nents, i) {
		if (sg_dma_address(s) != expected)
			break;
		expected = sg_dma_address(s) + sg_dma_len(s);
		size += sg_dma_len(s);
	}

	return size;
}

static int zynqmp_fpga_ops_write_sg(struct fpga_manager *mgr,
				    struct sg_table *sgt)
{
	dma_addr_t dma_addr, key_addr = 0;
	struct zynqmp_fpga_priv *priv;
	unsigned long contig_size;
	u32 eemi_flags = 0;
	u32 status;
	char *kbuf;
	int ret;

	priv = mgr->priv;

	dma_addr = sg_dma_address(sgt->sgl);
	contig_size = zynqmp_fpga_get_contiguous_size(sgt);

	if (priv->flags & FPGA_MGR_PARTIAL_RECONFIG)
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_PARTIAL;
	if (priv->flags & FPGA_MGR_USERKEY_ENCRYPTED_BITSTREAM)
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_ENCRYPTION_USERKEY;
	else if (priv->flags & FPGA_MGR_ENCRYPTED_BITSTREAM)
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_ENCRYPTION_DEVKEY;
	if (priv->flags & FPGA_MGR_DDR_MEM_AUTH_BITSTREAM)
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_AUTHENTICATION_DDR;
	else if (priv->flags & FPGA_MGR_SECURE_MEM_AUTH_BITSTREAM)
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_AUTHENTICATION_OCM;

	if (priv->flags & FPGA_MGR_USERKEY_ENCRYPTED_BITSTREAM) {
		kbuf = dma_alloc_coherent(priv->dev, ENCRYPTED_KEY_LEN,
					  &key_addr, GFP_KERNEL);
		if (!kbuf)
			return -ENOMEM;
		memcpy(kbuf, mgr->key, ENCRYPTED_KEY_LEN);
		ret = zynqmp_pm_fpga_load(dma_addr, key_addr,
					  eemi_flags, &status);
		dma_free_coherent(priv->dev, ENCRYPTED_KEY_LEN, kbuf, key_addr);
	} else {
		ret = zynqmp_pm_fpga_load(dma_addr, contig_size,
					  eemi_flags, &status);
	}

	if (status)
		return status;

	return ret;
}

static enum fpga_mgr_states zynqmp_fpga_ops_state(struct fpga_manager *mgr)
{
	u32 status = 0;

	zynqmp_pm_fpga_get_status(&status);
	if (status & IXR_FPGA_DONE_MASK)
		return FPGA_MGR_STATE_OPERATING;

	return FPGA_MGR_STATE_UNKNOWN;
}

static u64 zynqmp_fpga_ops_status(struct fpga_manager *mgr)
{
	unsigned int *buf, reg_val;
	dma_addr_t dma_addr = 0;
	u64 status = 0;
	int ret;

	buf = dma_alloc_coherent(mgr->dev.parent, READ_DMA_SIZE,
				 &dma_addr, GFP_KERNEL);
	if (!buf)
		return FPGA_MGR_STATUS_FIRMWARE_REQ_ERR;

	ret = zynqmp_pm_fpga_read(IXR_FPGA_CONFIG_STAT_OFFSET, dma_addr,
				  IXR_FPGA_READ_CONFIG_TYPE, &reg_val);
	if (ret) {
		status = FPGA_MGR_STATUS_FIRMWARE_REQ_ERR;
		goto free_dmabuf;
	}

	if (reg_val & IXR_FPGA_ERR_CRC_ERR)
		status |= FPGA_MGR_STATUS_CRC_ERR;
	if (reg_val & IXR_FPGA_ERR_SECURITY_ERR)
		status |= FPGA_MGR_STATUS_SECURITY_ERR;
	if (!(reg_val & IXR_FPGA_INIT_B_INTERNAL))
		status |= FPGA_MGR_STATUS_DEVICE_INIT_ERR;
	if (!(reg_val & IXR_FPGA_DONE_INTERNAL_SIGNAL))
		status |= FPGA_MGR_STATUS_SIGNAL_ERR;
	if (!(reg_val & IXR_FPGA_GST_CFG_B))
		status |= FPGA_MGR_STATUS_HIGH_Z_STATE_ERR;
	if (!(reg_val & IXR_FPGA_END_OF_STARTUP))
		status |= FPGA_MGR_STATUS_EOS_ERR;

free_dmabuf:
	dma_free_coherent(mgr->dev.parent, READ_DMA_SIZE, buf, dma_addr);

	return status;
}

static int zynqmp_fpga_read_cfgreg(struct fpga_manager *mgr,
				   struct seq_file *s)
{
	int ret = 0;
	u32 val;
	unsigned int *buf;
	dma_addr_t dma_addr = 0;
	struct zynqmp_fpga_priv *priv;
	struct zynqmp_configreg *p = cfgreg;

	priv = mgr->priv;

	if (!(priv->feature_list & XILINX_ZYNQMP_PM_FPGA_READ_BACK))
		return -EINVAL;

	buf = dma_alloc_coherent(mgr->dev.parent, READ_DMA_SIZE,
				 &dma_addr, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	seq_puts(s, "zynqMP FPGA Configuration register contents are\n");

	while (p->reg) {
		ret = zynqmp_pm_fpga_read(p->offset, dma_addr, readback_type,
					  &val);
		if (ret)
			goto free_dmabuf;
		seq_printf(s, "%s --> \t %x \t\r\n", p->reg, val);
		p++;
	}

free_dmabuf:
	dma_free_coherent(mgr->dev.parent, READ_DMA_SIZE, buf,
			  dma_addr);

	return ret;
}

static int zynqmp_fpga_read_cfgdata(struct fpga_manager *mgr,
				    struct seq_file *s)
{
	struct zynqmp_fpga_priv *priv;
	int ret;
	u32 data_offset;
	unsigned int *buf;
	dma_addr_t dma_addr = 0;
	size_t size;

	priv = mgr->priv;

	if (!(priv->feature_list & XILINX_ZYNQMP_PM_FPGA_REG_READ_BACK))
		return -EINVAL;

	size = priv->size + READ_DMA_SIZE + DUMMY_FRAMES_SIZE;

	buf = dma_alloc_coherent(mgr->dev.parent, size, &dma_addr,
				 GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	seq_puts(s, "zynqMP FPGA Configuration data contents are\n");
	ret = zynqmp_pm_fpga_read((priv->size + DUMMY_FRAMES_SIZE) / 4,
				  dma_addr, readback_type, &data_offset);
	if (ret)
		goto free_dmabuf;

	seq_write(s, &buf[data_offset], priv->size);

free_dmabuf:
	dma_free_coherent(mgr->dev.parent, size, buf, dma_addr);

	return ret;
}

static int zynqmp_fpga_ops_read(struct fpga_manager *mgr, struct seq_file *s)
{
	int ret;

	if (readback_type)
		ret = zynqmp_fpga_read_cfgdata(mgr, s);
	else
		ret = zynqmp_fpga_read_cfgreg(mgr, s);

	return ret;
}

static const struct fpga_manager_ops zynqmp_fpga_ops = {
	.state = zynqmp_fpga_ops_state,
	.status = zynqmp_fpga_ops_status,
	.write_init = zynqmp_fpga_ops_write_init,
	.write = zynqmp_fpga_ops_write,
	.write_sg = zynqmp_fpga_ops_write_sg,
	.read = zynqmp_fpga_ops_read,
};

static int zynqmp_fpga_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zynqmp_fpga_priv *priv;
	struct fpga_manager *mgr;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	if (!(zynqmp_pm_fpga_get_version(&priv->version))) {
		if (zynqmp_pm_fpga_get_feature_list(&priv->feature_list))
			priv->feature_list = DEFAULT_FEATURE_LIST;
	} else {
		priv->feature_list = DEFAULT_FEATURE_LIST;
	}

	mgr = devm_fpga_mgr_create(dev, "Xilinx ZynqMP FPGA Manager",
				   &zynqmp_fpga_ops, priv);
	if (!mgr)
		return -ENOMEM;

	return devm_fpga_mgr_register(dev, mgr);
}

#ifdef CONFIG_OF
static const struct of_device_id zynqmp_fpga_of_match[] = {
	{ .compatible = "xlnx,zynqmp-pcap-fpga", },
	{},
};
MODULE_DEVICE_TABLE(of, zynqmp_fpga_of_match);
#endif

static struct platform_driver zynqmp_fpga_driver = {
	.probe = zynqmp_fpga_probe,
	.driver = {
		.name = "zynqmp_fpga_manager",
		.of_match_table = of_match_ptr(zynqmp_fpga_of_match),
	},
};

module_platform_driver(zynqmp_fpga_driver);

MODULE_AUTHOR("Nava kishore Manne <navam@xilinx.com>");
MODULE_DESCRIPTION("Xilinx ZynqMp FPGA Manager");
MODULE_LICENSE("GPL");
