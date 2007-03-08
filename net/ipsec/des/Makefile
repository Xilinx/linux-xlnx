.EXPORT_ALL_VARIABLES:

OPENSWANSRCDIR := $(ROOTDIR)/openswan

EXTRA_CFLAGS += -I$(OPENSWANSRCDIR) -I$(OPENSWANSRCDIR)/lib \
			-I$(OPENSWANSRCDIR)/linux/net/ipsec \
			-I$(OPENSWANSRCDIR)/linux/include \
			-I$(ROOTDIR)/modules/ocf

# special hack required because of exported object files
$(obj)/.linked:
	ln -fs $(OPENSWANSRCDIR)/linux/net/ipsec/des/*.[cS] $(obj)/.
	touch $(obj)/.linked

-include $(obj)/.linked

include $(OPENSWANSRCDIR)/linux/net/ipsec/des/Makefile.fs2_6

clean:
	rm -f $(obj)/.linked $(obj)/*.o $(obj)/*.ko


