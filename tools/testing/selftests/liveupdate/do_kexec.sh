#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -e

kexec -l -s --reuse-cmdline /boot/bzImage
kexec -e
