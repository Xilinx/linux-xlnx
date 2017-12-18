/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "en.h"

static void mlx5e_get_drvinfo(struct net_device *dev,
			      struct ethtool_drvinfo *drvinfo)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	strlcpy(drvinfo->driver, DRIVER_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, DRIVER_VERSION " (" DRIVER_RELDATE ")",
		sizeof(drvinfo->version));
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%d.%d.%d",
		 fw_rev_maj(mdev), fw_rev_min(mdev), fw_rev_sub(mdev));
	strlcpy(drvinfo->bus_info, pci_name(mdev->pdev),
		sizeof(drvinfo->bus_info));
}

struct ptys2ethtool_config {
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(advertised);
	u32 speed;
};

static struct ptys2ethtool_config ptys2ethtool_table[MLX5E_LINK_MODES_NUMBER];

#define MLX5_BUILD_PTYS2ETHTOOL_CONFIG(reg_, speed_, ...)               \
	({                                                              \
		struct ptys2ethtool_config *cfg;                        \
		const unsigned int modes[] = { __VA_ARGS__ };           \
		unsigned int i;                                         \
		cfg = &ptys2ethtool_table[reg_];                        \
		cfg->speed = speed_;                                    \
		bitmap_zero(cfg->supported,                             \
			    __ETHTOOL_LINK_MODE_MASK_NBITS);            \
		bitmap_zero(cfg->advertised,                            \
			    __ETHTOOL_LINK_MODE_MASK_NBITS);            \
		for (i = 0 ; i < ARRAY_SIZE(modes) ; ++i) {             \
			__set_bit(modes[i], cfg->supported);            \
			__set_bit(modes[i], cfg->advertised);           \
		}                                                       \
	})

void mlx5e_build_ptys2ethtool_map(void)
{
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_1000BASE_CX_SGMII, SPEED_1000,
				       ETHTOOL_LINK_MODE_1000baseKX_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_1000BASE_KX, SPEED_1000,
				       ETHTOOL_LINK_MODE_1000baseKX_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_CX4, SPEED_10000,
				       ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_KX4, SPEED_10000,
				       ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_KR, SPEED_10000,
				       ETHTOOL_LINK_MODE_10000baseKR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_20GBASE_KR2, SPEED_20000,
				       ETHTOOL_LINK_MODE_20000baseKR2_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_40GBASE_CR4, SPEED_40000,
				       ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_40GBASE_KR4, SPEED_40000,
				       ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_56GBASE_R4, SPEED_56000,
				       ETHTOOL_LINK_MODE_56000baseKR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_CR, SPEED_10000,
				       ETHTOOL_LINK_MODE_10000baseKR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_SR, SPEED_10000,
				       ETHTOOL_LINK_MODE_10000baseKR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_ER, SPEED_10000,
				       ETHTOOL_LINK_MODE_10000baseKR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_40GBASE_SR4, SPEED_40000,
				       ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_40GBASE_LR4, SPEED_40000,
				       ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_50GBASE_SR2, SPEED_50000,
				       ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_100GBASE_CR4, SPEED_100000,
				       ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_100GBASE_SR4, SPEED_100000,
				       ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_100GBASE_KR4, SPEED_100000,
				       ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_100GBASE_LR4, SPEED_100000,
				       ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_T, SPEED_10000,
				       ETHTOOL_LINK_MODE_10000baseT_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_25GBASE_CR, SPEED_25000,
				       ETHTOOL_LINK_MODE_25000baseCR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_25GBASE_KR, SPEED_25000,
				       ETHTOOL_LINK_MODE_25000baseKR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_25GBASE_SR, SPEED_25000,
				       ETHTOOL_LINK_MODE_25000baseSR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_50GBASE_CR2, SPEED_50000,
				       ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_50GBASE_KR2, SPEED_50000,
				       ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT);
}

static unsigned long mlx5e_query_pfc_combined(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 pfc_en_tx;
	u8 pfc_en_rx;
	int err;

	err = mlx5_query_port_pfc(mdev, &pfc_en_tx, &pfc_en_rx);

	return err ? 0 : pfc_en_tx | pfc_en_rx;
}

static bool mlx5e_query_global_pause_combined(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 rx_pause;
	u32 tx_pause;
	int err;

	err = mlx5_query_port_pause(mdev, &rx_pause, &tx_pause);

	return err ? false : rx_pause | tx_pause;
}

#define MLX5E_NUM_Q_CNTRS(priv) (NUM_Q_COUNTERS * (!!priv->q_counter))
#define MLX5E_NUM_RQ_STATS(priv) \
	(NUM_RQ_STATS * priv->params.num_channels * \
	 test_bit(MLX5E_STATE_OPENED, &priv->state))
#define MLX5E_NUM_SQ_STATS(priv) \
	(NUM_SQ_STATS * priv->params.num_channels * priv->params.num_tc * \
	 test_bit(MLX5E_STATE_OPENED, &priv->state))
#define MLX5E_NUM_PFC_COUNTERS(priv) \
	((mlx5e_query_global_pause_combined(priv) + hweight8(mlx5e_query_pfc_combined(priv))) * \
	  NUM_PPORT_PER_PRIO_PFC_COUNTERS)

static int mlx5e_get_sset_count(struct net_device *dev, int sset)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	switch (sset) {
	case ETH_SS_STATS:
		return NUM_SW_COUNTERS +
		       MLX5E_NUM_Q_CNTRS(priv) +
		       NUM_VPORT_COUNTERS + NUM_PPORT_COUNTERS +
		       MLX5E_NUM_RQ_STATS(priv) +
		       MLX5E_NUM_SQ_STATS(priv) +
		       MLX5E_NUM_PFC_COUNTERS(priv);
	case ETH_SS_PRIV_FLAGS:
		return ARRAY_SIZE(mlx5e_priv_flags);
	/* fallthrough */
	default:
		return -EOPNOTSUPP;
	}
}

