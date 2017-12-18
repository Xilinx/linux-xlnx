/*
 * Copyright (c) 2014, Mellanox Technologies inc.  All rights reserved.
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

#include <linux/pci.h>
#include <linux/mlx5/driver.h>
#include "mlx5_core.h"
#ifdef CONFIG_MLX5_CORE_EN
#include "eswitch.h"
#endif

bool mlx5_sriov_is_enabled(struct mlx5_core_dev *dev)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;

	return !!sriov->num_vfs;
}

static int mlx5_device_enable_sriov(struct mlx5_core_dev *dev, int num_vfs)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	int err;
	int vf;

	if (sriov->enabled_vfs) {
		mlx5_core_warn(dev,
			       "failed to enable SRIOV on device, already enabled with %d vfs\n",
			       sriov->enabled_vfs);
		return -EBUSY;
	}

#ifdef CONFIG_MLX5_CORE_EN
	err = mlx5_eswitch_enable_sriov(dev->priv.eswitch, num_vfs, SRIOV_LEGACY);
	if (err) {
		mlx5_core_warn(dev,
			       "failed to enable eswitch SRIOV (%d)\n", err);
		return err;
	}
#endif

	for (vf = 0; vf < num_vfs; vf++) {
		err = mlx5_core_enable_hca(dev, vf + 1);
		if (err) {
			mlx5_core_warn(dev, "failed to enable VF %d (%d)\n", vf, err);
			continue;
		}
		sriov->vfs_ctx[vf].enabled = 1;
		sriov->enabled_vfs++;
		mlx5_core_dbg(dev, "successfully enabled VF* %d\n", vf);

	}

	return 0;
}

static void mlx5_device_disable_sriov(struct mlx5_core_dev *dev)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	int err;
	int vf;

	if (!sriov->enabled_vfs)
		return;

	for (vf = 0; vf < sriov->num_vfs; vf++) {
		if (!sriov->vfs_ctx[vf].enabled)
			continue;
		err = mlx5_core_disable_hca(dev, vf + 1);
		if (err) {
			mlx5_core_warn(dev, "failed to disable VF %d\n", vf);
			continue;
		}
		sriov->vfs_ctx[vf].enabled = 0;
		sriov->enabled_vfs--;
	}

#ifdef CONFIG_MLX5_CORE_EN
	mlx5_eswitch_disable_sriov(dev->priv.eswitch);
#endif

	if (mlx5_wait_for_vf_pages(dev))
		mlx5_core_warn(dev, "timeout reclaiming VFs pages\n");
}

static int mlx5_pci_enable_sriov(struct pci_dev *pdev, int num_vfs)
{
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);
	int err = 0;

	if (pci_num_vf(pdev)) {
		mlx5_core_warn(dev, "Unable to enable pci sriov, already enabled\n");
		return -EBUSY;
	}

	err = pci_enable_sriov(pdev, num_vfs);
	if (err)
		mlx5_core_warn(dev, "pci_enable_sriov failed : %d\n", err);

	return err;
}

static void mlx5_pci_disable_sriov(struct pci_dev *pdev)
{
	pci_disable_sriov(pdev);
}

static int mlx5_sriov_enable(struct pci_dev *pdev, int num_vfs)
{
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	int err = 0;

	err = mlx5_device_enable_sriov(dev, num_vfs);
	if (err) {
		mlx5_core_warn(dev, "mlx5_device_enable_sriov failed : %d\n", err);
		return err;
	}

	err = mlx5_pci_enable_sriov(pdev, num_vfs);
	if (err) {
		mlx5_core_warn(dev, "mlx5_pci_enable_sriov failed : %d\n", err);
		mlx5_device_disable_sriov(dev);
		return err;
	}

	sriov->num_vfs = num_vfs;

	return 0;
}

static void mlx5_sriov_disable(struct pci_dev *pdev)
{
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;

	mlx5_pci_disable_sriov(pdev);
	mlx5_device_disable_sriov(dev);
	sriov->num_vfs = 0;
}

int mlx5_core_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);
	int err = 0;

	mlx5_core_dbg(dev, "requested num_vfs %d\n", num_vfs);
	if (!mlx5_core_is_pf(dev))
		return -EPERM;

	if (num_vfs && mlx5_lag_is_active(dev)) {
		mlx5_core_warn(dev, "can't turn sriov on while LAG is active");
		return -EINVAL;
	}

	if (num_vfs)
		err = mlx5_sriov_enable(pdev, num_vfs);
	else
		mlx5_sriov_disable(pdev);

	return err ? err : num_vfs;
}

int mlx5_sriov_attach(struct mlx5_core_dev *dev)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;

	if (!mlx5_core_is_pf(dev) || !sriov->num_vfs)
		return 0;

	/* If sriov VFs exist in PCI level, enable them in device level */
	return mlx5_device_enable_sriov(dev, sriov->num_vfs);
}

void mlx5_sriov_detach(struct mlx5_core_dev *dev)
{
	if (!mlx5_core_is_pf(dev))
		return;

	mlx5_device_disable_sriov(dev);
}

int mlx5_sriov_init(struct mlx5_core_dev *dev)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	struct pci_dev *pdev = dev->pdev;
	int total_vfs;

	if (!mlx5_core_is_pf(dev))
		return 0;

	total_vfs = pci_sriov_get_totalvfs(pdev);
	sriov->num_vfs = pci_num_vf(pdev);
	sriov->vfs_ctx = kcalloc(total_vfs, sizeof(*sriov->vfs_ctx), GFP_KERNEL);
	if (!sriov->vfs_ctx)
		return -ENOMEM;

	return 0;
}

void mlx5_sriov_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;

	if (!mlx5_core_is_pf(dev))
		return;

	kfree(sriov->vfs_ctx);
}
