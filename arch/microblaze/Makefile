UTS_SYSNAME = -DUTS_SYSNAME=\"uClinux\"

CROSS_COMPILE=microblaze-uclinux-

CFLAGS += -g

# Use cpu-related CONFIG_ vars to set compile options.
CPUFLAGS-$(CONFIG_XILINX_MICROBLAZE0_USE_HW_MUL) += -mno-xl-soft-mul
CPUFLAGS-$(CONFIG_XILINX_MICROBLAZE0_USE_DIV) += -mno-xl-soft-div
CPUFLAGS-$(CONFIG_XILINX_MICROBLAZE0_USE_BARREL) += -mxl-barrel-shift
CPUFLAGS-$(CONFIG_XILINX_MICROBLAZE0_USE_PCMP) += -mxl-pattern-compare

# The various CONFIG_XILINX cpu feeatures options are integers 0/1
# rather than bools, so we we use -1 suffix instead of -y like
# the rest of the kernel
CFLAGS += $(CPUFLAGS-1)

# r31 holds current when in kernel moed
CFLAGS += -ffixed-r31

LDFLAGS_BLOB := --format binary --oformat elf32-microblaze

LIBGCC := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

head-y		:= arch/microblaze/kernel/head.o
libs-y		+= arch/microblaze/lib/ $(LIBGCC)
core-y		+= arch/microblaze/kernel/ arch/microblaze/mm/

boot := arch/microblaze/boot

all: linux.bin

#prepare: include/asm-$(ARCH)/asm-offsets.h

#arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
#				   include/config/MARKER

#include/asm-$(ARCH)/asm-offsets.h: arch/$(ARCH)/kernel/asm-offsets.s
#	$(call filechk,gen-asm-offsets)


archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

linux.bin linux.bin.gz: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@


#CLEAN_FILES += include/asm-$(ARCH)/asm-offsets.h

define archhelp
  echo  '* linux.bin    - Create raw binary'
  echo  '  linux.bin.gz - Create compressed raw binary'
endef
