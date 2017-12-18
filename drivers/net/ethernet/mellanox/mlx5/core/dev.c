/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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

#include <linux/mlx5/driver.h>
#include "mlx5_core.h"

static LIST_HEAD(intf_list);
static LIST_HEAD(mlx5_dev_list);
/* intf dev list mutex */
static DEFINE_MUTEX(mlx5_intf_mutex);

struct mlx5_device_context {
	struct list_head	list;
	struct mlx5_interface  *intf;
	void		       *context;
	unsigned long		state;
};

enum {
	MLX5_INTERFACE_ADDED,
	MLX5_INTERFACE_ATTACHED,
};

void mlx5_add_device(struct mlx5_interface *intf, struct mlx5_priv *priv)
{
	struct mlx5_device_context *dev_ctx;
	struct mlx5_core_dev *dev = container_of(priv, struct mlx5_core_dev, priv);

	if (!mlx5_lag_intf_add(intf, priv))
		return;

	dev_ctx = kzalloc(sizeof(*dev_ctx), GFP_KERNEL);
	if (!dev_ctx)
		return;

	dev_ctx->intf = intf;
	dev_ctx->context = intf->add(dev);
	set_bit(MLX5_INTERFACE_ADDED, &dev_ctx->state);
	if (intf->attach)
		set_bit(MLX5_INTERFACE_ATTACHED, &dev_ctx->state);

	if (dev_ctx->context) {
		spin_lock_irq(&priv->ctx_lock);
		list_add_tail(&dev_ctx->list, &priv->ctx_list);
		spin_unlock_irq(&priv->ctx_lock);
	} else {
		kfree(dev_ctx);
	}
}

static struct mlx5_device_context *mlx5_get_device(struct mlx5_interface *intf,
						   struct mlx5_priv *priv)
{
	struct mlx5_device_context *dev_ctx;

	list_for_each_entry(dev_ctx, &priv->ctx_list, list)
		if (dev_ctx->intf == intf)
			return dev_ctx;
	return NULL;
}

void mlx5_remove_device(struct mlx5_interface *intf, struct mlx5_priv *priv)
{
	struct mlx5_device_context *dev_ctx;
	struct mlx5_core_dev *dev = container_of(priv, struct mlx5_core_dev, priv);

	dev_ctx = mlx5_get_device(intf, priv);
	if (!dev_ctx)
		return;

	spin_lock_irq(&priv->ctx_lock);
	list_del(&dev_ctx->list);
	spin_unlock_irq(&priv->ctx_lock);

	if (test_bit(MLX5_INTERFACE_ADDED, &dev_ctx->state))
		intf->remove(dev, dev_ctx->context);

	kfree(dev_ctx);
}

static void mlx5_attach_interface(struct mlx5_interface *intf, struct mlx5_priv *priv)
{
	struct mlx5_device_context *dev_ctx;
	struct mlx5_core_dev *dev = container_of(priv, struct mlx5_core_dev, priv);

	dev_ctx = mlx5_get_device(intf, priv);
	if (!dev_ctx)
		return;

	if (intf->attach) {
		if (test_bit(MLX5_INTERFACE_ATTACHED, &dev_ctx->state))
			return;
		intf->attach(dev, dev_ctx->context);
		set_bit(MLX5_INTERFACE_ATTACHED, &dev_ctx->state);
	} else {
		if (test_bit(MLX5_INTERFACE_ADDED, &dev_ctx->state))
			return;
		dev_ctx->context = intf->add(dev);
		set_bit(MLX5_INTERFACE_ADDED, &dev_ctx->state);
	}
}

void mlx5_attach_device(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	struct mlx5_interface *intf;

	mutex_lock(&mlx5_intf_mutex);
	list_for_each_entry(intf, &intf_list, list)
		mlx5_attach_interface(intf, priv);
	mutex_unlock(&mlx5_intf_mutex);
}

static void mlx5_detach_interface(struct mlx5_interface *intf, struct mlx5_priv *priv)
{
	struct mlx5_device_context *dev_ctx;
	struct mlx5_core_dev *dev = container_of(priv, struct mlx5_core_dev, priv);

	dev_ctx = mlx5_get_device(intf, priv);
	if (!dev_ctx)
		return;

	if (intf->detach) {
		if (!test_bit(MLX5_INTERFACE_ATTACHED, &dev_ctx->state))
			return;
		intf->detach(dev, dev_ctx->context);
		clear_bit(MLX5_INTERFACE_ATTACHED, &dev_ctx->state);
	} else {
		if (!test_bit(MLX5_INTERFACE_ADDED, &dev_ctx->state))
			return;
		intf->remove(dev, dev_ctx->context);
		clear_bit(MLX5_INTERFACE_ADDED, &dev_ctx->state);
	}
}

void mlx5_detach_device(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	struct mlx5_interface *intf;

	mutex_lock(&mlx5_intf_mutex);
	list_for_each_entry(intf, &intf_list, list)
		mlx5_detach_interface(intf, priv);
	mutex_unlock(&mlx5_intf_mutex);
}

