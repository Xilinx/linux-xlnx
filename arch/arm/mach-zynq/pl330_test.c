#include <linux/init.h>
#include <linux/module.h>

#include <linux/platform_device.h>

#include <linux/dma-mapping.h>
#include <linux/dmapool.h>

#include <asm/sizes.h>

#include <asm/dma.h>
#include <mach/pl330.h>

/*
 * This is a test module for pl330 linux driver.
 *
 * There are a couple ways to run this test.
 *
 * One is to compile it as a loadable module, then run insmod pl330_test.ko.
 *
 * You also need to register a pl330_test device.
 *
 * The other way is to make it as part of the kernel and compile with the
 * kernel.
 *
 * Here are the steps:
 *
 * 1. Change the Makefile inside arch/arm/mach-xilinx/Makefile, and add
 * pl330_test.o the obj-y
 * 2. Add the following declaration to arch/arm/mach-xilinx/devices.c
 * static struct platform_device xilinx_dma_test = {
 * 	.name = "pl330_test",
 * 	.id = 0,
 * 	.dev = {
 *		.platform_data = NULL,
 * 		.dma_mask = &dma_mask,
 * 		.coherent_dma_mask = 0xFFFFFFFF,
 *	},
 *	.resource = NULL,
 * 	.num_resources = 0,
 * };
 * 3. Add the following line to struct platform_device *xilinx_pdevices[]
 * declaration:
 * 	&xilinx_dma_test,
 *
 * 4. Make the zImage
 *
 * Either way, a device named "pl330_test" must be registered first.
 *
 * The test has 8 test suites. To run individual suite, you need to change
 * the suite_num in the pl330_test.c to the suite you want to run, or you can
 * pass the suite_num parameter when you insmod.
 *
 * Each test suite has many test cases. To run a particular test case, you need
 * to modify the test_id in the pl330_test.c to the test case id you want to
 * run.
 *
 * Or you can pass the test_id parameter when you insmod.
 *
 * By default, all tests will be run.
 *
 */

#define DRIVER_NAME         "pl330_test"

/*
#define PL330_TEST_DEBUG
*/
#undef PDBG
#ifdef PL330_TEST_DEBUG
#	define PDBG(fmt, args...) printk(KERN_INFO fmt, ## args)
#else
#	define PDBG(fmt, args...)
#endif

#undef PINFO
#define PINFO(fmt, args...) printk(KERN_INFO fmt, ## args)

#define TEST_MAX_CHANNELS	8

static const char *PASS = "PASS";
static const char *FAIL = "FAIL";

static struct device *test_device;

/*
 * if suite_number is zero, all tests will be run.
 */
static int suite_num;
static int test_id = -1;
static int disp_dma_prog;

module_param(suite_num, int, S_IRUGO);
module_param(test_id, int, S_IRUGO);

static int tests_run;
static int tests_failed;
static int tests_passed;

#define dev_write8(data, addr)
#define dev_write16(data, addr)
#define dev_write32(data, addr)
#define dev_write64(data, addr)
#define dev_read8(addr) (0)
#define dev_read16(addr) (0)
#define dev_read32(addr) (0)
#define dev_read64(addr) (0)

#define MAX_FAILED_TESTS	128

/**
 * struct suite_case_pair - This defines a test case suite pair
 * @test_suite:		The test suite number
 * @test_case:		The test case number
 */
struct suite_case_pair {
	int test_suite;
	int test_case;
};

static struct suite_case_pair failed_tests[MAX_FAILED_TESTS];
static int tests_count;

static void failed_tests_clear(void)
{
	tests_count = 0;
}

static void failed_tests_add(int test_suite, int test_case)
{
	if (tests_count < MAX_FAILED_TESTS) {
		failed_tests[tests_count].test_suite = test_suite;
		failed_tests[tests_count].test_case = test_case;
		tests_count++;
	}
}

static void failed_tests_print(void)
{
	int i;

	if (!tests_count)
		return;

	printk(KERN_INFO "The following tests failed:\n");
	for (i = 0; i < tests_count; i++) {
		printk(KERN_INFO "  suite %d test %d\n",
			failed_tests[i].test_suite,
			failed_tests[i].test_case);
	}
}

/**
 * test_request_free_channels - Tests request_dma for all the channels. It
 * assumes all channels are free. It requests all the channels and expect to
 * get 0 as return value
 *
 * returns: 	0 - success
 * 		-1 - failure
 */
static int test_request_free_channels(void)
{
	int status = 0;
	unsigned int i;
	int st;

	PDBG("inside test_request_free_channels\n");

	for (i = 0; i < TEST_MAX_CHANNELS; i++) {
		st = request_dma(i, DRIVER_NAME);
		if (st == 0) {
			PDBG("request_dma(%d) free = %d %s\n", i, st, PASS);
		} else {
			PDBG("request_dma(%d) free = %d %s\n", i, st, FAIL);
			status = -1;
		}
	}
	PINFO("test_request_free_channels %s\n", (status ? FAIL : PASS));

	return status;
}

/**
 * test_request_busy_channels - Tests request_dma for all the channels that
 * have been requested. It expects request_dma returns -EBUSY.
 *
 * returns: 	0 - success
 * 		-1 - failure
 */
static int test_request_busy_channels(void)
{
	int status = 0;
	unsigned int i;
	int st;

	PDBG("inside test_request_busy_channels\n");

	for (i = 0; i < TEST_MAX_CHANNELS; i++) {
		st = request_dma(i, DRIVER_NAME);
		if (st == -EBUSY) {
			PDBG("request_dma(%d) busy = %d %s\n", i, st, PASS);
		} else {
			PDBG("request_dma(%d) busy = %d %s\n", i, st, FAIL);
			status = -1;
		}
	}

	PINFO("test_request_busy_channels %s\n", (status ? FAIL : PASS));

	return status;
}

/**
 * test_request_invalid_channels - Tests request_dma for all the channels that
 * are out of the valid channel range. It expects request_dma returns -EINVAL.
 *
 * returns: 	0 - success
 * 		-1 - failure
 */
static int test_request_invalid_channels(void)
{
	int status = 0;
	unsigned int i;
	int st;
	unsigned int chan2test[8] = {
		MAX_DMA_CHANNELS,
		MAX_DMA_CHANNELS + 1,
		MAX_DMA_CHANNELS + 2,
		MAX_DMA_CHANNELS + 3,
		MAX_DMA_CHANNELS * 10,
		MAX_DMA_CHANNELS * 10 + 1,
		MAX_DMA_CHANNELS * 10 + 2,
		MAX_DMA_CHANNELS * 10 + 3,
	};

	PDBG("inside test_request_invalid_channels\n");

	for (i = 0; i < 8; i++) {
		st = request_dma(chan2test[i], DRIVER_NAME);
		if (st == -EINVAL) {
			PDBG("request_dma(%d) invalid = %d %s\n",
			     chan2test[i], st, PASS);
		} else {
			PDBG("request_dma(%d) invalid = %d %s\n",
			     chan2test[i], st, FAIL);
			status = -1;

		}
	}

	PINFO("test_request_invalid_channels %s\n", (status ? FAIL : PASS));

	return status;

}

/**
 * free_all_channels - Frees all the channels.
 */
static void free_all_channels(void)
{
	unsigned int i;

	PDBG("inside free_channels\n");

	for (i = 0; i < TEST_MAX_CHANNELS; i++)
		free_dma(i);
	PDBG("free_channels DONE\n");

	return;
}

/**
 * test1 - Invokes test_request_invalid_channels, test_request_free_channels,
 * test_request_busy_channels, and free_all_channels to test the request_dma
 * and free_dma calls.
 *
 * returns:	0 on success, -1 on failure.
 */
