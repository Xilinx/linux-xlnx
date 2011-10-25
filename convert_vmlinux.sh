#!/bin/sh


if [ "$1" == "cpu1" ] ; then
	echo "Creating vmlinux1.bin for CPU1 at 66MB (0x4200000) start address"

	${CROSS_COMPILE:-arm-none-linux-gnueabi-}objcopy -R .note.gnu.build-id -R .comment -R .ARM.attributes --change-addresses -0xBBE00000 vmlinux vmlinux1.elf
	${CROSS_COMPILE:-arm-none-linux-gnueabi-}objcopy --gap-fill=0xFF -O binary vmlinux1.elf vmlinux1.bin

elif [ "$1" == "cpu0amp" ] ; then
	echo "Creating vmlinux.bin for CPU0 at 0x400000 start address"
	${CROSS_COMPILE:-arm-none-linux-gnueabi-}objcopy -R .note.gnu.build-id -R .comment -R .ARM.attributes --change-addresses -0xBC000000 vmlinux vmlinux.elf
	${CROSS_COMPILE:-arm-none-linux-gnueabi-}objcopy --gap-fill=0xFF -O binary vmlinux.elf vmlinux.bin
else
	echo "Creating vmlinux.bin for CPU0 at 0 start address"
	${CROSS_COMPILE:-arm-none-linux-gnueabi-}objcopy -R .note.gnu.build-id -R .comment -R .ARM.attributes --change-addresses -0xC0000000 vmlinux vmlinux.elf
	${CROSS_COMPILE:-arm-none-linux-gnueabi-}objcopy --gap-fill=0xFF -O binary vmlinux.elf vmlinux.bin
fi

