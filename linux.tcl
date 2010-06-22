connect cpu1

# unlock the PCAP, then mask out the ROM so that RAM is available
# in low and high addresses

mwr 0xFE007030 0x757BDF0D
mwr 0xFE007024 0xFFFFFFFF

# remap DDR to zero

mwr 0xfef00000 0
mwr 0xfef00040 0
mwr 0xfef00000 0x2

# load the ramdisk

dow -norst ramdisk1M.image.elf

# load the kernel

dow -norst vmlinux.elf

# start primary CPU only, assuming non-SMP kernel

con
