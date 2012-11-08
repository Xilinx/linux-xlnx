   zreladdr-y	+= 0x00008000
params_phys-y	:= 0x00000100
initrd_phys-y	:= 0x00800000

dtb-$(CONFIG_ARCH_ZYNQ) += zynq-zc702.dtb zynq-afx.dtb zynq-zc706.dtb \
				zynq-zc770-xm010.dtb zynq-zc770-xm011.dtb \
				zynq-zc770-xm012.dtb zynq-zc770-xm013.dtb \
				zynq-ep107.dtb
