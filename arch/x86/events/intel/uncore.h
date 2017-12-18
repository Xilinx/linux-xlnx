#include <linux/slab.h>
#include <linux/pci.h>
#include <asm/apicdef.h>

#include <linux/perf_event.h>
#include "../perf_event.h"

#define UNCORE_PMU_NAME_LEN		32
#define UNCORE_PMU_HRTIMER_INTERVAL	(60LL * NSEC_PER_SEC)
#define UNCORE_SNB_IMC_HRTIMER_INTERVAL (5ULL * NSEC_PER_SEC)

#define UNCORE_FIXED_EVENT		0xff
#define UNCORE_PMC_IDX_MAX_GENERIC	8
#define UNCORE_PMC_IDX_FIXED		UNCORE_PMC_IDX_MAX_GENERIC
#define UNCORE_PMC_IDX_MAX		(UNCORE_PMC_IDX_FIXED + 1)

#define UNCORE_PCI_DEV_FULL_DATA(dev, func, type, idx)	\
		((dev << 24) | (func << 16) | (type << 8) | idx)
#define UNCORE_PCI_DEV_DATA(type, idx)	((type << 8) | idx)
#define UNCORE_PCI_DEV_DEV(data)	((data >> 24) & 0xff)
#define UNCORE_PCI_DEV_FUNC(data)	((data >> 16) & 0xff)
#define UNCORE_PCI_DEV_TYPE(data)	((data >> 8) & 0xff)
#define UNCORE_PCI_DEV_IDX(data)	(data & 0xff)
#define UNCORE_EXTRA_PCI_DEV		0xff
#define UNCORE_EXTRA_PCI_DEV_MAX	3

#define UNCORE_EVENT_CONSTRAINT(c, n) EVENT_CONSTRAINT(c, n, 0xff)

struct pci_extra_dev {
	struct pci_dev *dev[UNCORE_EXTRA_PCI_DEV_MAX];
};

struct intel_uncore_ops;
struct intel_uncore_pmu;
struct intel_uncore_box;
struct uncore_event_desc;

struct intel_uncore_type {
	const char *name;
	int num_counters;
	int num_boxes;
	int perf_ctr_bits;
	int fixed_ctr_bits;
	unsigned perf_ctr;
	unsigned event_ctl;
	unsigned event_mask;
	unsigned event_mask_ext;
	unsigned fixed_ctr;
	unsigned fixed_ctl;
	unsigned box_ctl;
	unsigned msr_offset;
	unsigned num_shared_regs:8;
	unsigned single_fixed:1;
	unsigned pair_ctr_ctl:1;
	unsigned *msr_offsets;
	struct event_constraint unconstrainted;
	struct event_constraint *constraints;
	struct intel_uncore_pmu *pmus;
	struct intel_uncore_ops *ops;
	struct uncore_event_desc *event_descs;
	const struct attribute_group *attr_groups[4];
	struct pmu *pmu; /* for custom pmu ops */
};

#define pmu_group attr_groups[0]
#define format_group attr_groups[1]
#define events_group attr_groups[2]

struct intel_uncore_ops {
	void (*init_box)(struct intel_uncore_box *);
	void (*exit_box)(struct intel_uncore_box *);
	void (*disable_box)(struct intel_uncore_box *);
	void (*enable_box)(struct intel_uncore_box *);
	void (*disable_event)(struct intel_uncore_box *, struct perf_event *);
	void (*enable_event)(struct intel_uncore_box *, struct perf_event *);
	u64 (*read_counter)(struct intel_uncore_box *, struct perf_event *);
	int (*hw_config)(struct intel_uncore_box *, struct perf_event *);
	struct event_constraint *(*get_constraint)(struct intel_uncore_box *,
						   struct perf_event *);
	void (*put_constraint)(struct intel_uncore_box *, struct perf_event *);
};

struct intel_uncore_pmu {
	struct pmu			pmu;
	char				name[UNCORE_PMU_NAME_LEN];
	int				pmu_idx;
	int				func_id;
	bool				registered;
	atomic_t			activeboxes;
	struct intel_uncore_type	*type;
	struct intel_uncore_box		**boxes;
};

struct intel_uncore_extra_reg {
	raw_spinlock_t lock;
	u64 config, config1, config2;
	atomic_t ref;
};

struct intel_uncore_box {
	int pci_phys_id;
	int pkgid;
	int n_active;	/* number of active events */
	int n_events;
	int cpu;	/* cpu to collect events */
	unsigned long flags;
	atomic_t refcnt;
	struct perf_event *events[UNCORE_PMC_IDX_MAX];
	struct perf_event *event_list[UNCORE_PMC_IDX_MAX];
	struct event_constraint *event_constraint[UNCORE_PMC_IDX_MAX];
	unsigned long active_mask[BITS_TO_LONGS(UNCORE_PMC_IDX_MAX)];
	u64 tags[UNCORE_PMC_IDX_MAX];
	struct pci_dev *pci_dev;
	struct intel_uncore_pmu *pmu;
	u64 hrtimer_duration; /* hrtimer timeout for this box */
	struct hrtimer hrtimer;
	struct list_head list;
	struct list_head active_list;
	void *io_addr;
	struct intel_uncore_extra_reg shared_regs[0];
};

