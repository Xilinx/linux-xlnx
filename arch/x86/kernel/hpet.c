#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i8253.h>
#include <linux/slab.h>
#include <linux/hpet.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/pm.h>
#include <linux/io.h>

#include <asm/cpufeature.h>
#include <asm/irqdomain.h>
#include <asm/fixmap.h>
#include <asm/hpet.h>
#include <asm/time.h>

#define HPET_MASK			CLOCKSOURCE_MASK(32)

/* FSEC = 10^-15
   NSEC = 10^-9 */
#define FSEC_PER_NSEC			1000000L

#define HPET_DEV_USED_BIT		2
#define HPET_DEV_USED			(1 << HPET_DEV_USED_BIT)
#define HPET_DEV_VALID			0x8
#define HPET_DEV_FSB_CAP		0x1000
#define HPET_DEV_PERI_CAP		0x2000

#define HPET_MIN_CYCLES			128
#define HPET_MIN_PROG_DELTA		(HPET_MIN_CYCLES + (HPET_MIN_CYCLES >> 1))

/*
 * HPET address is set in acpi/boot.c, when an ACPI entry exists
 */
unsigned long				hpet_address;
u8					hpet_blockid; /* OS timer block num */
bool					hpet_msi_disable;

#ifdef CONFIG_PCI_MSI
static unsigned int			hpet_num_timers;
#endif
static void __iomem			*hpet_virt_address;

struct hpet_dev {
	struct clock_event_device	evt;
	unsigned int			num;
	int				cpu;
	unsigned int			irq;
	unsigned int			flags;
	char				name[10];
};

static inline struct hpet_dev *EVT_TO_HPET_DEV(struct clock_event_device *evtdev)
{
	return container_of(evtdev, struct hpet_dev, evt);
}

inline unsigned int hpet_readl(unsigned int a)
{
	return readl(hpet_virt_address + a);
}

static inline void hpet_writel(unsigned int d, unsigned int a)
{
	writel(d, hpet_virt_address + a);
}

#ifdef CONFIG_X86_64
#include <asm/pgtable.h>
#endif

static inline void hpet_set_mapping(void)
{
	hpet_virt_address = ioremap_nocache(hpet_address, HPET_MMAP_SIZE);
}

static inline void hpet_clear_mapping(void)
{
	iounmap(hpet_virt_address);
	hpet_virt_address = NULL;
}

/*
 * HPET command line enable / disable
 */
bool boot_hpet_disable;
bool hpet_force_user;
static bool hpet_verbose;

static int __init hpet_setup(char *str)
{
	while (str) {
		char *next = strchr(str, ',');

		if (next)
			*next++ = 0;
		if (!strncmp("disable", str, 7))
			boot_hpet_disable = true;
		if (!strncmp("force", str, 5))
			hpet_force_user = true;
		if (!strncmp("verbose", str, 7))
			hpet_verbose = true;
		str = next;
	}
	return 1;
}
__setup("hpet=", hpet_setup);

static int __init disable_hpet(char *str)
{
	boot_hpet_disable = true;
	return 1;
}
__setup("nohpet", disable_hpet);

static inline int is_hpet_capable(void)
{
	return !boot_hpet_disable && hpet_address;
}

/*
 * HPET timer interrupt enable / disable
 */
static bool hpet_legacy_int_enabled;

/**
 * is_hpet_enabled - check whether the hpet timer interrupt is enabled
 */
int is_hpet_enabled(void)
{
	return is_hpet_capable() && hpet_legacy_int_enabled;
}
EXPORT_SYMBOL_GPL(is_hpet_enabled);

static void _hpet_print_config(const char *function, int line)
{
	u32 i, timers, l, h;
	printk(KERN_INFO "hpet: %s(%d):\n", function, line);
	l = hpet_readl(HPET_ID);
	h = hpet_readl(HPET_PERIOD);
	timers = ((l & HPET_ID_NUMBER) >> HPET_ID_NUMBER_SHIFT) + 1;
	printk(KERN_INFO "hpet: ID: 0x%x, PERIOD: 0x%x\n", l, h);
	l = hpet_readl(HPET_CFG);
	h = hpet_readl(HPET_STATUS);
	printk(KERN_INFO "hpet: CFG: 0x%x, STATUS: 0x%x\n", l, h);
	l = hpet_readl(HPET_COUNTER);
	h = hpet_readl(HPET_COUNTER+4);
	printk(KERN_INFO "hpet: COUNTER_l: 0x%x, COUNTER_h: 0x%x\n", l, h);

	for (i = 0; i < timers; i++) {
		l = hpet_readl(HPET_Tn_CFG(i));
		h = hpet_readl(HPET_Tn_CFG(i)+4);
		printk(KERN_INFO "hpet: T%d: CFG_l: 0x%x, CFG_h: 0x%x\n",
		       i, l, h);
		l = hpet_readl(HPET_Tn_CMP(i));
		h = hpet_readl(HPET_Tn_CMP(i)+4);
		printk(KERN_INFO "hpet: T%d: CMP_l: 0x%x, CMP_h: 0x%x\n",
		       i, l, h);
		l = hpet_readl(HPET_Tn_ROUTE(i));
		h = hpet_readl(HPET_Tn_ROUTE(i)+4);
		printk(KERN_INFO "hpet: T%d ROUTE_l: 0x%x, ROUTE_h: 0x%x\n",
		       i, l, h);
	}
}

#define hpet_print_config()					\
do {								\
	if (hpet_verbose)					\
		_hpet_print_config(__func__, __LINE__);	\
} while (0)

