/*
 *  linux/include/asm-armnommu/arch-s3c3410/keyboard.h
 */
#ifndef __ASM_ARMNOMMU_ARCH_S3C3410_KEYBOARD_H
#define __ASM_ARMNOMMU_ARCH_S3C3410_KEYBOARD_H

#define kbd_setkeycode(sc,kc) (-EINVAL)
#define kbd_getkeycode(sc) (-EINVAL)
#define kbd_translate(sc,kcp,rm) ({ *(kcp) = (sc); 1; })
#define kbd_unexpected_up(kc) (0200)
#define kbd_leds(leds)
#define kbd_init_hw()
#define kbd_enable_irq()
#define kbd_disable_irq()

#endif /* __ASM_ARMNOMMU_ARCH_S3C3410_KEYBOARD_H */
