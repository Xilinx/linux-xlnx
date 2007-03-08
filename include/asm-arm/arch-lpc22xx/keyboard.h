/*
 *  linux/include/asm-arm/arch-lpc22xx/keyboard.h
 *
 *  Copyright (C) 2004 Philips Semiconductors
 *
 */
#ifndef __ASM_ARMNOMMU_ARCH_LPC22XX_KEYBOARD_H
#define __ASM_ARMNOMMU_ARCH_LPC22XX_KEYBOARD_H

#define kbd_setkeycode(sc,kc) (-EINVAL)
#define kbd_getkeycode(sc) (-EINVAL)
#define kbd_translate(sc,kcp,rm) ({ *(kcp) = (sc); 1; })
#define kbd_unexpected_up(kc) (0200)
#define kbd_leds(leds)
#define kbd_init_hw()
#define kbd_enable_irq()
#define kbd_disable_irq()

#endif /* __ASM_ARMNOMMU_ARCH_LPC22XX_KEYBOARD_H */
