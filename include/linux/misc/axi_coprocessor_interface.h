#ifndef AXI_COPROCESSOR_INTERFACE_H
#define AXI_COPROCESSOR_INTERFACE_H

struct interface_ioctl_data{
	unsigned int register_offset;
	unsigned int register_value;
};

/* registers */
#define INTERFACE_REGISTER_SPACE		0x1000
#define INTERFACE_CONTROL_REGISTER		0x00
#define INTERFACE_STATUS_REGISTER 		0x04 
#define INTERFACE_FIFO_IN_REGISTER		0x08
#define INTERFACE_FIFO_OUT_REGISTER		0x0C

#define INTERFACE_LATCH_FIFO_OUT_DATA_PIN	1 << 0

#define INTERFACE_FIFO_IN_EMPTY_MASK		1 << 0
#define INTERFACE_FIFO_IN_HALF_FULL_MASK	1 << 1
#define INTERFACE_FIFO_IN_FULL_MASK		1 << 2
#define INTERFACE_FIFO_OUT_EMPTY_MASK		1 << 3
#define INTERFACE_FIFO_OUT_HALF_FULL_MASK	1 << 4
#define INTERFACE_FIFO_OUT_FULL_MASK		1 << 5


/* ioctls */
#define INTERFACE_IOCTL_MAGIC			'I'
#define INTERFACE_SET_REGISTER  		_IOW(INTERFACE_IOCTL_MAGIC, 1, struct interface_ioctl_data *)
#define INTERFACE_GET_REGISTER 			_IOR(INTERFACE_IOCTL_MAGIC, 2, struct interface_ioctl_data *)

/* misc */
#define INTERFACE_MAJOR_NUMBER			95
#define INTERFACE_FILE_OPENED			1
#define INTERFACE_FILE_CLOSED			0

#endif 