bool mlx5_device_registered(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv;
	bool found = false;

	mutex_lock(&mlx5_intf_mutex);
	list_for_each_entry(priv, &mlx5_dev_list, dev_list)
		if (priv == &dev->priv)
			found = true;
	mutex_unlock(&mlx5_intf_mutex);

	return found;
}

int mlx5_register_device(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	struct mlx5_interface *intf;

	mutex_lock(&mlx5_intf_mutex);
	list_add_tail(&priv->dev_list, &mlx5_dev_list);
	list_for_each_entry(intf, &intf_list, list)
		mlx5_add_device(intf, priv);
	mutex_unlock(&mlx5_intf_mutex);

	return 0;
}

void mlx5_unregister_device(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	struct mlx5_interface *intf;

	mutex_lock(&mlx5_intf_mutex);
	list_for_each_entry(intf, &intf_list, list)
		mlx5_remove_device(intf, priv);
	list_del(&priv->dev_list);
	mutex_unlock(&mlx5_intf_mutex);
}

int mlx5_register_interface(struct mlx5_interface *intf)
{
	struct mlx5_priv *priv;

	if (!intf->add || !intf->remove)
		return -EINVAL;

	mutex_lock(&mlx5_intf_mutex);
	list_add_tail(&intf->list, &intf_list);
	list_for_each_entry(priv, &mlx5_dev_list, dev_list)
		mlx5_add_device(intf, priv);
	mutex_unlock(&mlx5_intf_mutex);

	return 0;
}
EXPORT_SYMBOL(mlx5_register_interface);

void mlx5_unregister_interface(struct mlx5_interface *intf)
{
	struct mlx5_priv *priv;

	mutex_lock(&mlx5_intf_mutex);
	list_for_each_entry(priv, &mlx5_dev_list, dev_list)
		mlx5_remove_device(intf, priv);
	list_del(&intf->list);
	mutex_unlock(&mlx5_intf_mutex);
}
EXPORT_SYMBOL(mlx5_unregister_interface);

void *mlx5_get_protocol_dev(struct mlx5_core_dev *mdev, int protocol)
{
	struct mlx5_priv *priv = &mdev->priv;
	struct mlx5_device_context *dev_ctx;
	unsigned long flags;
	void *result = NULL;

	spin_lock_irqsave(&priv->ctx_lock, flags);

	list_for_each_entry(dev_ctx, &mdev->priv.ctx_list, list)
		if ((dev_ctx->intf->protocol == protocol) &&
		    dev_ctx->intf->get_dev) {
			result = dev_ctx->intf->get_dev(dev_ctx->context);
			break;
		}

	spin_unlock_irqrestore(&priv->ctx_lock, flags);

	return result;
}
EXPORT_SYMBOL(mlx5_get_protocol_dev);

/* Must be called with intf_mutex held */
void mlx5_add_dev_by_protocol(struct mlx5_core_dev *dev, int protocol)
{
	struct mlx5_interface *intf;

	list_for_each_entry(intf, &intf_list, list)
		if (intf->protocol == protocol) {
			mlx5_add_device(intf, &dev->priv);
			break;
		}
}

/* Must be called with intf_mutex held */
void mlx5_remove_dev_by_protocol(struct mlx5_core_dev *dev, int protocol)
{
	struct mlx5_interface *intf;

	list_for_each_entry(intf, &intf_list, list)
		if (intf->protocol == protocol) {
			mlx5_remove_device(intf, &dev->priv);
			break;
		}
}

static u16 mlx5_gen_pci_id(struct mlx5_core_dev *dev)
{
	return (u16)((dev->pdev->bus->number << 8) |
		     PCI_SLOT(dev->pdev->devfn));
}

/* Must be called with intf_mutex held */
struct mlx5_core_dev *mlx5_get_next_phys_dev(struct mlx5_core_dev *dev)
{
	u16 pci_id = mlx5_gen_pci_id(dev);
	struct mlx5_core_dev *res = NULL;
	struct mlx5_core_dev *tmp_dev;
	struct mlx5_priv *priv;

	list_for_each_entry(priv, &mlx5_dev_list, dev_list) {
		tmp_dev = container_of(priv, struct mlx5_core_dev, priv);
		if ((dev != tmp_dev) && (mlx5_gen_pci_id(tmp_dev) == pci_id)) {
			res = tmp_dev;
			break;
		}
	}

	return res;
}

void mlx5_core_event(struct mlx5_core_dev *dev, enum mlx5_dev_event event,
		     unsigned long param)
{
	struct mlx5_priv *priv = &dev->priv;
	struct mlx5_device_context *dev_ctx;
	unsigned long flags;

	spin_lock_irqsave(&priv->ctx_lock, flags);

	list_for_each_entry(dev_ctx, &priv->ctx_list, list)
		if (dev_ctx->intf->event)
			dev_ctx->intf->event(dev, dev_ctx->context, event, param);

	spin_unlock_irqrestore(&priv->ctx_lock, flags);
}

void mlx5_dev_list_lock(void)
{
	mutex_lock(&mlx5_intf_mutex);
}

void mlx5_dev_list_unlock(void)
{
	mutex_unlock(&mlx5_intf_mutex);
}

int mlx5_dev_list_trylock(void)
{
	return mutex_trylock(&mlx5_intf_mutex);
}
