#
# arch/microblaze/boot/Makefile
#

hostprogs-y     := dtc

obj-y += linked_dtb.o

targets := linux.bin linux.bin.gz

OBJCOPYFLAGS_linux.bin  := -O binary

# Default flags to Device Tree Compiler
DTS_FLAGS       ?= -p 1024

# Where the DTS files live
dtstree         := $(srctree)/$(src)/dts

# Ensure system.dtb exists
$(obj)/linked_dtb.o: $(obj)/system.dtb

# Generate system.dtb from $(DTB).dtb
ifneq ($(DTB),system)
$(obj)/system.dtb: $(obj)/$(DTB).dtb
	cp $< $@
endif

$(obj)/linux.bin: vmlinux FORCE
	[ -n $(CONFIG_INITRAMFS_SOURCE) ] && [ ! -e $(CONFIG_INITRAMFS_SOURCE) ] && \
	touch $(CONFIG_INITRAMFS_SOURCE) || echo "No CPIO image"
	$(call if_changed,objcopy)
	@echo 'Kernel: $@ is ready' ' (#'`cat .version`')'

$(obj)/linux.bin.gz: $(obj)/linux.bin FORCE
	$(call if_changed,gzip)
	@echo 'Kernel: $@ is ready' ' (#'`cat .version`')'

# Rule to build device tree blobs
$(obj)/%.dtb: $(dtstree)/%.dts $(obj)/dtc
	$(obj)/dtc -O dtb -o $(obj)/$*.dtb -b 0 $(DTS_FLAGS) $(dtstree)/$*.dts

#############
# Bits for building dtc
# DTC_GENPARSER      := 1    # Uncomment to rebuild flex/bison output

dtc-objs := dtc.o flattree.o fstree.o data.o livetree.o treesource.o srcpos.o checks.o
dtc-objs += dtc-lexer.lex.o dtc-parser.tab.o
dtc-objs := $(addprefix dtc-src/, $(dtc-objs))

# prerequisites on generated files needs to be explicit
$(obj)/dtc-src/dtc-parser.tab.o: $(obj)/dtc-src/dtc-parser.tab.c $(obj)/dtc-src/dtc-parser.tab.h
$(obj)/dtc-src/dtc-lexer.lex.o:  $(obj)/dtc-src/dtc-lexer.lex.c $(obj)/dtc-src/dtc-parser.tab.h

HOSTCFLAGS += -I$(src)/dtc-src/ -I$(src)/libfdt/

targets += dtc-src/dtc-parser.tab.c
targets += dtc-src/dtc-lexer.lex.c

clean-files += dtc-src/dtc-parser.tab.h

ifdef DTC_GENPARSER
BISON = bison
FLEX = flex

quiet_cmd_bison = BISON   $@
      cmd_bison = $(BISON) -o$@ -d $<; cp $@ $@_shipped
quiet_cmd_flex = FLEX    $@
      cmd_flex = $(FLEX) -o$@ $<; cp $@ $@_shipped

$(obj)/dtc-src/dtc-parser.tab.c: $(src)/dtc-src/dtc-parser.y FORCE
	$(call if_changed,bison)

$(obj)/dtc-src/dtc-parser.tab.h: $(obj)/dtc-src/dtc-parser.tab.c

$(obj)/dtc-src/dtc-lexer.lex.c: $(src)/dtc-src/dtc-lexer.l FORCE
	$(call if_changed,flex)
endif

clean-kernel += linux.bin linux.bin.gz
