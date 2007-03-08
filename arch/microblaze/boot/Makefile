# arch/microblaze/boot/Makefile

targets := linux.bin linux.bin.gz

OBJCOPYFLAGS_linux.bin  := -O binary

$(obj)/linux.bin: vmlinux FORCE
	$(call if_changed,objcopy)
	@echo 'Kernel: $@ is ready' ' (#'`cat .version`')'

$(obj)/linux.bin.gz: $(obj)/linux.bin FORCE
	$(call if_changed,gzip)
	@echo 'Kernel: $@ is ready' ' (#'`cat .version`')'

CLEAN_FILES += arch/$(ARCH)/linux.bin arch/$(ARCH)/linux.bin.gz