static int test1(void)
{
	int status = 0;

	PDBG("inside pl330 test1\n");

	status |= test_request_invalid_channels();

	status |= test_request_free_channels();

	status |= test_request_busy_channels();

	status |= test_request_busy_channels();

	free_all_channels();

	status |= test_request_free_channels();

	status |= test_request_invalid_channels();

	status |= test_request_busy_channels();

	status |= test_request_busy_channels();

	free_all_channels();

	status |= test_request_free_channels();

	status |= test_request_busy_channels();

	free_all_channels();

	PINFO("PL330 test1 %s\n", (status ? FAIL : PASS));

	return status;
}

struct test_data_t {
	unsigned int channel;
	unsigned int dma_mode;
	dma_addr_t buf;
	void *buf_virt_addr;
	int count;
	int off;
	int id; /* test case id */
	int suite; /* suite number */
	unsigned int inc_dev_addr;
	struct pl330_client_data *client_data;
	void *dev_virt_addr;
	dma_addr_t dma_prog;
	void *dma_prog_v_addr;
	int dma_prog_len;

	int fault_expected;
	int expected_fault_channel;
	u32 expected_fault_type;
	u32 expected_fault_pc;
};

static struct pl330_client_data suite_client_data;
static struct test_data_t suite_test_data = {
	.count = 0,
	.buf = 0,
	.off = 0,
};

struct test_result {
	int status;
	int err_addr;
	int done;
};

#define MAX_TEST_RESULTS 1024

static volatile struct test_result test_results[MAX_TEST_RESULTS];


#define index2char(index, off) ((char)((index) + (off)))

/**
 * init_memory - Initializes a memory buffer with a particular pattern.
 * 	This function will be used if the source of a DMA transaction is
 * 	memory buffer.
 * @buf:	Pointer to the buffer
 * @count:	The buffer length in bytes
 * @off:	The starting value of the memory content
 *
 * returns:	0 on success, -1 on failure.
 */
static int init_memory(void *buf, int count, int off)
{
	int i;
	char *pt = (char *)buf;

	for (i = 0; i < count; i++)
		*pt++ = index2char(i, off);

	PDBG("pl330_test.init_memory: done\n");

	return 0;
}

/**
 * init_device - Initializes a device with a particular pattern.
 * 	This function will be used if the source of a DMA transaction is
 * 	memory buffer.  After initialization, this device will be ready for
 * 	a DMA transaction.
 * @dev_addr:	The device data buffer/FIFO address.
 * @count:	The buffer length in bytes
 * @off:	The starting value of the initial values
 * @burst_size:	The DMA burst size the device supports.
 *
 * returns:	0 on success, -1 on failure.
 */
static int init_device(void *dev_addr, int count, int off,
		       unsigned int burst_size)
{
	int i;
	int char_index;
	char local_buf[16];
	int residue;
	for (i = 0; i < count; i++) {
		char_index = i % burst_size;
		local_buf[char_index] = index2char(i, off);
		if (char_index == burst_size - 1) {
			/* it's tiime to write the word */
			switch (burst_size) {
			case 1:
				dev_write8(*((u8 *)local_buf),
					   dev_addr);
				break;
			case 2:
				dev_write16(*((u16 *)local_buf),
					    dev_addr);
				break;
			case 4:
				dev_write32(*((u32 *)local_buf),
					    dev_addr);
				break;
			case 8:
				dev_write64(*((u64 *)local_buf),
					    dev_addr);
				break;
			default:
				printk(KERN_ERR
				       "error in test_data_t\n");
				return -1;
			}
		}
	}
	residue = count % burst_size;
	if (!residue) {
		for (i = 0; i < residue; i++)
			dev_write8(local_buf[i], dev_addr);
	}


	PDBG("pl330_test.init_device mem: done\n");
	return 0;
}

/**
 * verify_memory - Verifies the target memory buffer to see whehter the DMA
 * 	transaction is completed successfully. This function is used when
 * 	the target of a DMA transaction is a memory buffer.
 * @buf:	Pointer to the buffer
 * @count:	The buffer length in bytes
 * @off:	The starting value of the memory content
 *
 * returns:	0 on success, -1 on failure.
 */
static int verify_memory(void *buf, int count, int off)
{
	int i;
	char *pt = (char *)buf;
	char expecting;
	char got;

	for (i = 0; i < count; i++) {
		got = *pt;
		expecting = index2char(i, off);
		if (expecting != got) {
			printk(KERN_ERR
			       "verify memory failed at address %x, "
			       "expecting %x got %x\n",
			       i, expecting, got);
			return -1;
		}
		pt++;
	}
	return 0;
}

/**
 * verify_device - Verifies the target device buffer to see whehter the DMA
 * 	transaction is completed successfully. This function is used when
 * 	the target of a DMA transaction is a device buffer.
 * @dev_addr:	The device data buffer/FIFO address.
 * @count:	The buffer length in bytes
 * @off:	The starting value of the initial values
 * @burst_size:	The DMA burst size the device supports.
 *
 * returns:	0 on success, -1 on failure.
 */
static int verify_device(void *dev_addr, int count, int off,
			 unsigned int burst_size)
{
	int i;
	int j;
	int char_index;
	char got_buf[16];
	char expecting_buf[16];
	int residue;

	for (i = 0; i < count; i++) {
		char_index = i % burst_size;
		expecting_buf[char_index] = index2char(i, off);

		if (char_index == burst_size - 1) {
			/* it's tiime to read the word */
			switch (burst_size) {
			case 1:
				(*((u8 *)got_buf)) = dev_read8(dev_addr);
				break;
			case 2:
				(*((u16 *)got_buf)) = dev_read16(dev_addr);
				break;
			case 4:
				(*((u32 *)got_buf)) = dev_read32(dev_addr);
				break;
			case 8:
				(*((u64 *)got_buf)) = dev_read64(dev_addr);
				break;
			default:
				printk(KERN_ERR
				       "verify_device error in test_data_t\n");
				return -1;
			}
			/* now compare */

			for (j = 0; j < burst_size; j++) {
				if (expecting_buf[j] != got_buf[j]) {
					printk(KERN_ERR
					       "verify device failed at byte "
					       "%x, expecting %x got %x\n",
					       i,
					       expecting_buf[j],
					       got_buf[j]);
					return -1;
				}
			}
		}
	}
	residue = count % burst_size;
	if (!residue)
		/* we are done */
		return 0;

	for (i = 0; i < residue; i++) {
		got_buf[i] = dev_read8(dev_addr);
		if (expecting_buf[i] != got_buf[i]) {
			printk(KERN_ERR
			       "verify memory failed at byte %x, "
			       "expecting %x got %x\n",
			       count - (residue - i),
			       expecting_buf[i], got_buf[i]);
			return -1;
		}
	}

	return 0;
}

/**
 * init_source - Initialize the source of a DMA transaction.
 * @test_data:	Intance pointer to the test_data_t struct.
 *
 * returns:	0 on success, -1 on failure.
 */
static int init_source(struct test_data_t *test_data)
{
	int count = test_data->count;
	int off = test_data->off;
	unsigned int dev_burst_size =
		test_data->client_data->dev_bus_des.burst_size;
	void *dev_addr = test_data->dev_virt_addr;
	int st;

	PDBG("pl330_test.init_source: entering\n");
	if (test_data->dma_mode == DMA_MODE_READ) {
		if (test_data->inc_dev_addr)
			st = init_memory(dev_addr, count, off);
		else
			st = init_device(dev_addr, count, off, dev_burst_size);
	} else {
		st = init_memory(test_data->buf_virt_addr, count, off);
	}

	PDBG("pl330_test.init_source: done\n");

	return st;
}


