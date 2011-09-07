#
# arch/microblaze/boot/Makefile
#

obj-y += linked_dtb.o

targets := linux.bin linux.bin.gz simpleImage.%

OBJCOPYFLAGS := -O binary

# Ensure system.dtb exists
$(obj)/linked_dtb.o: $(obj)/system.dtb

# Generate system.dtb from $(DTB).dtb
ifneq ($(DTB),system)
$(obj)/system.dtb: $(obj)/$(DTB).dtb
	$(call if_changed,cp)
endif

$(obj)/linux.bin: vmlinux FORCE
	$(call if_changed,objcopy)
	$(call if_changed,uimage)
	@echo 'Kernel: $@ is ready' ' (#'`cat .version`')'

$(obj)/linux.bin.gz: $(obj)/linux.bin FORCE
	$(call if_changed,gzip)
	@echo 'Kernel: $@ is ready' ' (#'`cat .version`')'

quiet_cmd_cp = CP      $@
	cmd_cp = cp $< $@ 

$(obj)/simpleImage.%: vmlinux FORCE
	$(call if_changed,cp)
	@echo 'Kernel: $@ is ready' ' (#'`cat .version`')'


# Rule to build device tree blobs
DTC_FLAGS := -p 1024

$(obj)/%.dtb: $(src)/dts/%.dts FORCE
	$(call cmd,dtc)

clean-files += *.dtb simpleImage.*.unstrip linux.bin.ub
