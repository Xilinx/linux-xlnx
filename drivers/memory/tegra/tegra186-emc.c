// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019-2025 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <soc/tegra/bpmp.h>
#include "mc.h"

struct tegra186_emc_dvfs {
	unsigned long latency;
	unsigned long rate;
};

enum emc_rate_request_type {
	EMC_RATE_DEBUG,
	EMC_RATE_ICC,
	EMC_RATE_TYPE_MAX,
};

struct emc_rate_request {
	unsigned long min_rate;
	unsigned long max_rate;
};

struct tegra186_emc {
	struct tegra_bpmp *bpmp;
	struct device *dev;
	struct clk *clk;

	struct tegra186_emc_dvfs *dvfs;
	unsigned int num_dvfs;

	struct {
		struct dentry *root;
		unsigned long min_rate;
		unsigned long max_rate;
	} debugfs;

	struct icc_provider provider;

	/*
	 * There are multiple sources in the EMC driver which could request
	 * a min/max clock rate, these rates are contained in this array.
	 */
	struct emc_rate_request requested_rate[EMC_RATE_TYPE_MAX];

	/* protect shared rate-change code path */
	struct mutex rate_lock;
};

static void tegra186_emc_rate_requests_init(struct tegra186_emc *emc)
{
	unsigned int i;

	for (i = 0; i < EMC_RATE_TYPE_MAX; i++) {
		emc->requested_rate[i].min_rate = 0;
		emc->requested_rate[i].max_rate = ULONG_MAX;
	}
}

static int emc_request_rate(struct tegra186_emc *emc,
			    unsigned long new_min_rate,
			    unsigned long new_max_rate,
			    enum emc_rate_request_type type)
{
	struct emc_rate_request *req = emc->requested_rate;
	unsigned long min_rate = 0, max_rate = ULONG_MAX;
	unsigned int i;
	int err;

	/* select minimum and maximum rates among the requested rates */
	for (i = 0; i < EMC_RATE_TYPE_MAX; i++, req++) {
		if (i == type) {
			min_rate = max(new_min_rate, min_rate);
			max_rate = min(new_max_rate, max_rate);
		} else {
			min_rate = max(req->min_rate, min_rate);
			max_rate = min(req->max_rate, max_rate);
		}
	}

	if (min_rate > max_rate) {
		dev_err_ratelimited(emc->dev, "%s: type %u: out of range: %lu %lu\n",
				    __func__, type, min_rate, max_rate);
		return -ERANGE;
	}

	err = clk_set_rate(emc->clk, min_rate);
	if (err)
		return err;

	emc->requested_rate[type].min_rate = new_min_rate;
	emc->requested_rate[type].max_rate = new_max_rate;

	return 0;
}

static int emc_set_min_rate(struct tegra186_emc *emc, unsigned long rate,
			    enum emc_rate_request_type type)
{
	struct emc_rate_request *req = &emc->requested_rate[type];
	int ret;

	mutex_lock(&emc->rate_lock);
	ret = emc_request_rate(emc, rate, req->max_rate, type);
	mutex_unlock(&emc->rate_lock);

	return ret;
}

static int emc_set_max_rate(struct tegra186_emc *emc, unsigned long rate,
			    enum emc_rate_request_type type)
{
	struct emc_rate_request *req = &emc->requested_rate[type];
	int ret;

	mutex_lock(&emc->rate_lock);
	ret = emc_request_rate(emc, req->min_rate, rate, type);
	mutex_unlock(&emc->rate_lock);

	return ret;
}

/*
 * debugfs interface
 *
 * The memory controller driver exposes some files in debugfs that can be used
 * to control the EMC frequency. The top-level directory can be found here:
 *
 *   /sys/kernel/debug/emc
 *
 * It contains the following files:
 *
 *   - available_rates: This file contains a list of valid, space-separated
 *     EMC frequencies.
 *
 *   - min_rate: Writing a value to this file sets the given frequency as the
 *       floor of the permitted range. If this is higher than the currently
 *       configured EMC frequency, this will cause the frequency to be
 *       increased so that it stays within the valid range.
 *
 *   - max_rate: Similarily to the min_rate file, writing a value to this file
 *       sets the given frequency as the ceiling of the permitted range. If
 *       the value is lower than the currently configured EMC frequency, this
 *       will cause the frequency to be decreased so that it stays within the
 *       valid range.
 */