/**
 * verify_destination - Initialize the source of a DMA transaction.
 * @test_data:	Intance pointer to the test_data_t struct.
 *
 * returns:	0 on success, -1 on failure.
 */
static int verify_destination(struct test_data_t *test_data)
{
	void *buf = test_data->buf_virt_addr;
	void *dev_addr = test_data->dev_virt_addr;

	int off = test_data->off;
	int count = test_data->count;
	unsigned int inc_dev_addr = test_data->inc_dev_addr;
	unsigned int dev_burst_size =
		test_data->client_data->dev_bus_des.burst_size;

	if (test_data->dma_mode == DMA_MODE_READ)
		return verify_memory(buf, count, off);
	else if (inc_dev_addr)
		return verify_memory(dev_addr, count, off);
	else
		return verify_device(dev_addr, count, off, dev_burst_size);
}

/**
 * print_dma_prog - Print the content of DMA program.
 * @dma_prog:	The starting address of the DMA program
 * @len:	The length of the DMA program.
 */
static void print_dma_prog(char *dma_prog, unsigned int len)
{
	int i;

	PINFO("DMA Program is\n");
	for (i = 0; i < len; i++)
		PINFO("[%02x]\t%02x\n", i, dma_prog[i]);
}

/**
 * verify_one_address - Verifies an address register to see whether it has the
 * 			expected value.
 * @start_addr:		Starting address
 * @count:		The length of the DMA transaction
 * @end_addr:		Ending address
 * @inc:		Tag indicating whether the address of a DMA is
 * 			incremental
 * @name:		The name of an address register.
 */
static int verify_one_address(u32 start_addr,
			      int count,
			      u32 end_addr,
			      int inc,
			      char *name)
{
	u32 expected;

	if (inc)
		expected = start_addr + count;
	else
		expected = start_addr;

	if (expected == end_addr) {
		PDBG("%s matches, started at %#08x ended at %#08x\n",
		     name,
		     start_addr, end_addr);
		return 0;
	} else {

		printk(KERN_ERR
		       "%s is not correct, expecting %#08x got %#08x "
		       "diff %d\n",
		       name,
		       expected, end_addr, end_addr - expected);
		return -1;
	}
}

/**
 * verify_address_registers - Verifies an address registers SA and DA to see
 * 		whether they have the expected values after DMA is done.
 * @test_data:	Intance pointer to the test_data_t struct.
 *
 * returns:	0 on success, -1 on failure.
 */
static int verify_address_registers(struct test_data_t *test_data)
{
	int status = 0;

	u32 sa = get_pl330_sa_reg(test_data->channel);
	u32 da = get_pl330_da_reg(test_data->channel);

	u32 sa_start;
	u32 da_start;

	int src_inc = 0;
	int dst_inc = 0;

	if (test_data->dma_mode == DMA_MODE_READ) {
		sa_start = test_data->client_data->dev_addr;
		da_start = test_data->buf;

		if (test_data->inc_dev_addr)
			src_inc = 1;
		dst_inc = 1;
	} else {
		sa_start = test_data->buf;
		da_start = test_data->client_data->dev_addr;

		src_inc = 1;
		if (test_data->inc_dev_addr)
			dst_inc = 1;
	}

	if (verify_one_address(sa_start,
			       test_data->count,
			       sa,
			       src_inc,
			       "SA"))
		status = -1;

	if (verify_one_address(da_start,
			       test_data->count,
			       da,
			       dst_inc,
			       "DA"))
		status = -1;

	return status;
}

/**
 * dma_done_callback2 - The callback function when the DMA is done.
 * 		This function verifies whether the destination has the
 * 		expected content and the SA and DA regsiters have the
 * 		expected values. If not, mark the test case as failure.
 * @channel:		The DMA channel number.
 * @data:		The callback data.
 */
static void dma_done_callback2(unsigned int channel, void *data)
{
	struct test_data_t *test_data = (struct test_data_t *)data;
	int status;

	char *dma_prog;
	unsigned int dma_prog_len;
	int id = test_data->id;

	PDBG("DMA channel %d done suite %d case %d\n",
	     channel, test_data->suite, id);

	status = verify_destination(test_data);

	if (verify_address_registers(test_data))
		status = -1;

	if (status || disp_dma_prog) {
		if (test_data->dma_prog) {
			dma_prog = (char *)test_data->dma_prog_v_addr;
			dma_prog_len = test_data->dma_prog_len;
		} else {
			dma_prog = get_pl330_dma_program(channel,
							 &dma_prog_len);
		}
		print_dma_prog(dma_prog, dma_prog_len);
	}

	test_results[id].status = status;
	test_results[id].done = status == 0 ? 1 : -1;

	barrier();
}

/**
 * dma_fault_callback2 - The callback function when the DMA is fault.
 * 		This function verifies whether the destination has the
 * 		expected content and the SA and DA regsiters have the
 * 		expected values. If not, mark the test case as failure.
 * @channel:		The DMA channel number.
 * @fault_type:		The DMA fault type.
 * @fault_address:	The DMA fault address.
 * @data:		The callback data.
 */
static void dma_fault_callback2(unsigned int channel, unsigned int fault_type,
				unsigned int fault_address, void *data)
{
	struct test_data_t *test_data = (struct test_data_t *)data;
	int id = test_data->id;
	char *dma_prog;
	unsigned int prog_size;
	int st = 0;

	if (test_data->fault_expected
	    && test_data->channel == channel) {
		if (test_data->expected_fault_type
		    &&	test_data->expected_fault_type != fault_type) {
			PINFO("DMA channel %d fault type is not in "
			      "expected way\n",
			      channel);
			PINFO("DMA fault expecting %#08x got %#08x\n",
			      test_data->expected_fault_type,
			      fault_type);
			st = -1;
		}

		if (test_data->expected_fault_pc
		    &&	test_data->expected_fault_pc != fault_address) {
			PINFO("DMA channel %d fault address is not in"
			      "expected way\n",
			      channel);
			PINFO("DMA fault address expecting %#08x got %#08x\n",
			      test_data->expected_fault_pc,
			      fault_address);
			st = -1;
		}
	} else
		st = -1;

	if (st) {
		PINFO("DMA fault: channel %d, "
		      "type %#08x, pc %#08x, test_data.count %d\n",
		      channel, fault_type, fault_address, test_data->count);
		PINFO("suite %d, case %d,  count %d\n",
		      test_data->suite,
		      test_data->id,
		      test_data->count);
		PINFO("SA %#08x, DA %#08x\n",
		      get_pl330_sa_reg(test_data->channel),
		      get_pl330_da_reg(test_data->channel));

		if (test_data->dma_prog) {
			dma_prog = (char *)test_data->dma_prog_v_addr;
			prog_size = test_data->dma_prog_len;
		} else {
			dma_prog = get_pl330_dma_program(channel, &prog_size);
		}

		print_dma_prog(dma_prog, prog_size);

		test_results[id].status = -1;
		test_results[id].done = -1;
	} else {
		test_results[id].status = 0;
		test_results[id].done = 1;

	}

	barrier();
}

/**
 * test_one_case - Run one DMA test case based on the configuration in the
 * 		test_data_t struct. This contains a full example of
 * 		how to use DMA.
 * @suite:		The test suite number
 * @test_data:		The instance pointer to test configuration.
 *
 */
