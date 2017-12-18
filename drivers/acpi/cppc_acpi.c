/*
 * CPPC (Collaborative Processor Performance Control) methods used by CPUfreq drivers.
 *
 * (C) Copyright 2014, 2015 Linaro Ltd.
 * Author: Ashwin Chaugule <ashwin.chaugule@linaro.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 * CPPC describes a few methods for controlling CPU performance using
 * information from a per CPU table called CPC. This table is described in
 * the ACPI v5.0+ specification. The table consists of a list of
 * registers which may be memory mapped or hardware registers and also may
 * include some static integer values.
 *
 * CPU performance is on an abstract continuous scale as against a discretized
 * P-state scale which is tied to CPU frequency only. In brief, the basic
 * operation involves:
 *
 * - OS makes a CPU performance request. (Can provide min and max bounds)
 *
 * - Platform (such as BMC) is free to optimize request within requested bounds
 *   depending on power/thermal budgets etc.
 *
 * - Platform conveys its decision back to OS
 *
 * The communication between OS and platform occurs through another medium
 * called (PCC) Platform Communication Channel. This is a generic mailbox like
 * mechanism which includes doorbell semantics to indicate register updates.
 * See drivers/mailbox/pcc.c for details on PCC.
 *
 * Finer details about the PCC and CPPC spec are available in the ACPI v5.1 and
 * above specifications.
 */

#define pr_fmt(fmt)	"ACPI CPPC: " fmt

#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/rwsem.h>
#include <linux/wait.h>

#include <acpi/cppc_acpi.h>

struct cppc_pcc_data {
	struct mbox_chan *pcc_channel;
	void __iomem *pcc_comm_addr;
	int pcc_subspace_idx;
	bool pcc_channel_acquired;
	ktime_t deadline;
	unsigned int pcc_mpar, pcc_mrtt, pcc_nominal;

	bool pending_pcc_write_cmd;	/* Any pending/batched PCC write cmds? */
	bool platform_owns_pcc;		/* Ownership of PCC subspace */
	unsigned int pcc_write_cnt;	/* Running count of PCC write commands */

	/*
	 * Lock to provide controlled access to the PCC channel.
	 *
	 * For performance critical usecases(currently cppc_set_perf)
	 *	We need to take read_lock and check if channel belongs to OSPM
	 * before reading or writing to PCC subspace
	 *	We need to take write_lock before transferring the channel
	 * ownership to the platform via a Doorbell
	 *	This allows us to batch a number of CPPC requests if they happen
	 * to originate in about the same time
	 *
	 * For non-performance critical usecases(init)
	 *	Take write_lock for all purposes which gives exclusive access
	 */
	struct rw_semaphore pcc_lock;

	/* Wait queue for CPUs whose requests were batched */
	wait_queue_head_t pcc_write_wait_q;
};

/* Structure to represent the single PCC channel */
static struct cppc_pcc_data pcc_data = {
	.pcc_subspace_idx = -1,
	.platform_owns_pcc = true,
};

/*
 * The cpc_desc structure contains the ACPI register details
 * as described in the per CPU _CPC tables. The details
 * include the type of register (e.g. PCC, System IO, FFH etc.)
 * and destination addresses which lets us READ/WRITE CPU performance
 * information using the appropriate I/O methods.
 */
static DEFINE_PER_CPU(struct cpc_desc *, cpc_desc_ptr);

/* pcc mapped address + header size + offset within PCC subspace */
#define GET_PCC_VADDR(offs) (pcc_data.pcc_comm_addr + 0x8 + (offs))

/* Check if a CPC regsiter is in PCC */
#define CPC_IN_PCC(cpc) ((cpc)->type == ACPI_TYPE_BUFFER &&		\
				(cpc)->cpc_entry.reg.space_id ==	\
				ACPI_ADR_SPACE_PLATFORM_COMM)

/* Evalutes to True if reg is a NULL register descriptor */
#define IS_NULL_REG(reg) ((reg)->space_id ==  ACPI_ADR_SPACE_SYSTEM_MEMORY && \
				(reg)->address == 0 &&			\
				(reg)->bit_width == 0 &&		\
				(reg)->bit_offset == 0 &&		\
				(reg)->access_width == 0)

/* Evalutes to True if an optional cpc field is supported */
#define CPC_SUPPORTED(cpc) ((cpc)->type == ACPI_TYPE_INTEGER ?		\
				!!(cpc)->cpc_entry.int_value :		\
				!IS_NULL_REG(&(cpc)->cpc_entry.reg))
/*
 * Arbitrary Retries in case the remote processor is slow to respond
 * to PCC commands. Keeping it high enough to cover emulators where
 * the processors run painfully slow.
 */
#define NUM_RETRIES 500

struct cppc_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj,
			struct attribute *attr, char *buf);
	ssize_t (*store)(struct kobject *kobj,
			struct attribute *attr, const char *c, ssize_t count);
};

