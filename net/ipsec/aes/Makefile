.EXPORT_ALL_VARIABLES:

OPENSWANSRCDIR := $(ROOTDIR)/openswan

EXTRA_CFLAGS += -I$(ROOTDIR)/openswan -I$(ROOTDIR)/openswan/lib \
			-I$(ROOTDIR)/openswan/linux/net/ipsec \
			-I$(ROOTDIR)/openswan/linux/include \
			-I$(ROOTDIR)/modules/ocf

include $(ROOTDIR)/openswan/linux/net/ipsec/aes/Makefile.fs2_6

# special hack required because of exported object files
$(obj)%.c: $(ROOTDIR)/openswan/linux/net/ipsec/aes/%.c
	ln -fs $< $@