/*
 * When the hpet driver (/dev/hpet) is enabled, we need to reserve
 * timer 0 and timer 1 in case of RTC emulation.
 */
#ifdef CONFIG_HPET

static void hpet_reserve_msi_timers(struct hpet_data *hd);

static void hpet_reserve_platform_timers(unsigned int id)
{
	struct hpet __iomem *hpet = hpet_virt_address;
	struct hpet_timer __iomem *timer = &hpet->hpet_timers[2];
	unsigned int nrtimers, i;
	struct hpet_data hd;

	nrtimers = ((id & HPET_ID_NUMBER) >> HPET_ID_NUMBER_SHIFT) + 1;

	memset(&hd, 0, sizeof(hd));
	hd.hd_phys_address	= hpet_address;
	hd.hd_address		= hpet;
	hd.hd_nirqs		= nrtimers;
	hpet_reserve_timer(&hd, 0);

#ifdef CONFIG_HPET_EMULATE_RTC
	hpet_reserve_timer(&hd, 1);
#endif

	/*
	 * NOTE that hd_irq[] reflects IOAPIC input pins (LEGACY_8254
	 * is wrong for i8259!) not the output IRQ.  Many BIOS writers
	 * don't bother configuring *any* comparator interrupts.
	 */
	hd.hd_irq[0] = HPET_LEGACY_8254;
	hd.hd_irq[1] = HPET_LEGACY_RTC;

	for (i = 2; i < nrtimers; timer++, i++) {
		hd.hd_irq[i] = (readl(&timer->hpet_config) &
			Tn_INT_ROUTE_CNF_MASK) >> Tn_INT_ROUTE_CNF_SHIFT;
	}

	hpet_reserve_msi_timers(&hd);

	hpet_alloc(&hd);

}
#else
static void hpet_reserve_platform_timers(unsigned int id) { }
#endif

/*
 * Common hpet info
 */
static unsigned long hpet_freq;

static struct clock_event_device hpet_clockevent;

static void hpet_stop_counter(void)
{
	u32 cfg = hpet_readl(HPET_CFG);
	cfg &= ~HPET_CFG_ENABLE;
	hpet_writel(cfg, HPET_CFG);
}

static void hpet_reset_counter(void)
{
	hpet_writel(0, HPET_COUNTER);
	hpet_writel(0, HPET_COUNTER + 4);
}

static void hpet_start_counter(void)
{
	unsigned int cfg = hpet_readl(HPET_CFG);
	cfg |= HPET_CFG_ENABLE;
	hpet_writel(cfg, HPET_CFG);
}

static void hpet_restart_counter(void)
{
	hpet_stop_counter();
	hpet_reset_counter();
	hpet_start_counter();
}

static void hpet_resume_device(void)
{
	force_hpet_resume();
}

static void hpet_resume_counter(struct clocksource *cs)
{
	hpet_resume_device();
	hpet_restart_counter();
}

static void hpet_enable_legacy_int(void)
{
	unsigned int cfg = hpet_readl(HPET_CFG);

	cfg |= HPET_CFG_LEGACY;
	hpet_writel(cfg, HPET_CFG);
	hpet_legacy_int_enabled = true;
}

static void hpet_legacy_clockevent_register(void)
{
	/* Start HPET legacy interrupts */
	hpet_enable_legacy_int();

	/*
	 * Start hpet with the boot cpu mask and make it
	 * global after the IO_APIC has been initialized.
	 */
	hpet_clockevent.cpumask = cpumask_of(smp_processor_id());
	clockevents_config_and_register(&hpet_clockevent, hpet_freq,
					HPET_MIN_PROG_DELTA, 0x7FFFFFFF);
	global_clock_event = &hpet_clockevent;
	printk(KERN_DEBUG "hpet clockevent registered\n");
}

static int hpet_set_periodic(struct clock_event_device *evt, int timer)
{
	unsigned int cfg, cmp, now;
	uint64_t delta;

	hpet_stop_counter();
	delta = ((uint64_t)(NSEC_PER_SEC / HZ)) * evt->mult;
	delta >>= evt->shift;
	now = hpet_readl(HPET_COUNTER);
	cmp = now + (unsigned int)delta;
	cfg = hpet_readl(HPET_Tn_CFG(timer));
	cfg |= HPET_TN_ENABLE | HPET_TN_PERIODIC | HPET_TN_SETVAL |
	       HPET_TN_32BIT;
	hpet_writel(cfg, HPET_Tn_CFG(timer));
	hpet_writel(cmp, HPET_Tn_CMP(timer));
	udelay(1);
	/*
	 * HPET on AMD 81xx needs a second write (with HPET_TN_SETVAL
	 * cleared) to T0_CMP to set the period. The HPET_TN_SETVAL
	 * bit is automatically cleared after the first write.
	 * (See AMD-8111 HyperTransport I/O Hub Data Sheet,
	 * Publication # 24674)
	 */
	hpet_writel((unsigned int)delta, HPET_Tn_CMP(timer));
	hpet_start_counter();
	hpet_print_config();

	return 0;
}

static int hpet_set_oneshot(struct clock_event_device *evt, int timer)
{
	unsigned int cfg;

	cfg = hpet_readl(HPET_Tn_CFG(timer));
	cfg &= ~HPET_TN_PERIODIC;
	cfg |= HPET_TN_ENABLE | HPET_TN_32BIT;
	hpet_writel(cfg, HPET_Tn_CFG(timer));

	return 0;
}