#define define_one_cppc_ro(_name)		\
static struct cppc_attr _name =			\
__ATTR(_name, 0444, show_##_name, NULL)

#define to_cpc_desc(a) container_of(a, struct cpc_desc, kobj)

static ssize_t show_feedback_ctrs(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	struct cpc_desc *cpc_ptr = to_cpc_desc(kobj);
	struct cppc_perf_fb_ctrs fb_ctrs = {0};

	cppc_get_perf_ctrs(cpc_ptr->cpu_id, &fb_ctrs);

	return scnprintf(buf, PAGE_SIZE, "ref:%llu del:%llu\n",
			fb_ctrs.reference, fb_ctrs.delivered);
}
define_one_cppc_ro(feedback_ctrs);

static ssize_t show_reference_perf(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	struct cpc_desc *cpc_ptr = to_cpc_desc(kobj);
	struct cppc_perf_fb_ctrs fb_ctrs = {0};

	cppc_get_perf_ctrs(cpc_ptr->cpu_id, &fb_ctrs);

	return scnprintf(buf, PAGE_SIZE, "%llu\n",
			fb_ctrs.reference_perf);
}
define_one_cppc_ro(reference_perf);

static ssize_t show_wraparound_time(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
	struct cpc_desc *cpc_ptr = to_cpc_desc(kobj);
	struct cppc_perf_fb_ctrs fb_ctrs = {0};

	cppc_get_perf_ctrs(cpc_ptr->cpu_id, &fb_ctrs);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", fb_ctrs.ctr_wrap_time);

}
define_one_cppc_ro(wraparound_time);

static struct attribute *cppc_attrs[] = {
	&feedback_ctrs.attr,
	&reference_perf.attr,
	&wraparound_time.attr,
	NULL
};

static struct kobj_type cppc_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.default_attrs = cppc_attrs,
};

static int check_pcc_chan(bool chk_err_bit)
{
	int ret = -EIO, status = 0;
	struct acpi_pcct_shared_memory __iomem *generic_comm_base = pcc_data.pcc_comm_addr;
	ktime_t next_deadline = ktime_add(ktime_get(), pcc_data.deadline);

	if (!pcc_data.platform_owns_pcc)
		return 0;

	/* Retry in case the remote processor was too slow to catch up. */
	while (!ktime_after(ktime_get(), next_deadline)) {
		/*
		 * Per spec, prior to boot the PCC space wil be initialized by
		 * platform and should have set the command completion bit when
		 * PCC can be used by OSPM
		 */
		status = readw_relaxed(&generic_comm_base->status);
		if (status & PCC_CMD_COMPLETE_MASK) {
			ret = 0;
			if (chk_err_bit && (status & PCC_ERROR_MASK))
				ret = -EIO;
			break;
		}
		/*
		 * Reducing the bus traffic in case this loop takes longer than
		 * a few retries.
		 */
		udelay(3);
	}

	if (likely(!ret))
		pcc_data.platform_owns_pcc = false;
	else
		pr_err("PCC check channel failed. Status=%x\n", status);

	return ret;
}

/*
 * This function transfers the ownership of the PCC to the platform
 * So it must be called while holding write_lock(pcc_lock)
 */
static int send_pcc_cmd(u16 cmd)
{
	int ret = -EIO, i;
	struct acpi_pcct_shared_memory *generic_comm_base =
		(struct acpi_pcct_shared_memory *) pcc_data.pcc_comm_addr;
	static ktime_t last_cmd_cmpl_time, last_mpar_reset;
	static int mpar_count;
	unsigned int time_delta;

	/*
	 * For CMD_WRITE we know for a fact the caller should have checked
	 * the channel before writing to PCC space
	 */
	if (cmd == CMD_READ) {
		/*
		 * If there are pending cpc_writes, then we stole the channel
		 * before write completion, so first send a WRITE command to
		 * platform
		 */
		if (pcc_data.pending_pcc_write_cmd)
			send_pcc_cmd(CMD_WRITE);

		ret = check_pcc_chan(false);
		if (ret)
			goto end;
	} else /* CMD_WRITE */
		pcc_data.pending_pcc_write_cmd = FALSE;

	/*
	 * Handle the Minimum Request Turnaround Time(MRTT)
	 * "The minimum amount of time that OSPM must wait after the completion
	 * of a command before issuing the next command, in microseconds"
	 */
	if (pcc_data.pcc_mrtt) {
		time_delta = ktime_us_delta(ktime_get(), last_cmd_cmpl_time);
		if (pcc_data.pcc_mrtt > time_delta)
			udelay(pcc_data.pcc_mrtt - time_delta);
	}

	/*
	 * Handle the non-zero Maximum Periodic Access Rate(MPAR)
	 * "The maximum number of periodic requests that the subspace channel can
	 * support, reported in commands per minute. 0 indicates no limitation."
	 *
	 * This parameter should be ideally zero or large enough so that it can
	 * handle maximum number of requests that all the cores in the system can
	 * collectively generate. If it is not, we will follow the spec and just
	 * not send the request to the platform after hitting the MPAR limit in
	 * any 60s window
	 */
	if (pcc_data.pcc_mpar) {
		if (mpar_count == 0) {
			time_delta = ktime_ms_delta(ktime_get(), last_mpar_reset);
			if (time_delta < 60 * MSEC_PER_SEC) {
				pr_debug("PCC cmd not sent due to MPAR limit");
				ret = -EIO;
				goto end;
			}
			last_mpar_reset = ktime_get();
			mpar_count = pcc_data.pcc_mpar;
		}
		mpar_count--;
	}

	/* Write to the shared comm region. */
	writew_relaxed(cmd, &generic_comm_base->command);

	/* Flip CMD COMPLETE bit */
	writew_relaxed(0, &generic_comm_base->status);

	pcc_data.platform_owns_pcc = true;

	/* Ring doorbell */
	ret = mbox_send_message(pcc_data.pcc_channel, &cmd);
	if (ret < 0) {
		pr_err("Err sending PCC mbox message. cmd:%d, ret:%d\n",
				cmd, ret);
		goto end;
	}

	/* wait for completion and check for PCC errro bit */
	ret = check_pcc_chan(true);

	if (pcc_data.pcc_mrtt)
		last_cmd_cmpl_time = ktime_get();

	if (pcc_data.pcc_channel->mbox->txdone_irq)
		mbox_chan_txdone(pcc_data.pcc_channel, ret);
	else
		mbox_client_txdone(pcc_data.pcc_channel, ret);

end:
	if (cmd == CMD_WRITE) {
		if (unlikely(ret)) {
			for_each_possible_cpu(i) {
				struct cpc_desc *desc = per_cpu(cpc_desc_ptr, i);
				if (!desc)
					continue;

				if (desc->write_cmd_id == pcc_data.pcc_write_cnt)
					desc->write_cmd_status = ret;
			}
		}
		pcc_data.pcc_write_cnt++;
		wake_up_all(&pcc_data.pcc_write_wait_q);
	}

	return ret;
}

