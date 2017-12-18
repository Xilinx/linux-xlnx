/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 *
 */
#include "scif_main.h"

static int scif_fdopen(struct inode *inode, struct file *f)
{
	struct scif_endpt *priv = scif_open();

	if (!priv)
		return -ENOMEM;
	f->private_data = priv;
	return 0;
}

static int scif_fdclose(struct inode *inode, struct file *f)
{
	struct scif_endpt *priv = f->private_data;

	return scif_close(priv);
}

static int scif_fdmmap(struct file *f, struct vm_area_struct *vma)
{
	struct scif_endpt *priv = f->private_data;

	return scif_mmap(vma, priv);
}

static unsigned int scif_fdpoll(struct file *f, poll_table *wait)
{
	struct scif_endpt *priv = f->private_data;

	return __scif_pollfd(f, wait, priv);
}

static int scif_fdflush(struct file *f, fl_owner_t id)
{
	struct scif_endpt *ep = f->private_data;

	spin_lock(&ep->lock);
	/*
	 * The listening endpoint stashes the open file information before
	 * waiting for incoming connections. The release callback would never be
	 * called if the application closed the endpoint, while waiting for
	 * incoming connections from a separate thread since the file descriptor
	 * reference count is bumped up in the accept IOCTL. Call the flush
	 * routine if the id matches the endpoint open file information so that
	 * the listening endpoint can be woken up and the fd released.
	 */
	if (ep->files == id)
		__scif_flush(ep);
	spin_unlock(&ep->lock);
	return 0;
}

static __always_inline void scif_err_debug(int err, const char *str)
{
	/*
	 * ENOTCONN is a common uninteresting error which is
	 * flooding debug messages to the console unnecessarily.
	 */
	if (err < 0 && err != -ENOTCONN)
		dev_dbg(scif_info.mdev.this_device, "%s err %d\n", str, err);
}