static int hpet_shutdown(struct clock_event_device *evt, int timer)
{
	unsigned int cfg;

	cfg = hpet_readl(HPET_Tn_CFG(timer));
	cfg &= ~HPET_TN_ENABLE;
	hpet_writel(cfg, HPET_Tn_CFG(timer));

	return 0;
}

static int hpet_resume(struct clock_event_device *evt, int timer)
{
	if (!timer) {
		hpet_enable_legacy_int();
	} else {
		struct hpet_dev *hdev = EVT_TO_HPET_DEV(evt);

		irq_domain_activate_irq(irq_get_irq_data(hdev->irq));
		disable_irq(hdev->irq);
		irq_set_affinity(hdev->irq, cpumask_of(hdev->cpu));
		enable_irq(hdev->irq);
	}
	hpet_print_config();

	return 0;
}

static int hpet_next_event(unsigned long delta,
			   struct clock_event_device *evt, int timer)
{
	u32 cnt;
	s32 res;

	cnt = hpet_readl(HPET_COUNTER);
	cnt += (u32) delta;
	hpet_writel(cnt, HPET_Tn_CMP(timer));

	/*
	 * HPETs are a complete disaster. The compare register is
	 * based on a equal comparison and neither provides a less
	 * than or equal functionality (which would require to take
	 * the wraparound into account) nor a simple count down event
	 * mode. Further the write to the comparator register is
	 * delayed internally up to two HPET clock cycles in certain
	 * chipsets (ATI, ICH9,10). Some newer AMD chipsets have even
	 * longer delays. We worked around that by reading back the
	 * compare register, but that required another workaround for
	 * ICH9,10 chips where the first readout after write can
	 * return the old stale value. We already had a minimum
	 * programming delta of 5us enforced, but a NMI or SMI hitting
	 * between the counter readout and the comparator write can
	 * move us behind that point easily. Now instead of reading
	 * the compare register back several times, we make the ETIME
	 * decision based on the following: Return ETIME if the
	 * counter value after the write is less than HPET_MIN_CYCLES
	 * away from the event or if the counter is already ahead of
	 * the event. The minimum programming delta for the generic
	 * clockevents code is set to 1.5 * HPET_MIN_CYCLES.
	 */
	res = (s32)(cnt - hpet_readl(HPET_COUNTER));

	return res < HPET_MIN_CYCLES ? -ETIME : 0;
}

static int hpet_legacy_shutdown(struct clock_event_device *evt)
{
	return hpet_shutdown(evt, 0);
}

static int hpet_legacy_set_oneshot(struct clock_event_device *evt)
{
	return hpet_set_oneshot(evt, 0);
}

static int hpet_legacy_set_periodic(struct clock_event_device *evt)
{
	return hpet_set_periodic(evt, 0);
}

static int hpet_legacy_resume(struct clock_event_device *evt)
{
	return hpet_resume(evt, 0);
}

static int hpet_legacy_next_event(unsigned long delta,
			struct clock_event_device *evt)
{
	return hpet_next_event(delta, evt, 0);
}

/*
 * The hpet clock event device
 */
static struct clock_event_device hpet_clockevent = {
	.name			= "hpet",
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.set_state_periodic	= hpet_legacy_set_periodic,
	.set_state_oneshot	= hpet_legacy_set_oneshot,
	.set_state_shutdown	= hpet_legacy_shutdown,
	.tick_resume		= hpet_legacy_resume,
	.set_next_event		= hpet_legacy_next_event,
	.irq			= 0,
	.rating			= 50,
};

/*
 * HPET MSI Support
 */
#ifdef CONFIG_PCI_MSI

static DEFINE_PER_CPU(struct hpet_dev *, cpu_hpet_dev);
static struct hpet_dev	*hpet_devs;
static struct irq_domain *hpet_domain;

void hpet_msi_unmask(struct irq_data *data)
{
	struct hpet_dev *hdev = irq_data_get_irq_handler_data(data);
	unsigned int cfg;

	/* unmask it */
	cfg = hpet_readl(HPET_Tn_CFG(hdev->num));
	cfg |= HPET_TN_ENABLE | HPET_TN_FSB;
	hpet_writel(cfg, HPET_Tn_CFG(hdev->num));
}

void hpet_msi_mask(struct irq_data *data)
{
	struct hpet_dev *hdev = irq_data_get_irq_handler_data(data);
	unsigned int cfg;

	/* mask it */
	cfg = hpet_readl(HPET_Tn_CFG(hdev->num));
	cfg &= ~(HPET_TN_ENABLE | HPET_TN_FSB);
	hpet_writel(cfg, HPET_Tn_CFG(hdev->num));
}

void hpet_msi_write(struct hpet_dev *hdev, struct msi_msg *msg)
{
	hpet_writel(msg->data, HPET_Tn_ROUTE(hdev->num));
	hpet_writel(msg->address_lo, HPET_Tn_ROUTE(hdev->num) + 4);
}

void hpet_msi_read(struct hpet_dev *hdev, struct msi_msg *msg)
{
	msg->data = hpet_readl(HPET_Tn_ROUTE(hdev->num));
	msg->address_lo = hpet_readl(HPET_Tn_ROUTE(hdev->num) + 4);
	msg->address_hi = 0;
}

static int hpet_msi_shutdown(struct clock_event_device *evt)
{
	struct hpet_dev *hdev = EVT_TO_HPET_DEV(evt);

	return hpet_shutdown(evt, hdev->num);
}

static int hpet_msi_set_oneshot(struct clock_event_device *evt)
{
	struct hpet_dev *hdev = EVT_TO_HPET_DEV(evt);

	return hpet_set_oneshot(evt, hdev->num);
}

