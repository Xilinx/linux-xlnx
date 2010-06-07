#!/bin/sh

${CROSS_COMPILE:-arm-none-linux-gnueabi-}objcopy -R .note.gnu.build-id -R .comment -R .ARM.attributes --change-addresses -0xBFE00000 vmlinux vmlinux.elf
${CROSS_COMPILE:-arm-none-linux-gnueabi-}objcopy --gap-fill=0xFF -O binary vmlinux.elf vmlinux.bin
