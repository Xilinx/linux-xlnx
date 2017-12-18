/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/selftest/conctl.c
 *
 * IOC handle in kernel
 *
 * Author: Liang Zhen <liangzhen@clusterfs.com>
 */

#include "../../include/linux/libcfs/libcfs.h"
#include "../../include/linux/lnet/lib-lnet.h"
#include "../../include/linux/lnet/lnetst.h"
#include "console.h"

static int
lst_session_new_ioctl(lstio_session_new_args_t *args)
{
	char *name;
	int rc;

	if (!args->lstio_ses_idp ||	/* address for output sid */
	    !args->lstio_ses_key ||	/* no key is specified */
	    !args->lstio_ses_namep ||	/* session name */
	    args->lstio_ses_nmlen <= 0 ||
	    args->lstio_ses_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	LIBCFS_ALLOC(name, args->lstio_ses_nmlen + 1);
	if (!name)
		return -ENOMEM;

	if (copy_from_user(name, args->lstio_ses_namep,
			   args->lstio_ses_nmlen)) {
		LIBCFS_FREE(name, args->lstio_ses_nmlen + 1);
		return -EFAULT;
	}

	name[args->lstio_ses_nmlen] = 0;

	rc = lstcon_session_new(name,
				args->lstio_ses_key,
				args->lstio_ses_feats,
				args->lstio_ses_force,
				args->lstio_ses_timeout,
				args->lstio_ses_idp);

	LIBCFS_FREE(name, args->lstio_ses_nmlen + 1);
	return rc;
}

static int
lst_session_end_ioctl(lstio_session_end_args_t *args)
{
	if (args->lstio_ses_key != console_session.ses_key)
		return -EACCES;

	return lstcon_session_end();
}

static int
lst_session_info_ioctl(lstio_session_info_args_t *args)
{
	/* no checking of key */

	if (!args->lstio_ses_idp ||	/* address for output sid */
	    !args->lstio_ses_keyp ||	/* address for output key */
	    !args->lstio_ses_featp ||	/* address for output features */
	    !args->lstio_ses_ndinfo ||	/* address for output ndinfo */
	    !args->lstio_ses_namep ||	/* address for output name */
	    args->lstio_ses_nmlen <= 0 ||
	    args->lstio_ses_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	return lstcon_session_info(args->lstio_ses_idp,
				   args->lstio_ses_keyp,
				   args->lstio_ses_featp,
				   args->lstio_ses_ndinfo,
				   args->lstio_ses_namep,
				   args->lstio_ses_nmlen);
}

static int
lst_debug_ioctl(lstio_debug_args_t *args)
{
	char *name = NULL;
	int client = 1;
	int rc;

	if (args->lstio_dbg_key != console_session.ses_key)
		return -EACCES;

	if (!args->lstio_dbg_resultp)
		return -EINVAL;

	if (args->lstio_dbg_namep &&	/* name of batch/group */
	    (args->lstio_dbg_nmlen <= 0 ||
	     args->lstio_dbg_nmlen > LST_NAME_SIZE))
		return -EINVAL;

	if (args->lstio_dbg_namep) {
		LIBCFS_ALLOC(name, args->lstio_dbg_nmlen + 1);
		if (!name)
			return -ENOMEM;

		if (copy_from_user(name, args->lstio_dbg_namep,
				   args->lstio_dbg_nmlen)) {
			LIBCFS_FREE(name, args->lstio_dbg_nmlen + 1);

			return -EFAULT;
		}

		name[args->lstio_dbg_nmlen] = 0;
	}

	rc = -EINVAL;

	switch (args->lstio_dbg_type) {
	case LST_OPC_SESSION:
		rc = lstcon_session_debug(args->lstio_dbg_timeout,
					  args->lstio_dbg_resultp);
		break;

	case LST_OPC_BATCHSRV:
		client = 0;
	case LST_OPC_BATCHCLI:
		if (!name)
			goto out;

		rc = lstcon_batch_debug(args->lstio_dbg_timeout,
					name, client, args->lstio_dbg_resultp);
		break;

	case LST_OPC_GROUP:
		if (!name)
			goto out;

		rc = lstcon_group_debug(args->lstio_dbg_timeout,
					name, args->lstio_dbg_resultp);
		break;

	case LST_OPC_NODES:
		if (args->lstio_dbg_count <= 0 ||
		    !args->lstio_dbg_idsp)
			goto out;

		rc = lstcon_nodes_debug(args->lstio_dbg_timeout,
					args->lstio_dbg_count,
					args->lstio_dbg_idsp,
					args->lstio_dbg_resultp);
		break;

	default:
		break;
	}

out:
	if (name)
		LIBCFS_FREE(name, args->lstio_dbg_nmlen + 1);

	return rc;
}

static int
lst_group_add_ioctl(lstio_group_add_args_t *args)
{
	char *name;
	int rc;

	if (args->lstio_grp_key != console_session.ses_key)
		return -EACCES;

	if (!args->lstio_grp_namep ||
	    args->lstio_grp_nmlen <= 0 ||
	    args->lstio_grp_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	LIBCFS_ALLOC(name, args->lstio_grp_nmlen + 1);
	if (!name)
		return -ENOMEM;

	if (copy_from_user(name, args->lstio_grp_namep,
			   args->lstio_grp_nmlen)) {
		LIBCFS_FREE(name, args->lstio_grp_nmlen);
		return -EFAULT;
	}

	name[args->lstio_grp_nmlen] = 0;

	rc = lstcon_group_add(name);

	LIBCFS_FREE(name, args->lstio_grp_nmlen + 1);

	return rc;
}

static int
lst_group_del_ioctl(lstio_group_del_args_t *args)
{
	int rc;
	char *name;

	if (args->lstio_grp_key != console_session.ses_key)
		return -EACCES;

	if (!args->lstio_grp_namep ||
	    args->lstio_grp_nmlen <= 0 ||
	    args->lstio_grp_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	LIBCFS_ALLOC(name, args->lstio_grp_nmlen + 1);
	if (!name)
		return -ENOMEM;

	if (copy_from_user(name, args->lstio_grp_namep,
			   args->lstio_grp_nmlen)) {
		LIBCFS_FREE(name, args->lstio_grp_nmlen + 1);
		return -EFAULT;
	}

	name[args->lstio_grp_nmlen] = 0;

	rc = lstcon_group_del(name);

	LIBCFS_FREE(name, args->lstio_grp_nmlen + 1);

	return rc;
}

static int
lst_group_update_ioctl(lstio_group_update_args_t *args)
{
	int rc;
	char *name;

	if (args->lstio_grp_key != console_session.ses_key)
		return -EACCES;

	if (!args->lstio_grp_resultp ||
	    !args->lstio_grp_namep ||
	    args->lstio_grp_nmlen <= 0 ||
	    args->lstio_grp_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	LIBCFS_ALLOC(name, args->lstio_grp_nmlen + 1);
	if (!name)
		return -ENOMEM;

	if (copy_from_user(name, args->lstio_grp_namep,
			   args->lstio_grp_nmlen)) {
		LIBCFS_FREE(name, args->lstio_grp_nmlen + 1);
		return -EFAULT;
	}

	name[args->lstio_grp_nmlen] = 0;

	switch (args->lstio_grp_opc) {
	case LST_GROUP_CLEAN:
		rc = lstcon_group_clean(name, args->lstio_grp_args);
		break;

	case LST_GROUP_REFRESH:
		rc = lstcon_group_refresh(name, args->lstio_grp_resultp);
		break;

	case LST_GROUP_RMND:
		if (args->lstio_grp_count <= 0 ||
		    !args->lstio_grp_idsp) {
			rc = -EINVAL;
			break;
		}
		rc = lstcon_nodes_remove(name, args->lstio_grp_count,
					 args->lstio_grp_idsp,
					 args->lstio_grp_resultp);
		break;

	default:
		rc = -EINVAL;
		break;
	}

	LIBCFS_FREE(name, args->lstio_grp_nmlen + 1);

	return rc;
}

static int
lst_nodes_add_ioctl(lstio_group_nodes_args_t *args)
{
	unsigned feats;
	int rc;
	char *name;

	if (args->lstio_grp_key != console_session.ses_key)
		return -EACCES;

	if (!args->lstio_grp_idsp ||	/* array of ids */
	    args->lstio_grp_count <= 0 ||
	    !args->lstio_grp_resultp ||
	    !args->lstio_grp_featp ||
	    !args->lstio_grp_namep ||
	    args->lstio_grp_nmlen <= 0 ||
	    args->lstio_grp_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	LIBCFS_ALLOC(name, args->lstio_grp_nmlen + 1);
	if (!name)
		return -ENOMEM;

	if (copy_from_user(name, args->lstio_grp_namep,
			   args->lstio_grp_nmlen)) {
		LIBCFS_FREE(name, args->lstio_grp_nmlen + 1);

		return -EFAULT;
	}

	name[args->lstio_grp_nmlen] = 0;

	rc = lstcon_nodes_add(name, args->lstio_grp_count,
			      args->lstio_grp_idsp, &feats,
			      args->lstio_grp_resultp);

	LIBCFS_FREE(name, args->lstio_grp_nmlen + 1);
	if (!rc &&
	    copy_to_user(args->lstio_grp_featp, &feats, sizeof(feats))) {
		return -EINVAL;
	}

	return rc;
}

static int
lst_group_list_ioctl(lstio_group_list_args_t *args)
{
	if (args->lstio_grp_key != console_session.ses_key)
		return -EACCES;

	if (args->lstio_grp_idx < 0 ||
	    !args->lstio_grp_namep ||
	    args->lstio_grp_nmlen <= 0 ||
	    args->lstio_grp_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	return lstcon_group_list(args->lstio_grp_idx,
				 args->lstio_grp_nmlen,
				 args->lstio_grp_namep);
}

static int
lst_group_info_ioctl(lstio_group_info_args_t *args)
{
	char *name;
	int ndent;
	int index;
	int rc;

	if (args->lstio_grp_key != console_session.ses_key)
		return -EACCES;

	if (!args->lstio_grp_namep ||
	    args->lstio_grp_nmlen <= 0 ||
	    args->lstio_grp_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	if (!args->lstio_grp_entp &&	/* output: group entry */
	    !args->lstio_grp_dentsp)	/* output: node entry */
		return -EINVAL;

	if (args->lstio_grp_dentsp) {		/* have node entry */
		if (!args->lstio_grp_idxp ||	/* node index */
		    !args->lstio_grp_ndentp)	/* # of node entry */
			return -EINVAL;

		if (copy_from_user(&ndent, args->lstio_grp_ndentp,
				   sizeof(ndent)) ||
		    copy_from_user(&index, args->lstio_grp_idxp,
				   sizeof(index)))
			return -EFAULT;

		if (ndent <= 0 || index < 0)
			return -EINVAL;
	}

	LIBCFS_ALLOC(name, args->lstio_grp_nmlen + 1);
	if (!name)
		return -ENOMEM;

	if (copy_from_user(name, args->lstio_grp_namep,
			   args->lstio_grp_nmlen)) {
		LIBCFS_FREE(name, args->lstio_grp_nmlen + 1);
		return -EFAULT;
	}

	name[args->lstio_grp_nmlen] = 0;

	rc = lstcon_group_info(name, args->lstio_grp_entp,
			       &index, &ndent, args->lstio_grp_dentsp);

	LIBCFS_FREE(name, args->lstio_grp_nmlen + 1);

	if (rc)
		return rc;

	if (args->lstio_grp_dentsp &&
	    (copy_to_user(args->lstio_grp_idxp, &index, sizeof(index)) ||
	     copy_to_user(args->lstio_grp_ndentp, &ndent, sizeof(ndent))))
		return -EFAULT;

	return 0;
}

static int
lst_batch_add_ioctl(lstio_batch_add_args_t *args)
{
	int rc;
	char *name;

	if (args->lstio_bat_key != console_session.ses_key)
		return -EACCES;

	if (!args->lstio_bat_namep ||
	    args->lstio_bat_nmlen <= 0 ||
	    args->lstio_bat_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	LIBCFS_ALLOC(name, args->lstio_bat_nmlen + 1);
	if (!name)
		return -ENOMEM;

	if (copy_from_user(name, args->lstio_bat_namep,
			   args->lstio_bat_nmlen)) {
		LIBCFS_FREE(name, args->lstio_bat_nmlen + 1);
		return -EFAULT;
	}

	name[args->lstio_bat_nmlen] = 0;

	rc = lstcon_batch_add(name);

	LIBCFS_FREE(name, args->lstio_bat_nmlen + 1);

	return rc;
}

static int
lst_batch_run_ioctl(lstio_batch_run_args_t *args)
{
	int rc;
	char *name;

	if (args->lstio_bat_key != console_session.ses_key)
		return -EACCES;

	if (!args->lstio_bat_namep ||
	    args->lstio_bat_nmlen <= 0 ||
	    args->lstio_bat_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	LIBCFS_ALLOC(name, args->lstio_bat_nmlen + 1);
	if (!name)
		return -ENOMEM;

	if (copy_from_user(name, args->lstio_bat_namep,
			   args->lstio_bat_nmlen)) {
		LIBCFS_FREE(name, args->lstio_bat_nmlen + 1);
		return -EFAULT;
	}

	name[args->lstio_bat_nmlen] = 0;

	rc = lstcon_batch_run(name, args->lstio_bat_timeout,
			      args->lstio_bat_resultp);

	LIBCFS_FREE(name, args->lstio_bat_nmlen + 1);

	return rc;
}

static int
lst_batch_stop_ioctl(lstio_batch_stop_args_t *args)
{
	int rc;
	char *name;

	if (args->lstio_bat_key != console_session.ses_key)
		return -EACCES;

	if (!args->lstio_bat_resultp ||
	    !args->lstio_bat_namep ||
	    args->lstio_bat_nmlen <= 0 ||
	    args->lstio_bat_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	LIBCFS_ALLOC(name, args->lstio_bat_nmlen + 1);
	if (!name)
		return -ENOMEM;

	if (copy_from_user(name, args->lstio_bat_namep,
			   args->lstio_bat_nmlen)) {
		LIBCFS_FREE(name, args->lstio_bat_nmlen + 1);
		return -EFAULT;
	}

	name[args->lstio_bat_nmlen] = 0;

	rc = lstcon_batch_stop(name, args->lstio_bat_force,
			       args->lstio_bat_resultp);

	LIBCFS_FREE(name, args->lstio_bat_nmlen + 1);

	return rc;
}

static int
lst_batch_query_ioctl(lstio_batch_query_args_t *args)
{
	char *name;
	int rc;

	if (args->lstio_bat_key != console_session.ses_key)
		return -EACCES;

	if (!args->lstio_bat_resultp ||
	    !args->lstio_bat_namep ||
	    args->lstio_bat_nmlen <= 0 ||
	    args->lstio_bat_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	if (args->lstio_bat_testidx < 0)
		return -EINVAL;

	LIBCFS_ALLOC(name, args->lstio_bat_nmlen + 1);
	if (!name)
		return -ENOMEM;

	if (copy_from_user(name, args->lstio_bat_namep,
			   args->lstio_bat_nmlen)) {
		LIBCFS_FREE(name, args->lstio_bat_nmlen + 1);
		return -EFAULT;
	}

	name[args->lstio_bat_nmlen] = 0;

	rc = lstcon_test_batch_query(name,
				     args->lstio_bat_testidx,
				     args->lstio_bat_client,
				     args->lstio_bat_timeout,
				     args->lstio_bat_resultp);

	LIBCFS_FREE(name, args->lstio_bat_nmlen + 1);

	return rc;
}

static int
lst_batch_list_ioctl(lstio_batch_list_args_t *args)
{
	if (args->lstio_bat_key != console_session.ses_key)
		return -EACCES;

	if (args->lstio_bat_idx < 0 ||
	    !args->lstio_bat_namep ||
	    args->lstio_bat_nmlen <= 0 ||
	    args->lstio_bat_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	return lstcon_batch_list(args->lstio_bat_idx,
			      args->lstio_bat_nmlen,
			      args->lstio_bat_namep);
}

static int
lst_batch_info_ioctl(lstio_batch_info_args_t *args)
{
	char *name;
	int rc;
	int index;
	int ndent;

	if (args->lstio_bat_key != console_session.ses_key)
		return -EACCES;

	if (!args->lstio_bat_namep ||	/* batch name */
	    args->lstio_bat_nmlen <= 0 ||
	    args->lstio_bat_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	if (!args->lstio_bat_entp &&	/* output: batch entry */
	    !args->lstio_bat_dentsp)	/* output: node entry */
		return -EINVAL;

	if (args->lstio_bat_dentsp) {		/* have node entry */
		if (!args->lstio_bat_idxp ||	/* node index */
		    !args->lstio_bat_ndentp)	/* # of node entry */
			return -EINVAL;

		if (copy_from_user(&index, args->lstio_bat_idxp,
				   sizeof(index)) ||
		    copy_from_user(&ndent, args->lstio_bat_ndentp,
				   sizeof(ndent)))
			return -EFAULT;

		if (ndent <= 0 || index < 0)
			return -EINVAL;
	}

	LIBCFS_ALLOC(name, args->lstio_bat_nmlen + 1);
	if (!name)
		return -ENOMEM;

	if (copy_from_user(name, args->lstio_bat_namep,
			   args->lstio_bat_nmlen)) {
		LIBCFS_FREE(name, args->lstio_bat_nmlen + 1);
		return -EFAULT;
	}

	name[args->lstio_bat_nmlen] = 0;

	rc = lstcon_batch_info(name, args->lstio_bat_entp,
			       args->lstio_bat_server, args->lstio_bat_testidx,
			       &index, &ndent, args->lstio_bat_dentsp);

	LIBCFS_FREE(name, args->lstio_bat_nmlen + 1);

	if (rc)
		return rc;

	if (args->lstio_bat_dentsp &&
	    (copy_to_user(args->lstio_bat_idxp, &index, sizeof(index)) ||
	     copy_to_user(args->lstio_bat_ndentp, &ndent, sizeof(ndent))))
		rc = -EFAULT;

	return rc;
}

static int
lst_stat_query_ioctl(lstio_stat_args_t *args)
{
	int rc;
	char *name = NULL;

	/* TODO: not finished */
	if (args->lstio_sta_key != console_session.ses_key)
		return -EACCES;

	if (!args->lstio_sta_resultp)
		return -EINVAL;

	if (args->lstio_sta_idsp) {
		if (args->lstio_sta_count <= 0)
			return -EINVAL;

		rc = lstcon_nodes_stat(args->lstio_sta_count,
				       args->lstio_sta_idsp,
				       args->lstio_sta_timeout,
				       args->lstio_sta_resultp);
	} else if (args->lstio_sta_namep) {
		if (args->lstio_sta_nmlen <= 0 ||
		    args->lstio_sta_nmlen > LST_NAME_SIZE)
			return -EINVAL;

		LIBCFS_ALLOC(name, args->lstio_sta_nmlen + 1);
		if (!name)
			return -ENOMEM;

		rc = copy_from_user(name, args->lstio_sta_namep,
				    args->lstio_sta_nmlen);
		if (!rc)
			rc = lstcon_group_stat(name, args->lstio_sta_timeout,
					       args->lstio_sta_resultp);
		else
			rc = -EFAULT;
	} else {
		rc = -EINVAL;
	}

	if (name)
		LIBCFS_FREE(name, args->lstio_sta_nmlen + 1);
	return rc;
}

static int lst_test_add_ioctl(lstio_test_args_t *args)
{
	char *batch_name;
	char *src_name = NULL;
	char *dst_name = NULL;
	void *param = NULL;
	int ret = 0;
	int rc = -ENOMEM;

	if (!args->lstio_tes_resultp ||
	    !args->lstio_tes_retp ||
	    !args->lstio_tes_bat_name ||	/* no specified batch */
	    args->lstio_tes_bat_nmlen <= 0 ||
	    args->lstio_tes_bat_nmlen > LST_NAME_SIZE ||
	    !args->lstio_tes_sgrp_name ||	/* no source group */
	    args->lstio_tes_sgrp_nmlen <= 0 ||
	    args->lstio_tes_sgrp_nmlen > LST_NAME_SIZE ||
	    !args->lstio_tes_dgrp_name ||	/* no target group */
	    args->lstio_tes_dgrp_nmlen <= 0 ||
	    args->lstio_tes_dgrp_nmlen > LST_NAME_SIZE)
		return -EINVAL;

	if (!args->lstio_tes_loop ||		/* negative is infinite */
	    args->lstio_tes_concur <= 0 ||
	    args->lstio_tes_dist <= 0 ||
	    args->lstio_tes_span <= 0)
		return -EINVAL;

	/* have parameter, check if parameter length is valid */
	if (args->lstio_tes_param &&
	    (args->lstio_tes_param_len <= 0 ||
	     args->lstio_tes_param_len >
	     PAGE_SIZE - sizeof(struct lstcon_test)))
		return -EINVAL;

	LIBCFS_ALLOC(batch_name, args->lstio_tes_bat_nmlen + 1);
	if (!batch_name)
		return rc;

	LIBCFS_ALLOC(src_name, args->lstio_tes_sgrp_nmlen + 1);
	if (!src_name)
		goto out;

	LIBCFS_ALLOC(dst_name, args->lstio_tes_dgrp_nmlen + 1);
	if (!dst_name)
		goto out;

	if (args->lstio_tes_param) {
		LIBCFS_ALLOC(param, args->lstio_tes_param_len);
		if (!param)
			goto out;
		if (copy_from_user(param, args->lstio_tes_param,
				   args->lstio_tes_param_len)) {
			rc = -EFAULT;
			goto out;
		}
	}

	rc = -EFAULT;
	if (copy_from_user(batch_name, args->lstio_tes_bat_name,
			   args->lstio_tes_bat_nmlen) ||
	    copy_from_user(src_name, args->lstio_tes_sgrp_name,
			   args->lstio_tes_sgrp_nmlen) ||
	    copy_from_user(dst_name, args->lstio_tes_dgrp_name,
			   args->lstio_tes_dgrp_nmlen))
		goto out;

	rc = lstcon_test_add(batch_name, args->lstio_tes_type,
			     args->lstio_tes_loop, args->lstio_tes_concur,
			     args->lstio_tes_dist, args->lstio_tes_span,
			     src_name, dst_name, param,
			     args->lstio_tes_param_len,
			     &ret, args->lstio_tes_resultp);

	if (ret)
		rc = (copy_to_user(args->lstio_tes_retp, &ret,
				   sizeof(ret))) ? -EFAULT : 0;
out:
	if (batch_name)
		LIBCFS_FREE(batch_name, args->lstio_tes_bat_nmlen + 1);

	if (src_name)
		LIBCFS_FREE(src_name, args->lstio_tes_sgrp_nmlen + 1);

	if (dst_name)
		LIBCFS_FREE(dst_name, args->lstio_tes_dgrp_nmlen + 1);

	if (param)
		LIBCFS_FREE(param, args->lstio_tes_param_len);

	return rc;
}

int
lstcon_ioctl_entry(unsigned int cmd, struct libcfs_ioctl_hdr *hdr)
{
	char *buf;
	struct libcfs_ioctl_data *data;
	int opc;
	int rc;

	if (cmd != IOC_LIBCFS_LNETST)
		return -EINVAL;

	data = container_of(hdr, struct libcfs_ioctl_data, ioc_hdr);

	opc = data->ioc_u32[0];

	if (data->ioc_plen1 > PAGE_SIZE)
		return -EINVAL;

	LIBCFS_ALLOC(buf, data->ioc_plen1);
	if (!buf)
		return -ENOMEM;

	/* copy in parameter */
	if (copy_from_user(buf, data->ioc_pbuf1, data->ioc_plen1)) {
		LIBCFS_FREE(buf, data->ioc_plen1);
		return -EFAULT;
	}

	mutex_lock(&console_session.ses_mutex);

	console_session.ses_laststamp = ktime_get_real_seconds();

	if (console_session.ses_shutdown) {
		rc = -ESHUTDOWN;
		goto out;
	}

	if (console_session.ses_expired)
		lstcon_session_end();

	if (opc != LSTIO_SESSION_NEW &&
	    console_session.ses_state == LST_SESSION_NONE) {
		CDEBUG(D_NET, "LST no active session\n");
		rc = -ESRCH;
		goto out;
	}

	memset(&console_session.ses_trans_stat, 0, sizeof(lstcon_trans_stat_t));

	switch (opc) {
	case LSTIO_SESSION_NEW:
		rc = lst_session_new_ioctl((lstio_session_new_args_t *)buf);
		break;
	case LSTIO_SESSION_END:
		rc = lst_session_end_ioctl((lstio_session_end_args_t *)buf);
		break;
	case LSTIO_SESSION_INFO:
		rc = lst_session_info_ioctl((lstio_session_info_args_t *)buf);
		break;
	case LSTIO_DEBUG:
		rc = lst_debug_ioctl((lstio_debug_args_t *)buf);
		break;
	case LSTIO_GROUP_ADD:
		rc = lst_group_add_ioctl((lstio_group_add_args_t *)buf);
		break;
	case LSTIO_GROUP_DEL:
		rc = lst_group_del_ioctl((lstio_group_del_args_t *)buf);
		break;
	case LSTIO_GROUP_UPDATE:
		rc = lst_group_update_ioctl((lstio_group_update_args_t *)buf);
		break;
	case LSTIO_NODES_ADD:
		rc = lst_nodes_add_ioctl((lstio_group_nodes_args_t *)buf);
		break;
	case LSTIO_GROUP_LIST:
		rc = lst_group_list_ioctl((lstio_group_list_args_t *)buf);
		break;
	case LSTIO_GROUP_INFO:
		rc = lst_group_info_ioctl((lstio_group_info_args_t *)buf);
		break;
	case LSTIO_BATCH_ADD:
		rc = lst_batch_add_ioctl((lstio_batch_add_args_t *)buf);
		break;
	case LSTIO_BATCH_START:
		rc = lst_batch_run_ioctl((lstio_batch_run_args_t *)buf);
		break;
	case LSTIO_BATCH_STOP:
		rc = lst_batch_stop_ioctl((lstio_batch_stop_args_t *)buf);
		break;
	case LSTIO_BATCH_QUERY:
		rc = lst_batch_query_ioctl((lstio_batch_query_args_t *)buf);
		break;
	case LSTIO_BATCH_LIST:
		rc = lst_batch_list_ioctl((lstio_batch_list_args_t *)buf);
		break;
	case LSTIO_BATCH_INFO:
		rc = lst_batch_info_ioctl((lstio_batch_info_args_t *)buf);
		break;
	case LSTIO_TEST_ADD:
		rc = lst_test_add_ioctl((lstio_test_args_t *)buf);
		break;
	case LSTIO_STAT_QUERY:
		rc = lst_stat_query_ioctl((lstio_stat_args_t *)buf);
		break;
	default:
		rc = -EINVAL;
	}

	if (copy_to_user(data->ioc_pbuf2, &console_session.ses_trans_stat,
			 sizeof(lstcon_trans_stat_t)))
		rc = -EFAULT;
out:
	mutex_unlock(&console_session.ses_mutex);

	LIBCFS_FREE(buf, data->ioc_plen1);

	return rc;
}