static void cppc_chan_tx_done(struct mbox_client *cl, void *msg, int ret)
{
	if (ret < 0)
		pr_debug("TX did not complete: CMD sent:%x, ret:%d\n",
				*(u16 *)msg, ret);
	else
		pr_debug("TX completed. CMD sent:%x, ret:%d\n",
				*(u16 *)msg, ret);
}

struct mbox_client cppc_mbox_cl = {
	.tx_done = cppc_chan_tx_done,
	.knows_txdone = true,
};

static int acpi_get_psd(struct cpc_desc *cpc_ptr, acpi_handle handle)
{
	int result = -EFAULT;
	acpi_status status = AE_OK;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	struct acpi_buffer format = {sizeof("NNNNN"), "NNNNN"};
	struct acpi_buffer state = {0, NULL};
	union acpi_object  *psd = NULL;
	struct acpi_psd_package *pdomain;

	status = acpi_evaluate_object_typed(handle, "_PSD", NULL, &buffer,
			ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	psd = buffer.pointer;
	if (!psd || psd->package.count != 1) {
		pr_debug("Invalid _PSD data\n");
		goto end;
	}

	pdomain = &(cpc_ptr->domain_info);

	state.length = sizeof(struct acpi_psd_package);
	state.pointer = pdomain;

	status = acpi_extract_package(&(psd->package.elements[0]),
		&format, &state);
	if (ACPI_FAILURE(status)) {
		pr_debug("Invalid _PSD data for CPU:%d\n", cpc_ptr->cpu_id);
		goto end;
	}

	if (pdomain->num_entries != ACPI_PSD_REV0_ENTRIES) {
		pr_debug("Unknown _PSD:num_entries for CPU:%d\n", cpc_ptr->cpu_id);
		goto end;
	}

	if (pdomain->revision != ACPI_PSD_REV0_REVISION) {
		pr_debug("Unknown _PSD:revision for CPU: %d\n", cpc_ptr->cpu_id);
		goto end;
	}

	if (pdomain->coord_type != DOMAIN_COORD_TYPE_SW_ALL &&
	    pdomain->coord_type != DOMAIN_COORD_TYPE_SW_ANY &&
	    pdomain->coord_type != DOMAIN_COORD_TYPE_HW_ALL) {
		pr_debug("Invalid _PSD:coord_type for CPU:%d\n", cpc_ptr->cpu_id);
		goto end;
	}

	result = 0;
end:
	kfree(buffer.pointer);
	return result;
}

/**
 * acpi_get_psd_map - Map the CPUs in a common freq domain.
 * @all_cpu_data: Ptrs to CPU specific CPPC data including PSD info.
 *
 *	Return: 0 for success or negative value for err.
 */
int acpi_get_psd_map(struct cppc_cpudata **all_cpu_data)
{
	int count_target;
	int retval = 0;
	unsigned int i, j;
	cpumask_var_t covered_cpus;
	struct cppc_cpudata *pr, *match_pr;
	struct acpi_psd_package *pdomain;
	struct acpi_psd_package *match_pdomain;
	struct cpc_desc *cpc_ptr, *match_cpc_ptr;

	if (!zalloc_cpumask_var(&covered_cpus, GFP_KERNEL))
		return -ENOMEM;

	/*
	 * Now that we have _PSD data from all CPUs, lets setup P-state
	 * domain info.
	 */
	for_each_possible_cpu(i) {
		pr = all_cpu_data[i];
		if (!pr)
			continue;

		if (cpumask_test_cpu(i, covered_cpus))
			continue;

		cpc_ptr = per_cpu(cpc_desc_ptr, i);
		if (!cpc_ptr) {
			retval = -EFAULT;
			goto err_ret;
		}

		pdomain = &(cpc_ptr->domain_info);
		cpumask_set_cpu(i, pr->shared_cpu_map);
		cpumask_set_cpu(i, covered_cpus);
		if (pdomain->num_processors <= 1)
			continue;

		/* Validate the Domain info */
		count_target = pdomain->num_processors;
		if (pdomain->coord_type == DOMAIN_COORD_TYPE_SW_ALL)
			pr->shared_type = CPUFREQ_SHARED_TYPE_ALL;
		else if (pdomain->coord_type == DOMAIN_COORD_TYPE_HW_ALL)
			pr->shared_type = CPUFREQ_SHARED_TYPE_HW;
		else if (pdomain->coord_type == DOMAIN_COORD_TYPE_SW_ANY)
			pr->shared_type = CPUFREQ_SHARED_TYPE_ANY;

		for_each_possible_cpu(j) {
			if (i == j)
				continue;

			match_cpc_ptr = per_cpu(cpc_desc_ptr, j);
			if (!match_cpc_ptr) {
				retval = -EFAULT;
				goto err_ret;
			}

			match_pdomain = &(match_cpc_ptr->domain_info);
			if (match_pdomain->domain != pdomain->domain)
				continue;

			/* Here i and j are in the same domain */
			if (match_pdomain->num_processors != count_target) {
				retval = -EFAULT;
				goto err_ret;
			}

			if (pdomain->coord_type != match_pdomain->coord_type) {
				retval = -EFAULT;
				goto err_ret;
			}

			cpumask_set_cpu(j, covered_cpus);
			cpumask_set_cpu(j, pr->shared_cpu_map);
		}

		for_each_possible_cpu(j) {
			if (i == j)
				continue;

			match_pr = all_cpu_data[j];
			if (!match_pr)
				continue;

			match_cpc_ptr = per_cpu(cpc_desc_ptr, j);
			if (!match_cpc_ptr) {
				retval = -EFAULT;
				goto err_ret;
			}

			match_pdomain = &(match_cpc_ptr->domain_info);
			if (match_pdomain->domain != pdomain->domain)
				continue;

			match_pr->shared_type = pr->shared_type;
			cpumask_copy(match_pr->shared_cpu_map,
				     pr->shared_cpu_map);
		}
	}

err_ret:
	for_each_possible_cpu(i) {
		pr = all_cpu_data[i];
		if (!pr)
			continue;

		/* Assume no coordination on any error parsing domain info */
		if (retval) {
			cpumask_clear(pr->shared_cpu_map);
			cpumask_set_cpu(i, pr->shared_cpu_map);
			pr->shared_type = CPUFREQ_SHARED_TYPE_ALL;
		}
	}

	free_cpumask_var(covered_cpus);
	return retval;
}
EXPORT_SYMBOL_GPL(acpi_get_psd_map);

static int register_pcc_channel(int pcc_subspace_idx)
{
	struct acpi_pcct_hw_reduced *cppc_ss;
	u64 usecs_lat;

	if (pcc_subspace_idx >= 0) {
		pcc_data.pcc_channel = pcc_mbox_request_channel(&cppc_mbox_cl,
				pcc_subspace_idx);

		if (IS_ERR(pcc_data.pcc_channel)) {
			pr_err("Failed to find PCC communication channel\n");
			return -ENODEV;
		}

		/*
		 * The PCC mailbox controller driver should
		 * have parsed the PCCT (global table of all
		 * PCC channels) and stored pointers to the
		 * subspace communication region in con_priv.
		 */
		cppc_ss = (pcc_data.pcc_channel)->con_priv;

		if (!cppc_ss) {
			pr_err("No PCC subspace found for CPPC\n");
			return -ENODEV;
		}

		/*
		 * cppc_ss->latency is just a Nominal value. In reality
		 * the remote processor could be much slower to reply.
		 * So add an arbitrary amount of wait on top of Nominal.
		 */
		usecs_lat = NUM_RETRIES * cppc_ss->latency;
		pcc_data.deadline = ns_to_ktime(usecs_lat * NSEC_PER_USEC);
		pcc_data.pcc_mrtt = cppc_ss->min_turnaround_time;
		pcc_data.pcc_mpar = cppc_ss->max_access_rate;
		pcc_data.pcc_nominal = cppc_ss->latency;

		pcc_data.pcc_comm_addr = acpi_os_ioremap(cppc_ss->base_address, cppc_ss->length);
		if (!pcc_data.pcc_comm_addr) {
			pr_err("Failed to ioremap PCC comm region mem\n");
			return -ENOMEM;
		}

		/* Set flag so that we dont come here for each CPU. */
		pcc_data.pcc_channel_acquired = true;
	}

	return 0;
}

/**
 * cpc_ffh_supported() - check if FFH reading supported
 *
 * Check if the architecture has support for functional fixed hardware
 * read/write capability.
 *
 * Return: true for supported, false for not supported
 */
bool __weak cpc_ffh_supported(void)
{
	return false;
}

/*
 * An example CPC table looks like the following.
 *
 *	Name(_CPC, Package()
 *			{
 *			17,
 *			NumEntries
 *			1,
 *			// Revision
 *			ResourceTemplate(){Register(PCC, 32, 0, 0x120, 2)},
 *			// Highest Performance
 *			ResourceTemplate(){Register(PCC, 32, 0, 0x124, 2)},
 *			// Nominal Performance
 *			ResourceTemplate(){Register(PCC, 32, 0, 0x128, 2)},
 *			// Lowest Nonlinear Performance
 *			ResourceTemplate(){Register(PCC, 32, 0, 0x12C, 2)},
 *			// Lowest Performance
 *			ResourceTemplate(){Register(PCC, 32, 0, 0x130, 2)},
 *			// Guaranteed Performance Register
 *			ResourceTemplate(){Register(PCC, 32, 0, 0x110, 2)},
 *			// Desired Performance Register
 *			ResourceTemplate(){Register(SystemMemory, 0, 0, 0, 0)},
 *			..
 *			..
 *			..
 *
 *		}
 * Each Register() encodes how to access that specific register.
 * e.g. a sample PCC entry has the following encoding:
 *
 *	Register (
 *		PCC,
 *		AddressSpaceKeyword
 *		8,
 *		//RegisterBitWidth
 *		8,
 *		//RegisterBitOffset
 *		0x30,
 *		//RegisterAddress
 *		9
 *		//AccessSize (subspace ID)
 *		0
 *		)
 *	}
 */

/**
 * acpi_cppc_processor_probe - Search for per CPU _CPC objects.
 * @pr: Ptr to acpi_processor containing this CPUs logical Id.
 *
 *	Return: 0 for success or negative value for err.
 */
int acpi_cppc_processor_probe(struct acpi_processor *pr)
{
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *out_obj, *cpc_obj;
	struct cpc_desc *cpc_ptr;
	struct cpc_reg *gas_t;
	struct device *cpu_dev;
	acpi_handle handle = pr->handle;
	unsigned int num_ent, i, cpc_rev;
	acpi_status status;
	int ret = -EFAULT;

	/* Parse the ACPI _CPC table for this cpu. */
	status = acpi_evaluate_object_typed(handle, "_CPC", NULL, &output,
			ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(status)) {
		ret = -ENODEV;
		goto out_buf_free;
	}

	out_obj = (union acpi_object *) output.pointer;

	cpc_ptr = kzalloc(sizeof(struct cpc_desc), GFP_KERNEL);
	if (!cpc_ptr) {
		ret = -ENOMEM;
		goto out_buf_free;
	}

	/* First entry is NumEntries. */
	cpc_obj = &out_obj->package.elements[0];
	if (cpc_obj->type == ACPI_TYPE_INTEGER)	{
		num_ent = cpc_obj->integer.value;
	} else {
		pr_debug("Unexpected entry type(%d) for NumEntries\n",
				cpc_obj->type);
		goto out_free;
	}

	/* Only support CPPCv2. Bail otherwise. */
	if (num_ent != CPPC_NUM_ENT) {
		pr_debug("Firmware exports %d entries. Expected: %d\n",
				num_ent, CPPC_NUM_ENT);
		goto out_free;
	}

	cpc_ptr->num_entries = num_ent;

	/* Second entry should be revision. */
	cpc_obj = &out_obj->package.elements[1];
	if (cpc_obj->type == ACPI_TYPE_INTEGER)	{
		cpc_rev = cpc_obj->integer.value;
	} else {
		pr_debug("Unexpected entry type(%d) for Revision\n",
				cpc_obj->type);
		goto out_free;
	}

	if (cpc_rev != CPPC_REV) {
		pr_debug("Firmware exports revision:%d. Expected:%d\n",
				cpc_rev, CPPC_REV);
		goto out_free;
	}

	/* Iterate through remaining entries in _CPC */
	for (i = 2; i < num_ent; i++) {
		cpc_obj = &out_obj->package.elements[i];

		if (cpc_obj->type == ACPI_TYPE_INTEGER)	{
			cpc_ptr->cpc_regs[i-2].type = ACPI_TYPE_INTEGER;
			cpc_ptr->cpc_regs[i-2].cpc_entry.int_value = cpc_obj->integer.value;
		} else if (cpc_obj->type == ACPI_TYPE_BUFFER) {
			gas_t = (struct cpc_reg *)
				cpc_obj->buffer.pointer;

			/*
			 * The PCC Subspace index is encoded inside
			 * the CPC table entries. The same PCC index
			 * will be used for all the PCC entries,
			 * so extract it only once.
			 */
			if (gas_t->space_id == ACPI_ADR_SPACE_PLATFORM_COMM) {
				if (pcc_data.pcc_subspace_idx < 0)
					pcc_data.pcc_subspace_idx = gas_t->access_width;
				else if (pcc_data.pcc_subspace_idx != gas_t->access_width) {
					pr_debug("Mismatched PCC ids.\n");
					goto out_free;
				}
			} else if (gas_t->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
				if (gas_t->address) {
					void __iomem *addr;

					addr = ioremap(gas_t->address, gas_t->bit_width/8);
					if (!addr)
						goto out_free;
					cpc_ptr->cpc_regs[i-2].sys_mem_vaddr = addr;
				}
			} else {
				if (gas_t->space_id != ACPI_ADR_SPACE_FIXED_HARDWARE || !cpc_ffh_supported()) {
					/* Support only PCC ,SYS MEM and FFH type regs */
					pr_debug("Unsupported register type: %d\n", gas_t->space_id);
					goto out_free;
				}
			}

			cpc_ptr->cpc_regs[i-2].type = ACPI_TYPE_BUFFER;
			memcpy(&cpc_ptr->cpc_regs[i-2].cpc_entry.reg, gas_t, sizeof(*gas_t));
		} else {
			pr_debug("Err in entry:%d in CPC table of CPU:%d \n", i, pr->id);
			goto out_free;
		}
	}
	/* Store CPU Logical ID */
	cpc_ptr->cpu_id = pr->id;

	/* Parse PSD data for this CPU */
	ret = acpi_get_psd(cpc_ptr, handle);
	if (ret)
		goto out_free;

	/* Register PCC channel once for all CPUs. */
	if (!pcc_data.pcc_channel_acquired) {
		ret = register_pcc_channel(pcc_data.pcc_subspace_idx);
		if (ret)
			goto out_free;

		init_rwsem(&pcc_data.pcc_lock);
		init_waitqueue_head(&pcc_data.pcc_write_wait_q);
	}

	/* Plug PSD data into this CPUs CPC descriptor. */
	per_cpu(cpc_desc_ptr, pr->id) = cpc_ptr;

	/* Everything looks okay */
	pr_debug("Parsed CPC struct for CPU: %d\n", pr->id);

	/* Add per logical CPU nodes for reading its feedback counters. */
	cpu_dev = get_cpu_device(pr->id);
	if (!cpu_dev)
		goto out_free;

	ret = kobject_init_and_add(&cpc_ptr->kobj, &cppc_ktype, &cpu_dev->kobj,
			"acpi_cppc");
	if (ret)
		goto out_free;

	kfree(output.pointer);
	return 0;

out_free:
	/* Free all the mapped sys mem areas for this CPU */
	for (i = 2; i < cpc_ptr->num_entries; i++) {
		void __iomem *addr = cpc_ptr->cpc_regs[i-2].sys_mem_vaddr;

		if (addr)
			iounmap(addr);
	}
	kfree(cpc_ptr);

out_buf_free:
	kfree(output.pointer);
	return ret;
}
EXPORT_SYMBOL_GPL(acpi_cppc_processor_probe);

/**
 * acpi_cppc_processor_exit - Cleanup CPC structs.
 * @pr: Ptr to acpi_processor containing this CPUs logical Id.
 *
 * Return: Void
 */
void acpi_cppc_processor_exit(struct acpi_processor *pr)
{
	struct cpc_desc *cpc_ptr;
	unsigned int i;
	void __iomem *addr;

	cpc_ptr = per_cpu(cpc_desc_ptr, pr->id);

	/* Free all the mapped sys mem areas for this CPU */
	for (i = 2; i < cpc_ptr->num_entries; i++) {
		addr = cpc_ptr->cpc_regs[i-2].sys_mem_vaddr;
		if (addr)
			iounmap(addr);
	}

	kobject_put(&cpc_ptr->kobj);
	kfree(cpc_ptr);
}
EXPORT_SYMBOL_GPL(acpi_cppc_processor_exit);

/**
 * cpc_read_ffh() - Read FFH register
 * @cpunum:	cpu number to read
 * @reg:	cppc register information
 * @val:	place holder for return value
 *
 * Read bit_width bits from a specified address and bit_offset
 *
 * Return: 0 for success and error code
 */
int __weak cpc_read_ffh(int cpunum, struct cpc_reg *reg, u64 *val)
{
	return -ENOTSUPP;
}

/**
 * cpc_write_ffh() - Write FFH register
 * @cpunum:	cpu number to write
 * @reg:	cppc register information
 * @val:	value to write
 *
 * Write value of bit_width bits to a specified address and bit_offset
 *
 * Return: 0 for success and error code
 */
int __weak cpc_write_ffh(int cpunum, struct cpc_reg *reg, u64 val)
{
	return -ENOTSUPP;
}

/*
 * Since cpc_read and cpc_write are called while holding pcc_lock, it should be
 * as fast as possible. We have already mapped the PCC subspace during init, so
 * we can directly write to it.
 */

static int cpc_read(int cpu, struct cpc_register_resource *reg_res, u64 *val)
{
	int ret_val = 0;
	void __iomem *vaddr = 0;
	struct cpc_reg *reg = &reg_res->cpc_entry.reg;

	if (reg_res->type == ACPI_TYPE_INTEGER) {
		*val = reg_res->cpc_entry.int_value;
		return ret_val;
	}

	*val = 0;
	if (reg->space_id == ACPI_ADR_SPACE_PLATFORM_COMM)
		vaddr = GET_PCC_VADDR(reg->address);
	else if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY)
		vaddr = reg_res->sys_mem_vaddr;
	else if (reg->space_id == ACPI_ADR_SPACE_FIXED_HARDWARE)
		return cpc_read_ffh(cpu, reg, val);
	else
		return acpi_os_read_memory((acpi_physical_address)reg->address,
				val, reg->bit_width);

	switch (reg->bit_width) {
		case 8:
			*val = readb_relaxed(vaddr);
			break;
		case 16:
			*val = readw_relaxed(vaddr);
			break;
		case 32:
			*val = readl_relaxed(vaddr);
			break;
		case 64:
			*val = readq_relaxed(vaddr);
			break;
		default:
			pr_debug("Error: Cannot read %u bit width from PCC\n",
					reg->bit_width);
			ret_val = -EFAULT;
	}

	return ret_val;
}

