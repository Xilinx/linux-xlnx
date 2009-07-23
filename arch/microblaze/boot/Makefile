#
# arch/microblaze/boot/Makefile
#

obj-y += linked_dtb.o

targets := linux.bin linux.bin.gz simpleImage.%

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

quiet_cmd_cp = CP      $@$(if $2,$2)
      cmd_cp = cp $< $@$(if $2,$2)

quiet_cmd_strip = STRIP   $@
      cmd_strip = $(STRIP) -K _start -K _end -K __log_buf -K _fdt_start vmlinux -o $@

$(obj)/simpleImage.%: vmlinux FORCE
	$(call if_changed,cp)
	$(call if_changed,cp,.unstrip)
	$(call if_changed,strip)
	@echo 'Kernel: $@ is ready' ' (#'`cat .version`')'

# Rule to build device tree blobs
DTC = $(objtree)/scripts/dtc/dtc

# Rule to build device tree blobs
$(obj)/%.dtb: $(dtstree)/%.dts
	$(DTC) -O dtb -o $(obj)/$*.dtb -b 0 $(DTS_FLAGS) $(dtstree)/$*.dts

#############
# Bits for building dtc
# DTC_GENPARSER      := 1    # Uncomment to rebuild flex/bison output

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

clean-kernel += linux.bin linux.bin.gz simpleImage.*

clean-files += *.dtb