static int hpet_msi_set_periodic(struct clock_event_device *evt)
{
	struct hpet_dev *hdev = EVT_TO_HPET_DEV(evt);

	return hpet_set_periodic(evt, hdev->num);
}

static int hpet_msi_resume(struct clock_event_device *evt)
{
	struct hpet_dev *hdev = EVT_TO_HPET_DEV(evt);

	return hpet_resume(evt, hdev->num);
}

static int hpet_msi_next_event(unsigned long delta,
				struct clock_event_device *evt)
{
	struct hpet_dev *hdev = EVT_TO_HPET_DEV(evt);
	return hpet_next_event(delta, evt, hdev->num);
}

static irqreturn_t hpet_interrupt_handler(int irq, void *data)
{
	struct hpet_dev *dev = (struct hpet_dev *)data;
	struct clock_event_device *hevt = &dev->evt;

	if (!hevt->event_handler) {
		printk(KERN_INFO "Spurious HPET timer interrupt on HPET timer %d\n",
				dev->num);
		return IRQ_HANDLED;
	}

	hevt->event_handler(hevt);
	return IRQ_HANDLED;
}

static int hpet_setup_irq(struct hpet_dev *dev)
{

	if (request_irq(dev->irq, hpet_interrupt_handler,
			IRQF_TIMER | IRQF_NOBALANCING,
			dev->name, dev))
		return -1;

	disable_irq(dev->irq);
	irq_set_affinity(dev->irq, cpumask_of(dev->cpu));
	enable_irq(dev->irq);

	printk(KERN_DEBUG "hpet: %s irq %d for MSI\n",
			 dev->name, dev->irq);

	return 0;
}

/* This should be called in specific @cpu */
static void init_one_hpet_msi_clockevent(struct hpet_dev *hdev, int cpu)
{
	struct clock_event_device *evt = &hdev->evt;

	WARN_ON(cpu != smp_processor_id());
	if (!(hdev->flags & HPET_DEV_VALID))
		return;

	hdev->cpu = cpu;
	per_cpu(cpu_hpet_dev, cpu) = hdev;
	evt->name = hdev->name;
	hpet_setup_irq(hdev);
	evt->irq = hdev->irq;

	evt->rating = 110;
	evt->features = CLOCK_EVT_FEAT_ONESHOT;
	if (hdev->flags & HPET_DEV_PERI_CAP) {
		evt->features |= CLOCK_EVT_FEAT_PERIODIC;
		evt->set_state_periodic = hpet_msi_set_periodic;
	}

	evt->set_state_shutdown = hpet_msi_shutdown;
	evt->set_state_oneshot = hpet_msi_set_oneshot;
	evt->tick_resume = hpet_msi_resume;
	evt->set_next_event = hpet_msi_next_event;
	evt->cpumask = cpumask_of(hdev->cpu);

	clockevents_config_and_register(evt, hpet_freq, HPET_MIN_PROG_DELTA,
					0x7FFFFFFF);
}

#ifdef CONFIG_HPET
/* Reserve at least one timer for userspace (/dev/hpet) */
#define RESERVE_TIMERS 1
#else
#define RESERVE_TIMERS 0
#endif

static void hpet_msi_capability_lookup(unsigned int start_timer)
{
	unsigned int id;
	unsigned int num_timers;
	unsigned int num_timers_used = 0;
	int i, irq;

	if (hpet_msi_disable)
		return;

	if (boot_cpu_has(X86_FEATURE_ARAT))
		return;
	id = hpet_readl(HPET_ID);

	num_timers = ((id & HPET_ID_NUMBER) >> HPET_ID_NUMBER_SHIFT);
	num_timers++; /* Value read out starts from 0 */
	hpet_print_config();

	hpet_domain = hpet_create_irq_domain(hpet_blockid);
	if (!hpet_domain)
		return;

	hpet_devs = kzalloc(sizeof(struct hpet_dev) * num_timers, GFP_KERNEL);
	if (!hpet_devs)
		return;

	hpet_num_timers = num_timers;

	for (i = start_timer; i < num_timers - RESERVE_TIMERS; i++) {
		struct hpet_dev *hdev = &hpet_devs[num_timers_used];
		unsigned int cfg = hpet_readl(HPET_Tn_CFG(i));

		/* Only consider HPET timer with MSI support */
		if (!(cfg & HPET_TN_FSB_CAP))
			continue;

		hdev->flags = 0;
		if (cfg & HPET_TN_PERIODIC_CAP)
			hdev->flags |= HPET_DEV_PERI_CAP;
		sprintf(hdev->name, "hpet%d", i);
		hdev->num = i;

		irq = hpet_assign_irq(hpet_domain, hdev, hdev->num);
		if (irq <= 0)
			continue;

		hdev->irq = irq;
		hdev->flags |= HPET_DEV_FSB_CAP;
		hdev->flags |= HPET_DEV_VALID;
		num_timers_used++;
		if (num_timers_used == num_possible_cpus())
			break;
	}

	printk(KERN_INFO "HPET: %d timers in total, %d timers will be used for per-cpu timer\n",
		num_timers, num_timers_used);
}

#ifdef CONFIG_HPET
static void hpet_reserve_msi_timers(struct hpet_data *hd)
{
	int i;

	if (!hpet_devs)
		return;

	for (i = 0; i < hpet_num_timers; i++) {
		struct hpet_dev *hdev = &hpet_devs[i];

		if (!(hdev->flags & HPET_DEV_VALID))
			continue;

		hd->hd_irq[hdev->num] = hdev->irq;
		hpet_reserve_timer(hd, hdev->num);
	}
}
#endif

