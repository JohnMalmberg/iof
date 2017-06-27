/* Copyright (C) 2016-2017 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <CUnit/Basic.h>
#include "ctrl_fs_util.h"
#include "iof_ioctl.h"

static const char *cnss_prefix;
static char *mount_dir;

#define BUF_SIZE 4096

char big_string[BUF_SIZE];

static int init_suite(void)
{
	char buf1[CTRL_FS_MAX_LEN];
	char buf2[CTRL_FS_MAX_LEN];
	FILE *fp;
	int mnt_num = 0;
	int rc = CUE_SUCCESS;
	int id;

	rc = ctrl_fs_util_init(&cnss_prefix, &id);
	if (rc != 0) {
		printf("ERROR: Could not find cnss\n");
		return -1;
	}

	for (;;) {
		sprintf(buf1, "iof/projections/%d/mount_point", mnt_num++);
		rc = ctrl_fs_read_str(buf2, CTRL_FS_MAX_LEN, buf1);

		if (rc != 0) {
			printf("ERROR: No writeable mount found\n");
			return CUE_FOPEN_FAILED;
		}

		sprintf(buf1, "%s/ioil_test_file", buf2);
		fp = fopen(buf1, "w");
		if (fp == NULL) {
			printf("Skipping PA mount.  Can't write %s\n",
			       buf1);
			continue;
		}

		fclose(fp);

		mount_dir = strdup(buf2);
		if (mount_dir == NULL)
			rc = CUE_NOMEMORY;

		break;
	}

	return rc;
}

static int clean_suite(void)
{
	free(mount_dir);

	ctrl_fs_util_finalize();

	return CUE_SUCCESS;
}

static void gah_test(void)
{
	char buf[BUF_SIZE];
	struct iof_gah_info gah_info;
	int fd;
	int rc;

	snprintf(buf, BUF_SIZE, "%s/ioil_test_file", mount_dir);
	buf[BUF_SIZE - 1] = 0;

	fd = open(buf, O_RDONLY);

	CU_ASSERT_NOT_EQUAL(fd, -1);
	if (fd == -1) {
		printf("ERROR: Failed to open file for test: %s\n", buf);
		return;
	}

	rc = ioctl(fd, IOF_IOCTL_GAH, &gah_info);

	CU_ASSERT_EQUAL(rc, 0);
	CU_ASSERT_EQUAL(gah_info.version, IOF_IOCTL_VERSION);
	if (rc != 0)
		printf("ERROR: Failed ioctl test of IOF file: %s : %s\n",
		       buf, strerror(errno));
	else
		printf("ioctl returned " GAH_PRINT_STR "\n",
		       GAH_PRINT_VAL(gah_info.gah));

	/* Run ioctl test on stdout.  Should fail */
	rc = ioctl(1, IOF_IOCTL_GAH, &gah_info);

	CU_ASSERT_NOT_EQUAL(rc, 0);

	if (rc == 0)
		printf("ERROR: Failed ioctl test of non-IOF file: %s\n", buf);
}

