connect cpu1

# unlock the PCAP, then mask out the ROM so that RAM is available
# in low and high addresses

mwr 0xFE007034 0x757BDF0D
mwr 0xFE007028 0xFFFFFFFF

# remap DDR to zero

mwr 0xfef00000 0
mwr 0xfef00040 0
mwr 0xfef00000 0x2

# load the ramdisk

dow -norst ramdisk1M.image.gz.elf

# load the kernel

dow -norst vmlinux.elf

# set the pc on the 2nd cpu to the start of the kernel
# then start it executing prior to letting the 1st cpu run

targets cpu2
rwr pc 0x8000
con

# start the kernel on the 1st cpu

targets cpu1
con
