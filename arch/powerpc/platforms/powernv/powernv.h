#ifndef _POWERNV_H
#define _POWERNV_H

#ifdef CONFIG_SMP
extern void pnv_smp_init(void);
#else
static inline void pnv_smp_init(void) { }
#endif

struct pci_dev;

#ifdef CONFIG_PCI
extern void pnv_pci_init(void);
extern void pnv_pci_shutdown(void);
#else
static inline void pnv_pci_init(void) { }
static inline void pnv_pci_shutdown(void) { }
#endif

extern u32 pnv_get_supported_cpuidle_states(void);
extern u64 pnv_deepest_stop_state;

extern void pnv_lpc_init(void);

extern void opal_handle_events(uint64_t events);
extern void opal_event_shutdown(void);

bool cpu_core_split_required(void);

#endif /* _POWERNV_H */
