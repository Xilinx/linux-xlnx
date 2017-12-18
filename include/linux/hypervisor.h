#ifndef __LINUX_HYPEVISOR_H
#define __LINUX_HYPEVISOR_H

/*
 *	Generic Hypervisor support
 *		Juergen Gross <jgross@suse.com>
 */

#ifdef CONFIG_HYPERVISOR_GUEST
#include <asm/hypervisor.h>
#else
static inline void hypervisor_pin_vcpu(int cpu)
{
}
#endif

#endif /* __LINUX_HYPEVISOR_H */
