UTS_SYSNAME = -DUTS_SYSNAME=\"uClinux\"


# r31 holds current when in kernel mode
CFLAGS += -ffixed-r31

ifdef CONFIG_MICROBLAZE_USE_BARREL
CFLAGS += -mxl-barrel-shift
endif

ifdef CONFIG_MICROBLAZE_USE_FPU
CFLAGS += -mhard-float
endif

ifdef CONFIG_MICROBLAZE_USE_PCMP_INSTR
CFLAGS += -mxl-pattern-compare
endif

ifdef CONFIG_MICROBLAZE_USE_DIV
CFLAGS += -mno-xl-soft-mul
endif

ifdef CONFIG_MICROBLAZE_USE_HW_MUL
CFLAGS += -mno-xl-soft-div
endif

LDFLAGS_BLOB := --format binary --oformat elf32-microblaze

LIBGCC := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

head-y		:= arch/microblaze/kernel/head.o
libs-y		+= arch/microblaze/lib/ $(LIBGCC)
core-y		+= arch/microblaze/kernel/ arch/microblaze/mm/


boot := arch/microblaze/boot

all: linux.bin


archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

linux.bin linux.bin.gz: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@


define archhelp
  echo  '* linux.bin    - Create raw binary'
  echo  '  linux.bin.gz - Create compressed raw binary'
endef