static int test_one_case(int suite, struct test_data_t *test_data)
{
	int status;
	unsigned int channel = test_data->channel;
	int id = test_data->id;

	if (test_id >= 0 && test_id != id)
		return 0;

	tests_run++;

	test_results[id].status = 0;
	test_results[id].done = 0;

	barrier();

	PDBG("suite %d test_one_case: %d\n", suite, id);

	if (!test_data) {
		printk(KERN_ERR
		       "ERROR[pl330_test.test_one_case]: test_data is null\n");
		failed_tests_add(suite, id);
		return -1;

	}

	status = init_source(test_data);
	if (status != 0) {
		failed_tests_add(suite, id);
		return -1;
	}

	if (test_data->dma_mode == DMA_MODE_READ) {
		PDBG("test_one_case: clearing buf %x\n",
		     (unsigned int)test_data->buf_virt_addr);
		memset(test_data->buf_virt_addr, 0, test_data->count);
	} else if (test_data->inc_dev_addr) {
		PDBG("test_one_case: clearing devmem %x\n",
		       (unsigned int)test_data->dev_virt_addr);
		memset(test_data->dev_virt_addr, 0, test_data->count);
	}

	status = request_dma(channel, DRIVER_NAME);

	if (status != 0)
		goto req_failed;

	PDBG("test_one_case: channel %d requested\n", channel);

	if (test_data->dma_mode == DMA_MODE_READ)
		PDBG("test_one_case: setting DMA mode DMA_MODE_READ\n");
	else if (test_data->dma_mode == DMA_MODE_WRITE)
		PDBG("test_one_case: setting DMA mode DMA_MODE_WRITE\n");
	else
		PDBG("test_one_case: setting DMA mode DMA_MODE_UNKNOWN\n");

	set_dma_mode(channel, test_data->dma_mode);

	PDBG("test_one_case: setting DMA addr %#08x\n",
	       (u32)test_data->buf);
	set_dma_addr(channel, test_data->buf);

	set_dma_count(channel, test_data->count);

	set_pl330_client_data(channel, test_data->client_data);

	set_pl330_incr_dev_addr(channel, test_data->inc_dev_addr);

	set_pl330_done_callback(channel, dma_done_callback2, test_data);
	set_pl330_fault_callback(channel, dma_fault_callback2, test_data);

	set_pl330_dma_prog_addr(channel, test_data->dma_prog);

	enable_dma(channel);

	while (!test_results[id].done)
		barrier();

	disable_dma(channel);

	free_dma(channel);

	if (test_results[id].status) {
		failed_tests_add(suite, id);
		PINFO("PL330 test suite %d case %d %s \n", suite, id, FAIL);
	} else {
		PINFO("PL330 test suite %d case %d %s \n", suite, id, PASS);
	}

	if (!test_results[id].status)
		tests_passed++;

	return test_results[id].status;

 req_failed:
	PINFO("PL330 test suite %d case %d reqeust_dma %s\n", suite, id, FAIL);
	failed_tests_add(suite, id);
	return -1;
}

/**
 * clear_test_count - Clear the global counters for tests.
 *
 */
static void clear_test_counts(void)
{
	tests_run = 0;
	tests_failed = 0;
	tests_passed = 0;
}

static void print_test_suite_results(int suite)
{
	tests_failed = tests_run - tests_passed;

	if (tests_failed)
		PINFO("PL330 test suite %d %s: "
		      "run %d, passed %d, failed %d\n",
		      suite, FAIL,
		      tests_run, tests_passed, tests_failed);
	else
		PINFO("PL330 test suite %d %s: "
		      "run %d all passed\n",
		      suite, PASS, tests_run);
}


static int off_array[] = {35, 43, 33, 27, 98, 17, 19, 25, 9, 15, 19};

/**
 * pl330_test_suite_1 - tests DMA_MODE_READ for all channels with default
 * 	bus_des
 * @returns	0 on success, -1 on failure
 */
static int pl330_test_suite_1(void)
{
	int suite = 1;

	int mode_sel;
	unsigned int channel;
	dma_addr_t buf_d_addr;
	void *buf_v_addr;
	dma_addr_t dev_d_addr;
	void *dev_v_addr;
	int status;
	unsigned int dma_modes[2] = {DMA_MODE_READ, DMA_MODE_WRITE};
	int id;

	clear_test_counts();

	buf_v_addr = dma_alloc_coherent(test_device, SZ_4K,
					&buf_d_addr, GFP_KERNEL);
	if (!buf_v_addr) {
		printk(KERN_ERR
		       "test_suite_1: dma_alloc_coherent buf failed\n");
		return -1;
	}
	PDBG("pl330_test_suite_1: buf_v_addr %#08x, buf_dma_addr %#08x\n",
	     (u32)buf_v_addr, (u32)buf_d_addr);
	PDBG("pl330_test_suite_1: virt_to_dma %#08x, dma_to_virt %#08x\n",
	     (u32)virt_to_dma(test_device, buf_v_addr),
	     (u32)dma_to_virt(test_device, buf_d_addr));
	PDBG("pl330_test_suite_1: bus_to_virt %#08x, virt_to_bus %#08x\n",
	     (u32)bus_to_virt(buf_d_addr),
	     (u32)virt_to_bus(bus_to_virt(buf_d_addr)));
	PDBG("pl330_test_suite_1: page_to_phys %#08x\n",
	     (u32)page_to_phys(virt_to_page(buf_v_addr)));

	dev_v_addr = dma_alloc_coherent(test_device, SZ_4K,
					&dev_d_addr, GFP_KERNEL);
	if (!dev_v_addr) {
		printk(KERN_ERR
		       "test_suite_1: dma_alloc_coherent dev failed\n");
		return -1;
	}

	memset(&suite_test_data, 0, sizeof(struct test_data_t));

	PINFO("test suite 1 started\n");
	status = 0;
	for (mode_sel = 0; mode_sel < 2; mode_sel++) {
		for (channel = 0; channel < TEST_MAX_CHANNELS; channel++) {
			suite_test_data.suite = suite;
			suite_test_data.channel = channel;
			suite_test_data.dma_mode = dma_modes[mode_sel];
			suite_test_data.count = SZ_1K;
			suite_test_data.buf = buf_d_addr;
			suite_test_data.buf_virt_addr = buf_v_addr;

			id = mode_sel * TEST_MAX_CHANNELS + channel;
			suite_test_data.id = id;
			suite_test_data.off =
				off_array[id % ARRAY_SIZE(off_array)];
			suite_test_data.inc_dev_addr = 1;

			memset(&suite_client_data, 0,
			       sizeof(struct pl330_client_data));
			suite_client_data.dev_addr = dev_d_addr;
			suite_test_data.dev_virt_addr = dev_v_addr;
			suite_test_data.client_data = &suite_client_data;

			if (test_one_case(suite, &suite_test_data))
				status = -1;
		}
	}
	PDBG("PL330 test suite %d %s\n", suite, (status ? FAIL : PASS));

	print_test_suite_results(suite);

	dma_free_coherent(test_device, SZ_4K, buf_v_addr, buf_d_addr);
	dma_free_coherent(test_device, SZ_4K, dev_v_addr, dev_d_addr);

	return status;
}

/**
 * pl330_test_suite_2 - The suite 2 exercises all burst sizes and burst
 * 	lengths for DMA read and write.
 * @returns	0 on success, -1 on failure
 */