static long scif_fdioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct scif_endpt *priv = f->private_data;
	void __user *argp = (void __user *)arg;
	int err = 0;
	struct scifioctl_msg request;
	bool non_block = false;

	non_block = !!(f->f_flags & O_NONBLOCK);

	switch (cmd) {
	case SCIF_BIND:
	{
		int pn;

		if (copy_from_user(&pn, argp, sizeof(pn)))
			return -EFAULT;

		pn = scif_bind(priv, pn);
		if (pn < 0)
			return pn;

		if (copy_to_user(argp, &pn, sizeof(pn)))
			return -EFAULT;

		return 0;
	}
	case SCIF_LISTEN:
		return scif_listen(priv, arg);
	case SCIF_CONNECT:
	{
		struct scifioctl_connect req;
		struct scif_endpt *ep = (struct scif_endpt *)priv;

		if (copy_from_user(&req, argp, sizeof(req)))
			return -EFAULT;

		err = __scif_connect(priv, &req.peer, non_block);
		if (err < 0)
			return err;

		req.self.node = ep->port.node;
		req.self.port = ep->port.port;

		if (copy_to_user(argp, &req, sizeof(req)))
			return -EFAULT;

		return 0;
	}
	/*
	 * Accept is done in two halves.  The request ioctl does the basic
	 * functionality of accepting the request and returning the information
	 * about it including the internal ID of the end point.  The register
	 * is done with the internal ID on a new file descriptor opened by the
	 * requesting process.
	 */
	case SCIF_ACCEPTREQ:
	{
		struct scifioctl_accept request;
		scif_epd_t *ep = (scif_epd_t *)&request.endpt;

		if (copy_from_user(&request, argp, sizeof(request)))
			return -EFAULT;

		err = scif_accept(priv, &request.peer, ep, request.flags);
		if (err < 0)
			return err;

		if (copy_to_user(argp, &request, sizeof(request))) {
			scif_close(*ep);
			return -EFAULT;
		}
		/*
		 * Add to the list of user mode eps where the second half
		 * of the accept is not yet completed.
		 */
		mutex_lock(&scif_info.eplock);
		list_add_tail(&((*ep)->miacceptlist), &scif_info.uaccept);
		list_add_tail(&((*ep)->liacceptlist), &priv->li_accept);
		(*ep)->listenep = priv;
		priv->acceptcnt++;
		mutex_unlock(&scif_info.eplock);

		return 0;
	}
	case SCIF_ACCEPTREG:
	{
		struct scif_endpt *priv = f->private_data;
		struct scif_endpt *newep;
		struct scif_endpt *lisep;
		struct scif_endpt *fep = NULL;
		struct scif_endpt *tmpep;
		struct list_head *pos, *tmpq;

		/* Finally replace the pointer to the accepted endpoint */
		if (copy_from_user(&newep, argp, sizeof(void *)))
			return -EFAULT;

		/* Remove form the user accept queue */
		mutex_lock(&scif_info.eplock);
		list_for_each_safe(pos, tmpq, &scif_info.uaccept) {
			tmpep = list_entry(pos,
					   struct scif_endpt, miacceptlist);
			if (tmpep == newep) {
				list_del(pos);
				fep = tmpep;
				break;
			}
		}

		if (!fep) {
			mutex_unlock(&scif_info.eplock);
			return -ENOENT;
		}

		lisep = newep->listenep;
		list_for_each_safe(pos, tmpq, &lisep->li_accept) {
			tmpep = list_entry(pos,
					   struct scif_endpt, liacceptlist);
			if (tmpep == newep) {
				list_del(pos);
				lisep->acceptcnt--;
				break;
			}
		}

		mutex_unlock(&scif_info.eplock);

		/* Free the resources automatically created from the open. */
		scif_anon_inode_fput(priv);
		scif_teardown_ep(priv);
		scif_add_epd_to_zombie_list(priv, !SCIF_EPLOCK_HELD);
		f->private_data = newep;
		return 0;
	}
	case SCIF_SEND:
	{
		struct scif_endpt *priv = f->private_data;

		if (copy_from_user(&request, argp,
				   sizeof(struct scifioctl_msg))) {
			err = -EFAULT;
			goto send_err;
		}
		err = scif_user_send(priv, (void __user *)request.msg,
				     request.len, request.flags);
		if (err < 0)
			goto send_err;
		if (copy_to_user(&
				 ((struct scifioctl_msg __user *)argp)->out_len,
				 &err, sizeof(err))) {
			err = -EFAULT;
			goto send_err;
		}
		err = 0;
send_err:
		scif_err_debug(err, "scif_send");
		return err;
	}
	case SCIF_RECV:
	{
		struct scif_endpt *priv = f->private_data;

		if (copy_from_user(&request, argp,
				   sizeof(struct scifioctl_msg))) {
			err = -EFAULT;
			goto recv_err;
		}

		err = scif_user_recv(priv, (void __user *)request.msg,
				     request.len, request.flags);
		if (err < 0)
			goto recv_err;

		if (copy_to_user(&
				 ((struct scifioctl_msg __user *)argp)->out_len,
			&err, sizeof(err))) {
			err = -EFAULT;
			goto recv_err;
		}
		err = 0;
recv_err:
		scif_err_debug(err, "scif_recv");
		return err;
	}
	case SCIF_GET_NODEIDS:
	{
		struct scifioctl_node_ids node_ids;
		int entries;
		u16 *nodes;
		void __user *unodes, *uself;
		u16 self;

		if (copy_from_user(&node_ids, argp, sizeof(node_ids))) {
			err = -EFAULT;
			goto getnodes_err2;
		}

		entries = min_t(int, scif_info.maxid, node_ids.len);
		nodes = kmalloc_array(entries, sizeof(u16), GFP_KERNEL);
		if (entries && !nodes) {
			err = -ENOMEM;
			goto getnodes_err2;
		}
		node_ids.len = scif_get_node_ids(nodes, entries, &self);

		unodes = (void __user *)node_ids.nodes;
		if (copy_to_user(unodes, nodes, sizeof(u16) * entries)) {
			err = -EFAULT;
			goto getnodes_err1;
		}

		uself = (void __user *)node_ids.self;
		if (copy_to_user(uself, &self, sizeof(u16))) {
			err = -EFAULT;
			goto getnodes_err1;
		}

		if (copy_to_user(argp, &node_ids, sizeof(node_ids))) {
			err = -EFAULT;
			goto getnodes_err1;
		}
getnodes_err1:
		kfree(nodes);
getnodes_err2:
		return err;
	}
	case SCIF_REG:
	{
		struct scif_endpt *priv = f->private_data;
		struct scifioctl_reg reg;
		off_t ret;

		if (copy_from_user(&reg, argp, sizeof(reg))) {
			err = -EFAULT;
			goto reg_err;
		}
		if (reg.flags & SCIF_MAP_KERNEL) {
			err = -EINVAL;
			goto reg_err;
		}
		ret = scif_register(priv, (void *)reg.addr, reg.len,
				    reg.offset, reg.prot, reg.flags);
		if (ret < 0) {
			err = (int)ret;
			goto reg_err;
		}

		if (copy_to_user(&((struct scifioctl_reg __user *)argp)
				 ->out_offset, &ret, sizeof(reg.out_offset))) {
			err = -EFAULT;
			goto reg_err;
		}
		err = 0;
reg_err:
		scif_err_debug(err, "scif_register");
		return err;
	}
	case SCIF_UNREG:
	{
		struct scif_endpt *priv = f->private_data;
		struct scifioctl_unreg unreg;

		if (copy_from_user(&unreg, argp, sizeof(unreg))) {
			err = -EFAULT;
			goto unreg_err;
		}
		err = scif_unregister(priv, unreg.offset, unreg.len);
unreg_err:
		scif_err_debug(err, "scif_unregister");
		return err;
	}
	case SCIF_READFROM:
	{
		struct scif_endpt *priv = f->private_data;
		struct scifioctl_copy copy;

		if (copy_from_user(&copy, argp, sizeof(copy))) {
			err = -EFAULT;
			goto readfrom_err;
		}
		err = scif_readfrom(priv, copy.loffset, copy.len, copy.roffset,
				    copy.flags);
readfrom_err:
		scif_err_debug(err, "scif_readfrom");
		return err;
	}
	case SCIF_WRITETO:
	{
		struct scif_endpt *priv = f->private_data;
		struct scifioctl_copy copy;

		if (copy_from_user(&copy, argp, sizeof(copy))) {
			err = -EFAULT;
			goto writeto_err;
		}
		err = scif_writeto(priv, copy.loffset, copy.len, copy.roffset,
				   copy.flags);
writeto_err:
		scif_err_debug(err, "scif_writeto");
		return err;
	}
	case SCIF_VREADFROM:
	{
		struct scif_endpt *priv = f->private_data;
		struct scifioctl_copy copy;

		if (copy_from_user(&copy, argp, sizeof(copy))) {
			err = -EFAULT;
			goto vreadfrom_err;
		}
		err = scif_vreadfrom(priv, (void __force *)copy.addr, copy.len,
				     copy.roffset, copy.flags);
vreadfrom_err:
		scif_err_debug(err, "scif_vreadfrom");
		return err;
	}
	case SCIF_VWRITETO:
	{
		struct scif_endpt *priv = f->private_data;
		struct scifioctl_copy copy;

		if (copy_from_user(&copy, argp, sizeof(copy))) {
			err = -EFAULT;
			goto vwriteto_err;
		}
		err = scif_vwriteto(priv, (void __force *)copy.addr, copy.len,
				    copy.roffset, copy.flags);
vwriteto_err:
		scif_err_debug(err, "scif_vwriteto");
		return err;
	}
	case SCIF_FENCE_MARK:
	{
		struct scif_endpt *priv = f->private_data;
		struct scifioctl_fence_mark mark;
		int tmp_mark = 0;

		if (copy_from_user(&mark, argp, sizeof(mark))) {
			err = -EFAULT;
			goto fence_mark_err;
		}
		err = scif_fence_mark(priv, mark.flags, &tmp_mark);
		if (err)
			goto fence_mark_err;
		if (copy_to_user((void __user *)mark.mark, &tmp_mark,
				 sizeof(tmp_mark))) {
			err = -EFAULT;
			goto fence_mark_err;
		}
fence_mark_err:
		scif_err_debug(err, "scif_fence_mark");
		return err;
	}
	case SCIF_FENCE_WAIT:
	{
		struct scif_endpt *priv = f->private_data;

		err = scif_fence_wait(priv, arg);
		scif_err_debug(err, "scif_fence_wait");
		return err;
	}
	case SCIF_FENCE_SIGNAL:
	{
		struct scif_endpt *priv = f->private_data;
		struct scifioctl_fence_signal signal;

		if (copy_from_user(&signal, argp, sizeof(signal))) {
			err = -EFAULT;
			goto fence_signal_err;
		}

		err = scif_fence_signal(priv, signal.loff, signal.lval,
					signal.roff, signal.rval, signal.flags);
fence_signal_err:
		scif_err_debug(err, "scif_fence_signal");
		return err;
	}
	}
	return -EINVAL;
}

const struct file_operations scif_fops = {
	.open = scif_fdopen,
	.release = scif_fdclose,
	.unlocked_ioctl = scif_fdioctl,
	.mmap = scif_fdmmap,
	.poll = scif_fdpoll,
	.flush = scif_fdflush,
	.owner = THIS_MODULE,
};
