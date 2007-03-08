# arch/niosnommu/Makefile
#
# Makefile for the architecture dependent flags and dependencies on the
# nios.
#
# Copyright (C) 2001 Vic Phillips (vic@microtronix.com)
#
# based on sparcnommu/Makefile:
#
# Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
#
KERNELLOAD = ${shell echo `grep "nasys_program_mem " include/asm/nios.h | sed 's/^.*\*)//' | sed 's/)//'`}

HARDWARE_MK = arch/$(ARCH)/hardware.mk

platform-$(CONFIG_NIOS)		:= NIOS2
PLATFORM := $(platform-y)

board-$(CONFIG_ALTERA_STRATIX)	:= altera_stratix
board-$(CONFIG_ALTERA_STRATIX_PRO)	:= altera_stratix_pro
board-$(CONFIG_ALTERA_STRATIX_II)	:= altera_stratix_ii
board-$(CONFIG_ALTERA_CYCLONE)	:= altera_cyclone
board-$(CONFIG_ALTERA_CYCLONE_1C12_EVAL)	:= altera_cyclone_1c12_eval
board-$(CONFIG_MICROTRONIX_STRATIX)	:= microtronix_stratix
board-$(CONFIG_MICROTRONIX_CYCLONE)	:= microtronix_cyclone
board-$(CONFIG_MICROTRONIX_UKIT)	:= microtronix_ukit
board-$(CONFIG_MICROTRONIX_PSK)	:= microtronix_psk
BOARD := $(board-y)

model-$(CONFIG_RAMKERNEL)	:= ram
model-$(CONFIG_ROMKERNEL)	:= rom
model-$(CONFIG_HIMEMKERNEL)	:= himem
MODEL := $(model-y)

export PLATFORM BOARD MODEL

CFLAGS += -DNO_MM -pipe -D__linux__ -D__ELF__
#CFLAGS += -DNO_MM -save-temps -D__linux__ -D__ELF__

# Uncomment this if you are doing gdb source level
# debugging of the kernel to get the proper debugging information.
#
#CFLAGS +=  -DDEBUG

# Turn on/off various hardware multiply options
cpu-cflags-$(CONFIG_NIOS2_HW_MUL_OFF) += -mno-hw-mul -mno-hw-mulx
cpu-cflags-$(CONFIG_NIOS2_HW_MUL) += -mhw-mul -mno-hw-mulx
cpu-cflags-$(CONFIG_NIOS2_HW_MULX) += -mhw-mul -mhw-mulx
CFLAGS += $(cpu-cflags-y)

# mulx flags currently cause older version of nios2-elf-gcc to fail
# The following line ensures that all mulx flags are removed before
# it is passed to the compiler.
mulx_help_text:= $(shell $(CC) --target-help | grep mulx)
ifeq "$(mulx_help_text)" ""
CFLAGS := $(filter-out -mhw-mulx -mno-hw-mulx, $(CFLAGS))
endif

# Temporary workaround for nios2-elf-gcc bug
# First noticed in v3.4.1 (Altera Nios II 1.1 b131)
# To be removed at a later date when bug is resolved.
CFLAGS += -fno-optimize-sibling-calls

# This undefines the "__init" type used in defining initialization
# procedures.  When defined, the procedures are put into an 'init' data
# section that GDB doesn't recognize as source.
#
CFLAGS += -DNO_TEXT_SECTIONS
CFLAGS += -fno-builtin
CFLAGS += -O2 -g -G 0
CFLAGS += -DUTS_SYSNAME=\"uClinux\"

CFLAGS_GCC_INC := $(shell $(CC) -print-file-name=include)
CFLAGS += -I$(CFLAGS_GCC_INC)

AFLAGS += -DNO_MM -g
#AFLAGS += -DNO_MM -g -save-temps

# vic - add this to get name of nios gcc library
LIBGCC_CFLAGS = $(if $(CONFIG_NIOS2_HW_MUL_OFF),-mno-hw-mul)
LIBGCC	:= `$(CC) --print-libgcc-file-name $(LIBGCC_CFLAGS)`

# add this to avoid multiple '_stack' and '_vecbase' definition errors
#
ifdef niosgnu
# Include the path to the lib directory where the ldscripts are found to fix
# a problem with the cygwin/bash environment.

#cygwhack: kenw - this following section could be a possible problem
#                 due to the O= option on the command line.
LDSCRIPTS:=$(shell nios2-elf-gcc -print-file-name=ldscripts)
LDFLAGS += -mnios2elf -L $(LDSCRIPTS)/..
else
LDFLAGS += -mnios2elf
LDLIBS := -L `$(CC) -print-file-name=m32`  -l gcc
endif

head-y := arch/nios2nommu/kernel/head.o arch/nios2nommu/kernel/init_task.o

CLEAN_FILES := include/asm-$(ARCH)/asm-offsets.h \
	       $(HARDWARE_MK) \
	       arch/$(ARCH)/kernel/asm-offsets.s \
	       linux.srec \
	       linux.flash \
	       linux.bin \
	       linux.bin.srec

core-y	+= arch/nios2nommu/kernel/ \
	   arch/nios2nommu/mm/ \
	   arch/nios2nommu/drivers/ 

libs-y	+= arch/nios2nommu/lib/

libs-y		+= $(LIBGCC)
####;dgt2;tmp;

# force user to configure hardware before building kernel

pardoned_targets = clean mrproper sgmldocs psdocs pdfdocs \
                   htmldocs mandocs

-include $(HARDWARE_MK)
build_targets = $(filter-out $(pardoned_targets), $(MAKECMDGOALS))
ifneq '$(strip $(build_targets))' ''
  ifndef SYSPTF
    ifneq '$(firstword $(MAKECMDGOALS))' 'hwselect'
      $(error Run "make hwselect SYSPTF=<system.ptf>" first)
    endif
  endif
endif

quiet_cmd_gen_mk = '  RUNNING $@'
define cmd_gen_mk
	mkdir -p $(dir $(objtree)/$(HARDWARE_MK)); \
	perl -I$(TOPDIR)/arch/$(ARCH)/scripts \
		$(srctree)/arch/$(ARCH)/scripts/hwselect.pl $(SYSPTF) \
		$(objtree)/$(HARDWARE_MK)
endef

.PHONY: hwselect
hwselect:
	@echo $($(quiet)cmd_gen_mk);
	@$(cmd_gen_mk)	
	
prepare: include/nios2_system.h

archclean:
	$(call descend arch/$(ARCH)/boot, subdirclean)

define filechk_nios2_system.h
	# call perl script that will build nios2_system.h file
	perl -I$(TOPDIR)/arch/$(ARCH)/scripts \
	  $(TOPDIR)/arch/$(ARCH)/scripts/gen_nios2_system.h.pl $(CPU) $(EXEMEM) $(UPLMEM)
endef

include/nios2_system.h: $(SYSPTF) FORCE
	$(call filechk,nios2_system.h)

quiet_cmd_touch = '  TOUCH   $@'
      cmd_touch = touch $(TOPDIR)/$@
	
arch/$(ARCH)/kernel/vmlinux.lds.S: FORCE
	@echo $($(quiet)cmd_touch);
	@$(cmd_touch)
	
linuxsrec: linux
	$(OBJCOPY) -O srec $(LINUX) linux.srec

boot := arch/nios2nommu/boot

zImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

compressed: zImage

CLEAN_FILES += include/nios2_system.h

archmrproper:

archdep:

