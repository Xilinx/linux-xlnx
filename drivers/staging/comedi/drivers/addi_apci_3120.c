#include <linux/module.h>
#include <linux/pci.h>

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#include "addi-data/hwdrv_apci3120.c"

enum apci3120_boardid {
	BOARD_APCI3120,
	BOARD_APCI3001,
};

static const struct addi_board apci3120_boardtypes[] = {
	[BOARD_APCI3120] = {
		.pc_DriverName		= "apci3120",
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_NbrAoChannel		= 8,
		.i_AiMaxdata		= 0xffff,
		.i_AoMaxdata		= 0x3fff,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 0x0f,
		.interrupt		= v_APCI3120_Interrupt,
	},
	[BOARD_APCI3001] = {
		.pc_DriverName		= "apci3001",
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 0xfff,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 0x0f,
		.interrupt		= v_APCI3120_Interrupt,
	},
};

static irqreturn_t v_ADDI_Interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	const struct addi_board *this_board = comedi_board(dev);

	this_board->interrupt(irq, d);
	return IRQ_RETVAL(1);
}

static int apci3120_auto_attach(struct comedi_device *dev,
				unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct addi_board *this_board = NULL;
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ret, pages, i;

	if (context < ARRAY_SIZE(apci3120_boardtypes))
		this_board = &apci3120_boardtypes[context];
	if (!this_board)
		return -ENODEV;
	dev->board_ptr = this_board;
	dev->board_name = this_board->pc_DriverName;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	pci_set_master(pcidev);

	dev->iobase = pci_resource_start(pcidev, 1);
	devpriv->iobase = dev->iobase;
	devpriv->i_IobaseAmcc = pci_resource_start(pcidev, 0);
	devpriv->i_IobaseAddon = pci_resource_start(pcidev, 2);
	devpriv->i_IobaseReserved = pci_resource_start(pcidev, 3);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, v_ADDI_Interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	devpriv->us_UseDma = ADDI_ENABLE;

	/* Allocate DMA buffers */
	devpriv->b_DmaDoubleBuffer = 0;
	for (i = 0; i < 2; i++) {
		for (pages = 4; pages >= 0; pages--) {
			devpriv->ul_DmaBufferVirtual[i] =
				(void *) __get_free_pages(GFP_KERNEL, pages);

			if (devpriv->ul_DmaBufferVirtual[i])
				break;
		}
		if (devpriv->ul_DmaBufferVirtual[i]) {
			devpriv->ui_DmaBufferPages[i] = pages;
			devpriv->ui_DmaBufferSize[i] = PAGE_SIZE * pages;
			devpriv->ul_DmaBufferHw[i] =
				virt_to_bus((void *)devpriv->
				ul_DmaBufferVirtual[i]);
		}
	}
	if (!devpriv->ul_DmaBufferVirtual[0])
		devpriv->us_UseDma = ADDI_DISABLE;

	if (devpriv->ul_DmaBufferVirtual[1])
		devpriv->b_DmaDoubleBuffer = 1;

	ret = comedi_alloc_subdevices(dev, 5);
	if (ret)
		return ret;

	/*  Allocate and Initialise AI Subdevice Structures */
	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags =
		SDF_READABLE | SDF_COMMON | SDF_GROUND
		| SDF_DIFF;
	if (this_board->i_NbrAiChannel) {
		s->n_chan = this_board->i_NbrAiChannel;
		devpriv->b_SingelDiff = 0;
	} else {
		s->n_chan = this_board->i_NbrAiChannelDiff;
		devpriv->b_SingelDiff = 1;
	}
	s->maxdata = this_board->i_AiMaxdata;
	s->len_chanlist = this_board->i_AiChannelList;
	s->range_table = &range_apci3120_ai;

	s->insn_config = i_APCI3120_InsnConfigAnalogInput;
	s->insn_read = i_APCI3120_InsnReadAnalogInput;
	s->do_cmdtest = i_APCI3120_CommandTestAnalogInput;
	s->do_cmd = i_APCI3120_CommandAnalogInput;
	s->cancel = i_APCI3120_StopCyclicAcquisition;

	/*  Allocate and Initialise AO Subdevice Structures */
	s = &dev->subdevices[1];
	if (this_board->i_NbrAoChannel) {
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = this_board->i_NbrAoChannel;
		s->maxdata = this_board->i_AoMaxdata;
		s->len_chanlist = this_board->i_NbrAoChannel;
		s->range_table = &range_apci3120_ao;
		s->insn_write = i_APCI3120_InsnWriteAnalogOutput;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise DI Subdevice Structures */
	s = &dev->subdevices[2];
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = this_board->i_NbrDiChannel;
	s->maxdata = 1;
	s->len_chanlist = this_board->i_NbrDiChannel;
	s->range_table = &range_digital;
	s->insn_bits = apci3120_di_insn_bits;

	/*  Allocate and Initialise DO Subdevice Structures */
	s = &dev->subdevices[3];
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags =
		SDF_READABLE | SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = this_board->i_NbrDoChannel;
	s->maxdata = this_board->i_DoMaxdata;
	s->len_chanlist = this_board->i_NbrDoChannel;
	s->range_table = &range_digital;
	s->insn_bits = apci3120_do_insn_bits;

	/*  Allocate and Initialise Timer Subdevice Structures */
	s = &dev->subdevices[4];
	s->type = COMEDI_SUBD_TIMER;
	s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 1;
	s->maxdata = 0;
	s->len_chanlist = 1;
	s->range_table = &range_digital;

	s->insn_write = i_APCI3120_InsnWriteTimer;
	s->insn_read = i_APCI3120_InsnReadTimer;
	s->insn_config = i_APCI3120_InsnConfigTimer;

	i_APCI3120_Reset(dev);
	return 0;
}

static void apci3120_detach(struct comedi_device *dev)
{
	struct addi_private *devpriv = dev->private;

	if (devpriv) {
		if (dev->iobase)
			i_APCI3120_Reset(dev);
		if (dev->irq)
			free_irq(dev->irq, dev);
		if (devpriv->ul_DmaBufferVirtual[0]) {
			free_pages((unsigned long)devpriv->
				ul_DmaBufferVirtual[0],
				devpriv->ui_DmaBufferPages[0]);
		}
		if (devpriv->ul_DmaBufferVirtual[1]) {
			free_pages((unsigned long)devpriv->
				ul_DmaBufferVirtual[1],
				devpriv->ui_DmaBufferPages[1]);
		}
	}
	comedi_pci_disable(dev);
}

static struct comedi_driver apci3120_driver = {
	.driver_name	= "addi_apci_3120",
	.module		= THIS_MODULE,
	.auto_attach	= apci3120_auto_attach,
	.detach		= apci3120_detach,
};

static int apci3120_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci3120_driver, id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(apci3120_pci_table) = {
	{ PCI_VDEVICE(AMCC, 0x818d), BOARD_APCI3120 },
	{ PCI_VDEVICE(AMCC, 0x828d), BOARD_APCI3001 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci3120_pci_table);

static struct pci_driver apci3120_pci_driver = {
	.name		= "addi_apci_3120",
	.id_table	= apci3120_pci_table,
	.probe		= apci3120_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci3120_driver, apci3120_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
