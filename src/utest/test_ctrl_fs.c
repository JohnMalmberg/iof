#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <utime.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "log.h"
#include "ctrl_common.h"

int cnss_shutdown(void *arg)
{
	IOF_LOG_INFO("shutdown");
	return 0;
}

int cnss_client_attach(int client_id, void *arg)
{
	IOF_LOG_INFO("attached %d", client_id);
	return 0;
}

int cnss_client_detach(int client_id, void *arg)
{
	IOF_LOG_INFO("detached %d", client_id);
	return 0;
}

static int read_foo(char *buf, size_t len, void *arg)
{
	int *foo = (int *)arg;

	snprintf(buf, len, "%d\n", *foo);

	return 0;
}

static int write_foo(const char *value, void *arg)
{
	int val = atoi(value);
	int *foo = (int *)arg;

	*foo += val;

	return 0;
}

static int check_file_read(const char *prefix, const char *fname,
			   const char *expected, const char *source,
			   int line)
{
	char buf[256]; /* all of our values are less than 256 */
	int bytes;
	int fd;

	IOF_LOG_INFO("Run check at %s:%d\n", source, line);

	sprintf(buf, "%s%s", prefix, fname);

	fd = open(buf, O_RDONLY);

	if (fd == -1) {
		if (expected == NULL)
			return 0; /* Expected a failure */
		printf("Could not open %s.  Test %s:%d failed\n", fname,
		       source, line);
		return 1;
	}

	if (expected == NULL) {
		close(fd);
		printf("Should not be able to open %s.  Test %s:%d failed\n",
		       fname, source, line);
		return 1;
	}

	bytes = read(fd, buf, 256);

	close(fd);

	if (bytes == -1) {
		printf("Error reading %s.  Test %s:%d failed\n", fname, source,
		       line);
		return 1;
	}

	buf[bytes] = 0;

	IOF_LOG_INFO("Finished reading %s", buf);
	IOF_LOG_INFO("Comparing to %s", expected);

	if (strcmp(buf, expected) != 0) {
		printf("Value unexpected in %s: (%s != %s).  Test"
		       " %s:%d failed\n", fname, buf, expected, source, line);
		return 1;
	}

	IOF_LOG_INFO("Done with check at %s:%d\n", source, line);

	return 0;
}

static int check_file_write(const char *prefix, const char *fname,
			    const char *value, const char *source,
			    int line)
{
	char buf[256]; /* all of our values are less than 256 */
	int bytes;
	int fd;

	IOF_LOG_INFO("Run check at %s:%d\n", source, line);

	sprintf(buf, "%s%s", prefix, fname);

	fd = open(buf, O_WRONLY|O_TRUNC);

	if (fd == -1) {
		if (value == NULL)
			return 0; /* Expected a failure */
		printf("Could not open %s.  Test %s:%d failed\n", fname,
		       source, line);
		return 1;
	}

	if (value == NULL) {
		close(fd);
		printf("Should not be able to open %s.  Test %s:%d failed\n",
		       fname, source, line);
		return 1;
	}

	bytes = write(fd, value, strlen(value));

	close(fd);

	if (bytes == -1) {
		printf("Error writing %s.  Test %s:%d failed\n", fname, source,
		       line);
		return 1;
	}

	if (bytes != strlen(value)) {
		printf("Error writing %s to %s.  Test %s:%d failed\n", value,
		       fname, source, line);
		return 1;
	}

	IOF_LOG_INFO("Done with check at %s:%d\n", source, line);

	return 0;
}


#define CHECK_FILE_READ(prefix, name, expected) \
	check_file_read(prefix, name, expected, __FILE__, __LINE__)

#define CHECK_FILE_WRITE(prefix, name, value) \
	check_file_write(prefix, name, value, __FILE__, __LINE__)

static int run_tests(const char *ctrl_prefix)
{
	int num_failures = 0;

	num_failures += CHECK_FILE_READ(ctrl_prefix, "/class/bar/hello",
					"Hello World\n");
	num_failures += CHECK_FILE_READ(ctrl_prefix, "/class/bar/foo", "0\n");
	num_failures += CHECK_FILE_WRITE(ctrl_prefix, "/class/bar/foo", "10");
	num_failures += CHECK_FILE_READ(ctrl_prefix, "/class/bar/foo", "10\n");
	num_failures += CHECK_FILE_WRITE(ctrl_prefix, "/class/bar/foo", "55");
	num_failures += CHECK_FILE_READ(ctrl_prefix, "/class/bar/foo", "65\n");
	num_failures += CHECK_FILE_WRITE(ctrl_prefix, "/class/bar/foo", "-12");
	num_failures += CHECK_FILE_READ(ctrl_prefix, "/class/bar/foo", "53\n");
	num_failures += CHECK_FILE_READ(ctrl_prefix, "/client", "3\n");
	num_failures += CHECK_FILE_READ(ctrl_prefix, "/client", "4\n");
	num_failures += CHECK_FILE_READ(ctrl_prefix, "/client", "5\n");
	num_failures += CHECK_FILE_READ(ctrl_prefix, "/client", "6\n");

	return num_failures;
}

int main(int argc, char **argv)
{
	char *prefix;
	char buf[32];
	char ctrl_prefix[32];
	int foo = 0;
	int opt;
	int num_failures;
	bool interactive = false;

	for (;;) {
		opt = getopt(argc, argv, "i");
		if (opt == -1)
			break;
		switch (opt) {
		case 'i':
			interactive = true;
			break;
		default:
			printf("Usage: %s [-i]\n", argv[0]);
			printf("\n    -i  Interactive run\n");
			return -1;
		}
	}

	strcpy(buf, "/tmp/XXXXXX");

	prefix = mkdtemp(buf);

	if (prefix == NULL) {
		printf("Could not allocate temporary directory for tests\n");
		return -1;
	}

	printf("Testing ctrl_fs in %s\n", prefix);

	setenv("MCL_LOG_DIR", prefix, 1);
	setenv("MCL_LOG_LEVEL", "4", 1);
	iof_log_init("ctrl_fs_test");

	sprintf(ctrl_prefix, "%s/.ctrl", prefix);

	ctrl_fs_start(ctrl_prefix);

	register_cnss_controls(3, NULL);

	ctrl_register_variable("/class/bar/foo", read_foo,
			       write_foo, NULL, &foo);
	ctrl_register_constant("/class/bar/hello", "Hello World\n");

	num_failures = run_tests(ctrl_prefix);
	if (num_failures != 0)
		printf("%d ctrl_fs tests failed\n", num_failures);
	else
		printf("All ctrl_fs tests passed\n");

	if (!interactive) { /* Invoke shutdown */
		sprintf(ctrl_prefix, "%s/.ctrl/shutdown", prefix);
		utime(ctrl_prefix, NULL);
	}

	ctrl_fs_wait();

	iof_log_close();

	if (!interactive) { /* Delete the temporary directory */
		sprintf(ctrl_prefix, "rm -rf %s", prefix);
		system(ctrl_prefix);
	}

	return num_failures;
}