static int cpc_write(int cpu, struct cpc_register_resource *reg_res, u64 val)
{
	int ret_val = 0;
	void __iomem *vaddr = 0;
	struct cpc_reg *reg = &reg_res->cpc_entry.reg;

	if (reg->space_id == ACPI_ADR_SPACE_PLATFORM_COMM)
		vaddr = GET_PCC_VADDR(reg->address);
	else if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY)
		vaddr = reg_res->sys_mem_vaddr;
	else if (reg->space_id == ACPI_ADR_SPACE_FIXED_HARDWARE)
		return cpc_write_ffh(cpu, reg, val);
	else
		return acpi_os_write_memory((acpi_physical_address)reg->address,
				val, reg->bit_width);

	switch (reg->bit_width) {
		case 8:
			writeb_relaxed(val, vaddr);
			break;
		case 16:
			writew_relaxed(val, vaddr);
			break;
		case 32:
			writel_relaxed(val, vaddr);
			break;
		case 64:
			writeq_relaxed(val, vaddr);
			break;
		default:
			pr_debug("Error: Cannot write %u bit width to PCC\n",
					reg->bit_width);
			ret_val = -EFAULT;
			break;
	}

	return ret_val;
}

/**
 * cppc_get_perf_caps - Get a CPUs performance capabilities.
 * @cpunum: CPU from which to get capabilities info.
 * @perf_caps: ptr to cppc_perf_caps. See cppc_acpi.h
 *
 * Return: 0 for success with perf_caps populated else -ERRNO.
 */
