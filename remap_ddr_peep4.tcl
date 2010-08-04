connect cpu1

# unlock the PCAP, then mask out the ROM so that RAM is available
# in low and high addresses

mwr 0xF8007034 0x757BDF0D
mwr 0xF8007028 0xFFFFFFFF

# map ram into upper addresses

mwr 0xf8000008 0xdf0d
mwr 0xf8000910 0x1F

# remap DDR to zero

mwr 0xf8f00000 0
mwr 0xf8f00040 0
mwr 0xf8f00000 0x2