static int pl330_test_suite_2(void)
{
	int suite = 2;

	int mode_sel;
	int size_sel;
	int burst_size;
	int burst_len;
	unsigned int channel;
	dma_addr_t buf_d_addr;
	void *buf_v_addr;
	dma_addr_t dev_d_addr;
	void *dev_v_addr;
	int status;
	unsigned int dma_modes[2] = {DMA_MODE_READ, DMA_MODE_WRITE};
	int id;


	clear_test_counts();

	buf_v_addr = dma_alloc_coherent(test_device, SZ_4K,
					&buf_d_addr, GFP_KERNEL);
	if (!buf_v_addr) {
		printk(KERN_ERR
		       "test_suite_2: dma_alloc_coherent buf failed\n");
		return -1;
	}
	dev_v_addr = dma_alloc_coherent(test_device, SZ_4K,
					&dev_d_addr, GFP_KERNEL);
	if (!dev_v_addr) {
		printk(KERN_ERR
		       "test_suite_2: dma_alloc_coherent dev failed\n");
		return -1;
	}
	PDBG("test_suite_2: buf_v_addr %#08x, buf_d_addr %#08x\n",
	     (u32)buf_v_addr, (u32)buf_d_addr);
	PDBG("test_suite_2: dev_v_addr %#08x, dev_d_addr %#08x\n",
	     (u32)dev_v_addr, (u32)dev_d_addr);

	memset(&suite_test_data, 0, sizeof(struct test_data_t));

	status = 0;
	id = 0;
	channel = 0;
	for (mode_sel = 0; mode_sel < 2; mode_sel++) {
		for (size_sel = 0; size_sel < 4; size_sel++) {
			burst_size = 1 << size_sel;
			for (burst_len = 1; burst_len <= 16; burst_len++) {
				suite_test_data.suite = suite;
				suite_test_data.channel = channel;
				suite_test_data.dma_mode = dma_modes[mode_sel];
				suite_test_data.count = SZ_1K;
				suite_test_data.buf = buf_d_addr;
				suite_test_data.buf_virt_addr = buf_v_addr;
				suite_test_data.off =
					off_array[id % ARRAY_SIZE(off_array)];
				suite_test_data.id = id;
				suite_test_data.inc_dev_addr = 1;

				memset(&suite_client_data, 0,
				       sizeof(struct pl330_client_data));

				suite_client_data.dev_addr = (u32)dev_d_addr;

				suite_client_data.dev_bus_des.burst_size =
					burst_size;
				suite_client_data.dev_bus_des.burst_len =
					burst_len;

				suite_client_data.mem_bus_des.burst_size =
					burst_size;
				suite_client_data.mem_bus_des.burst_len =
					burst_len;

				suite_test_data.dev_virt_addr = dev_v_addr;
				suite_test_data.client_data =
					&suite_client_data;

				if (test_one_case(suite, &suite_test_data))
					status = -1;

				id++;
			}
		}
	}

	PINFO("PL330 test suite %d %s\n", suite, (status ? FAIL : PASS));

	dma_free_coherent(test_device, SZ_4K, buf_v_addr, buf_d_addr);
	dma_free_coherent(test_device, SZ_4K, dev_v_addr, dev_d_addr);

	print_test_suite_results(suite);

	return status;
}

/**
 * pl330_test_suite_3 - The suite 3 exercises unaligned head and tail.
 * @returns	0 on success, -1 on failure
 */
static int pl330_test_suite_3(void)
{
	int suite = 3;

	int size_sel;
	int burst_size;
	int burst_len;
	unsigned int channel;
	dma_addr_t buf_d_addr;
	void *buf_v_addr;
	dma_addr_t dev_d_addr;
	void *dev_v_addr;
	int status;
	int id;
	int head_off;
	int inc_dev_addr;

	struct pl330_bus_des *dev_bus_des;
	struct pl330_bus_des *mem_bus_des;

	clear_test_counts();

	buf_v_addr = dma_alloc_coherent(test_device, SZ_4K,
					&buf_d_addr, GFP_KERNEL);
	if (!buf_v_addr) {
		printk(KERN_ERR
		       "test_suite_3: dma_alloc_coherent buf failed\n");
		return -1;
	}
	dev_v_addr = dma_alloc_coherent(test_device, SZ_4K,
					&dev_d_addr, GFP_KERNEL);
	if (!dev_v_addr) {
		printk(KERN_ERR
		       "test_suite_3: dma_alloc_coherent buf failed\n");
		return -1;
	}

	memset(&suite_test_data, 0, sizeof(struct test_data_t));

	status = 0;
	id = 0;
	channel = 0;
	for (head_off = 1; head_off < 8; head_off++) {
		for (size_sel = 0; size_sel < 4; size_sel++) {
			burst_size = 1 << size_sel;
			for (burst_len = 1; burst_len <= 16; burst_len++) {
				suite_test_data.suite = suite;
				suite_test_data.channel = channel;
				suite_test_data.dma_mode = DMA_MODE_READ;
				suite_test_data.count = SZ_1K + 64;
				suite_test_data.buf =
					buf_d_addr + head_off;
				suite_test_data.buf_virt_addr =
					(char *)buf_v_addr + head_off;
				suite_test_data.id = id;
				suite_test_data.off =
					off_array[id % ARRAY_SIZE(off_array)];

				inc_dev_addr = 1;
				suite_test_data.inc_dev_addr = inc_dev_addr;

				memset(&suite_client_data, 0,
				       sizeof(struct pl330_client_data));

				suite_client_data.dev_addr = dev_d_addr;

				dev_bus_des = &suite_client_data.dev_bus_des;
				dev_bus_des->burst_size = burst_size;
				dev_bus_des->burst_len = burst_len;

				mem_bus_des = &suite_client_data.mem_bus_des;
				mem_bus_des->burst_size = burst_size;
				mem_bus_des->burst_len = burst_len;

				suite_test_data.dev_virt_addr = dev_v_addr;

				if (inc_dev_addr) {
					suite_client_data.dev_addr +=
						head_off;
					suite_test_data.dev_virt_addr +=
						head_off;
				}

				suite_test_data.client_data =
					&suite_client_data;

				if (test_one_case(suite, &suite_test_data))
					status = -1;

				id++;
			}
		}
	}

	dma_free_coherent(test_device, SZ_4K, buf_v_addr, buf_d_addr);
	dma_free_coherent(test_device, SZ_4K, dev_v_addr, dev_d_addr);

	print_test_suite_results(suite);

	return status;
}


/**
 * pl330_test_suite_4 - The suite 4 exercises unaligned tail special cases.
 * @returns	0 on success, -1 on failure
 *
 */
static int pl330_test_suite_4(void)
{
	int suite = 4;

	int burst_size;
	int burst_len;
	unsigned int channel;
	dma_addr_t buf_d_addr;
	void *buf_v_addr;
	dma_addr_t dev_d_addr;
	void *dev_v_addr;
	int status;
	int id;
	int head_off;
	struct pl330_bus_des *dev_bus_des;
	struct pl330_bus_des *mem_bus_des;


	clear_test_counts();

	buf_v_addr = dma_alloc_coherent(test_device, SZ_4K,
					&buf_d_addr, GFP_KERNEL);
	if (!buf_v_addr) {
		printk(KERN_ERR
		       "test_suite_4: dma_alloc_coherent buf failed\n");
		return -1;
	}
	dev_v_addr = dma_alloc_coherent(test_device, SZ_4K,
					&dev_d_addr, GFP_KERNEL);
	if (!dev_v_addr) {
		printk(KERN_ERR
		       "test_suite_4: dma_alloc_coherent buf failed\n");
		return -1;
	}

	memset(&suite_test_data, 0, sizeof(struct test_data_t));

	status = 0;
	id = 0;
	channel = 0;
	burst_size = 4;
	burst_len = 4;

	head_off = 0;
	suite_test_data.suite = suite;
	suite_test_data.channel = channel;
	suite_test_data.dma_mode = DMA_MODE_READ;
	suite_test_data.count = SZ_1K + 1;
	suite_test_data.buf =
		buf_d_addr + head_off;
	suite_test_data.buf_virt_addr =
		(char *)buf_v_addr + head_off;
	suite_test_data.off = 95;
	suite_test_data.id = id;
	suite_test_data.inc_dev_addr = 1;

	memset(&suite_client_data, 0,
	       sizeof(struct pl330_client_data));

	suite_client_data.dev_addr = dev_d_addr;

	dev_bus_des = &suite_client_data.dev_bus_des;
	dev_bus_des->burst_size = burst_size;
	dev_bus_des->burst_len = burst_len;

	mem_bus_des = &suite_client_data.mem_bus_des;
	mem_bus_des->burst_size = burst_size;
	mem_bus_des->burst_len = burst_len;

	suite_test_data.dev_virt_addr = dev_v_addr;
	suite_test_data.client_data =
		&suite_client_data;

	if (test_one_case(suite, &suite_test_data))
		status = -1;

	id++;

	dma_free_coherent(test_device, SZ_4K, buf_v_addr, buf_d_addr);
	dma_free_coherent(test_device, SZ_4K, dev_v_addr, dev_d_addr);

	print_test_suite_results(suite);

	return status;
}

