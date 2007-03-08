#
# Makefile for the linux kernel.
#

obj-pci-y	:=
obj-pci-n	:=

obj-pci-$(CONFIG_ARCH_IXDP4XX)		+= ixdp425-pci.o
obj-pci-$(CONFIG_MACH_IXDPG425)		+= ixdpg425-pci.o
obj-pci-$(CONFIG_ARCH_ADI_COYOTE)	+= coyote-pci.o
obj-pci-$(CONFIG_MACH_GTWX5715)		+= gtwx5715-pci.o
obj-pci-$(CONFIG_MACH_NSLU2)		+= nslu2-pci.o
obj-pci-$(CONFIG_MACH_NAS100D)		+= nas100d-pci.o
obj-pci-$(CONFIG_MACH_ESS710)		+= ess710-pci.o
obj-pci-$(CONFIG_MACH_SE5100)		+= ixdpg425-pci.o
obj-pci-$(CONFIG_MACH_SG565)		+= sg5xx-pci.o
obj-pci-$(CONFIG_MACH_SG590)		+= sg720-pci.o
obj-pci-$(CONFIG_MACH_SG720)		+= sg720-pci.o
obj-pci-$(CONFIG_MACH_SG8100)		+= sg5xx-pci.o

obj-y	+= common.o

obj-$(CONFIG_ARCH_IXDP4XX)	+= ixdp425-setup.o
obj-$(CONFIG_MACH_IXDPG425)	+= coyote-setup.o
obj-$(CONFIG_ARCH_ADI_COYOTE)	+= coyote-setup.o
obj-$(CONFIG_MACH_GTWX5715)	+= gtwx5715-setup.o
obj-$(CONFIG_MACH_NSLU2)	+= nslu2-setup.o nslu2-power.o
obj-$(CONFIG_MACH_NAS100D)	+= nas100d-setup.o nas100d-power.o
obj-$(CONFIG_ARCH_SE4000)	+= sg-setup.o
obj-$(CONFIG_MACH_SE5100)	+= sg-setup.o
obj-$(CONFIG_MACH_SG560)	+= sg-setup.o
obj-$(CONFIG_MACH_SG565)	+= sg-setup.o
obj-$(CONFIG_MACH_SG580)	+= sg-setup.o
obj-$(CONFIG_MACH_SG590)	+= sg-setup.o
obj-$(CONFIG_MACH_SG640)	+= sg-setup.o
obj-$(CONFIG_MACH_ESS710)	+= sg-setup.o
obj-$(CONFIG_MACH_SG720)	+= sg-setup.o
obj-$(CONFIG_MACH_SG8100)	+= sg-setup.o
obj-$(CONFIG_MACH_SGARMAUTO)	+= sg-setup.o

obj-$(CONFIG_PCI)		+= $(obj-pci-$(CONFIG_PCI)) common-pci.o
obj-$(CONFIG_IXP4XX_DEBUG_FIQ)	+= fiq.o
