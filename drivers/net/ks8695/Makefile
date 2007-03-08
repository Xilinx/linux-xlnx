
CFLAGS_KS8695 += -DRX_TASK -DTX_TASK -DARM_LINUX -DEXPORT_SYMTAB
#CFLAGS_KS8695 += -Wno-strict-prototypes 

CFLAGS_ks8695_main.o += $(CFLAGS_KS8695)
CFLAGS_ks8695_fxhw.o += $(CFLAGS_KS8695)
CFLAGS_ks8695_cache.o += $(CFLAGS_KS8695)


obj-$(CONFIG_ETH_KS8695) += ks8695.o

ks8695-y := ks8695_main.o ks8695_fxhw.o ks8695_cache.o