static void mlx5e_fill_stats_strings(struct mlx5e_priv *priv, uint8_t *data)
{
	int i, j, tc, prio, idx = 0;
	unsigned long pfc_combined;

	/* SW counters */
	for (i = 0; i < NUM_SW_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, sw_stats_desc[i].format);

	/* Q counters */
	for (i = 0; i < MLX5E_NUM_Q_CNTRS(priv); i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, q_stats_desc[i].format);

	/* VPORT counters */
	for (i = 0; i < NUM_VPORT_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       vport_stats_desc[i].format);

	/* PPORT counters */
	for (i = 0; i < NUM_PPORT_802_3_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       pport_802_3_stats_desc[i].format);

	for (i = 0; i < NUM_PPORT_2863_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       pport_2863_stats_desc[i].format);

	for (i = 0; i < NUM_PPORT_2819_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       pport_2819_stats_desc[i].format);

	for (prio = 0; prio < NUM_PPORT_PRIO; prio++) {
		for (i = 0; i < NUM_PPORT_PER_PRIO_TRAFFIC_COUNTERS; i++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				pport_per_prio_traffic_stats_desc[i].format, prio);
	}

	pfc_combined = mlx5e_query_pfc_combined(priv);
	for_each_set_bit(prio, &pfc_combined, NUM_PPORT_PRIO) {
		for (i = 0; i < NUM_PPORT_PER_PRIO_PFC_COUNTERS; i++) {
			char pfc_string[ETH_GSTRING_LEN];

			snprintf(pfc_string, sizeof(pfc_string), "prio%d", prio);
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				pport_per_prio_pfc_stats_desc[i].format, pfc_string);
		}
	}

	if (mlx5e_query_global_pause_combined(priv)) {
		for (i = 0; i < NUM_PPORT_PER_PRIO_PFC_COUNTERS; i++) {
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				pport_per_prio_pfc_stats_desc[i].format, "global");
		}
	}

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return;

	/* per channel counters */
	for (i = 0; i < priv->params.num_channels; i++)
		for (j = 0; j < NUM_RQ_STATS; j++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				rq_stats_desc[j].format, i);

	for (tc = 0; tc < priv->params.num_tc; tc++)
		for (i = 0; i < priv->params.num_channels; i++)
			for (j = 0; j < NUM_SQ_STATS; j++)
				sprintf(data + (idx++) * ETH_GSTRING_LEN,
					sq_stats_desc[j].format,
					priv->channeltc_to_txq_map[i][tc]);
}

static void mlx5e_get_strings(struct net_device *dev,
			      uint32_t stringset, uint8_t *data)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int i;

	switch (stringset) {
	case ETH_SS_PRIV_FLAGS:
		for (i = 0; i < ARRAY_SIZE(mlx5e_priv_flags); i++)
			strcpy(data + i * ETH_GSTRING_LEN, mlx5e_priv_flags[i]);
		break;

	case ETH_SS_TEST:
		break;

	case ETH_SS_STATS:
		mlx5e_fill_stats_strings(priv, data);
		break;
	}
}

