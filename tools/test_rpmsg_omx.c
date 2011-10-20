/*
 * test_rpmsg_omx
 *
 * user space testing tool for the rpmsg_omx driver
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of Texas Instruments Incorporated nor the names of
 *   its contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/eventfd.h>

#include "../include/linux/rpmsg_omx.h"

static pthread_t listener;
static int omxfd;
static int die_now_fd;

static void *listener_cb(void *arg)
{
	fd_set rfds;
	struct timeval tv;
	int ret = 0, maxfd;
	char buf[512];

	do {
		/* Wait for OMX messages or self die_now notification */
		FD_ZERO(&rfds);
		FD_SET(omxfd, &rfds);
		FD_SET(die_now_fd, &rfds);

		maxfd = (die_now_fd > omxfd ? die_now_fd : omxfd) + 1;
		ret = select(maxfd, &rfds, NULL, NULL, NULL);
		if (ret == -1) {
			perror("select()");
			break;
		}

		if (FD_ISSET(omxfd, &rfds)) {
			ret = read(omxfd, buf, sizeof(buf));
			if (ret < 0) {
				perror("Can't connect to OMX instance");
				break;
			}

			printf("RX: %s\n", buf);
		}

		if (FD_ISSET(die_now_fd, &rfds)) {
			printf("Need to die!\n");
			break;
		}

	} while (1);

	return (void *)ret;
}

int send_messages(void)
{
	const char *msg = "Hello there!";
	int ret, i;

	/* blather */
	for (i = 0; i < 10; i++) {
		ret = write(omxfd, msg, strlen(msg) + 1);
		if (ret < 0) {
			perror("Can't connect to OMX instance");
			return -1;
		}

		printf("TX: %s\n", msg);
		sleep(5);
	}

	return i;
}

int main(void)
{
	int ret;
	uint64_t die_event = 1;
	ssize_t s;
	struct omx_conn_req connreq = { .name = "OMX" };

	/* open the first OMX device */
	omxfd = open("/dev/rpmsg-omx0", O_RDWR);
	if (omxfd < 0) {
		perror("Can't open OMX device");
		return 1;
	}

	printf("Connecting to %s\n", connreq.name);
	/* connect to an h264_decode instance */
	ret = ioctl(omxfd, OMX_IOCCONNECT, &connreq);
	if (ret < 0) {
		perror("Can't connect to OMX instance");
		return 1;
	}

	printf("Connected!\n", connreq.name);

	die_now_fd = eventfd(0, 0);
	if (die_now_fd < 0) {
		perror("eventfd failed");
		return 1;
	}

	ret = pthread_create(&listener, NULL, listener_cb, NULL);
	if (ret) {
		printf("can't spawn thread: %s\n", strerror(ret));
		return 1;
	}

	ret = send_messages();
	if (ret < 0)
		return 1;

	/* terminate connection and destroy OMX instance */
	ret = close(omxfd);
	if (ret < 0) {
		perror("Can't close OMX fd ??");
		return 1;
	}

	s = write(die_now_fd, &die_event, sizeof(uint64_t));
	if (s != sizeof(uint64_t)) {
		printf("failed to write die_now event\n");
		return 1;
	}

	ret = pthread_join(listener, NULL);
	if (ret) {
		printf("can't join thread: %s\n", strerror(ret));
		return 1;
	}

	printf("Bye!\n");

	return 0;
}