static void do_write_tests(int fd, char *buf, size_t len)
{
	struct iovec iov[2];
	ssize_t bytes;
	off_t offset;
	int rc;

	bytes = write(fd, buf, len);
	printf("Wrote %zd bytes, expected %zu\n", bytes, len);
	CU_ASSERT_EQUAL(bytes, len);

	offset = lseek(fd, 0, SEEK_CUR);
	printf("Seek offset is %zd, expected %zu\n", offset, len);
	CU_ASSERT_EQUAL(offset, len);

	bytes = pwrite(fd, buf, len, len);
	printf("Wrote %zd bytes, expected %zu\n", bytes, len);
	CU_ASSERT_EQUAL(bytes, len);

	offset = lseek(fd, 0, SEEK_CUR);
	printf("Seek offset is %zd, expected %zu\n", offset, len);
	CU_ASSERT_EQUAL(offset, len);

	offset = lseek(fd, len, SEEK_CUR);
	printf("Seek offset is %zd, expected %zu\n", offset, len * 2);
	CU_ASSERT_EQUAL(offset, len * 2);

	iov[0].iov_len = len;
	iov[0].iov_base = buf;
	iov[1].iov_len = len;
	iov[1].iov_base = buf;

	bytes = writev(fd, iov, 2);
	printf("Wrote %zd bytes, expected %zu\n", bytes, len * 2);
	CU_ASSERT_EQUAL(bytes, len * 2);

	offset = lseek(fd, 0, SEEK_END);
	printf("Seek offset is %zd, expected %zu\n", offset, len * 4);
	CU_ASSERT_EQUAL(offset, len * 4);

	memset(big_string, 'a', BUF_SIZE - 1);
	big_string[BUF_SIZE - 1] = 0;
	bytes = write(fd, big_string, BUF_SIZE);

	rc = close(fd);
	printf("Closed file, rc = %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);
}

static void do_read_tests(const char *fname, size_t len)
{
	char buf[BUF_SIZE * 2];
	char buf2[len + 1];
	struct iovec iov[2];
	ssize_t bytes;
	off_t offset;
	int pos;
	int fd;
	int rc;

	memset(buf, 0, sizeof(buf));
	memset(buf2, 0, sizeof(buf2));

	fd = open(fname, O_RDONLY);
	printf("Opened %s, fd = %d\n", fname, fd);
	CU_ASSERT_NOT_EQUAL_FATAL(fd, -1);

	bytes = read(fd, buf, BUF_SIZE * 2);
	printf("Read %zd bytes, expected %zu\n", bytes, BUF_SIZE + (len * 4));
	CU_ASSERT_EQUAL(bytes, BUF_SIZE + (len * 4));

	offset = lseek(fd, 0, SEEK_CUR);
	printf("Seek offset is %zd, expected %zu\n", offset,
	       BUF_SIZE + (len * 4));
	CU_ASSERT_EQUAL(offset, BUF_SIZE + len * 4);

	pos = 0;
	while (pos < (len * 4)) {
		CU_ASSERT_NSTRING_EQUAL(fname, buf + pos, len);
		pos += len;
	}
	CU_ASSERT_NSTRING_EQUAL(big_string, buf + pos, BUF_SIZE);

	offset = lseek(fd, 0, SEEK_SET);
	printf("Seek offset is %zd, expected 0\n", offset);
	CU_ASSERT_EQUAL(offset, 0);

	memset(buf, 0, sizeof(buf));

	bytes = pread(fd, buf, len, len);
	printf("Read %zd bytes, expected %zu\n", bytes, len);
	CU_ASSERT_EQUAL(bytes, len);

	CU_ASSERT_STRING_EQUAL(fname, buf);

	offset = lseek(fd, 0, SEEK_CUR);
	printf("Seek offset is %zd, expected 0\n", offset);
	CU_ASSERT_EQUAL(offset, 0);

	memset(buf, 0, sizeof(buf));

	iov[0].iov_len = len;
	iov[0].iov_base = buf2;
	iov[1].iov_len = len;
	iov[1].iov_base = buf;

	bytes = readv(fd, iov, 2);
	printf("Read %zd bytes, expected %zu\n", bytes, len * 2);
	CU_ASSERT_EQUAL(bytes, len * 2);

	CU_ASSERT_STRING_EQUAL(fname, buf);
	CU_ASSERT_STRING_EQUAL(fname, buf2);

	rc = close(fd);
	printf("Closed file, rc = %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);
}

static void do_misc_tests(const char *fname, size_t len)
{
	struct stat stat_info;
	void *address;
	char buf[BUF_SIZE];
	FILE *fp;
	size_t items;
	int rc;
	int fd;
	int new_fd;

	memset(buf, 0, sizeof(buf));

	rc = stat(fname, &stat_info);
	CU_ASSERT_EQUAL_FATAL(rc, 0);
	CU_ASSERT_NOT_EQUAL_FATAL(stat_info.st_size, 0);
	fd = open(fname, O_RDWR);
	printf("Opened %s, fd = %d\n", fname, fd);
	CU_ASSERT_NOT_EQUAL(fd, -1);

	new_fd = dup(fd);
	printf("Duped %d, new_fd = %d\n", fd, new_fd);
	CU_ASSERT_NOT_EQUAL(new_fd, -1);

	rc = close(new_fd);
	printf("close returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	new_fd = dup2(fd, 80);
	printf("dup2(%d, 80) returned %d\n", fd, new_fd);
	CU_ASSERT_EQUAL(new_fd, 80);

	rc = close(new_fd);
	printf("close returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	new_fd = fcntl(fd, F_DUPFD, 80);
	printf("fcntl(%d, F_DUPFD, 80) returned %d\n", fd, new_fd);
	CU_ASSERT(new_fd >= 80);

	rc = close(new_fd);
	printf("close returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	new_fd = fcntl(fd, F_DUPFD_CLOEXEC, 90);
	printf("fcntl(%d, F_DUPFD, 90) returned %d\n", fd, new_fd);
	CU_ASSERT(new_fd >= 90);

	rc = close(new_fd);
	printf("close returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	rc = fsync(fd);
	printf("fsync returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	rc = fdatasync(fd);
	printf("fdatasync returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	address = mmap(NULL, BUF_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, 0);

	printf("mmap returned %p\n", address);
	if (address == MAP_FAILED && errno == ENODEV) {
		printf("mmap not supported on file system\n");
		goto skip_mmap;
	}
	CU_ASSERT_PTR_NOT_EQUAL_FATAL(address, MAP_FAILED);

	memset(address, '@', BUF_SIZE);

	rc = munmap(address, BUF_SIZE);
	printf("munmap returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	fp = fdopen(fd, "r");
	printf("fdopen returned %p\n", fp);
	CU_ASSERT_PTR_NOT_EQUAL(fp, NULL);

	if (fp != NULL) {
		items = fread(buf, 1, 8, fp);
		printf("Read %zd items, expected 8\n", items);
		CU_ASSERT_EQUAL(items, 8);

		CU_ASSERT_STRING_EQUAL(buf, "@@@@@@@@");
	}
skip_mmap:
	rc = close(fd);
	printf("close returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	fd = open(fname, O_RDWR);
	printf("Opened %s, fd = %d\n", fname, fd);
	CU_ASSERT_NOT_EQUAL(fd, -1);

	rc = fcntl(fd, F_SETFL, O_APPEND);
	printf("fcntl F_SETFL returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	rc = fcntl(fd, F_GETFL);
	printf("fcntl F_GETFL returned %d\n", rc);
	CU_ASSERT(rc & O_APPEND);

	rc = close(fd);
	printf("close returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);
}

/* Simple sanity test to ensure low-level POSIX APIs work */
void sanity(void)
{
	char buf[BUF_SIZE];
	size_t len;
	int fd;

	len = strlen(mount_dir);
	snprintf(buf, BUF_SIZE - len, "%s/sanity", mount_dir);
	buf[BUF_SIZE - 1] = 0;

	unlink(buf);
	len = strlen(buf);
	fd = open(buf, O_WRONLY|O_CREAT|O_TRUNC, 0600);
	CU_ASSERT_NOT_EQUAL_FATAL(fd, -1);

	do_write_tests(fd, buf, len);
	do_read_tests(buf, len);
	do_misc_tests(buf, len);
}

int main(int argc, char **argv)
{
	int failures;
	CU_pSuite pSuite = NULL;

	if (CU_initialize_registry() != CUE_SUCCESS) {
		printf("CU_initialize_registry() failed\n");
		return CU_get_error();
	}

	pSuite = CU_add_suite("IO interception library test",
			      init_suite, clean_suite);

	if (!pSuite) {
		CU_cleanup_registry();
		printf("CU_add_suite() failed\n");
		return CU_get_error();
	}

	if (!CU_add_test(pSuite, "gah ioctl test", gah_test) ||
	    !CU_add_test(pSuite, "libioil sanity test", sanity)) {
		CU_cleanup_registry();
		printf("CU_add_test() failed\n");
		return CU_get_error();
	}

	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	failures = CU_get_number_of_failures();
	CU_cleanup_registry();

	return failures;
}