static void mlx5e_get_ethtool_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int i, j, tc, prio, idx = 0;
	unsigned long pfc_combined;

	if (!data)
		return;

	mutex_lock(&priv->state_lock);
	if (test_bit(MLX5E_STATE_OPENED, &priv->state))
		mlx5e_update_stats(priv);
	mutex_unlock(&priv->state_lock);

	for (i = 0; i < NUM_SW_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_CPU(&priv->stats.sw,
						   sw_stats_desc, i);

	for (i = 0; i < MLX5E_NUM_Q_CNTRS(priv); i++)
		data[idx++] = MLX5E_READ_CTR32_CPU(&priv->stats.qcnt,
						   q_stats_desc, i);

	for (i = 0; i < NUM_VPORT_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_BE(priv->stats.vport.query_vport_out,
						  vport_stats_desc, i);

	for (i = 0; i < NUM_PPORT_802_3_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_BE(&priv->stats.pport.IEEE_802_3_counters,
						  pport_802_3_stats_desc, i);

	for (i = 0; i < NUM_PPORT_2863_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_BE(&priv->stats.pport.RFC_2863_counters,
						  pport_2863_stats_desc, i);

	for (i = 0; i < NUM_PPORT_2819_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_BE(&priv->stats.pport.RFC_2819_counters,
						  pport_2819_stats_desc, i);

	for (prio = 0; prio < NUM_PPORT_PRIO; prio++) {
		for (i = 0; i < NUM_PPORT_PER_PRIO_TRAFFIC_COUNTERS; i++)
			data[idx++] = MLX5E_READ_CTR64_BE(&priv->stats.pport.per_prio_counters[prio],
						 pport_per_prio_traffic_stats_desc, i);
	}

	pfc_combined = mlx5e_query_pfc_combined(priv);
	for_each_set_bit(prio, &pfc_combined, NUM_PPORT_PRIO) {
		for (i = 0; i < NUM_PPORT_PER_PRIO_PFC_COUNTERS; i++) {
			data[idx++] = MLX5E_READ_CTR64_BE(&priv->stats.pport.per_prio_counters[prio],
							  pport_per_prio_pfc_stats_desc, i);
		}
	}

	if (mlx5e_query_global_pause_combined(priv)) {
		for (i = 0; i < NUM_PPORT_PER_PRIO_PFC_COUNTERS; i++) {
			data[idx++] = MLX5E_READ_CTR64_BE(&priv->stats.pport.per_prio_counters[0],
							  pport_per_prio_pfc_stats_desc, i);
		}
	}

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return;

	/* per channel counters */
	for (i = 0; i < priv->params.num_channels; i++)
		for (j = 0; j < NUM_RQ_STATS; j++)
			data[idx++] =
			       MLX5E_READ_CTR64_CPU(&priv->channel[i]->rq.stats,
						    rq_stats_desc, j);

	for (tc = 0; tc < priv->params.num_tc; tc++)
		for (i = 0; i < priv->params.num_channels; i++)
			for (j = 0; j < NUM_SQ_STATS; j++)
				data[idx++] = MLX5E_READ_CTR64_CPU(&priv->channel[i]->sq[tc].stats,
								   sq_stats_desc, j);
}

static u32 mlx5e_rx_wqes_to_packets(struct mlx5e_priv *priv, int rq_wq_type,
				    int num_wqe)
{
	int packets_per_wqe;
	int stride_size;
	int num_strides;
	int wqe_size;

	if (rq_wq_type != MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ)
		return num_wqe;

	stride_size = 1 << priv->params.mpwqe_log_stride_sz;
	num_strides = 1 << priv->params.mpwqe_log_num_strides;
	wqe_size = stride_size * num_strides;

	packets_per_wqe = wqe_size /
			  ALIGN(ETH_DATA_LEN, stride_size);
	return (1 << (order_base_2(num_wqe * packets_per_wqe) - 1));
}

static u32 mlx5e_packets_to_rx_wqes(struct mlx5e_priv *priv, int rq_wq_type,
				    int num_packets)
{
	int packets_per_wqe;
	int stride_size;
	int num_strides;
	int wqe_size;
	int num_wqes;

	if (rq_wq_type != MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ)
		return num_packets;

	stride_size = 1 << priv->params.mpwqe_log_stride_sz;
	num_strides = 1 << priv->params.mpwqe_log_num_strides;
	wqe_size = stride_size * num_strides;

	num_packets = (1 << order_base_2(num_packets));

	packets_per_wqe = wqe_size /
			  ALIGN(ETH_DATA_LEN, stride_size);
	num_wqes = DIV_ROUND_UP(num_packets, packets_per_wqe);
	return 1 << (order_base_2(num_wqes));
}

static void mlx5e_get_ringparam(struct net_device *dev,
				struct ethtool_ringparam *param)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int rq_wq_type = priv->params.rq_wq_type;

	param->rx_max_pending = mlx5e_rx_wqes_to_packets(priv, rq_wq_type,
							 1 << mlx5_max_log_rq_size(rq_wq_type));
	param->tx_max_pending = 1 << MLX5E_PARAMS_MAXIMUM_LOG_SQ_SIZE;
	param->rx_pending = mlx5e_rx_wqes_to_packets(priv, rq_wq_type,
						     1 << priv->params.log_rq_size);
	param->tx_pending     = 1 << priv->params.log_sq_size;
}

static int mlx5e_set_ringparam(struct net_device *dev,
			       struct ethtool_ringparam *param)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	bool was_opened;
	int rq_wq_type = priv->params.rq_wq_type;
	u32 rx_pending_wqes;
	u32 min_rq_size;
	u32 max_rq_size;
	u16 min_rx_wqes;
	u8 log_rq_size;
	u8 log_sq_size;
	u32 num_mtts;
	int err = 0;

	if (param->rx_jumbo_pending) {
		netdev_info(dev, "%s: rx_jumbo_pending not supported\n",
			    __func__);
		return -EINVAL;
	}
	if (param->rx_mini_pending) {
		netdev_info(dev, "%s: rx_mini_pending not supported\n",
			    __func__);
		return -EINVAL;
	}

	min_rq_size = mlx5e_rx_wqes_to_packets(priv, rq_wq_type,
					       1 << mlx5_min_log_rq_size(rq_wq_type));
	max_rq_size = mlx5e_rx_wqes_to_packets(priv, rq_wq_type,
					       1 << mlx5_max_log_rq_size(rq_wq_type));
	rx_pending_wqes = mlx5e_packets_to_rx_wqes(priv, rq_wq_type,
						   param->rx_pending);

	if (param->rx_pending < min_rq_size) {
		netdev_info(dev, "%s: rx_pending (%d) < min (%d)\n",
			    __func__, param->rx_pending,
			    min_rq_size);
		return -EINVAL;
	}
	if (param->rx_pending > max_rq_size) {
		netdev_info(dev, "%s: rx_pending (%d) > max (%d)\n",
			    __func__, param->rx_pending,
			    max_rq_size);
		return -EINVAL;
	}

	num_mtts = MLX5E_REQUIRED_MTTS(priv->params.num_channels,
				       rx_pending_wqes);
	if (priv->params.rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ &&
	    !MLX5E_VALID_NUM_MTTS(num_mtts)) {
		netdev_info(dev, "%s: rx_pending (%d) request can't be satisfied, try to reduce.\n",
			    __func__, param->rx_pending);
		return -EINVAL;
	}

	if (param->tx_pending < (1 << MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE)) {
		netdev_info(dev, "%s: tx_pending (%d) < min (%d)\n",
			    __func__, param->tx_pending,
			    1 << MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE);
		return -EINVAL;
	}
	if (param->tx_pending > (1 << MLX5E_PARAMS_MAXIMUM_LOG_SQ_SIZE)) {
		netdev_info(dev, "%s: tx_pending (%d) > max (%d)\n",
			    __func__, param->tx_pending,
			    1 << MLX5E_PARAMS_MAXIMUM_LOG_SQ_SIZE);
		return -EINVAL;
	}

	log_rq_size = order_base_2(rx_pending_wqes);
	log_sq_size = order_base_2(param->tx_pending);
	min_rx_wqes = mlx5_min_rx_wqes(rq_wq_type, rx_pending_wqes);

	if (log_rq_size == priv->params.log_rq_size &&
	    log_sq_size == priv->params.log_sq_size &&
	    min_rx_wqes == priv->params.min_rx_wqes)
		return 0;

	mutex_lock(&priv->state_lock);

	was_opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	if (was_opened)
		mlx5e_close_locked(dev);

	priv->params.log_rq_size = log_rq_size;
	priv->params.log_sq_size = log_sq_size;
	priv->params.min_rx_wqes = min_rx_wqes;

	if (was_opened)
		err = mlx5e_open_locked(dev);

	mutex_unlock(&priv->state_lock);

	return err;
}

static void mlx5e_get_channels(struct net_device *dev,
			       struct ethtool_channels *ch)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	ch->max_combined   = mlx5e_get_max_num_channels(priv->mdev);
	ch->combined_count = priv->params.num_channels;
}

static int mlx5e_set_channels(struct net_device *dev,
			      struct ethtool_channels *ch)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int ncv = mlx5e_get_max_num_channels(priv->mdev);
	unsigned int count = ch->combined_count;
	bool arfs_enabled;
	bool was_opened;
	u32 num_mtts;
	int err = 0;

	if (!count) {
		netdev_info(dev, "%s: combined_count=0 not supported\n",
			    __func__);
		return -EINVAL;
	}
	if (ch->rx_count || ch->tx_count) {
		netdev_info(dev, "%s: separate rx/tx count not supported\n",
			    __func__);
		return -EINVAL;
	}
	if (count > ncv) {
		netdev_info(dev, "%s: count (%d) > max (%d)\n",
			    __func__, count, ncv);
		return -EINVAL;
	}

	num_mtts = MLX5E_REQUIRED_MTTS(count, BIT(priv->params.log_rq_size));
	if (priv->params.rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ &&
	    !MLX5E_VALID_NUM_MTTS(num_mtts)) {
		netdev_info(dev, "%s: rx count (%d) request can't be satisfied, try to reduce.\n",
			    __func__, count);
		return -EINVAL;
	}

	if (priv->params.num_channels == count)
		return 0;

	mutex_lock(&priv->state_lock);

	was_opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	if (was_opened)
		mlx5e_close_locked(dev);

	arfs_enabled = dev->features & NETIF_F_NTUPLE;
	if (arfs_enabled)
		mlx5e_arfs_disable(priv);

	priv->params.num_channels = count;
	mlx5e_build_default_indir_rqt(priv->mdev, priv->params.indirection_rqt,
				      MLX5E_INDIR_RQT_SIZE, count);

	if (was_opened)
		err = mlx5e_open_locked(dev);
	if (err)
		goto out;

	if (arfs_enabled) {
		err = mlx5e_arfs_enable(priv);
		if (err)
			netdev_err(dev, "%s: mlx5e_arfs_enable failed: %d\n",
				   __func__, err);
	}

out:
	mutex_unlock(&priv->state_lock);

	return err;
}

