// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdarg.h>

#include "luo_test_utils.h"

int luo_open_device(void)
{
	return open(LUO_DEVICE, O_RDWR);
}

int luo_create_session(int luo_fd, const char *name)
{
	struct liveupdate_ioctl_create_session arg = { .size = sizeof(arg) };

	snprintf((char *)arg.name, LIVEUPDATE_SESSION_NAME_LENGTH, "%.*s",
		 LIVEUPDATE_SESSION_NAME_LENGTH - 1, name);

	if (ioctl(luo_fd, LIVEUPDATE_IOCTL_CREATE_SESSION, &arg) < 0)
		return -errno;

	return arg.fd;
}

int luo_retrieve_session(int luo_fd, const char *name)
{
	struct liveupdate_ioctl_retrieve_session arg = { .size = sizeof(arg) };

	snprintf((char *)arg.name, LIVEUPDATE_SESSION_NAME_LENGTH, "%.*s",
		 LIVEUPDATE_SESSION_NAME_LENGTH - 1, name);

	if (ioctl(luo_fd, LIVEUPDATE_IOCTL_RETRIEVE_SESSION, &arg) < 0)
		return -errno;

	return arg.fd;
}

int create_and_preserve_memfd(int session_fd, int token, const char *data)
{
	struct liveupdate_session_preserve_fd arg = { .size = sizeof(arg) };
	long page_size = sysconf(_SC_PAGE_SIZE);
	void *map = MAP_FAILED;
	int mfd = -1, ret = -1;

	mfd = memfd_create("test_mfd", 0);
	if (mfd < 0)
		return -errno;

	if (ftruncate(mfd, page_size) != 0)
		goto out;

	map = mmap(NULL, page_size, PROT_WRITE, MAP_SHARED, mfd, 0);
	if (map == MAP_FAILED)
		goto out;

	snprintf(map, page_size, "%s", data);
	munmap(map, page_size);

	arg.fd = mfd;
	arg.token = token;
	if (ioctl(session_fd, LIVEUPDATE_SESSION_PRESERVE_FD, &arg) < 0)
		goto out;

	ret = 0;
out:
	if (ret != 0 && errno != 0)
		ret = -errno;
	if (mfd >= 0)
		close(mfd);
	return ret;
}

int restore_and_verify_memfd(int session_fd, int token,
			     const char *expected_data)
{
	struct liveupdate_session_retrieve_fd arg = { .size = sizeof(arg) };
	long page_size = sysconf(_SC_PAGE_SIZE);
	void *map = MAP_FAILED;
	int mfd = -1, ret = -1;

	arg.token = token;
	if (ioctl(session_fd, LIVEUPDATE_SESSION_RETRIEVE_FD, &arg) < 0)
		return -errno;
	mfd = arg.fd;

	map = mmap(NULL, page_size, PROT_READ, MAP_SHARED, mfd, 0);
	if (map == MAP_FAILED)
		goto out;

	if (expected_data && strcmp(expected_data, map) != 0) {
		ksft_print_msg("Data mismatch! Expected '%s', Got '%s'\n",
			       expected_data, (char *)map);
		ret = -EINVAL;
		goto out_munmap;
	}

	ret = mfd;
out_munmap:
	munmap(map, page_size);
out:
	if (ret < 0 && errno != 0)
		ret = -errno;
	if (ret < 0 && mfd >= 0)
		close(mfd);
	return ret;
}

int luo_session_finish(int session_fd)
{
	struct liveupdate_session_finish arg = { .size = sizeof(arg) };

	if (ioctl(session_fd, LIVEUPDATE_SESSION_FINISH, &arg) < 0)
		return -errno;

	return 0;
}

void create_state_file(int luo_fd, const char *session_name, int token,
		       int next_stage)
{
	char buf[32];
	int state_session_fd;

	state_session_fd = luo_create_session(luo_fd, session_name);
	if (state_session_fd < 0)
		fail_exit("luo_create_session for state tracking");

	snprintf(buf, sizeof(buf), "%d", next_stage);
	if (create_and_preserve_memfd(state_session_fd, token, buf) < 0)
		fail_exit("create_and_preserve_memfd for state tracking");

	/*
	 * DO NOT close session FD, otherwise it is going to be unpreserved
	 */
}

void restore_and_read_stage(int state_session_fd, int token, int *stage)
{
	char buf[32] = {0};
	int mfd;

	mfd = restore_and_verify_memfd(state_session_fd, token, NULL);
	if (mfd < 0)
		fail_exit("failed to restore state memfd");

	if (read(mfd, buf, sizeof(buf) - 1) < 0)
		fail_exit("failed to read state mfd");

	*stage = atoi(buf);

	close(mfd);
}
