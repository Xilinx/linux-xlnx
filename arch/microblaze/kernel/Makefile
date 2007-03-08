#
# Makefile
#

extra-y := head.o vmlinux.lds

obj-y += init_task.o setup.o traps.o process.o time.o ptrace.o irq.o \
		semaphore.o opb_intc.o opb_timer.o entry.o heartbeat.o \
		sys_microblaze.o signal.o cache.o

obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o