int cppc_get_perf_caps(int cpunum, struct cppc_perf_caps *perf_caps)
{
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpunum);
	struct cpc_register_resource *highest_reg, *lowest_reg, *ref_perf,
								 *nom_perf;
	u64 high, low, nom;
	int ret = 0, regs_in_pcc = 0;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpunum);
		return -ENODEV;
	}

	highest_reg = &cpc_desc->cpc_regs[HIGHEST_PERF];
	lowest_reg = &cpc_desc->cpc_regs[LOWEST_PERF];
	ref_perf = &cpc_desc->cpc_regs[REFERENCE_PERF];
	nom_perf = &cpc_desc->cpc_regs[NOMINAL_PERF];

	/* Are any of the regs PCC ?*/
	if (CPC_IN_PCC(highest_reg) || CPC_IN_PCC(lowest_reg) ||
		CPC_IN_PCC(ref_perf) || CPC_IN_PCC(nom_perf)) {
		regs_in_pcc = 1;
		down_write(&pcc_data.pcc_lock);
		/* Ring doorbell once to update PCC subspace */
		if (send_pcc_cmd(CMD_READ) < 0) {
			ret = -EIO;
			goto out_err;
		}
	}

	cpc_read(cpunum, highest_reg, &high);
	perf_caps->highest_perf = high;

	cpc_read(cpunum, lowest_reg, &low);
	perf_caps->lowest_perf = low;

	cpc_read(cpunum, nom_perf, &nom);
	perf_caps->nominal_perf = nom;

	if (!high || !low || !nom)
		ret = -EFAULT;