/**
 * pl330_test_suite_5 - Tests user defined program.
 * @returns	0 on success, -1 on failure
 */
static int pl330_test_suite_5(void)
{
	int suite = 5;

	int burst_size;
	int burst_len;
	unsigned int channel;
	dma_addr_t buf_d_addr;
	void *buf_v_addr;
	dma_addr_t dev_d_addr;
	void *dev_v_addr;
	dma_addr_t prog_d_addr;
	void *prog_v_addr;
	int status;
	int id;
	int head_off;
	struct pl330_bus_des *dev_bus_des;
	struct pl330_bus_des *mem_bus_des;

	char prog[] = {
		/* [0] */	0xbc, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* [6] */	0xbc, 0x02, 0x00, 0x00, 0x00, 0x00,
		/* [12]*/	0xbc, 0x00, 0x00, 0x20, 0xc4, 0x01,
		/* [18]*/	0xbc, 0x02, 0x01, 0x50, 0xe5, 0x01,
		/* DMAMOV CCR SS32 SB4 DS32 SB4
		 * CCR[31:16]: 31 30 9 8 7 6 5 4 3 2 1 20 9 8 7 16
		 *       	0  0 0 0 0 0 0 0 0 0 0 0  1 1 0 1
		 * CCR[15:0] : 15 4 3 2 1 10 9 8 7 6 5 4 3 2 1 0
		 *	 	0 1 0 0 0 0  0 0 0 0 1 1 0 1 0 1
		 * 0x000d4035
		 */
		/*[24]*/	0xbc, 0x01, 0x35, 0x40, 0x0d, 0x00,
		/*[30]*/	0x04,
		/*[31]*/	0x08,
		/*[32]*/	0x34, 0x00,
		/*[34]*/	0x00,
	};

	/* for fixed unalgined burst, use this CCR
	 * DMAMOV CCR SS32 SB4 SAF DS32 SB4 DAF
	 * CCR[31:16]: 31 30 9 8 7 6 5 4 3 2 1 20 9 8 7 16
	 *       	0  0 0 0 0 0 0 0 0 0 0 0  1 1 0 1
	 * CCR[15:0] : 15 4 3 2 1 10 9 8 7 6 5 4 3 2 1 0
	 *	 	0 0 0 0 0 0  0 0 0 0 1 1 0 1 0 0
	 * 0x000d0034
	 */
	char prog1[] = {
		/* [0] */	0xbc, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* [6] */	0xbc, 0x02, 0x00, 0x00, 0x00, 0x00,
		/* [12]*/	0xbc, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* [18]*/	0xbc, 0x02, 0x00, 0x00, 0x00, 0x00,

		/*[24]*/	0xbc, 0x01, 0x35, 0x40, 0x0d, 0x00,
		/*[30]*/	0x04,
		/*[30]*/	0x04,
		/*[31]*/	0x08,
		/*[31]*/	0x08,
		/*[32]*/	0x34, 0x00,
		/*[34]*/	0x00,
	};

	clear_test_counts();

	buf_v_addr = dma_alloc_coherent(test_device, SZ_4K,
					&buf_d_addr, GFP_KERNEL);
	if (!buf_v_addr) {
		printk(KERN_ERR
		       "test_suite_5: dma_alloc_coherent buf failed\n");
		return -1;
	}
	dev_v_addr = dma_alloc_coherent(test_device, SZ_4K,
					&dev_d_addr, GFP_KERNEL);
	if (!dev_v_addr) {
		printk(KERN_ERR
		       "test_suite_5: dma_alloc_coherent buf failed\n");
		return -1;
	}
	prog_v_addr = dma_alloc_coherent(test_device, SZ_1K,
					 &prog_d_addr, GFP_KERNEL);
	if (!prog_v_addr) {
		printk(KERN_ERR
		       "test_suite_5: dma_alloc_coherent buf failed\n");
		return -1;
	}

	memset(&suite_test_data, 0, sizeof(struct test_data_t));

	status = 0;
	id = 0;
	channel = 0;
	burst_size = 4;
	burst_len = 4;

	head_off = 0;
	suite_test_data.suite = suite;
	suite_test_data.channel = channel;
	suite_test_data.count = 15;
	suite_test_data.buf =
		buf_d_addr + 1;
	suite_test_data.buf_virt_addr =
		(char *)buf_v_addr + 1;
	suite_test_data.off = 95;
	suite_test_data.inc_dev_addr = 1;

	memset(&suite_client_data, 0,
	       sizeof(struct pl330_client_data));

	suite_client_data.dev_addr = dev_d_addr + 5;

	dev_bus_des = &suite_client_data.dev_bus_des;
	dev_bus_des->burst_size = burst_size;
	dev_bus_des->burst_len = burst_len;

	mem_bus_des = &suite_client_data.mem_bus_des;
	mem_bus_des->burst_size = burst_size;
	mem_bus_des->burst_len = burst_len;

	suite_test_data.dev_virt_addr = dev_v_addr + 5;
	suite_test_data.client_data = &suite_client_data;

	suite_test_data.id = id;

	suite_test_data.dma_mode = DMA_MODE_READ;
	memcpy(prog_v_addr, prog, ARRAY_SIZE(prog));
	*((u32 *)(prog_v_addr + 14)) = suite_client_data.dev_addr;
	*((u32 *)(prog_v_addr + 20)) = suite_test_data.buf;

	suite_test_data.dma_prog = prog_d_addr;
	suite_test_data.dma_prog_v_addr = prog_v_addr;
	suite_test_data.dma_prog_len = ARRAY_SIZE(prog);

	if (test_one_case(suite, &suite_test_data))
		status = -1;

	id++;

	suite_test_data.id = id;
	suite_test_data.dma_mode = DMA_MODE_WRITE;
	*((u32 *)(prog_v_addr + 14)) = suite_test_data.buf;
	*((u32 *)(prog_v_addr + 20)) = suite_client_data.dev_addr;

	if (test_one_case(suite, &suite_test_data))
		status = -1;

	id++;

	suite_test_data.id = id;
	suite_test_data.dma_mode = DMA_MODE_READ;
	suite_test_data.count = 31;
	memcpy(prog_v_addr, prog1, ARRAY_SIZE(prog1));
	*((u32 *)(prog_v_addr + 14)) = suite_client_data.dev_addr;
	*((u32 *)(prog_v_addr + 20)) = suite_test_data.buf;
	suite_test_data.dma_prog = prog_d_addr;
	suite_test_data.dma_prog_v_addr = prog_v_addr;
	suite_test_data.dma_prog_len = ARRAY_SIZE(prog1);

	if (test_one_case(suite, &suite_test_data))
		status = -1;

	id++;

	dma_free_coherent(test_device, SZ_4K, buf_v_addr, buf_d_addr);
	dma_free_coherent(test_device, SZ_4K, dev_v_addr, dev_d_addr);
	dma_free_coherent(test_device, SZ_1K, prog_v_addr, prog_d_addr);

	print_test_suite_results(suite);

	return status;
}

