/*
 * Xilinx AXI Performance Monitor Example
 *
 * Copyright (c) 2013 Xilinx Inc.
 *
 * The code may be used by anyone for any purpose and can serve as a
 * starting point for developing applications using Xilinx AXI
 * Performance Monitor.
 *
 * This example based on Xilinx AXI Performance Monitor UIO driver shows
 * sequence to read metrics from Xilinx AXI Performance Monitor IP.
 * User need to provide the uio device file with option -d:
 * main -d /dev/uio0, say /dev/uio0 as device file for AXI Performance
 * Monitor driver. User need not clear Interrupt Status Register after
 * waiting for interrupt on read since driver clears it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <stdint.h>
#include "xaxipmon.h"

#define MAP_SIZE 4096

void usage(void)
{
	printf("*argv[0] -d <UIO_DEV_FILE> -i|-o <VALUE>\n");
	printf(" -d UIO device file. e.g. /dev/uio0\n");
	return;
}

static void start(int fd)
{
	u8 slot = 2;
	int tmp;
	u32 isr;

	setmetrics(slot, XAPM_METRIC_SET_4, XAPM_METRIC_COUNTER_0);
	setsampleinterval(0x3FFFFFF);

	loadsic();

	intrenable(XAPM_IXR_SIC_OVERFLOW_MASK);

	intrglobalenable();

	enablemetricscounter();

	enablesic();

	isr = intrgetstatus();
	/* Wait for SIC overflow interrupt */
	if (read(fd, &tmp, 4) < 0)
		perror("Read\n");
	/* Driver clears the interrupt and occured interrupt status is
		stored in param->isr */
	isr = intrgetstatus();
	if (isr & XAPM_IXR_SIC_OVERFLOW_MASK)
		disablesic();

	disablemetricscounter();

	intrdisable(XAPM_IXR_SIC_OVERFLOW_MASK);

	intrglobaldisable();

	printf("Required metrics: %u\n",
		getsampledmetriccounter(XAPM_METRIC_COUNTER_0) *
		params->scalefactor);
}

int main(int argc, char *argv[])
{
	int c;
	char *uiod;
	int fd;

	while ((c = getopt(argc, argv, "d:h")) != -1) {
		switch (c) {
		case 'd':
			uiod = optarg;
			break;
		case 'h':
			usage();
			return 0;
		default:
			printf("invalid option: %c\n", (char)c);
			usage();
			return -1;
		}
	}

	/* Open the UIO device file */
	fd = open(uiod, O_RDWR);
	if (fd < 1) {
		perror(argv[0]);
		printf("Invalid UIO device file:%s.\n", uiod);
		usage();
		return -1;
	}

	baseaddr = (ulong)mmap(0, MAP_SIZE , PROT_READ|PROT_WRITE,
				MAP_SHARED , fd, 0);
	if ((u32 *)baseaddr == MAP_FAILED)
		perror("mmap failed\n");

	/* mmap the UIO device */
	params = (struct xapm_param *)mmap(0, MAP_SIZE , PROT_READ|PROT_WRITE,
				MAP_SHARED , fd, getpagesize());
	if (params == MAP_FAILED)
		perror("mmap failed\n");

	if (params->mode == 1)
		printf("AXI PMON is in Advanced Mode\n");
	else if (params->mode == 2)
		printf("AXI PMON is in Profile Mode\n");
	else
		printf("AXI PMON is in trace Mode\n");

	start(fd);

	close(fd);
	munmap((u32 *)baseaddr, MAP_SIZE);
	munmap(params, MAP_SIZE);

	return 0;
}
