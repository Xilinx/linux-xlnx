/*
 * Copyright (c) 2015 Oracle.  All rights reserved.
 */

/* rpcrdma.ko module initialization
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sunrpc/svc_rdma.h>
#include "xprt_rdma.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

MODULE_AUTHOR("Open Grid Computing and Network Appliance, Inc.");
MODULE_DESCRIPTION("RPC/RDMA Transport");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("svcrdma");
MODULE_ALIAS("xprtrdma");

static void __exit rpc_rdma_cleanup(void)
{
	xprt_rdma_cleanup();
	svc_rdma_cleanup();
}

static int __init rpc_rdma_init(void)
{
	int rc;

	rc = svc_rdma_init();
	if (rc)
		goto out;

	rc = xprt_rdma_init();
	if (rc)
		svc_rdma_cleanup();

out:
	return rc;
}

module_init(rpc_rdma_init);
module_exit(rpc_rdma_cleanup);