out_err:
	if (regs_in_pcc)
		up_write(&pcc_data.pcc_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(cppc_get_perf_caps);

/**
 * cppc_get_perf_ctrs - Read a CPUs performance feedback counters.
 * @cpunum: CPU from which to read counters.
 * @perf_fb_ctrs: ptr to cppc_perf_fb_ctrs. See cppc_acpi.h
 *
 * Return: 0 for success with perf_fb_ctrs populated else -ERRNO.
 */
int cppc_get_perf_ctrs(int cpunum, struct cppc_perf_fb_ctrs *perf_fb_ctrs)
{
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpunum);
	struct cpc_register_resource *delivered_reg, *reference_reg,
		*ref_perf_reg, *ctr_wrap_reg;
	u64 delivered, reference, ref_perf, ctr_wrap_time;
	int ret = 0, regs_in_pcc = 0;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpunum);
		return -ENODEV;
	}

	delivered_reg = &cpc_desc->cpc_regs[DELIVERED_CTR];
	reference_reg = &cpc_desc->cpc_regs[REFERENCE_CTR];
	ref_perf_reg = &cpc_desc->cpc_regs[REFERENCE_PERF];
	ctr_wrap_reg = &cpc_desc->cpc_regs[CTR_WRAP_TIME];

	/*
	 * If refernce perf register is not supported then we should
	 * use the nominal perf value
	 */
	if (!CPC_SUPPORTED(ref_perf_reg))
		ref_perf_reg = &cpc_desc->cpc_regs[NOMINAL_PERF];

	/* Are any of the regs PCC ?*/
	if (CPC_IN_PCC(delivered_reg) || CPC_IN_PCC(reference_reg) ||
		CPC_IN_PCC(ctr_wrap_reg) || CPC_IN_PCC(ref_perf_reg)) {
		down_write(&pcc_data.pcc_lock);
		regs_in_pcc = 1;
		/* Ring doorbell once to update PCC subspace */
		if (send_pcc_cmd(CMD_READ) < 0) {
			ret = -EIO;
			goto out_err;
		}
	}

	cpc_read(cpunum, delivered_reg, &delivered);
	cpc_read(cpunum, reference_reg, &reference);
	cpc_read(cpunum, ref_perf_reg, &ref_perf);

	/*
	 * Per spec, if ctr_wrap_time optional register is unsupported, then the
	 * performance counters are assumed to never wrap during the lifetime of
	 * platform
	 */
	ctr_wrap_time = (u64)(~((u64)0));
	if (CPC_SUPPORTED(ctr_wrap_reg))
		cpc_read(cpunum, ctr_wrap_reg, &ctr_wrap_time);

	if (!delivered || !reference ||	!ref_perf) {
		ret = -EFAULT;
		goto out_err;
	}

	perf_fb_ctrs->delivered = delivered;
	perf_fb_ctrs->reference = reference;
	perf_fb_ctrs->reference_perf = ref_perf;
	perf_fb_ctrs->ctr_wrap_time = ctr_wrap_time;