static int mlx5e_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coal)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	if (!MLX5_CAP_GEN(priv->mdev, cq_moderation))
		return -ENOTSUPP;

	coal->rx_coalesce_usecs       = priv->params.rx_cq_moderation.usec;
	coal->rx_max_coalesced_frames = priv->params.rx_cq_moderation.pkts;
	coal->tx_coalesce_usecs       = priv->params.tx_cq_moderation.usec;
	coal->tx_max_coalesced_frames = priv->params.tx_cq_moderation.pkts;
	coal->use_adaptive_rx_coalesce = priv->params.rx_am_enabled;

	return 0;
}

static int mlx5e_set_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coal)
{
	struct mlx5e_priv *priv    = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_channel *c;
	bool restart =
		!!coal->use_adaptive_rx_coalesce != priv->params.rx_am_enabled;
	bool was_opened;
	int err = 0;
	int tc;
	int i;

	if (!MLX5_CAP_GEN(mdev, cq_moderation))
		return -ENOTSUPP;

	mutex_lock(&priv->state_lock);

	was_opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	if (was_opened && restart) {
		mlx5e_close_locked(netdev);
		priv->params.rx_am_enabled = !!coal->use_adaptive_rx_coalesce;
	}

	priv->params.tx_cq_moderation.usec = coal->tx_coalesce_usecs;
	priv->params.tx_cq_moderation.pkts = coal->tx_max_coalesced_frames;
	priv->params.rx_cq_moderation.usec = coal->rx_coalesce_usecs;
	priv->params.rx_cq_moderation.pkts = coal->rx_max_coalesced_frames;

	if (!was_opened || restart)
		goto out;

	for (i = 0; i < priv->params.num_channels; ++i) {
		c = priv->channel[i];

		for (tc = 0; tc < c->num_tc; tc++) {
			mlx5_core_modify_cq_moderation(mdev,
						&c->sq[tc].cq.mcq,
						coal->tx_coalesce_usecs,
						coal->tx_max_coalesced_frames);
		}

		mlx5_core_modify_cq_moderation(mdev, &c->rq.cq.mcq,
					       coal->rx_coalesce_usecs,
					       coal->rx_max_coalesced_frames);
	}

out:
	if (was_opened && restart)
		err = mlx5e_open_locked(netdev);

	mutex_unlock(&priv->state_lock);
	return err;
}