static struct hpet_dev *hpet_get_unused_timer(void)
{
	int i;

	if (!hpet_devs)
		return NULL;

	for (i = 0; i < hpet_num_timers; i++) {
		struct hpet_dev *hdev = &hpet_devs[i];

		if (!(hdev->flags & HPET_DEV_VALID))
			continue;
		if (test_and_set_bit(HPET_DEV_USED_BIT,
			(unsigned long *)&hdev->flags))
			continue;
		return hdev;
	}
	return NULL;
}

struct hpet_work_struct {
	struct delayed_work work;
	struct completion complete;
};

static void hpet_work(struct work_struct *w)
{
	struct hpet_dev *hdev;
	int cpu = smp_processor_id();
	struct hpet_work_struct *hpet_work;

	hpet_work = container_of(w, struct hpet_work_struct, work.work);

	hdev = hpet_get_unused_timer();
	if (hdev)
		init_one_hpet_msi_clockevent(hdev, cpu);

	complete(&hpet_work->complete);
}

static int hpet_cpuhp_online(unsigned int cpu)
{
	struct hpet_work_struct work;

	INIT_DELAYED_WORK_ONSTACK(&work.work, hpet_work);
	init_completion(&work.complete);
	/* FIXME: add schedule_work_on() */
	schedule_delayed_work_on(cpu, &work.work, 0);
	wait_for_completion(&work.complete);
	destroy_delayed_work_on_stack(&work.work);
	return 0;
}

static int hpet_cpuhp_dead(unsigned int cpu)
{
	struct hpet_dev *hdev = per_cpu(cpu_hpet_dev, cpu);

	if (!hdev)
		return 0;
	free_irq(hdev->irq, hdev);
	hdev->flags &= ~HPET_DEV_USED;
	per_cpu(cpu_hpet_dev, cpu) = NULL;
	return 0;
}
#else

static void hpet_msi_capability_lookup(unsigned int start_timer)
{
	return;
}

#ifdef CONFIG_HPET
static void hpet_reserve_msi_timers(struct hpet_data *hd)
{
	return;
}
#endif

#define hpet_cpuhp_online	NULL
#define hpet_cpuhp_dead		NULL

#endif

/*
 * Clock source related code
 */
#if defined(CONFIG_SMP) && defined(CONFIG_64BIT)
/*
 * Reading the HPET counter is a very slow operation. If a large number of
 * CPUs are trying to access the HPET counter simultaneously, it can cause
 * massive delay and slow down system performance dramatically. This may
 * happen when HPET is the default clock source instead of TSC. For a
 * really large system with hundreds of CPUs, the slowdown may be so
 * severe that it may actually crash the system because of a NMI watchdog
 * soft lockup, for example.
 *
 * If multiple CPUs are trying to access the HPET counter at the same time,
 * we don't actually need to read the counter multiple times. Instead, the
 * other CPUs can use the counter value read by the first CPU in the group.
 *
 * This special feature is only enabled on x86-64 systems. It is unlikely
 * that 32-bit x86 systems will have enough CPUs to require this feature
 * with its associated locking overhead. And we also need 64-bit atomic
 * read.
 *
 * The lock and the hpet value are stored together and can be read in a
 * single atomic 64-bit read. It is explicitly assumed that arch_spinlock_t
 * is 32 bits in size.
 */
union hpet_lock {
	struct {
		arch_spinlock_t lock;
		u32 value;
	};
	u64 lockval;
};

static union hpet_lock hpet __cacheline_aligned = {
	{ .lock = __ARCH_SPIN_LOCK_UNLOCKED, },
};

static cycle_t read_hpet(struct clocksource *cs)
{
	unsigned long flags;
	union hpet_lock old, new;

	BUILD_BUG_ON(sizeof(union hpet_lock) != 8);

	/*
	 * Read HPET directly if in NMI.
	 */
	if (in_nmi())
		return (cycle_t)hpet_readl(HPET_COUNTER);

	/*
	 * Read the current state of the lock and HPET value atomically.
	 */
	old.lockval = READ_ONCE(hpet.lockval);

	if (arch_spin_is_locked(&old.lock))
		goto contended;

	local_irq_save(flags);
	if (arch_spin_trylock(&hpet.lock)) {
		new.value = hpet_readl(HPET_COUNTER);
		/*
		 * Use WRITE_ONCE() to prevent store tearing.
		 */
		WRITE_ONCE(hpet.value, new.value);
		arch_spin_unlock(&hpet.lock);
		local_irq_restore(flags);
		return (cycle_t)new.value;
	}
	local_irq_restore(flags);

contended:
	/*
	 * Contended case
	 * --------------
	 * Wait until the HPET value change or the lock is free to indicate
	 * its value is up-to-date.
	 *
	 * It is possible that old.value has already contained the latest
	 * HPET value while the lock holder was in the process of releasing
	 * the lock. Checking for lock state change will enable us to return
	 * the value immediately instead of waiting for the next HPET reader
	 * to come along.
	 */
	do {
		cpu_relax();
		new.lockval = READ_ONCE(hpet.lockval);
	} while ((new.value == old.value) && arch_spin_is_locked(&new.lock));

	return (cycle_t)new.value;
}
#else
/*
 * For UP or 32-bit.
 */
static cycle_t read_hpet(struct clocksource *cs)
{
	return (cycle_t)hpet_readl(HPET_COUNTER);
}
#endif

