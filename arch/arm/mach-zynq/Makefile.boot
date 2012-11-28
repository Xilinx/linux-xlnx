   zreladdr-y	+= 0x00008000
params_phys-y	:= 0x00000100
initrd_phys-y	:= 0x00800000

dtb-$(CONFIG_ARCH_ZYNQ) += \
			zynq-afx-nand.dtb \
			zynq-afx-nor.dtb \
			zynq-ep107.dtb \
			zynq-zc702.dtb \
			zynq-zc706.dtb \
			zynq-zc770-xm010.dtb \
			zynq-zc770-xm011.dtb \
			zynq-zc770-xm012.dtb \
			zynq-zc770-xm013.dtb \
			zynq-zed.dtb