static void ptys2ethtool_supported_link(unsigned long *supported_modes,
					u32 eth_proto_cap)
{
	unsigned long proto_cap = eth_proto_cap;
	int proto;

	for_each_set_bit(proto, &proto_cap, MLX5E_LINK_MODES_NUMBER)
		bitmap_or(supported_modes, supported_modes,
			  ptys2ethtool_table[proto].supported,
			  __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static void ptys2ethtool_adver_link(unsigned long *advertising_modes,
				    u32 eth_proto_cap)
{
	unsigned long proto_cap = eth_proto_cap;
	int proto;

	for_each_set_bit(proto, &proto_cap, MLX5E_LINK_MODES_NUMBER)
		bitmap_or(advertising_modes, advertising_modes,
			  ptys2ethtool_table[proto].advertised,
			  __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static void ptys2ethtool_supported_port(struct ethtool_link_ksettings *link_ksettings,
					u32 eth_proto_cap)
{
	if (eth_proto_cap & (MLX5E_PROT_MASK(MLX5E_10GBASE_CR)
			   | MLX5E_PROT_MASK(MLX5E_10GBASE_SR)
			   | MLX5E_PROT_MASK(MLX5E_40GBASE_CR4)
			   | MLX5E_PROT_MASK(MLX5E_40GBASE_SR4)
			   | MLX5E_PROT_MASK(MLX5E_100GBASE_SR4)
			   | MLX5E_PROT_MASK(MLX5E_1000BASE_CX_SGMII))) {
		ethtool_link_ksettings_add_link_mode(link_ksettings, supported, FIBRE);
	}

	if (eth_proto_cap & (MLX5E_PROT_MASK(MLX5E_100GBASE_KR4)
			   | MLX5E_PROT_MASK(MLX5E_40GBASE_KR4)
			   | MLX5E_PROT_MASK(MLX5E_10GBASE_KR)
			   | MLX5E_PROT_MASK(MLX5E_10GBASE_KX4)
			   | MLX5E_PROT_MASK(MLX5E_1000BASE_KX))) {
		ethtool_link_ksettings_add_link_mode(link_ksettings, supported, Backplane);
	}
}

int mlx5e_get_max_linkspeed(struct mlx5_core_dev *mdev, u32 *speed)
{
	u32 max_speed = 0;
	u32 proto_cap;
	int err;
	int i;

	err = mlx5_query_port_proto_cap(mdev, &proto_cap, MLX5_PTYS_EN);
	if (err)
		return err;

	for (i = 0; i < MLX5E_LINK_MODES_NUMBER; ++i)
		if (proto_cap & MLX5E_PROT_MASK(i))
			max_speed = max(max_speed, ptys2ethtool_table[i].speed);

	*speed = max_speed;
	return 0;
}

static void get_speed_duplex(struct net_device *netdev,
			     u32 eth_proto_oper,
			     struct ethtool_link_ksettings *link_ksettings)
{
	int i;
	u32 speed = SPEED_UNKNOWN;
	u8 duplex = DUPLEX_UNKNOWN;

	if (!netif_carrier_ok(netdev))
		goto out;

	for (i = 0; i < MLX5E_LINK_MODES_NUMBER; ++i) {
		if (eth_proto_oper & MLX5E_PROT_MASK(i)) {
			speed = ptys2ethtool_table[i].speed;
			duplex = DUPLEX_FULL;
			break;
		}
	}
out:
	link_ksettings->base.speed = speed;
	link_ksettings->base.duplex = duplex;
}

static void get_supported(u32 eth_proto_cap,
			  struct ethtool_link_ksettings *link_ksettings)
{
	unsigned long *supported = link_ksettings->link_modes.supported;

	ptys2ethtool_supported_port(link_ksettings, eth_proto_cap);
	ptys2ethtool_supported_link(supported, eth_proto_cap);
	ethtool_link_ksettings_add_link_mode(link_ksettings, supported, Pause);
	ethtool_link_ksettings_add_link_mode(link_ksettings, supported, Asym_Pause);
}

static void get_advertising(u32 eth_proto_cap, u8 tx_pause,
			    u8 rx_pause,
			    struct ethtool_link_ksettings *link_ksettings)
{
	unsigned long *advertising = link_ksettings->link_modes.advertising;

	ptys2ethtool_adver_link(advertising, eth_proto_cap);
	if (tx_pause)
		ethtool_link_ksettings_add_link_mode(link_ksettings, advertising, Pause);
	if (tx_pause ^ rx_pause)
		ethtool_link_ksettings_add_link_mode(link_ksettings, advertising, Asym_Pause);
}

static u8 get_connector_port(u32 eth_proto)
{
	if (eth_proto & (MLX5E_PROT_MASK(MLX5E_10GBASE_SR)
			 | MLX5E_PROT_MASK(MLX5E_40GBASE_SR4)
			 | MLX5E_PROT_MASK(MLX5E_100GBASE_SR4)
			 | MLX5E_PROT_MASK(MLX5E_1000BASE_CX_SGMII))) {
			return PORT_FIBRE;
	}

	if (eth_proto & (MLX5E_PROT_MASK(MLX5E_40GBASE_CR4)
			 | MLX5E_PROT_MASK(MLX5E_10GBASE_CR)
			 | MLX5E_PROT_MASK(MLX5E_100GBASE_CR4))) {
			return PORT_DA;
	}

	if (eth_proto & (MLX5E_PROT_MASK(MLX5E_10GBASE_KX4)
			 | MLX5E_PROT_MASK(MLX5E_10GBASE_KR)
			 | MLX5E_PROT_MASK(MLX5E_40GBASE_KR4)
			 | MLX5E_PROT_MASK(MLX5E_100GBASE_KR4))) {
			return PORT_NONE;
	}

	return PORT_OTHER;
}

static void get_lp_advertising(u32 eth_proto_lp,
			       struct ethtool_link_ksettings *link_ksettings)
{
	unsigned long *lp_advertising = link_ksettings->link_modes.lp_advertising;

	ptys2ethtool_adver_link(lp_advertising, eth_proto_lp);
}

static int mlx5e_get_link_ksettings(struct net_device *netdev,
				    struct ethtool_link_ksettings *link_ksettings)
{
	struct mlx5e_priv *priv    = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 out[MLX5_ST_SZ_DW(ptys_reg)] = {0};
	u32 eth_proto_cap;
	u32 eth_proto_admin;
	u32 eth_proto_lp;
	u32 eth_proto_oper;
	u8 an_disable_admin;
	u8 an_status;
	int err;

	err = mlx5_query_port_ptys(mdev, out, sizeof(out), MLX5_PTYS_EN, 1);
	if (err) {
		netdev_err(netdev, "%s: query port ptys failed: %d\n",
			   __func__, err);
		goto err_query_ptys;
	}

	eth_proto_cap    = MLX5_GET(ptys_reg, out, eth_proto_capability);
	eth_proto_admin  = MLX5_GET(ptys_reg, out, eth_proto_admin);
	eth_proto_oper   = MLX5_GET(ptys_reg, out, eth_proto_oper);
	eth_proto_lp     = MLX5_GET(ptys_reg, out, eth_proto_lp_advertise);
	an_disable_admin = MLX5_GET(ptys_reg, out, an_disable_admin);
	an_status        = MLX5_GET(ptys_reg, out, an_status);

	ethtool_link_ksettings_zero_link_mode(link_ksettings, supported);
	ethtool_link_ksettings_zero_link_mode(link_ksettings, advertising);

	get_supported(eth_proto_cap, link_ksettings);
	get_advertising(eth_proto_admin, 0, 0, link_ksettings);
	get_speed_duplex(netdev, eth_proto_oper, link_ksettings);

	eth_proto_oper = eth_proto_oper ? eth_proto_oper : eth_proto_cap;

	link_ksettings->base.port = get_connector_port(eth_proto_oper);
	get_lp_advertising(eth_proto_lp, link_ksettings);

	if (an_status == MLX5_AN_COMPLETE)
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     lp_advertising, Autoneg);

	link_ksettings->base.autoneg = an_disable_admin ? AUTONEG_DISABLE :
							  AUTONEG_ENABLE;
	ethtool_link_ksettings_add_link_mode(link_ksettings, supported,
					     Autoneg);
	if (!an_disable_admin)
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, Autoneg);

err_query_ptys:
	return err;
}

static u32 mlx5e_ethtool2ptys_adver_link(const unsigned long *link_modes)
{
	u32 i, ptys_modes = 0;

	for (i = 0; i < MLX5E_LINK_MODES_NUMBER; ++i) {
		if (bitmap_intersects(ptys2ethtool_table[i].advertised,
				      link_modes,
				      __ETHTOOL_LINK_MODE_MASK_NBITS))
			ptys_modes |= MLX5E_PROT_MASK(i);
	}

	return ptys_modes;
}

static u32 mlx5e_ethtool2ptys_speed_link(u32 speed)
{
	u32 i, speed_links = 0;

	for (i = 0; i < MLX5E_LINK_MODES_NUMBER; ++i) {
		if (ptys2ethtool_table[i].speed == speed)
			speed_links |= MLX5E_PROT_MASK(i);
	}

	return speed_links;
}

static int mlx5e_set_link_ksettings(struct net_device *netdev,
				    const struct ethtool_link_ksettings *link_ksettings)
{
	struct mlx5e_priv *priv    = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 eth_proto_cap, eth_proto_admin;
	bool an_changes = false;
	u8 an_disable_admin;
	u8 an_disable_cap;
	bool an_disable;
	u32 link_modes;
	u8 an_status;
	u32 speed;
	int err;

	speed = link_ksettings->base.speed;

	link_modes = link_ksettings->base.autoneg == AUTONEG_ENABLE ?
		mlx5e_ethtool2ptys_adver_link(link_ksettings->link_modes.advertising) :
		mlx5e_ethtool2ptys_speed_link(speed);

	err = mlx5_query_port_proto_cap(mdev, &eth_proto_cap, MLX5_PTYS_EN);
	if (err) {
		netdev_err(netdev, "%s: query port eth proto cap failed: %d\n",
			   __func__, err);
		goto out;
	}

	link_modes = link_modes & eth_proto_cap;
	if (!link_modes) {
		netdev_err(netdev, "%s: Not supported link mode(s) requested",
			   __func__);
		err = -EINVAL;
		goto out;
	}

	err = mlx5_query_port_proto_admin(mdev, &eth_proto_admin, MLX5_PTYS_EN);
	if (err) {
		netdev_err(netdev, "%s: query port eth proto admin failed: %d\n",
			   __func__, err);
		goto out;
	}

	mlx5_query_port_autoneg(mdev, MLX5_PTYS_EN, &an_status,
				&an_disable_cap, &an_disable_admin);

	an_disable = link_ksettings->base.autoneg == AUTONEG_DISABLE;
	an_changes = ((!an_disable && an_disable_admin) ||
		      (an_disable && !an_disable_admin));

	if (!an_changes && link_modes == eth_proto_admin)
		goto out;

	mlx5_set_port_ptys(mdev, an_disable, link_modes, MLX5_PTYS_EN);
	mlx5_toggle_port_link(mdev);

out:
	return err;
}

static u32 mlx5e_get_rxfh_key_size(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return sizeof(priv->params.toeplitz_hash_key);
}

static u32 mlx5e_get_rxfh_indir_size(struct net_device *netdev)
{
	return MLX5E_INDIR_RQT_SIZE;
}

static int mlx5e_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			  u8 *hfunc)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	if (indir)
		memcpy(indir, priv->params.indirection_rqt,
		       sizeof(priv->params.indirection_rqt));

	if (key)
		memcpy(key, priv->params.toeplitz_hash_key,
		       sizeof(priv->params.toeplitz_hash_key));

	if (hfunc)
		*hfunc = priv->params.rss_hfunc;

	return 0;
}