static struct clocksource clocksource_hpet = {
	.name		= "hpet",
	.rating		= 250,
	.read		= read_hpet,
	.mask		= HPET_MASK,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
	.resume		= hpet_resume_counter,
};

static int hpet_clocksource_register(void)
{
	u64 start, now;
	cycle_t t1;

	/* Start the counter */
	hpet_restart_counter();

	/* Verify whether hpet counter works */
	t1 = hpet_readl(HPET_COUNTER);
	start = rdtsc();

	/*
	 * We don't know the TSC frequency yet, but waiting for
	 * 200000 TSC cycles is safe:
	 * 4 GHz == 50us
	 * 1 GHz == 200us
	 */
	do {
		rep_nop();
		now = rdtsc();
	} while ((now - start) < 200000UL);

	if (t1 == hpet_readl(HPET_COUNTER)) {
		printk(KERN_WARNING
		       "HPET counter not counting. HPET disabled\n");
		return -ENODEV;
	}

	clocksource_register_hz(&clocksource_hpet, (u32)hpet_freq);
	return 0;
}

static u32 *hpet_boot_cfg;

/**
 * hpet_enable - Try to setup the HPET timer. Returns 1 on success.
 */
int __init hpet_enable(void)
{
	u32 hpet_period, cfg, id;
	u64 freq;
	unsigned int i, last;

	if (!is_hpet_capable())
		return 0;

	hpet_set_mapping();

	/*
	 * Read the period and check for a sane value:
	 */
	hpet_period = hpet_readl(HPET_PERIOD);

	/*
	 * AMD SB700 based systems with spread spectrum enabled use a
	 * SMM based HPET emulation to provide proper frequency
	 * setting. The SMM code is initialized with the first HPET
	 * register access and takes some time to complete. During
	 * this time the config register reads 0xffffffff. We check
	 * for max. 1000 loops whether the config register reads a non
	 * 0xffffffff value to make sure that HPET is up and running
	 * before we go further. A counting loop is safe, as the HPET
	 * access takes thousands of CPU cycles. On non SB700 based
	 * machines this check is only done once and has no side
	 * effects.
	 */
	for (i = 0; hpet_readl(HPET_CFG) == 0xFFFFFFFF; i++) {
		if (i == 1000) {
			printk(KERN_WARNING
			       "HPET config register value = 0xFFFFFFFF. "
			       "Disabling HPET\n");
			goto out_nohpet;
		}
	}

	if (hpet_period < HPET_MIN_PERIOD || hpet_period > HPET_MAX_PERIOD)
		goto out_nohpet;

	/*
	 * The period is a femto seconds value. Convert it to a
	 * frequency.
	 */
	freq = FSEC_PER_SEC;
	do_div(freq, hpet_period);
	hpet_freq = freq;

	/*
	 * Read the HPET ID register to retrieve the IRQ routing
	 * information and the number of channels
	 */
	id = hpet_readl(HPET_ID);
	hpet_print_config();

	last = (id & HPET_ID_NUMBER) >> HPET_ID_NUMBER_SHIFT;

#ifdef CONFIG_HPET_EMULATE_RTC
	/*
	 * The legacy routing mode needs at least two channels, tick timer
	 * and the rtc emulation channel.
	 */
	if (!last)
		goto out_nohpet;
#endif

	cfg = hpet_readl(HPET_CFG);
	hpet_boot_cfg = kmalloc((last + 2) * sizeof(*hpet_boot_cfg),
				GFP_KERNEL);
	if (hpet_boot_cfg)
		*hpet_boot_cfg = cfg;
	else
		pr_warn("HPET initial state will not be saved\n");
	cfg &= ~(HPET_CFG_ENABLE | HPET_CFG_LEGACY);
	hpet_writel(cfg, HPET_CFG);
	if (cfg)
		pr_warn("HPET: Unrecognized bits %#x set in global cfg\n",
			cfg);

	for (i = 0; i <= last; ++i) {
		cfg = hpet_readl(HPET_Tn_CFG(i));
		if (hpet_boot_cfg)
			hpet_boot_cfg[i + 1] = cfg;
		cfg &= ~(HPET_TN_ENABLE | HPET_TN_LEVEL | HPET_TN_FSB);
		hpet_writel(cfg, HPET_Tn_CFG(i));
		cfg &= ~(HPET_TN_PERIODIC | HPET_TN_PERIODIC_CAP
			 | HPET_TN_64BIT_CAP | HPET_TN_32BIT | HPET_TN_ROUTE
			 | HPET_TN_FSB | HPET_TN_FSB_CAP);
		if (cfg)
			pr_warn("HPET: Unrecognized bits %#x set in cfg#%u\n",
				cfg, i);
	}
	hpet_print_config();

	if (hpet_clocksource_register())
		goto out_nohpet;

	if (id & HPET_ID_LEGSUP) {
		hpet_legacy_clockevent_register();
		return 1;
	}
	return 0;

out_nohpet:
	hpet_clear_mapping();
	hpet_address = 0;
	return 0;
}

/*
 * Needs to be late, as the reserve_timer code calls kalloc !
 *
 * Not a problem on i386 as hpet_enable is called from late_time_init,
 * but on x86_64 it is necessary !
 */