#ifdef PL330_TEST_DEBUG
/**
 * print_buf - Prints the content of a buffer.
 * @buf:	Memory buffer
 * @len:	Buffer length in bytes
 * @buf_name:	Buffer name
 */
static void print_buf(void *buf, int len, char *buf_name)
{
	int i;
	PINFO("content of %s\n", buf_name);

	for (i = 0; i < len; i++)
		PINFO("[%02x] %02x\n", i, *((u8 *)(buf + i)));
}
#endif /* PL330_TEST_DEBUG */

/**
 * pl330_test_suite_6 - The suite 6 exercises small DMA size.
 * @returns	0 on success, -1 on failure
 */
static int pl330_test_suite_6(void)
{
	int suite = 6;

	int burst_size;
	int burst_len;
	int size_sel;
	unsigned int channel;
	dma_addr_t buf_d_addr;
	void *buf_v_addr;
	dma_addr_t dev_d_addr;
	void *dev_v_addr;
	int status;
	unsigned int dma_modes[2] = {DMA_MODE_READ, DMA_MODE_WRITE};
	int id;
	int count;
	int i;

	clear_test_counts();

	buf_v_addr = dma_alloc_coherent(test_device, SZ_4K,
					&buf_d_addr, GFP_KERNEL);
	if (!buf_v_addr) {
		printk(KERN_ERR
		       "test_suite_6: dma_alloc_coherent buf failed\n");
		return -1;
	}
	dev_v_addr = dma_alloc_coherent(test_device, SZ_4K,
					&dev_d_addr, GFP_KERNEL);
	if (!dev_v_addr) {
		printk(KERN_ERR
		       "test_suite_6: dma_alloc_coherent dev failed\n");
		return -1;
	}
	PDBG("test_suite_6: buf_v_addr %#08x, buf_d_addr %#08x\n",
	     (u32)buf_v_addr, (u32)buf_d_addr);
	PDBG("test_suite_6: dev_v_addr %#08x, dev_d_addr %#08x\n",
	     (u32)dev_v_addr, (u32)dev_d_addr);
	status = 0;
	id = 0;
	channel = 0;
	burst_len = 1;
	memset(&suite_test_data, 0, sizeof(struct test_data_t));
	for (count = 1; count < 71; count++) {
		for (size_sel = 0; size_sel <= 3; size_sel++) {
			burst_size = 1 << size_sel;

			channel = count % TEST_MAX_CHANNELS;
			suite_test_data.suite = suite;
			suite_test_data.channel = channel;
			suite_test_data.dma_mode = dma_modes[count % 2];
			suite_test_data.count = count;
			suite_test_data.buf = buf_d_addr;
			suite_test_data.buf_virt_addr = buf_v_addr;
			suite_test_data.id = id;
			suite_test_data.off =
				off_array[id % ARRAY_SIZE(off_array)];
			suite_test_data.inc_dev_addr = 1;

			memset(&suite_client_data, 0,
			       sizeof(struct pl330_client_data));

			suite_client_data.dev_addr = (u32)dev_d_addr;

			suite_client_data.dev_bus_des.burst_size =
				burst_size;
			suite_client_data.dev_bus_des.burst_len =
				burst_len;

			suite_client_data.mem_bus_des.burst_size =
				burst_size;
			suite_client_data.mem_bus_des.burst_len =
				burst_len;

			suite_test_data.dev_virt_addr = dev_v_addr;
			suite_test_data.client_data = &suite_client_data;

			if (test_one_case(suite, &suite_test_data)) {
				status = -1;
				printk(KERN_INFO "First 16 bytes of buf\n");
				for (i = 0; i < 16; i++) {
					printk(KERN_INFO "[%02x] %02x\n",
					       i, *((u8 *)(buf_v_addr + i)));
				}

				printk(KERN_INFO "First 16 bytes of dev\n");
				for (i = 0; i < 16; i++) {
					printk(KERN_INFO "[%02x] %02x\n",
					       i, *((u8 *)(dev_v_addr + i)));
				}
			}

			id++;
		}
	}

	dma_free_coherent(test_device, SZ_4K, buf_v_addr, buf_d_addr);
	dma_free_coherent(test_device, SZ_4K, dev_v_addr, dev_d_addr);

	print_test_suite_results(suite);

	return status;
}

/**
 * pl330_test_suite_7 - The suite 7 exercises big DMA size.
 * @returns	0 on success, -1 on failure
 */
static int pl330_test_suite_7(void)
{
	int suite = 7;

	int burst_size;
	int burst_len;
	int size_sel;
	unsigned int channel;
	dma_addr_t buf_d_addr;
	void *buf_v_addr;
	dma_addr_t dev_d_addr;
	void *dev_v_addr;
	int status;
	unsigned int dma_modes[2] = {DMA_MODE_READ, DMA_MODE_WRITE};
	int counts_a[] = {SZ_4K, SZ_8K, SZ_16K, SZ_64K, SZ_128K};
	int count_sel;
	int id;
	int count;

	clear_test_counts();

	buf_v_addr = dma_alloc_coherent(test_device, SZ_128K,
					&buf_d_addr, GFP_KERNEL);
	if (!buf_v_addr) {
		printk(KERN_ERR
		       "test_suite_7: dma_alloc_coherent buf failed\n");
		return -1;
	}
	dev_v_addr = dma_alloc_coherent(test_device, SZ_128K,
					&dev_d_addr, GFP_KERNEL);
	if (!dev_v_addr) {
		printk(KERN_ERR
		       "test_suite_7: dma_alloc_coherent dev failed\n");
		return -1;
	}
	PDBG("test_suite_7: buf_v_addr %#08x, buf_d_addr %#08x\n",
	     (u32)buf_v_addr, (u32)buf_d_addr);
	PDBG("test_suite_7: dev_v_addr %#08x, dev_d_addr %#08x\n",
	     (u32)dev_v_addr, (u32)dev_d_addr);

	memset(&suite_test_data, 0, sizeof(struct test_data_t));

	status = 0;
	id = 0;
	channel = 0;
	count = SZ_8M;
	burst_size = 8;
	burst_len = 16;

	for (count_sel = 0; count_sel < ARRAY_SIZE(counts_a); count_sel++) {
		for (size_sel = 2; size_sel <= 3; size_sel++) {
			for (burst_len = 8; burst_len <= 16; burst_len++) {

				suite_test_data.suite = suite;
				suite_test_data.channel = channel;
				suite_test_data.dma_mode =
					dma_modes[channel % 2];
				suite_test_data.count = counts_a[count_sel];
				suite_test_data.buf = buf_d_addr;
				suite_test_data.buf_virt_addr = buf_v_addr;
				suite_test_data.id = id;
				suite_test_data.off =
					off_array[id % ARRAY_SIZE(off_array)];
				suite_test_data.inc_dev_addr = 1;

				memset(&suite_client_data, 0,
				       sizeof(struct pl330_client_data));

				suite_client_data.dev_addr = (u32)dev_d_addr;

				burst_size = 1 << size_sel;
				suite_client_data.dev_bus_des.burst_size =
					burst_size;
				suite_client_data.dev_bus_des.burst_len =
					burst_len;

				suite_client_data.mem_bus_des.burst_size =
					burst_size;
				suite_client_data.mem_bus_des.burst_len =
					burst_len;

				suite_test_data.dev_virt_addr = dev_v_addr;
				suite_test_data.client_data =
					&suite_client_data;

				if (test_one_case(suite, &suite_test_data))
					status = -1;

				id++;

				channel = (channel + 1) % TEST_MAX_CHANNELS;
			}
		}
	}

	dma_free_coherent(test_device, SZ_128K, buf_v_addr, buf_d_addr);
	dma_free_coherent(test_device, SZ_128K, dev_v_addr, dev_d_addr);

	print_test_suite_results(suite);

	return status;
}