static void mlx5e_modify_tirs_hash(struct mlx5e_priv *priv, void *in, int inlen)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	void *tirc = MLX5_ADDR_OF(modify_tir_in, in, ctx);
	int i;

	MLX5_SET(modify_tir_in, in, bitmask.hash, 1);
	mlx5e_build_tir_ctx_hash(tirc, priv);

	for (i = 0; i < MLX5E_NUM_INDIR_TIRS; i++)
		mlx5_core_modify_tir(mdev, priv->indir_tir[i].tirn, in, inlen);
}

static int mlx5e_set_rxfh(struct net_device *dev, const u32 *indir,
			  const u8 *key, const u8 hfunc)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int inlen = MLX5_ST_SZ_BYTES(modify_tir_in);
	void *in;

	if ((hfunc != ETH_RSS_HASH_NO_CHANGE) &&
	    (hfunc != ETH_RSS_HASH_XOR) &&
	    (hfunc != ETH_RSS_HASH_TOP))
		return -EINVAL;

	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	mutex_lock(&priv->state_lock);

	if (indir) {
		u32 rqtn = priv->indir_rqt.rqtn;

		memcpy(priv->params.indirection_rqt, indir,
		       sizeof(priv->params.indirection_rqt));
		mlx5e_redirect_rqt(priv, rqtn, MLX5E_INDIR_RQT_SIZE, 0);
	}

	if (key)
		memcpy(priv->params.toeplitz_hash_key, key,
		       sizeof(priv->params.toeplitz_hash_key));

	if (hfunc != ETH_RSS_HASH_NO_CHANGE)
		priv->params.rss_hfunc = hfunc;

	mlx5e_modify_tirs_hash(priv, in, inlen);

	mutex_unlock(&priv->state_lock);

	kvfree(in);

	return 0;
}