static __init int hpet_late_init(void)
{
	int ret;

	if (boot_hpet_disable)
		return -ENODEV;

	if (!hpet_address) {
		if (!force_hpet_address)
			return -ENODEV;

		hpet_address = force_hpet_address;
		hpet_enable();
	}

	if (!hpet_virt_address)
		return -ENODEV;

	if (hpet_readl(HPET_ID) & HPET_ID_LEGSUP)
		hpet_msi_capability_lookup(2);
	else
		hpet_msi_capability_lookup(0);

	hpet_reserve_platform_timers(hpet_readl(HPET_ID));
	hpet_print_config();

	if (hpet_msi_disable)
		return 0;

	if (boot_cpu_has(X86_FEATURE_ARAT))
		return 0;

	/* This notifier should be called after workqueue is ready */
	ret = cpuhp_setup_state(CPUHP_AP_X86_HPET_ONLINE, "AP_X86_HPET_ONLINE",
				hpet_cpuhp_online, NULL);
	if (ret)
		return ret;
	ret = cpuhp_setup_state(CPUHP_X86_HPET_DEAD, "X86_HPET_DEAD", NULL,
				hpet_cpuhp_dead);
	if (ret)
		goto err_cpuhp;
	return 0;

err_cpuhp:
	cpuhp_remove_state(CPUHP_AP_X86_HPET_ONLINE);
	return ret;
}
fs_initcall(hpet_late_init);

void hpet_disable(void)
{
	if (is_hpet_capable() && hpet_virt_address) {
		unsigned int cfg = hpet_readl(HPET_CFG), id, last;

		if (hpet_boot_cfg)
			cfg = *hpet_boot_cfg;
		else if (hpet_legacy_int_enabled) {
			cfg &= ~HPET_CFG_LEGACY;
			hpet_legacy_int_enabled = false;
		}
		cfg &= ~HPET_CFG_ENABLE;
		hpet_writel(cfg, HPET_CFG);

		if (!hpet_boot_cfg)
			return;

		id = hpet_readl(HPET_ID);
		last = ((id & HPET_ID_NUMBER) >> HPET_ID_NUMBER_SHIFT);

		for (id = 0; id <= last; ++id)
			hpet_writel(hpet_boot_cfg[id + 1], HPET_Tn_CFG(id));

		if (*hpet_boot_cfg & HPET_CFG_ENABLE)
			hpet_writel(*hpet_boot_cfg, HPET_CFG);
	}
}

#ifdef CONFIG_HPET_EMULATE_RTC

/* HPET in LegacyReplacement Mode eats up RTC interrupt line. When, HPET
 * is enabled, we support RTC interrupt functionality in software.
 * RTC has 3 kinds of interrupts:
 * 1) Update Interrupt - generate an interrupt, every sec, when RTC clock
 *    is updated
 * 2) Alarm Interrupt - generate an interrupt at a specific time of day
 * 3) Periodic Interrupt - generate periodic interrupt, with frequencies
 *    2Hz-8192Hz (2Hz-64Hz for non-root user) (all freqs in powers of 2)
 * (1) and (2) above are implemented using polling at a frequency of
 * 64 Hz. The exact frequency is a tradeoff between accuracy and interrupt
 * overhead. (DEFAULT_RTC_INT_FREQ)
 * For (3), we use interrupts at 64Hz or user specified periodic
 * frequency, whichever is higher.
 */
#include <linux/mc146818rtc.h>
#include <linux/rtc.h>

#define DEFAULT_RTC_INT_FREQ	64
#define DEFAULT_RTC_SHIFT	6
#define RTC_NUM_INTS		1

static unsigned long hpet_rtc_flags;
static int hpet_prev_update_sec;
static struct rtc_time hpet_alarm_time;
static unsigned long hpet_pie_count;
static u32 hpet_t1_cmp;
static u32 hpet_default_delta;
static u32 hpet_pie_delta;
static unsigned long hpet_pie_limit;

static rtc_irq_handler irq_handler;

/*
 * Check that the hpet counter c1 is ahead of the c2
 */
static inline int hpet_cnt_ahead(u32 c1, u32 c2)
{
	return (s32)(c2 - c1) < 0;
}

/*
 * Registers a IRQ handler.
 */
int hpet_register_irq_handler(rtc_irq_handler handler)
{
	if (!is_hpet_enabled())
		return -ENODEV;
	if (irq_handler)
		return -EBUSY;

	irq_handler = handler;

	return 0;
}
EXPORT_SYMBOL_GPL(hpet_register_irq_handler);

/*
 * Deregisters the IRQ handler registered with hpet_register_irq_handler()
 * and does cleanup.
 */
void hpet_unregister_irq_handler(rtc_irq_handler handler)
{
	if (!is_hpet_enabled())
		return;

	irq_handler = NULL;
	hpet_rtc_flags = 0;
}
EXPORT_SYMBOL_GPL(hpet_unregister_irq_handler);

/*
 * Timer 1 for RTC emulation. We use one shot mode, as periodic mode
 * is not supported by all HPET implementations for timer 1.
 *
 * hpet_rtc_timer_init() is called when the rtc is initialized.
 */
int hpet_rtc_timer_init(void)
{
	unsigned int cfg, cnt, delta;
	unsigned long flags;

	if (!is_hpet_enabled())
		return 0;

	if (!hpet_default_delta) {
		uint64_t clc;

		clc = (uint64_t) hpet_clockevent.mult * NSEC_PER_SEC;
		clc >>= hpet_clockevent.shift + DEFAULT_RTC_SHIFT;
		hpet_default_delta = clc;
	}

	if (!(hpet_rtc_flags & RTC_PIE) || hpet_pie_limit)
		delta = hpet_default_delta;
	else
		delta = hpet_pie_delta;

	local_irq_save(flags);

	cnt = delta + hpet_readl(HPET_COUNTER);
	hpet_writel(cnt, HPET_T1_CMP);
	hpet_t1_cmp = cnt;

	cfg = hpet_readl(HPET_T1_CFG);
	cfg &= ~HPET_TN_PERIODIC;
	cfg |= HPET_TN_ENABLE | HPET_TN_32BIT;
	hpet_writel(cfg, HPET_T1_CFG);

	local_irq_restore(flags);

	return 1;
}
EXPORT_SYMBOL_GPL(hpet_rtc_timer_init);