static bool tegra186_emc_validate_rate(struct tegra186_emc *emc,
				       unsigned long rate)
{
	unsigned int i;

	for (i = 0; i < emc->num_dvfs; i++)
		if (rate == emc->dvfs[i].rate)
			return true;

	return false;
}

static int tegra186_emc_debug_available_rates_show(struct seq_file *s,
						   void *data)
{
	struct tegra186_emc *emc = s->private;
	const char *prefix = "";
	unsigned int i;

	for (i = 0; i < emc->num_dvfs; i++) {
		seq_printf(s, "%s%lu", prefix, emc->dvfs[i].rate);
		prefix = " ";
	}

	seq_puts(s, "\n");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tegra186_emc_debug_available_rates);

static int tegra186_emc_debug_min_rate_get(void *data, u64 *rate)
{
	struct tegra186_emc *emc = data;

	*rate = emc->debugfs.min_rate;

	return 0;
}

static int tegra186_emc_debug_min_rate_set(void *data, u64 rate)
{
	struct tegra186_emc *emc = data;
	int err;

	if (!tegra186_emc_validate_rate(emc, rate))
		return -EINVAL;

	err = emc_set_min_rate(emc, rate, EMC_RATE_DEBUG);
	if (err < 0)
		return err;

	emc->debugfs.min_rate = rate;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(tegra186_emc_debug_min_rate_fops,
			  tegra186_emc_debug_min_rate_get,
			  tegra186_emc_debug_min_rate_set, "%llu\n");

static int tegra186_emc_debug_max_rate_get(void *data, u64 *rate)
{
	struct tegra186_emc *emc = data;

	*rate = emc->debugfs.max_rate;

	return 0;
}

static int tegra186_emc_debug_max_rate_set(void *data, u64 rate)
{
	struct tegra186_emc *emc = data;
	int err;

	if (!tegra186_emc_validate_rate(emc, rate))
		return -EINVAL;

	err = emc_set_max_rate(emc, rate, EMC_RATE_DEBUG);
	if (err < 0)
		return err;

	emc->debugfs.max_rate = rate;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(tegra186_emc_debug_max_rate_fops,
			  tegra186_emc_debug_max_rate_get,
			  tegra186_emc_debug_max_rate_set, "%llu\n");

static int tegra186_emc_get_emc_dvfs_latency(struct tegra186_emc *emc)
{
	struct mrq_emc_dvfs_latency_response response;
	struct tegra_bpmp_message msg;
	unsigned int i;
	int err;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_EMC_DVFS_LATENCY;
	msg.tx.data = NULL;
	msg.tx.size = 0;
	msg.rx.data = &response;
	msg.rx.size = sizeof(response);

	err = tegra_bpmp_transfer(emc->bpmp, &msg);
	if (err < 0) {
		dev_err(emc->dev, "failed to EMC DVFS pairs: %d\n", err);
		return err;
	}
	if (msg.rx.ret < 0) {
		dev_err(emc->dev, "EMC DVFS MRQ failed: %d (BPMP error code)\n", msg.rx.ret);
		return -EINVAL;
	}

	emc->debugfs.min_rate = ULONG_MAX;
	emc->debugfs.max_rate = 0;

	emc->num_dvfs = response.num_pairs;

	emc->dvfs = devm_kmalloc_array(emc->dev, emc->num_dvfs, sizeof(*emc->dvfs), GFP_KERNEL);
	if (!emc->dvfs)
		return -ENOMEM;

	dev_dbg(emc->dev, "%u DVFS pairs:\n", emc->num_dvfs);

	for (i = 0; i < emc->num_dvfs; i++) {
		emc->dvfs[i].rate = response.pairs[i].freq * 1000;
		emc->dvfs[i].latency = response.pairs[i].latency;

		if (emc->dvfs[i].rate < emc->debugfs.min_rate)
			emc->debugfs.min_rate = emc->dvfs[i].rate;

		if (emc->dvfs[i].rate > emc->debugfs.max_rate)
			emc->debugfs.max_rate = emc->dvfs[i].rate;

		dev_dbg(emc->dev, "  %2u: %lu Hz -> %lu us\n", i,
			emc->dvfs[i].rate, emc->dvfs[i].latency);
	}

	err = clk_set_rate_range(emc->clk, emc->debugfs.min_rate, emc->debugfs.max_rate);
	if (err < 0) {
		dev_err(emc->dev, "failed to set rate range [%lu-%lu] for %pC\n",
			emc->debugfs.min_rate, emc->debugfs.max_rate, emc->clk);
		return err;
	}

	emc->debugfs.root = debugfs_create_dir("emc", NULL);
	debugfs_create_file("available_rates", 0444, emc->debugfs.root, emc,
			    &tegra186_emc_debug_available_rates_fops);
	debugfs_create_file("min_rate", 0644, emc->debugfs.root, emc,
			    &tegra186_emc_debug_min_rate_fops);
	debugfs_create_file("max_rate", 0644, emc->debugfs.root, emc,
			    &tegra186_emc_debug_max_rate_fops);

	return 0;
}

static inline struct tegra186_emc *
to_tegra186_emc_provider(struct icc_provider *provider)
{
	return container_of(provider, struct tegra186_emc, provider);
}

/*
 * tegra186_emc_icc_set_bw() - Set BW api for EMC provider
 * @src: ICC node for External Memory Controller (EMC)
 * @dst: ICC node for External Memory (DRAM)
 *
 * Do nothing here as info to BPMP-FW is now passed in the BW set function
 * of the MC driver. BPMP-FW sets the final Freq based on the passed values.
 */
static int tegra186_emc_icc_set_bw(struct icc_node *src, struct icc_node *dst)
{
	struct tegra186_emc *emc = to_tegra186_emc_provider(dst->provider);
	struct tegra_mc *mc = dev_get_drvdata(emc->dev->parent);
	unsigned long long peak_bw = icc_units_to_bps(dst->peak_bw);
	unsigned long long avg_bw = icc_units_to_bps(dst->avg_bw);
	unsigned long long rate = max(avg_bw, peak_bw);
	const unsigned int ddr = 2;
	int err;

	/*
	 * Do nothing here if bwmgr is supported in BPMP-FW. BPMP-FW sets the final
	 * Freq based on the passed values.
	 */
	if (mc->bwmgr_mrq_supported)
		return 0;

	/*
	 * Tegra186 EMC runs on a clock rate of SDRAM bus. This means that
	 * EMC clock rate is twice smaller than the peak data rate because
	 * data is sampled on both EMC clock edges.
	 */
	do_div(rate, ddr);
	rate = min_t(u64, rate, U32_MAX);

	err = emc_set_min_rate(emc, rate, EMC_RATE_ICC);
	if (err)
		return err;

	return 0;
}

static struct icc_node *
tegra186_emc_of_icc_xlate(const struct of_phandle_args *spec, void *data)
{
	struct icc_provider *provider = data;
	struct icc_node *node;

	/* External Memory is the only possible ICC route */
	list_for_each_entry(node, &provider->nodes, node_list) {
		if (node->id != TEGRA_ICC_EMEM)
			continue;

		return node;
	}

	return ERR_PTR(-EPROBE_DEFER);
}

static int tegra186_emc_icc_get_init_bw(struct icc_node *node, u32 *avg, u32 *peak)
{
	*avg = 0;
	*peak = 0;

	return 0;
}

static int tegra186_emc_interconnect_init(struct tegra186_emc *emc)
{
	struct tegra_mc *mc = dev_get_drvdata(emc->dev->parent);
	const struct tegra_mc_soc *soc = mc->soc;
	struct icc_node *node;
	int err;

	emc->provider.dev = emc->dev;
	emc->provider.set = tegra186_emc_icc_set_bw;
	emc->provider.data = &emc->provider;
	emc->provider.aggregate = soc->icc_ops->aggregate;
	emc->provider.xlate = tegra186_emc_of_icc_xlate;
	emc->provider.get_bw = tegra186_emc_icc_get_init_bw;

	icc_provider_init(&emc->provider);

	/* create External Memory Controller node */
	node = icc_node_create(TEGRA_ICC_EMC);
	if (IS_ERR(node))
		return PTR_ERR(node);

	node->name = "External Memory Controller";
	icc_node_add(node, &emc->provider);

	/* link External Memory Controller to External Memory (DRAM) */
	err = icc_link_create(node, TEGRA_ICC_EMEM);
	if (err)
		goto remove_nodes;

	/* create External Memory node */
	node = icc_node_create(TEGRA_ICC_EMEM);
	if (IS_ERR(node)) {
		err = PTR_ERR(node);
		goto remove_nodes;
	}

	node->name = "External Memory (DRAM)";
	icc_node_add(node, &emc->provider);

	err = icc_provider_register(&emc->provider);
	if (err)
		goto remove_nodes;

	return 0;

remove_nodes:
	icc_nodes_remove(&emc->provider);

	return dev_err_probe(emc->dev, err, "failed to initialize ICC\n");
}

static int tegra186_emc_probe(struct platform_device *pdev)
{
	struct tegra_mc *mc = dev_get_drvdata(pdev->dev.parent);
	struct tegra186_emc *emc;
	int err;

	emc = devm_kzalloc(&pdev->dev, sizeof(*emc), GFP_KERNEL);
	if (!emc)
		return -ENOMEM;

	emc->bpmp = tegra_bpmp_get(&pdev->dev);
	if (IS_ERR(emc->bpmp))
		return dev_err_probe(&pdev->dev, PTR_ERR(emc->bpmp),
				     "failed to get BPMP\n");

	emc->clk = devm_clk_get(&pdev->dev, "emc");
	if (IS_ERR(emc->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(emc->clk),
				     "failed to get EMC clock\n");

	platform_set_drvdata(pdev, emc);
	emc->dev = &pdev->dev;

	tegra186_emc_rate_requests_init(emc);

	if (tegra_bpmp_mrq_is_supported(emc->bpmp, MRQ_EMC_DVFS_LATENCY)) {
		err = tegra186_emc_get_emc_dvfs_latency(emc);
		if (err)
			goto put_bpmp;
	}

	if (mc && mc->soc->icc_ops) {
		if (tegra_bpmp_mrq_is_supported(emc->bpmp, MRQ_BWMGR_INT)) {
			mc->bwmgr_mrq_supported = true;

			/*
			 * MC driver probe can't get BPMP reference as it gets probed
			 * earlier than BPMP. So, save the BPMP ref got from the EMC
			 * DT node in the mc->bpmp and use it in MC's icc_set hook.
			 */
			mc->bpmp = emc->bpmp;
			barrier();
		}

		/*
		 * Initialize the ICC even if BPMP-FW doesn't support 'MRQ_BWMGR_INT'.
		 * Use the flag 'mc->bwmgr_mrq_supported' within MC driver and return
		 * EINVAL instead of passing the request to BPMP-FW later when the BW
		 * request is made by client with 'icc_set_bw()' call.
		 */
		err = tegra186_emc_interconnect_init(emc);
		if (err) {
			mc->bpmp = NULL;
			goto put_bpmp;
		}
	}

	return 0;

put_bpmp:
	tegra_bpmp_put(emc->bpmp);
	return err;
}

static void tegra186_emc_remove(struct platform_device *pdev)
{
	struct tegra_mc *mc = dev_get_drvdata(pdev->dev.parent);
	struct tegra186_emc *emc = platform_get_drvdata(pdev);

	debugfs_remove_recursive(emc->debugfs.root);

	mc->bpmp = NULL;
	tegra_bpmp_put(emc->bpmp);
}

static const struct of_device_id tegra186_emc_of_match[] = {
#if defined(CONFIG_ARCH_TEGRA_186_SOC)
	{ .compatible = "nvidia,tegra186-emc" },
#endif
#if defined(CONFIG_ARCH_TEGRA_194_SOC)
	{ .compatible = "nvidia,tegra194-emc" },
#endif
#if defined(CONFIG_ARCH_TEGRA_234_SOC)
	{ .compatible = "nvidia,tegra234-emc" },
#endif
#if defined(CONFIG_ARCH_TEGRA_264_SOC)
	{ .compatible = "nvidia,tegra264-emc" },
#endif
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tegra186_emc_of_match);

static struct platform_driver tegra186_emc_driver = {
	.driver = {
		.name = "tegra186-emc",
		.of_match_table = tegra186_emc_of_match,
		.suppress_bind_attrs = true,
		.sync_state = icc_sync_state,
	},
	.probe = tegra186_emc_probe,
	.remove = tegra186_emc_remove,
};
module_platform_driver(tegra186_emc_driver);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra186 External Memory Controller driver");