static int mlx5e_get_rxnfc(struct net_device *netdev,
			   struct ethtool_rxnfc *info, u32 *rule_locs)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err = 0;

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = priv->params.num_channels;
		break;
	case ETHTOOL_GRXCLSRLCNT:
		info->rule_cnt = priv->fs.ethtool.tot_num_rules;
		break;
	case ETHTOOL_GRXCLSRULE:
		err = mlx5e_ethtool_get_flow(priv, info, info->fs.location);
		break;
	case ETHTOOL_GRXCLSRLALL:
		err = mlx5e_ethtool_get_all_flows(priv, info, rule_locs);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int mlx5e_get_tunable(struct net_device *dev,
			     const struct ethtool_tunable *tuna,
			     void *data)
{
	const struct mlx5e_priv *priv = netdev_priv(dev);
	int err = 0;

	switch (tuna->id) {
	case ETHTOOL_TX_COPYBREAK:
		*(u32 *)data = priv->params.tx_max_inline;
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int mlx5e_set_tunable(struct net_device *dev,
			     const struct ethtool_tunable *tuna,
			     const void *data)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	bool was_opened;
	u32 val;
	int err = 0;

	switch (tuna->id) {
	case ETHTOOL_TX_COPYBREAK:
		val = *(u32 *)data;
		if (val > mlx5e_get_max_inline_cap(mdev)) {
			err = -EINVAL;
			break;
		}

		mutex_lock(&priv->state_lock);

		was_opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
		if (was_opened)
			mlx5e_close_locked(dev);

		priv->params.tx_max_inline = val;

		if (was_opened)
			err = mlx5e_open_locked(dev);

		mutex_unlock(&priv->state_lock);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static void mlx5e_get_pauseparam(struct net_device *netdev,
				 struct ethtool_pauseparam *pauseparam)
{
	struct mlx5e_priv *priv    = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	err = mlx5_query_port_pause(mdev, &pauseparam->rx_pause,
				    &pauseparam->tx_pause);
	if (err) {
		netdev_err(netdev, "%s: mlx5_query_port_pause failed:0x%x\n",
			   __func__, err);
	}
}

static int mlx5e_set_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pauseparam)
{
	struct mlx5e_priv *priv    = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	if (pauseparam->autoneg)
		return -EINVAL;

	err = mlx5_set_port_pause(mdev,
				  pauseparam->rx_pause ? 1 : 0,
				  pauseparam->tx_pause ? 1 : 0);
	if (err) {
		netdev_err(netdev, "%s: mlx5_set_port_pause failed:0x%x\n",
			   __func__, err);
	}

	return err;
}

static int mlx5e_get_ts_info(struct net_device *dev,
			     struct ethtool_ts_info *info)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int ret;

	ret = ethtool_op_get_ts_info(dev, info);
	if (ret)
		return ret;

	info->phc_index = priv->tstamp.ptp ?
			  ptp_clock_index(priv->tstamp.ptp) : -1;

	if (!MLX5_CAP_GEN(priv->mdev, device_frequency_khz))
		return 0;

	info->so_timestamping |= SOF_TIMESTAMPING_TX_HARDWARE |
				 SOF_TIMESTAMPING_RX_HARDWARE |
				 SOF_TIMESTAMPING_RAW_HARDWARE;

	info->tx_types = (BIT(1) << HWTSTAMP_TX_OFF) |
			 (BIT(1) << HWTSTAMP_TX_ON);

	info->rx_filters = (BIT(1) << HWTSTAMP_FILTER_NONE) |
			   (BIT(1) << HWTSTAMP_FILTER_ALL);

	return 0;
}

static __u32 mlx5e_get_wol_supported(struct mlx5_core_dev *mdev)
{
	__u32 ret = 0;

	if (MLX5_CAP_GEN(mdev, wol_g))
		ret |= WAKE_MAGIC;

	if (MLX5_CAP_GEN(mdev, wol_s))
		ret |= WAKE_MAGICSECURE;

	if (MLX5_CAP_GEN(mdev, wol_a))
		ret |= WAKE_ARP;

	if (MLX5_CAP_GEN(mdev, wol_b))
		ret |= WAKE_BCAST;

	if (MLX5_CAP_GEN(mdev, wol_m))
		ret |= WAKE_MCAST;

	if (MLX5_CAP_GEN(mdev, wol_u))
		ret |= WAKE_UCAST;

	if (MLX5_CAP_GEN(mdev, wol_p))
		ret |= WAKE_PHY;

	return ret;
}

static __u32 mlx5e_refomrat_wol_mode_mlx5_to_linux(u8 mode)
{
	__u32 ret = 0;

	if (mode & MLX5_WOL_MAGIC)
		ret |= WAKE_MAGIC;

	if (mode & MLX5_WOL_SECURED_MAGIC)
		ret |= WAKE_MAGICSECURE;

	if (mode & MLX5_WOL_ARP)
		ret |= WAKE_ARP;

	if (mode & MLX5_WOL_BROADCAST)
		ret |= WAKE_BCAST;

	if (mode & MLX5_WOL_MULTICAST)
		ret |= WAKE_MCAST;

	if (mode & MLX5_WOL_UNICAST)
		ret |= WAKE_UCAST;

	if (mode & MLX5_WOL_PHY_ACTIVITY)
		ret |= WAKE_PHY;

	return ret;
}

static u8 mlx5e_refomrat_wol_mode_linux_to_mlx5(__u32 mode)
{
	u8 ret = 0;

	if (mode & WAKE_MAGIC)
		ret |= MLX5_WOL_MAGIC;

	if (mode & WAKE_MAGICSECURE)
		ret |= MLX5_WOL_SECURED_MAGIC;

	if (mode & WAKE_ARP)
		ret |= MLX5_WOL_ARP;

	if (mode & WAKE_BCAST)
		ret |= MLX5_WOL_BROADCAST;

	if (mode & WAKE_MCAST)
		ret |= MLX5_WOL_MULTICAST;

	if (mode & WAKE_UCAST)
		ret |= MLX5_WOL_UNICAST;

	if (mode & WAKE_PHY)
		ret |= MLX5_WOL_PHY_ACTIVITY;

	return ret;
}

static void mlx5e_get_wol(struct net_device *netdev,
			  struct ethtool_wolinfo *wol)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 mlx5_wol_mode;
	int err;

	memset(wol, 0, sizeof(*wol));

	wol->supported = mlx5e_get_wol_supported(mdev);
	if (!wol->supported)
		return;

	err = mlx5_query_port_wol(mdev, &mlx5_wol_mode);
	if (err)
		return;

	wol->wolopts = mlx5e_refomrat_wol_mode_mlx5_to_linux(mlx5_wol_mode);
}

static int mlx5e_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	__u32 wol_supported = mlx5e_get_wol_supported(mdev);
	u32 mlx5_wol_mode;

	if (!wol_supported)
		return -ENOTSUPP;

	if (wol->wolopts & ~wol_supported)
		return -EINVAL;

	mlx5_wol_mode = mlx5e_refomrat_wol_mode_linux_to_mlx5(wol->wolopts);

	return mlx5_set_port_wol(mdev, mlx5_wol_mode);
}

static int mlx5e_set_phys_id(struct net_device *dev,
			     enum ethtool_phys_id_state state)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u16 beacon_duration;

	if (!MLX5_CAP_GEN(mdev, beacon_led))
		return -EOPNOTSUPP;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		beacon_duration = MLX5_BEACON_DURATION_INF;
		break;
	case ETHTOOL_ID_INACTIVE:
		beacon_duration = MLX5_BEACON_DURATION_OFF;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return mlx5_set_port_beacon(mdev, beacon_duration);
}