/**
 * pl330_test_suite_8 - suite_8 tests fault interrupt.
 * @returns	0 on success, -1 on failure
 */
static int pl330_test_suite_8(void)
{
	int suite = 8;

	int burst_size;
	int burst_len;
	unsigned int channel;
	dma_addr_t buf_d_addr;
	void *buf_v_addr;
	dma_addr_t dev_d_addr;
	void *dev_v_addr;
	dma_addr_t prog_d_addr;
	void *prog_v_addr;
	int status;
	int id;
	int head_off;
	struct pl330_bus_des *dev_bus_des;
	struct pl330_bus_des *mem_bus_des;

	char prog[] = {
		/* [0] */	0xbc, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* [6] */	0xbc, 0x02, 0x00, 0x00, 0x00, 0x00,
		/* [12]*/	0xbc, 0x00, 0x00, 0x20, 0xc4, 0x01,
		/* [18]*/	0xbc, 0x02, 0x01, 0x50, 0xe5, 0x01,
		/* DMAMOV CCR SS32 SB4 DS32 SB4
		 * CCR[31:16]: 31 30 9 8 7 6 5 4 3 2 1 20 9 8 7 16
		 *       	0  0 0 0 0 0 0 0 0 0 0 0  1 1 0 1
		 * CCR[15:0] : 15 4 3 2 1 10 9 8 7 6 5 4 3 2 1 0
		 *	 	0 1 0 0 0 0  0 0 0 0 1 1 0 1 0 1
		 * 0x000d4035
		 */
		/*[24]*/	0xbc, 0x01, 0x35, 0x40, 0x0d, 0x00,
		/*[30]*/	0x08,
		/*[31]*/	0x00,
	};

	clear_test_counts();

	buf_v_addr = dma_alloc_coherent(test_device, SZ_4K,
					&buf_d_addr, GFP_KERNEL);
	if (!buf_v_addr) {
		printk(KERN_ERR
		       "test_suite_8: dma_alloc_coherent buf failed\n");
		return -1;
	}
	dev_v_addr = dma_alloc_coherent(test_device, SZ_4K,
					&dev_d_addr, GFP_KERNEL);
	if (!dev_v_addr) {
		printk(KERN_ERR
		       "test_suite_8: dma_alloc_coherent buf failed\n");
		return -1;
	}
	prog_v_addr = dma_alloc_coherent(test_device, SZ_1K,
					 &prog_d_addr, GFP_KERNEL);
	if (!prog_v_addr) {
		printk(KERN_ERR
		       "test_suite_8: dma_alloc_coherent buf failed\n");
		return -1;
	}

	memset(&suite_test_data, 0, sizeof(struct test_data_t));

	status = 0;
	id = 0;
	channel = 0;
	burst_size = 4;
	burst_len = 4;

	head_off = 0;
	suite_test_data.suite = suite;
	suite_test_data.count = 15;
	suite_test_data.buf =
		buf_d_addr + 1;
	suite_test_data.buf_virt_addr =
		(char *)buf_v_addr + 1;
	suite_test_data.off = 95;
	suite_test_data.inc_dev_addr = 1;

	memset(&suite_client_data, 0,
	       sizeof(struct pl330_client_data));

	suite_client_data.dev_addr = dev_d_addr + 5;

	dev_bus_des = &suite_client_data.dev_bus_des;
	dev_bus_des->burst_size = burst_size;
	dev_bus_des->burst_len = burst_len;

	mem_bus_des = &suite_client_data.mem_bus_des;
	mem_bus_des->burst_size = burst_size;
	mem_bus_des->burst_len = burst_len;

	suite_test_data.dev_virt_addr = dev_v_addr + 5;
	suite_test_data.client_data = &suite_client_data;

	suite_test_data.id = id;

	suite_test_data.dma_mode = DMA_MODE_READ;
	memcpy(prog_v_addr, prog, ARRAY_SIZE(prog));
	*((u32 *)(prog_v_addr + 14)) = suite_client_data.dev_addr;
	*((u32 *)(prog_v_addr + 20)) = suite_test_data.buf;

	suite_test_data.dma_prog = prog_d_addr;
	suite_test_data.dma_prog_v_addr = prog_v_addr;
	suite_test_data.dma_prog_len = ARRAY_SIZE(prog);

	suite_test_data.fault_expected = 1;
	suite_test_data.expected_fault_type = 0x2000;

	for (channel = 0; channel < TEST_MAX_CHANNELS; channel++) {
		suite_test_data.channel = channel;
		suite_test_data.id = id;
		if (test_one_case(suite, &suite_test_data))
			status = -1;

		id++;
	}

	dma_free_coherent(test_device, SZ_4K, buf_v_addr, buf_d_addr);
	dma_free_coherent(test_device, SZ_4K, dev_v_addr, dev_d_addr);
	dma_free_coherent(test_device, SZ_1K, prog_v_addr, prog_d_addr);

	print_test_suite_results(suite);

	return status;
}

static int pl330_test_probe(struct platform_device *pdev)
{

	int pdev_id;

	int st = 0;

	if (!pdev) {
		dev_err(&pdev->dev,
			"pl330_test_probe called with NULL param.\n");
		return -ENODEV;
	}

	PDBG("pl330_test probing dev_id %d\n", pdev->id);

	pdev_id = 0;

	test_device = &pdev->dev;

	failed_tests_clear();

	if (suite_num == 0)
		st |= test1();

	if (!st && (suite_num == 0 || suite_num == 1))
		st |= pl330_test_suite_1();

	if (!st && (suite_num == 0 || suite_num == 2))
		st |= pl330_test_suite_2();

	if (!st && (suite_num == 0 || suite_num == 3))
		st |= pl330_test_suite_3();

	if (!st && (suite_num == 0 || suite_num == 4))
		st |= pl330_test_suite_4();

	if (!st && (suite_num == 0 || suite_num == 5))
		st |= pl330_test_suite_5();

	if (!st && (suite_num == 0 || suite_num == 6))
		st |= pl330_test_suite_6();

	if (!st && (suite_num == 0 || suite_num == 7))
		st |= pl330_test_suite_7();

	if (!st && (suite_num == 0 || suite_num == 8))
		st |= pl330_test_suite_8();

	printk(KERN_INFO "PL330 test %s\n", st ? FAIL : PASS);

	failed_tests_print();

	return 0;
}

static int pl330_test_remove(struct platform_device *pdev)
{
	test_device = NULL;

	return 0;
}

static struct platform_driver pl330_test_driver = {
	.probe = pl330_test_probe,
	.remove = pl330_test_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init pl330_test(void)
{
	int st;

	st = platform_driver_register(&pl330_test_driver);
	if (st) {
		printk(KERN_ERR
		       "platform_driver_register(pl330_test_device0) %s\n",
		       FAIL);
		return st;
	} else {
		PDBG("platform_driver_register(pl330_test_device0) done\n");

	}

	return st;
}

static void __exit pl330_test_exit(void)
{
	platform_driver_unregister(&pl330_test_driver);
}

module_init(pl330_test);

module_exit(pl330_test_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("pl330 driver test");
MODULE_AUTHOR("Xilinx, Inc.");
MODULE_VERSION("1.00a");
