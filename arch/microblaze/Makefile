UTS_SYSNAME = -DUTS_SYSNAME=\"uClinux\"

CFLAGS += -g
CFLAGS += -mno-xl-soft-mul
CFLAGS += -mno-xl-soft-div
CFLAGS += -mxl-barrel-shift
CFLAGS += -ffixed-r31

LDFLAGS_BLOB := --format binary --oformat elf32-microblaze

LIBGCC := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

head-y		:= arch/microblaze/kernel/head.o
libs-y		+= arch/microblaze/lib/ $(LIBGCC)
core-y		+= arch/microblaze/kernel/ arch/microblaze/mm/

boot := arch/microblaze/boot

all: linux.bin

prepare: include/asm-$(ARCH)/asm-offsets.h

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/asm-offsets.h: arch/$(ARCH)/kernel/asm-offsets.s
	$(call filechk,gen-asm-offsets)


archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

linux.bin linux.bin.gz: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@


CLEAN_FILES += include/asm-$(ARCH)/asm-offsets.h

define archhelp
  echo  '* linux.bin    - Create raw binary'
  echo  '  linux.bin.gz - Create compressed raw binary'
endef