static int mlx5e_get_module_info(struct net_device *netdev,
				 struct ethtool_modinfo *modinfo)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *dev = priv->mdev;
	int size_read = 0;
	u8 data[4];

	size_read = mlx5_query_module_eeprom(dev, 0, 2, data);
	if (size_read < 2)
		return -EIO;

	/* data[0] = identifier byte */
	switch (data[0]) {
	case MLX5_MODULE_ID_QSFP:
		modinfo->type       = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		break;
	case MLX5_MODULE_ID_QSFP_PLUS:
	case MLX5_MODULE_ID_QSFP28:
		/* data[1] = revision id */
		if (data[0] == MLX5_MODULE_ID_QSFP28 || data[1] >= 0x3) {
			modinfo->type       = ETH_MODULE_SFF_8636;
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		} else {
			modinfo->type       = ETH_MODULE_SFF_8436;
			modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		}
		break;
	case MLX5_MODULE_ID_SFP:
		modinfo->type       = ETH_MODULE_SFF_8472;
		modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		break;
	default:
		netdev_err(priv->netdev, "%s: cable type not recognized:0x%x\n",
			   __func__, data[0]);
		return -EINVAL;
	}

	return 0;
}

static int mlx5e_get_module_eeprom(struct net_device *netdev,
				   struct ethtool_eeprom *ee,
				   u8 *data)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int offset = ee->offset;
	int size_read;
	int i = 0;

	if (!ee->len)
		return -EINVAL;

	memset(data, 0, ee->len);

	while (i < ee->len) {
		size_read = mlx5_query_module_eeprom(mdev, offset, ee->len - i,
						     data + i);

		if (!size_read)
			/* Done reading */
			return 0;

		if (size_read < 0) {
			netdev_err(priv->netdev, "%s: mlx5_query_eeprom failed:0x%x\n",
				   __func__, size_read);
			return 0;
		}

		i += size_read;
		offset += size_read;
	}

	return 0;
}

typedef int (*mlx5e_pflag_handler)(struct net_device *netdev, bool enable);

static int set_pflag_rx_cqe_based_moder(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	bool rx_mode_changed;
	u8 rx_cq_period_mode;
	int err = 0;
	bool reset;

	rx_cq_period_mode = enable ?
		MLX5_CQ_PERIOD_MODE_START_FROM_CQE :
		MLX5_CQ_PERIOD_MODE_START_FROM_EQE;
	rx_mode_changed = rx_cq_period_mode != priv->params.rx_cq_period_mode;

	if (rx_cq_period_mode == MLX5_CQ_PERIOD_MODE_START_FROM_CQE &&
	    !MLX5_CAP_GEN(mdev, cq_period_start_from_cqe))
		return -ENOTSUPP;

	if (!rx_mode_changed)
		return 0;

	reset = test_bit(MLX5E_STATE_OPENED, &priv->state);
	if (reset)
		mlx5e_close_locked(netdev);

	mlx5e_set_rx_cq_mode_params(&priv->params, rx_cq_period_mode);

	if (reset)
		err = mlx5e_open_locked(netdev);

	return err;
}

static int mlx5e_handle_pflag(struct net_device *netdev,
			      u32 wanted_flags,
			      enum mlx5e_priv_flag flag,
			      mlx5e_pflag_handler pflag_handler)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	bool enable = !!(wanted_flags & flag);
	u32 changes = wanted_flags ^ priv->pflags;
	int err;

	if (!(changes & flag))
		return 0;

	err = pflag_handler(netdev, enable);
	if (err) {
		netdev_err(netdev, "%s private flag 0x%x failed err %d\n",
			   enable ? "Enable" : "Disable", flag, err);
		return err;
	}

	MLX5E_SET_PRIV_FLAG(priv, flag, enable);
	return 0;
}

static int mlx5e_set_priv_flags(struct net_device *netdev, u32 pflags)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	mutex_lock(&priv->state_lock);

	err = mlx5e_handle_pflag(netdev, pflags,
				 MLX5E_PFLAG_RX_CQE_BASED_MODER,
				 set_pflag_rx_cqe_based_moder);

	mutex_unlock(&priv->state_lock);
	return err ? -EINVAL : 0;
}

static u32 mlx5e_get_priv_flags(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return priv->pflags;
}

static int mlx5e_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	int err = 0;
	struct mlx5e_priv *priv = netdev_priv(dev);

	switch (cmd->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		err = mlx5e_ethtool_flow_replace(priv, &cmd->fs);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		err = mlx5e_ethtool_flow_remove(priv, cmd->fs.location);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

const struct ethtool_ops mlx5e_ethtool_ops = {
	.get_drvinfo       = mlx5e_get_drvinfo,
	.get_link          = ethtool_op_get_link,
	.get_strings       = mlx5e_get_strings,
	.get_sset_count    = mlx5e_get_sset_count,
	.get_ethtool_stats = mlx5e_get_ethtool_stats,
	.get_ringparam     = mlx5e_get_ringparam,
	.set_ringparam     = mlx5e_set_ringparam,
	.get_channels      = mlx5e_get_channels,
	.set_channels      = mlx5e_set_channels,
	.get_coalesce      = mlx5e_get_coalesce,
	.set_coalesce      = mlx5e_set_coalesce,
	.get_link_ksettings  = mlx5e_get_link_ksettings,
	.set_link_ksettings  = mlx5e_set_link_ksettings,
	.get_rxfh_key_size   = mlx5e_get_rxfh_key_size,
	.get_rxfh_indir_size = mlx5e_get_rxfh_indir_size,
	.get_rxfh          = mlx5e_get_rxfh,
	.set_rxfh          = mlx5e_set_rxfh,
	.get_rxnfc         = mlx5e_get_rxnfc,
	.set_rxnfc         = mlx5e_set_rxnfc,
	.get_tunable       = mlx5e_get_tunable,
	.set_tunable       = mlx5e_set_tunable,
	.get_pauseparam    = mlx5e_get_pauseparam,
	.set_pauseparam    = mlx5e_set_pauseparam,
	.get_ts_info       = mlx5e_get_ts_info,
	.set_phys_id       = mlx5e_set_phys_id,
	.get_wol	   = mlx5e_get_wol,
	.set_wol	   = mlx5e_set_wol,
	.get_module_info   = mlx5e_get_module_info,
	.get_module_eeprom = mlx5e_get_module_eeprom,
	.get_priv_flags    = mlx5e_get_priv_flags,
	.set_priv_flags    = mlx5e_set_priv_flags
};
