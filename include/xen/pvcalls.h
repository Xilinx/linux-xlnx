#ifndef __LINUX_NET_PVCALLS_H
#define __LINUX_NET_PVCALLS_H

#include <linux/net.h>

#ifdef CONFIG_XEN_PVCALLS_FRONTEND
extern bool pvcalls;
#else
#define pvcalls (0)
#endif
extern const struct proto_ops pvcalls_stream_ops;

#endif