static void hpet_disable_rtc_channel(void)
{
	u32 cfg = hpet_readl(HPET_T1_CFG);
	cfg &= ~HPET_TN_ENABLE;
	hpet_writel(cfg, HPET_T1_CFG);
}

/*
 * The functions below are called from rtc driver.
 * Return 0 if HPET is not being used.
 * Otherwise do the necessary changes and return 1.
 */
int hpet_mask_rtc_irq_bit(unsigned long bit_mask)
{
	if (!is_hpet_enabled())
		return 0;

	hpet_rtc_flags &= ~bit_mask;
	if (unlikely(!hpet_rtc_flags))
		hpet_disable_rtc_channel();

	return 1;
}
EXPORT_SYMBOL_GPL(hpet_mask_rtc_irq_bit);

int hpet_set_rtc_irq_bit(unsigned long bit_mask)
{
	unsigned long oldbits = hpet_rtc_flags;

	if (!is_hpet_enabled())
		return 0;

	hpet_rtc_flags |= bit_mask;

	if ((bit_mask & RTC_UIE) && !(oldbits & RTC_UIE))
		hpet_prev_update_sec = -1;

	if (!oldbits)
		hpet_rtc_timer_init();

	return 1;
}
EXPORT_SYMBOL_GPL(hpet_set_rtc_irq_bit);

int hpet_set_alarm_time(unsigned char hrs, unsigned char min,
			unsigned char sec)
{
	if (!is_hpet_enabled())
		return 0;

	hpet_alarm_time.tm_hour = hrs;
	hpet_alarm_time.tm_min = min;
	hpet_alarm_time.tm_sec = sec;

	return 1;
}
EXPORT_SYMBOL_GPL(hpet_set_alarm_time);

int hpet_set_periodic_freq(unsigned long freq)
{
	uint64_t clc;

	if (!is_hpet_enabled())
		return 0;

	if (freq <= DEFAULT_RTC_INT_FREQ)
		hpet_pie_limit = DEFAULT_RTC_INT_FREQ / freq;
	else {
		clc = (uint64_t) hpet_clockevent.mult * NSEC_PER_SEC;
		do_div(clc, freq);
		clc >>= hpet_clockevent.shift;
		hpet_pie_delta = clc;
		hpet_pie_limit = 0;
	}
	return 1;
}
EXPORT_SYMBOL_GPL(hpet_set_periodic_freq);

int hpet_rtc_dropped_irq(void)
{
	return is_hpet_enabled();
}
EXPORT_SYMBOL_GPL(hpet_rtc_dropped_irq);

static void hpet_rtc_timer_reinit(void)
{
	unsigned int delta;
	int lost_ints = -1;

	if (unlikely(!hpet_rtc_flags))
		hpet_disable_rtc_channel();

	if (!(hpet_rtc_flags & RTC_PIE) || hpet_pie_limit)
		delta = hpet_default_delta;
	else
		delta = hpet_pie_delta;

	/*
	 * Increment the comparator value until we are ahead of the
	 * current count.
	 */
	do {
		hpet_t1_cmp += delta;
		hpet_writel(hpet_t1_cmp, HPET_T1_CMP);
		lost_ints++;
	} while (!hpet_cnt_ahead(hpet_t1_cmp, hpet_readl(HPET_COUNTER)));

	if (lost_ints) {
		if (hpet_rtc_flags & RTC_PIE)
			hpet_pie_count += lost_ints;
		if (printk_ratelimit())
			printk(KERN_WARNING "hpet1: lost %d rtc interrupts\n",
				lost_ints);
	}
}

irqreturn_t hpet_rtc_interrupt(int irq, void *dev_id)
{
	struct rtc_time curr_time;
	unsigned long rtc_int_flag = 0;

	hpet_rtc_timer_reinit();
	memset(&curr_time, 0, sizeof(struct rtc_time));

	if (hpet_rtc_flags & (RTC_UIE | RTC_AIE))
		mc146818_get_time(&curr_time);

	if (hpet_rtc_flags & RTC_UIE &&
	    curr_time.tm_sec != hpet_prev_update_sec) {
		if (hpet_prev_update_sec >= 0)
			rtc_int_flag = RTC_UF;
		hpet_prev_update_sec = curr_time.tm_sec;
	}

	if (hpet_rtc_flags & RTC_PIE &&
	    ++hpet_pie_count >= hpet_pie_limit) {
		rtc_int_flag |= RTC_PF;
		hpet_pie_count = 0;
	}

	if (hpet_rtc_flags & RTC_AIE &&
	    (curr_time.tm_sec == hpet_alarm_time.tm_sec) &&
	    (curr_time.tm_min == hpet_alarm_time.tm_min) &&
	    (curr_time.tm_hour == hpet_alarm_time.tm_hour))
			rtc_int_flag |= RTC_AF;

	if (rtc_int_flag) {
		rtc_int_flag |= (RTC_IRQF | (RTC_NUM_INTS << 8));
		if (irq_handler)
			irq_handler(rtc_int_flag, dev_id);
	}
	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(hpet_rtc_interrupt);
#endif