#define UNCORE_BOX_FLAG_INITIATED	0
#define UNCORE_BOX_FLAG_CTL_OFFS8	1 /* event config registers are 8-byte apart */

struct uncore_event_desc {
	struct kobj_attribute attr;
	const char *config;
};

struct pci2phy_map {
	struct list_head list;
	int segment;
	int pbus_to_physid[256];
};

struct pci2phy_map *__find_pci2phy_map(int segment);

ssize_t uncore_event_show(struct kobject *kobj,
			  struct kobj_attribute *attr, char *buf);

#define INTEL_UNCORE_EVENT_DESC(_name, _config)			\
{								\
	.attr	= __ATTR(_name, 0444, uncore_event_show, NULL),	\
	.config	= _config,					\
}

#define DEFINE_UNCORE_FORMAT_ATTR(_var, _name, _format)			\
static ssize_t __uncore_##_var##_show(struct kobject *kobj,		\
				struct kobj_attribute *attr,		\
				char *page)				\
{									\
	BUILD_BUG_ON(sizeof(_format) >= PAGE_SIZE);			\
	return sprintf(page, _format "\n");				\
}									\
static struct kobj_attribute format_attr_##_var =			\
	__ATTR(_name, 0444, __uncore_##_var##_show, NULL)

static inline unsigned uncore_pci_box_ctl(struct intel_uncore_box *box)
{
	return box->pmu->type->box_ctl;
}

static inline unsigned uncore_pci_fixed_ctl(struct intel_uncore_box *box)
{
	return box->pmu->type->fixed_ctl;
}

static inline unsigned uncore_pci_fixed_ctr(struct intel_uncore_box *box)
{
	return box->pmu->type->fixed_ctr;
}

static inline
unsigned uncore_pci_event_ctl(struct intel_uncore_box *box, int idx)
{
	if (test_bit(UNCORE_BOX_FLAG_CTL_OFFS8, &box->flags))
		return idx * 8 + box->pmu->type->event_ctl;

	return idx * 4 + box->pmu->type->event_ctl;
}

static inline
unsigned uncore_pci_perf_ctr(struct intel_uncore_box *box, int idx)
{
	return idx * 8 + box->pmu->type->perf_ctr;
}

static inline unsigned uncore_msr_box_offset(struct intel_uncore_box *box)
{
	struct intel_uncore_pmu *pmu = box->pmu;
	return pmu->type->msr_offsets ?
		pmu->type->msr_offsets[pmu->pmu_idx] :
		pmu->type->msr_offset * pmu->pmu_idx;
}

static inline unsigned uncore_msr_box_ctl(struct intel_uncore_box *box)
{
	if (!box->pmu->type->box_ctl)
		return 0;
	return box->pmu->type->box_ctl + uncore_msr_box_offset(box);
}

static inline unsigned uncore_msr_fixed_ctl(struct intel_uncore_box *box)
{
	if (!box->pmu->type->fixed_ctl)
		return 0;
	return box->pmu->type->fixed_ctl + uncore_msr_box_offset(box);
}

static inline unsigned uncore_msr_fixed_ctr(struct intel_uncore_box *box)
{
	return box->pmu->type->fixed_ctr + uncore_msr_box_offset(box);
}

static inline
unsigned uncore_msr_event_ctl(struct intel_uncore_box *box, int idx)
{
	return box->pmu->type->event_ctl +
		(box->pmu->type->pair_ctr_ctl ? 2 * idx : idx) +
		uncore_msr_box_offset(box);
}

static inline
unsigned uncore_msr_perf_ctr(struct intel_uncore_box *box, int idx)
{
	return box->pmu->type->perf_ctr +
		(box->pmu->type->pair_ctr_ctl ? 2 * idx : idx) +
		uncore_msr_box_offset(box);
}

static inline
unsigned uncore_fixed_ctl(struct intel_uncore_box *box)
{
	if (box->pci_dev)
		return uncore_pci_fixed_ctl(box);
	else
		return uncore_msr_fixed_ctl(box);
}

static inline
unsigned uncore_fixed_ctr(struct intel_uncore_box *box)
{
	if (box->pci_dev)
		return uncore_pci_fixed_ctr(box);
	else
		return uncore_msr_fixed_ctr(box);
}

static inline
unsigned uncore_event_ctl(struct intel_uncore_box *box, int idx)
{
	if (box->pci_dev)
		return uncore_pci_event_ctl(box, idx);
	else
		return uncore_msr_event_ctl(box, idx);
}

static inline
unsigned uncore_perf_ctr(struct intel_uncore_box *box, int idx)
{
	if (box->pci_dev)
		return uncore_pci_perf_ctr(box, idx);
	else
		return uncore_msr_perf_ctr(box, idx);
}

static inline int uncore_perf_ctr_bits(struct intel_uncore_box *box)
{
	return box->pmu->type->perf_ctr_bits;
}

static inline int uncore_fixed_ctr_bits(struct intel_uncore_box *box)
{
	return box->pmu->type->fixed_ctr_bits;
}

static inline int uncore_num_counters(struct intel_uncore_box *box)
{
	return box->pmu->type->num_counters;
}

static inline void uncore_disable_box(struct intel_uncore_box *box)
{
	if (box->pmu->type->ops->disable_box)
		box->pmu->type->ops->disable_box(box);
}

static inline void uncore_enable_box(struct intel_uncore_box *box)
{
	if (box->pmu->type->ops->enable_box)
		box->pmu->type->ops->enable_box(box);
}

static inline void uncore_disable_event(struct intel_uncore_box *box,
				struct perf_event *event)
{
	box->pmu->type->ops->disable_event(box, event);
}

static inline void uncore_enable_event(struct intel_uncore_box *box,
				struct perf_event *event)
{
	box->pmu->type->ops->enable_event(box, event);
}

static inline u64 uncore_read_counter(struct intel_uncore_box *box,
				struct perf_event *event)
{
	return box->pmu->type->ops->read_counter(box, event);
}

static inline void uncore_box_init(struct intel_uncore_box *box)
{
	if (!test_and_set_bit(UNCORE_BOX_FLAG_INITIATED, &box->flags)) {
		if (box->pmu->type->ops->init_box)
			box->pmu->type->ops->init_box(box);
	}
}

static inline void uncore_box_exit(struct intel_uncore_box *box)
{
	if (test_and_clear_bit(UNCORE_BOX_FLAG_INITIATED, &box->flags)) {
		if (box->pmu->type->ops->exit_box)
			box->pmu->type->ops->exit_box(box);
	}
}

static inline bool uncore_box_is_fake(struct intel_uncore_box *box)
{
	return (box->pkgid < 0);
}

static inline struct intel_uncore_pmu *uncore_event_to_pmu(struct perf_event *event)
{
	return container_of(event->pmu, struct intel_uncore_pmu, pmu);
}

static inline struct intel_uncore_box *uncore_event_to_box(struct perf_event *event)
{
	return event->pmu_private;
}

struct intel_uncore_box *uncore_pmu_to_box(struct intel_uncore_pmu *pmu, int cpu);
u64 uncore_msr_read_counter(struct intel_uncore_box *box, struct perf_event *event);
void uncore_pmu_start_hrtimer(struct intel_uncore_box *box);
void uncore_pmu_cancel_hrtimer(struct intel_uncore_box *box);
void uncore_pmu_event_read(struct perf_event *event);
void uncore_perf_event_update(struct intel_uncore_box *box, struct perf_event *event);
struct event_constraint *
uncore_get_constraint(struct intel_uncore_box *box, struct perf_event *event);
void uncore_put_constraint(struct intel_uncore_box *box, struct perf_event *event);
u64 uncore_shared_reg_config(struct intel_uncore_box *box, int idx);

extern struct intel_uncore_type **uncore_msr_uncores;
extern struct intel_uncore_type **uncore_pci_uncores;
extern struct pci_driver *uncore_pci_driver;
extern raw_spinlock_t pci2phy_map_lock;
extern struct list_head pci2phy_map_head;
extern struct pci_extra_dev *uncore_extra_pci_dev;
extern struct event_constraint uncore_constraint_empty;

/* perf_event_intel_uncore_snb.c */
int snb_uncore_pci_init(void);
int ivb_uncore_pci_init(void);
int hsw_uncore_pci_init(void);
int bdw_uncore_pci_init(void);
int skl_uncore_pci_init(void);
void snb_uncore_cpu_init(void);
void nhm_uncore_cpu_init(void);
void skl_uncore_cpu_init(void);
int snb_pci2phy_map_init(int devid);

/* perf_event_intel_uncore_snbep.c */
int snbep_uncore_pci_init(void);
void snbep_uncore_cpu_init(void);
int ivbep_uncore_pci_init(void);
void ivbep_uncore_cpu_init(void);
int hswep_uncore_pci_init(void);
void hswep_uncore_cpu_init(void);
int bdx_uncore_pci_init(void);
void bdx_uncore_cpu_init(void);
int knl_uncore_pci_init(void);
void knl_uncore_cpu_init(void);
int skx_uncore_pci_init(void);
void skx_uncore_cpu_init(void);

/* perf_event_intel_uncore_nhmex.c */
void nhmex_uncore_cpu_init(void);
