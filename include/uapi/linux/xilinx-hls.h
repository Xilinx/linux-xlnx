#ifndef __UAPI_XILINX_HLS_H__
#define __UAPI_XILINX_HLS_H__

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/videodev2.h>

struct xilinx_axi_hls_register {
	__u32 offset;
	__u32 value;
};

struct xilinx_axi_hls_registers {
	__u32 num_regs;
	struct xilinx_axi_hls_register __user *regs;
};

#define XILINX_AXI_HLS_READ	_IOWR('V', BASE_VIDIOC_PRIVATE+0, struct xilinx_axi_hls_registers)
#define XILINX_AXI_HLS_WRITE	_IOW('V', BASE_VIDIOC_PRIVATE+1, struct xilinx_axi_hls_registers)

#endif /* __UAPI_XILINX_HLS_H__ */