out_err:
	if (regs_in_pcc)
		up_write(&pcc_data.pcc_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(cppc_get_perf_ctrs);

/**
 * cppc_set_perf - Set a CPUs performance controls.
 * @cpu: CPU for which to set performance controls.
 * @perf_ctrls: ptr to cppc_perf_ctrls. See cppc_acpi.h
 *
 * Return: 0 for success, -ERRNO otherwise.
 */
int cppc_set_perf(int cpu, struct cppc_perf_ctrls *perf_ctrls)
{
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpu);
	struct cpc_register_resource *desired_reg;
	int ret = 0;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpu);
		return -ENODEV;
	}

	desired_reg = &cpc_desc->cpc_regs[DESIRED_PERF];

	/*
	 * This is Phase-I where we want to write to CPC registers
	 * -> We want all CPUs to be able to execute this phase in parallel
	 *
	 * Since read_lock can be acquired by multiple CPUs simultaneously we
	 * achieve that goal here
	 */
	if (CPC_IN_PCC(desired_reg)) {
		down_read(&pcc_data.pcc_lock);	/* BEGIN Phase-I */
		if (pcc_data.platform_owns_pcc) {
			ret = check_pcc_chan(false);
			if (ret) {
				up_read(&pcc_data.pcc_lock);
				return ret;
			}
		}
		/*
		 * Update the pending_write to make sure a PCC CMD_READ will not
		 * arrive and steal the channel during the switch to write lock
		 */
		pcc_data.pending_pcc_write_cmd = true;
		cpc_desc->write_cmd_id = pcc_data.pcc_write_cnt;
		cpc_desc->write_cmd_status = 0;
	}

	/*
	 * Skip writing MIN/MAX until Linux knows how to come up with
	 * useful values.
	 */
	cpc_write(cpu, desired_reg, perf_ctrls->desired_perf);

	if (CPC_IN_PCC(desired_reg))
		up_read(&pcc_data.pcc_lock);	/* END Phase-I */
	/*
	 * This is Phase-II where we transfer the ownership of PCC to Platform
	 *
	 * Short Summary: Basically if we think of a group of cppc_set_perf
	 * requests that happened in short overlapping interval. The last CPU to
	 * come out of Phase-I will enter Phase-II and ring the doorbell.
	 *
	 * We have the following requirements for Phase-II:
	 *     1. We want to execute Phase-II only when there are no CPUs
	 * currently executing in Phase-I
	 *     2. Once we start Phase-II we want to avoid all other CPUs from
	 * entering Phase-I.
	 *     3. We want only one CPU among all those who went through Phase-I
	 * to run phase-II
	 *
	 * If write_trylock fails to get the lock and doesn't transfer the
	 * PCC ownership to the platform, then one of the following will be TRUE
	 *     1. There is at-least one CPU in Phase-I which will later execute
	 * write_trylock, so the CPUs in Phase-I will be responsible for
	 * executing the Phase-II.
	 *     2. Some other CPU has beaten this CPU to successfully execute the
	 * write_trylock and has already acquired the write_lock. We know for a
	 * fact it(other CPU acquiring the write_lock) couldn't have happened
	 * before this CPU's Phase-I as we held the read_lock.
	 *     3. Some other CPU executing pcc CMD_READ has stolen the
	 * down_write, in which case, send_pcc_cmd will check for pending
	 * CMD_WRITE commands by checking the pending_pcc_write_cmd.
	 * So this CPU can be certain that its request will be delivered
	 *    So in all cases, this CPU knows that its request will be delivered
	 * by another CPU and can return
	 *
	 * After getting the down_write we still need to check for
	 * pending_pcc_write_cmd to take care of the following scenario
	 *    The thread running this code could be scheduled out between
	 * Phase-I and Phase-II. Before it is scheduled back on, another CPU
	 * could have delivered the request to Platform by triggering the
	 * doorbell and transferred the ownership of PCC to platform. So this
	 * avoids triggering an unnecessary doorbell and more importantly before
	 * triggering the doorbell it makes sure that the PCC channel ownership
	 * is still with OSPM.
	 *   pending_pcc_write_cmd can also be cleared by a different CPU, if
	 * there was a pcc CMD_READ waiting on down_write and it steals the lock
	 * before the pcc CMD_WRITE is completed. pcc_send_cmd checks for this
	 * case during a CMD_READ and if there are pending writes it delivers
	 * the write command before servicing the read command
	 */
	if (CPC_IN_PCC(desired_reg)) {
		if (down_write_trylock(&pcc_data.pcc_lock)) {	/* BEGIN Phase-II */
			/* Update only if there are pending write commands */
			if (pcc_data.pending_pcc_write_cmd)
				send_pcc_cmd(CMD_WRITE);
			up_write(&pcc_data.pcc_lock);		/* END Phase-II */
		} else
			/* Wait until pcc_write_cnt is updated by send_pcc_cmd */
			wait_event(pcc_data.pcc_write_wait_q,
				cpc_desc->write_cmd_id != pcc_data.pcc_write_cnt);

		/* send_pcc_cmd updates the status in case of failure */
		ret = cpc_desc->write_cmd_status;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(cppc_set_perf);

/**
 * cppc_get_transition_latency - returns frequency transition latency in ns
 *
 * ACPI CPPC does not explicitly specifiy how a platform can specify the
 * transition latency for perfromance change requests. The closest we have
 * is the timing information from the PCCT tables which provides the info
 * on the number and frequency of PCC commands the platform can handle.
 */
unsigned int cppc_get_transition_latency(int cpu_num)
{
	/*
	 * Expected transition latency is based on the PCCT timing values
	 * Below are definition from ACPI spec:
	 * pcc_nominal- Expected latency to process a command, in microseconds
	 * pcc_mpar   - The maximum number of periodic requests that the subspace
	 *              channel can support, reported in commands per minute. 0
	 *              indicates no limitation.
	 * pcc_mrtt   - The minimum amount of time that OSPM must wait after the
	 *              completion of a command before issuing the next command,
	 *              in microseconds.
	 */
	unsigned int latency_ns = 0;
	struct cpc_desc *cpc_desc;
	struct cpc_register_resource *desired_reg;

	cpc_desc = per_cpu(cpc_desc_ptr, cpu_num);
	if (!cpc_desc)
		return CPUFREQ_ETERNAL;

	desired_reg = &cpc_desc->cpc_regs[DESIRED_PERF];
	if (!CPC_IN_PCC(desired_reg))
		return CPUFREQ_ETERNAL;

	if (pcc_data.pcc_mpar)
		latency_ns = 60 * (1000 * 1000 * 1000 / pcc_data.pcc_mpar);

	latency_ns = max(latency_ns, pcc_data.pcc_nominal * 1000);
	latency_ns = max(latency_ns, pcc_data.pcc_mrtt * 1000);

	return latency_ns;
}
EXPORT_SYMBOL_GPL(cppc_get_transition_latency);
