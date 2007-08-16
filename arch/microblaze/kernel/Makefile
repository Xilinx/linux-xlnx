#
# Makefile
#

extra-y := head.o vmlinux.lds

obj-y += init_task.o setup.o traps.o process.o time.o ptrace.o irq.o \
		semaphore.o opb_intc.o opb_timer.o entry.o heartbeat.o \
		sys_microblaze.o signal.o platform.o \
		hw_exception_handler.o exceptions.o

obj-y += cpu/

obj-$(CONFIG_WANT_DEVICE_TREE) += of_platform.o of_device.o prom.o prom_parse.o system.dtb.o

obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o

system.dtb: ../boot/dts/$(CONFIG_DEVICE_TREE)
	dtc -f -O dtb -b 0 -V 16 $< > $@

%.dtb.o: %.dtb
	${CROSS_COMPILE}objcopy -I binary -O elf32-big $< $@
