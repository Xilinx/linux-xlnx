.EXPORT_ALL_VARIABLES:

OPENSWANSRCDIR := $(ROOTDIR)/openswan

EXTRA_CFLAGS += -I$(OPENSWANSRCDIR) -I$(OPENSWANSRCDIR)/openswan/lib \
			-I$(OPENSWANSRCDIR)/linux/net/ipsec \
			-I$(OPENSWANSRCDIR)/linux/include \
			-I$(ROOTDIR)/modules/ocf

$(obj)/.linked:
	ln -fs $(OPENSWANSRCDIR)/linux/net/ipsec/*.[cS] $(obj)/.
	touch $(obj)/.linked

-include $(obj)/.linked

include $(OPENSWANSRCDIR)/linux/net/ipsec/Makefile.fs2_6

$(obj)/version.c:     $(OPENSWANSRCDIR)/linux/net/ipsec/version.in.c
	sed '/"/s/xxx/$(IPSECVERSION)/' $? > $@

clean:
	rm -f $(obj)/.linked $(obj)/*.o $(obj)/*.ko $(obj)/version.c


