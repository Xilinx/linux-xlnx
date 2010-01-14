#!/bin/sh

${CROSS_COMPILE:-arm-none-linux-gnueabi-}objcopy -R .note.gnu.build-id -R .comment -R .ARM.attributes --change-addresses -0xC0000000 vmlinux vmlinux.elf
