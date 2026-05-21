// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <linux/ioctl.h>

#define MYFS_NAME_LEN		16
#define MYFS_MAX_FILES		15
#define MYFS_SECTOR_SIZE	512
#define MYFS_IOCTL_MAGIC	0x4D59

struct myfs_file_hash {
	char name[MYFS_NAME_LEN];
	uint32_t hash;
};

struct myfs_hlist {
	uint32_t count;
	struct myfs_file_hash hashes[MYFS_MAX_FILES];
};

struct myfs_mapping {
	uint32_t offset;
	uint32_t size;
};

struct myfs_mapping_req {
	char name[MYFS_NAME_LEN];
	struct myfs_mapping mapping;
};

#define MYFS_IOCTL_ZERO_FILES	_IO(MYFS_IOCTL_MAGIC, 0)
#define MYFS_IOCTL_ERASE_FS	_IO(MYFS_IOCTL_MAGIC, 1)
#define MYFS_IOCTL_LIST_HASHES	_IOR(MYFS_IOCTL_MAGIC, 2, struct myfs_hlist)
#define MYFS_IOCTL_GET_MAPPING	_IOWR(MYFS_IOCTL_MAGIC, 3, struct myfs_mapping_req)

/*
 * zlib-compatible CRC32 to match kernel crc32().
 * Initial value is 0 and no final XOR.
 */
static uint32_t crc32_table[256];
static int crc32_table_init;

static void init_crc32_table(void)
{
	uint32_t c;
	int n, k;

	for (n = 0; n < 256; n++) {
		c = (uint32_t)n;
		for (k = 0; k < 8; k++) {
			if (c & 1)
				c = 0xEDB88320L ^ (c >> 1);
			else
				c = c >> 1;
		}
		crc32_table[n] = c;
	}
	crc32_table_init = 1;
}

static uint32_t calc_crc32(const void *data, size_t len)
{
	const uint8_t *buf = data;
	uint32_t crc = 0;
	size_t i;

	if (!crc32_table_init)
		init_crc32_table();

	for (i = 0; i < len; i++)
		crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);

	return crc;
}

static void auto_test(const char *mnt)
{
	DIR *dir = opendir(mnt);
	struct dirent *ent;
	int count = 0;

	if (!dir) {
		perror("opendir");
		exit(1);
	}

	while ((ent = readdir(dir))) {
		char path[256];
		int fd;
		uint32_t val, read_val;
		ssize_t n;

		if (ent->d_type != DT_REG)
			continue;

		snprintf(path, sizeof(path), "%s/%s", mnt, ent->d_name);
		fd = open(path, O_RDWR);
		if (fd < 0) {
			perror(path);
			continue;
		}

		val = (uint32_t)rand();
		n = write(fd, &val, sizeof(val));
		if (n != sizeof(val)) {
			fprintf(stderr, "FAIL: %s write returned %zd\n", path, n);
			close(fd);
			closedir(dir);
			exit(1);
		}

		if (lseek(fd, 0, SEEK_SET) < 0) {
			perror("lseek");
			close(fd);
			closedir(dir);
			exit(1);
		}

		n = read(fd, &read_val, sizeof(read_val));
		if (n != sizeof(read_val) || val != read_val) {
			fprintf(stderr, "FAIL: %s expected %u got %u (n=%zd)\n",
				path, val, read_val, n);
			close(fd);
			closedir(dir);
			exit(1);
		}

		printf("PASS: %s wrote=%u read=%u\n", path, val, read_val);
		close(fd);
		count++;
	}
	closedir(dir);

	if (count == 0) {
		fprintf(stderr, "FAIL: no regular files found in %s\n", mnt);
		exit(1);
	}
	printf("Auto-test passed on %d files\n", count);
}

static int open_any_file(const char *mnt)
{
	DIR *dir = opendir(mnt);
	struct dirent *ent;
	char path[256];
	int fd = -1;

	if (!dir)
		return -1;

	while ((ent = readdir(dir))) {
		if (ent->d_type != DT_REG)
			continue;
		snprintf(path, sizeof(path), "%s/%s", mnt, ent->d_name);
		fd = open(path, O_RDWR);
		if (fd >= 0)
			break;
	}
	closedir(dir);
	return fd;
}

static int open_first_file(const char *mnt, char *path, size_t path_len)
{
	DIR *dir = opendir(mnt);
	struct dirent *ent;
	int fd = -1;

	if (!dir)
		return -1;

	while ((ent = readdir(dir))) {
		if (ent->d_type != DT_REG)
			continue;
		snprintf(path, path_len, "%s/%s", mnt, ent->d_name);
		fd = open(path, O_RDWR);
		if (fd >= 0)
			break;
	}
	closedir(dir);
	return fd;
}

static int get_ioctl_fd(const char *mnt)
{
	int fd = open_any_file(mnt);

	if (fd < 0) {
		fprintf(stderr, "FAIL: no file to ioctl on\n");
		exit(1);
	}
	return fd;
}

static void ioctl_zero(const char *mnt)
{
	int fd = get_ioctl_fd(mnt);
	int ret;

	ret = ioctl(fd, MYFS_IOCTL_ZERO_FILES);
	if (ret < 0) {
		perror("MYFS_IOCTL_ZERO_FILES");
		close(fd);
		exit(1);
	}
	printf("IOCTL zero files: OK\n");
	close(fd);
}

static void ioctl_erase(const char *mnt)
{
	int fd = get_ioctl_fd(mnt);
	int ret;

	ret = ioctl(fd, MYFS_IOCTL_ERASE_FS);
	if (ret < 0) {
		perror("MYFS_IOCTL_ERASE_FS");
		close(fd);
		exit(1);
	}
	printf("IOCTL erase fs: OK\n");
	close(fd);
}

static void ioctl_list_hashes(const char *mnt)
{
	int fd = get_ioctl_fd(mnt);
	struct myfs_hlist list;
	unsigned int i;
	int ret;

	ret = ioctl(fd, MYFS_IOCTL_LIST_HASHES, &list);
	if (ret < 0) {
		perror("MYFS_IOCTL_LIST_HASHES");
		close(fd);
		exit(1);
	}

	printf("IOCTL list hashes (%u files):\n", list.count);
	for (i = 0; i < list.count; i++)
		printf("  %s: hash=0x%08x\n", list.hashes[i].name, list.hashes[i].hash);
	close(fd);
}

static void ioctl_get_mapping(const char *mnt, const char *filename)
{
	int fd = get_ioctl_fd(mnt);
	struct myfs_mapping_req req;
	int ret;

	strncpy(req.name, filename, sizeof(req.name) - 1);
	req.name[sizeof(req.name) - 1] = '\0';

	ret = ioctl(fd, MYFS_IOCTL_GET_MAPPING, &req);
	if (ret < 0) {
		perror("MYFS_IOCTL_GET_MAPPING");
		close(fd);
		exit(1);
	}

	printf("IOCTL mapping for %s: offset=%u size=%u sectors\n",
	       filename, req.mapping.offset, req.mapping.size);
	close(fd);
}

static void concurrent_test(const char *mnt, int nproc)
{
	DIR *dir = opendir(mnt);
	struct dirent *ent;
	char **files = NULL;
	int nfiles = 0;
	int capacity = 0;
	int i;

	if (!dir) {
		perror("opendir");
		exit(1);
	}

	while ((ent = readdir(dir))) {
		char path[256];

		if (ent->d_type != DT_REG)
			continue;
		if (nfiles >= capacity) {
			capacity = capacity ? capacity * 2 : 8;
			files = realloc(files, capacity * sizeof(char *));
			if (!files) {
				perror("realloc");
				exit(1);
			}
		}
		snprintf(path, sizeof(path), "%s/%s", mnt, ent->d_name);
		files[nfiles] = strdup(path);
		nfiles++;
	}
	closedir(dir);

	if (nfiles == 0) {
		fprintf(stderr, "FAIL: no regular files found in %s\n", mnt);
		exit(1);
	}
	if (nproc > nfiles)
		nproc = nfiles;

	for (i = 0; i < nproc; i++) {
		pid_t pid = fork();

		if (pid < 0) {
			perror("fork");
			exit(1);
		}
		if (pid == 0) {
			int fd = open(files[i], O_RDWR);
			uint32_t pattern = 0xDEADBEEF + (uint32_t)i;
			ssize_t n;

			if (fd < 0) {
				perror(files[i]);
				_exit(1);
			}
			n = write(fd, &pattern, sizeof(pattern));
			if (n != sizeof(pattern)) {
				fprintf(stderr, "FAIL: %s write returned %zd\n", files[i], n);
				close(fd);
				_exit(1);
			}
			close(fd);
			_exit(0);
		}
	}

	for (i = 0; i < nproc; i++) {
		int status;

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			fprintf(stderr, "FAIL: child %d exited with error\n", i);
			exit(1);
		}
	}

	for (i = 0; i < nproc; i++) {
		int fd = open(files[i], O_RDONLY);
		uint32_t expected = 0xDEADBEEF + (uint32_t)i;
		uint32_t read_val;
		ssize_t n;

		if (fd < 0) {
			perror(files[i]);
			exit(1);
		}
		n = read(fd, &read_val, sizeof(read_val));
		if (n != sizeof(read_val) || read_val != expected) {
			fprintf(stderr, "FAIL: %s expected 0x%08x got 0x%08x (n=%zd)\n",
				files[i], expected, read_val, n);
			close(fd);
			exit(1);
		}
		close(fd);
		printf("PASS: %s concurrent write verified\n", files[i]);
	}

	for (i = 0; i < nfiles; i++)
		free(files[i]);
	free(files);
	printf("Concurrent test passed on %d files\n", nproc);
}

static void ioctl_invalid(const char *mnt)
{
	int fd = get_ioctl_fd(mnt);
	int ret;

	ret = ioctl(fd, _IO(0xFF, 99));
	if (ret >= 0 || errno != ENOTTY) {
		fprintf(stderr, "FAIL: invalid ioctl should return ENOTTY, got ret=%d errno=%d\n",
			ret, errno);
		close(fd);
		exit(1);
	}
	printf("PASS: invalid ioctl returned ENOTTY\n");
	close(fd);
}

static void ioctl_mapping_notfound(const char *mnt)
{
	int fd = get_ioctl_fd(mnt);
	struct myfs_mapping_req req;
	int ret;

	strcpy(req.name, "nonexistent");
	ret = ioctl(fd, MYFS_IOCTL_GET_MAPPING, &req);
	if (ret >= 0 || errno != ENOENT) {
		fprintf(stderr, "FAIL: mapping notfound should return ENOENT, got ret=%d errno=%d\n",
			ret, errno);
		close(fd);
		exit(1);
	}
	printf("PASS: mapping notfound returned ENOENT\n");
	close(fd);
}

static void edge_test(const char *mnt)
{
	int fd;
	char path[256];
	ssize_t n;
	uint8_t buf = 0xAB;
	DIR *dir = opendir(mnt);
	struct dirent *ent;

	if (!dir) {
		perror("opendir");
		exit(1);
	}

	while ((ent = readdir(dir))) {
		if (ent->d_type == DT_REG) {
			snprintf(path, sizeof(path), "%s/%s", mnt, ent->d_name);
			break;
		}
	}
	closedir(dir);
	if (!ent) {
		fprintf(stderr, "FAIL: no regular files found\n");
		exit(1);
	}

	fd = open(path, O_RDWR);
	if (fd < 0) {
		perror(path);
		exit(1);
	}

	n = write(fd, &buf, 1);
	if (n != 1) {
		fprintf(stderr, "FAIL: write returned %zd\n", n);
		close(fd);
		exit(1);
	}

	if (lseek(fd, 0, SEEK_END) < 0) {
		perror("lseek");
		close(fd);
		exit(1);
	}
	n = read(fd, &buf, 1);
	if (n != 0) {
		fprintf(stderr, "FAIL: read at EOF should return 0, got %zd\n", n);
		close(fd);
		exit(1);
	}
	printf("PASS: read at EOF returned 0\n");

	n = write(fd, &buf, 1);
	if (n >= 0 || errno != ENOSPC) {
		fprintf(stderr, "FAIL: write at EOF should return ENOSPC, got n=%zd errno=%d\n",
			n, errno);
		close(fd);
		exit(1);
	}
	printf("PASS: write at EOF returned ENOSPC\n");

	close(fd);
}

/* New tests --------------------------------------------------------------- */

static void verify_zero(const char *mnt)
{
	DIR *dir = opendir(mnt);
	struct dirent *ent;
	int count = 0;
	uint8_t zero[MYFS_SECTOR_SIZE];

	memset(zero, 0, sizeof(zero));

	if (!dir) {
		perror("opendir");
		exit(1);
	}

	while ((ent = readdir(dir))) {
		char path[256];
		int fd;
		ssize_t n;
		uint8_t buf[MYFS_SECTOR_SIZE];

		if (ent->d_type != DT_REG)
			continue;

		snprintf(path, sizeof(path), "%s/%s", mnt, ent->d_name);
		fd = open(path, O_RDONLY);
		if (fd < 0) {
			perror(path);
			continue;
		}

		n = read(fd, buf, sizeof(buf));
		if (n < 0) {
			perror("read");
			close(fd);
			closedir(dir);
			exit(1);
		}

		if (n > 0 && memcmp(buf, zero, n) != 0) {
			fprintf(stderr, "FAIL: %s is not zeroed\n", path);
			close(fd);
			closedir(dir);
			exit(1);
		}

		printf("PASS: %s is zeroed\n", path);
		close(fd);
		count++;
	}
	closedir(dir);

	if (count == 0) {
		fprintf(stderr, "FAIL: no regular files found\n");
		exit(1);
	}
	printf("Verify zero passed on %d files\n", count);
}

static void write_pattern(const char *mnt, uint32_t pattern)
{
	DIR *dir = opendir(mnt);
	struct dirent *ent;
	int count = 0;

	if (!dir) {
		perror("opendir");
		exit(1);
	}

	while ((ent = readdir(dir))) {
		char path[256];
		int fd;
		ssize_t n;

		if (ent->d_type != DT_REG)
			continue;

		snprintf(path, sizeof(path), "%s/%s", mnt, ent->d_name);
		fd = open(path, O_RDWR);
		if (fd < 0) {
			perror(path);
			continue;
		}

		n = write(fd, &pattern, sizeof(pattern));
		if (n != sizeof(pattern)) {
			fprintf(stderr, "FAIL: %s write returned %zd\n", path, n);
			close(fd);
			closedir(dir);
			exit(1);
		}
		close(fd);
		count++;
	}
	closedir(dir);

	if (count == 0) {
		fprintf(stderr, "FAIL: no regular files found\n");
		exit(1);
	}
	printf("Write pattern 0x%08x to %d files\n", pattern, count);
}

static void verify_pattern(const char *mnt, uint32_t pattern)
{
	DIR *dir = opendir(mnt);
	struct dirent *ent;
	int count = 0;

	if (!dir) {
		perror("opendir");
		exit(1);
	}

	while ((ent = readdir(dir))) {
		char path[256];
		int fd;
		ssize_t n;
		uint32_t read_val;

		if (ent->d_type != DT_REG)
			continue;

		snprintf(path, sizeof(path), "%s/%s", mnt, ent->d_name);
		fd = open(path, O_RDONLY);
		if (fd < 0) {
			perror(path);
			continue;
		}

		n = read(fd, &read_val, sizeof(read_val));
		if (n != sizeof(read_val) || read_val != pattern) {
			fprintf(stderr, "FAIL: %s expected 0x%08x got 0x%08x (n=%zd)\n",
				path, pattern, read_val, n);
			close(fd);
			closedir(dir);
			exit(1);
		}
		close(fd);
		printf("PASS: %s pattern verified\n", path);
		count++;
	}
	closedir(dir);

	if (count == 0) {
		fprintf(stderr, "FAIL: no regular files found\n");
		exit(1);
	}
	printf("Verify pattern passed on %d files\n", count);
}

static void partial_sector_test(const char *mnt)
{
	char path[256];
	int fd;
	ssize_t n;
	uint8_t wbuf = 0x42;
	uint8_t rbuf = 0;

	fd = open_first_file(mnt, path, sizeof(path));
	if (fd < 0) {
		fprintf(stderr, "FAIL: no file to test\n");
		exit(1);
	}

	n = write(fd, &wbuf, 1);
	if (n != 1) {
		fprintf(stderr, "FAIL: partial write returned %zd\n", n);
		close(fd);
		exit(1);
	}

	if (lseek(fd, 0, SEEK_SET) < 0) {
		perror("lseek");
		close(fd);
		exit(1);
	}

	n = read(fd, &rbuf, 1);
	if (n != 1 || rbuf != wbuf) {
		fprintf(stderr, "FAIL: partial read expected 0x%02x got 0x%02x (n=%zd)\n",
			wbuf, rbuf, n);
		close(fd);
		exit(1);
	}
	printf("PASS: partial sector read/write OK\n");
	close(fd);
}

static void seek_test(const char *mnt)
{
	char path[256];
	int fd;
	ssize_t n;
	uint32_t pattern = 0xCAFEBABE;
	uint32_t read_val;

	fd = open_first_file(mnt, path, sizeof(path));
	if (fd < 0) {
		fprintf(stderr, "FAIL: no file to test\n");
		exit(1);
	}

	if (lseek(fd, 4, SEEK_SET) < 0) {
		perror("lseek");
		close(fd);
		exit(1);
	}

	n = write(fd, &pattern, sizeof(pattern));
	if (n != sizeof(pattern)) {
		fprintf(stderr, "FAIL: seek write returned %zd\n", n);
		close(fd);
		exit(1);
	}

	if (lseek(fd, 4, SEEK_SET) < 0) {
		perror("lseek");
		close(fd);
		exit(1);
	}

	n = read(fd, &read_val, sizeof(read_val));
	if (n != sizeof(read_val) || read_val != pattern) {
		fprintf(stderr, "FAIL: seek read expected 0x%08x got 0x%08x (n=%zd)\n",
			pattern, read_val, n);
		close(fd);
		exit(1);
	}
	printf("PASS: seek read/write OK\n");
	close(fd);
}

static void multi_write_test(const char *mnt, int nwrites)
{
	char path[256];
	int fd;
	ssize_t n;
	int i;
	uint32_t last_pattern = 0;
	uint32_t read_val;

	fd = open_first_file(mnt, path, sizeof(path));
	if (fd < 0) {
		fprintf(stderr, "FAIL: no file to test\n");
		exit(1);
	}

	for (i = 0; i < nwrites; i++) {
		uint32_t pattern = 0xAABBCC00 + (uint32_t)i;

		if (lseek(fd, 0, SEEK_SET) < 0) {
			perror("lseek");
			close(fd);
			exit(1);
		}
		n = write(fd, &pattern, sizeof(pattern));
		if (n != sizeof(pattern)) {
			fprintf(stderr, "FAIL: multi-write %d returned %zd\n", i, n);
			close(fd);
			exit(1);
		}
		last_pattern = pattern;
	}

	if (lseek(fd, 0, SEEK_SET) < 0) {
		perror("lseek");
		close(fd);
		exit(1);
	}
	n = read(fd, &read_val, sizeof(read_val));
	if (n != sizeof(read_val) || read_val != last_pattern) {
		fprintf(stderr, "FAIL: multi-write expected 0x%08x got 0x%08x\n",
			last_pattern, read_val);
		close(fd);
		exit(1);
	}
	printf("PASS: multi-write (%d times) OK\n", nwrites);
	close(fd);
}

static void read_unwritten(const char *mnt)
{
	char path[256];
	int fd;
	ssize_t n;
	uint8_t buf[MYFS_SECTOR_SIZE];
	int i;

	fd = open_first_file(mnt, path, sizeof(path));
	if (fd < 0) {
		fprintf(stderr, "FAIL: no file to test\n");
		exit(1);
	}

	/* Do NOT write; just read */
	n = read(fd, buf, sizeof(buf));
	if (n < 0) {
		perror("read");
		close(fd);
		exit(1);
	}

	for (i = 0; i < n; i++) {
		if (buf[i] != 0) {
			fprintf(stderr, "FAIL: unwritten file byte %d is 0x%02x, expected 0x00\n",
				i, buf[i]);
			close(fd);
			exit(1);
		}
	}
	printf("PASS: unwritten file reads all zeros\n");
	close(fd);
}

static void verify_hashes(const char *mnt)
{
	DIR *dir = opendir(mnt);
	struct dirent *ent;
	int fd_ioctl;
	struct myfs_hlist list;
	struct myfs_mapping_req req;
	int ret;
	int count = 0;
	uint32_t expected_hash;
	uint32_t file_size_sectors = 0;
	uint8_t *buf = NULL;

	if (!dir) {
		perror("opendir");
		exit(1);
	}

	/* Get file size from first file mapping */
	fd_ioctl = get_ioctl_fd(mnt);
	while ((ent = readdir(dir))) {
		if (ent->d_type != DT_REG)
			continue;

		strncpy(req.name, ent->d_name, sizeof(req.name) - 1);
		req.name[sizeof(req.name) - 1] = '\0';
		ret = ioctl(fd_ioctl, MYFS_IOCTL_GET_MAPPING, &req);
		if (ret == 0) {
			file_size_sectors = req.mapping.size;
			break;
		}
	}
	rewinddir(dir);

	if (file_size_sectors == 0) {
		fprintf(stderr, "FAIL: could not determine file size\n");
		close(fd_ioctl);
		closedir(dir);
		exit(1);
	}

	/* Write known pattern to all files first */
	while ((ent = readdir(dir))) {
		char path[256];
		int fd;
		uint32_t pattern = 0x12345678;
		ssize_t n;

		if (ent->d_type != DT_REG)
			continue;

		snprintf(path, sizeof(path), "%s/%s", mnt, ent->d_name);
		fd = open(path, O_RDWR);
		if (fd < 0) {
			perror(path);
			continue;
		}
		n = write(fd, &pattern, sizeof(pattern));
		if (n != sizeof(pattern)) {
			fprintf(stderr, "FAIL: %s write returned %zd\n", path, n);
			close(fd);
			closedir(dir);
			close(fd_ioctl);
			exit(1);
		}
		close(fd);
		count++;
	}
	closedir(dir);

	if (count == 0) {
		fprintf(stderr, "FAIL: no regular files found\n");
		close(fd_ioctl);
		exit(1);
	}

	/* Get hashes via ioctl */
	ret = ioctl(fd_ioctl, MYFS_IOCTL_LIST_HASHES, &list);
	if (ret < 0) {
		perror("MYFS_IOCTL_LIST_HASHES");
		close(fd_ioctl);
		exit(1);
	}
	close(fd_ioctl);

	/* Compute expected hash: pattern padded with zeros to full file size */
	buf = calloc(file_size_sectors, MYFS_SECTOR_SIZE);
	if (!buf) {
		perror("calloc");
		exit(1);
	}
	memcpy(buf, "\x78\x56\x34\x12", 4); /* little-endian 0x12345678 */
	expected_hash = calc_crc32(buf, file_size_sectors * MYFS_SECTOR_SIZE);
	free(buf);

	for (int i = 0; i < (int)list.count; i++) {
		if (list.hashes[i].hash != expected_hash) {
			fprintf(stderr, "FAIL: %s hash expected 0x%08x got 0x%08x\n",
				list.hashes[i].name, expected_hash, list.hashes[i].hash);
			exit(1);
		}
		printf("PASS: %s hash 0x%08x correct\n", list.hashes[i].name, list.hashes[i].hash);
	}
	printf("Verify hashes passed on %d files\n", count);
}

static void verify_all_mappings(const char *mnt)
{
	DIR *dir = opendir(mnt);
	struct dirent *ent;
	int fd_ioctl;
	int count = 0;
	uint32_t offsets[MYFS_MAX_FILES];
	uint32_t sizes[MYFS_MAX_FILES];

	fd_ioctl = get_ioctl_fd(mnt);

	if (!dir) {
		perror("opendir");
		exit(1);
	}

	while ((ent = readdir(dir))) {
		struct myfs_mapping_req req;
		int ret;

		if (ent->d_type != DT_REG)
			continue;

		strncpy(req.name, ent->d_name, sizeof(req.name) - 1);
		req.name[sizeof(req.name) - 1] = '\0';

		ret = ioctl(fd_ioctl, MYFS_IOCTL_GET_MAPPING, &req);
		if (ret < 0) {
			perror("MYFS_IOCTL_GET_MAPPING");
			closedir(dir);
			close(fd_ioctl);
			exit(1);
		}

		offsets[count] = req.mapping.offset;
		sizes[count] = req.mapping.size;
		printf("PASS: %s offset=%u size=%u\n", ent->d_name, req.mapping.offset, req.mapping.size);
		count++;
	}
	closedir(dir);
	close(fd_ioctl);

	for (int i = 0; i < count; i++) {
		for (int j = i + 1; j < count; j++) {
			uint32_t i_end = offsets[i] + sizes[i];
			uint32_t j_end = offsets[j] + sizes[j];

			if (offsets[i] < j_end && offsets[j] < i_end) {
				fprintf(stderr,
					"FAIL: overlap between file %d (off=%u size=%u) and file %d (off=%u size=%u)\n",
					i, offsets[i], sizes[i], j, offsets[j], sizes[j]);
				exit(1);
			}
		}
	}
	printf("Verify mappings passed on %d files, no overlaps\n", count);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [MOUNTPOINT] [OPTION]\n"
		"Options:\n"
		"  (none)                 Auto-test: write random numbers and read back\n"
		"  --ioctl-zero           Zero all files\n"
		"  --ioctl-erase          Erase the filesystem\n"
		"  --ioctl-list           List file hashes\n"
		"  --ioctl-mapping FILE   Get sector mapping for FILE\n"
		"  --concurrent-test N    Test concurrent writes by N processes\n"
		"  --ioctl-invalid        Test invalid ioctl returns ENOTTY\n"
		"  --ioctl-mapping-bad    Test mapping of missing file returns ENOENT\n"
		"  --edge-test            Test read/write at EOF behavior\n"
		"  --verify-zero          Verify all files are zeroed\n"
		"  --write-pattern NUM    Write NUM to all files\n"
		"  --verify-pattern NUM   Verify all files contain NUM\n"
		"  --partial-sector       Test partial sector read/write\n"
		"  --seek-test            Test seek/read/write at offset 4\n"
		"  --multi-write N        Write N patterns sequentially to same file\n"
		"  --read-unwritten       Verify unwritten file reads all zeros\n"
		"  --verify-hashes        Verify ioctl hashes match computed CRC32\n"
		"  --verify-mappings      Verify all file mappings and no overlaps\n",
		prog);
}

int main(int argc, char **argv)
{
	const char *mnt = "/mnt";
	const char *opt = NULL;
	const char *arg = NULL;

	srand((unsigned int)time(NULL));

	if (argc > 1 && argv[1][0] != '-') {
		mnt = argv[1];
		if (argc > 2)
			opt = argv[2];
		if (argc > 3)
			arg = argv[3];
	} else if (argc > 1) {
		opt = argv[1];
		if (argc > 2)
			arg = argv[2];
	}

	if (opt && strcmp(opt, "--help") == 0) {
		usage(argv[0]);
		return 0;
	}

	if (!opt) {
		auto_test(mnt);
	} else if (strcmp(opt, "--ioctl-zero") == 0) {
		ioctl_zero(mnt);
	} else if (strcmp(opt, "--ioctl-erase") == 0) {
		ioctl_erase(mnt);
	} else if (strcmp(opt, "--ioctl-list") == 0) {
		ioctl_list_hashes(mnt);
	} else if (strcmp(opt, "--ioctl-mapping") == 0) {
		if (!arg) {
			usage(argv[0]);
			return 1;
		}
		ioctl_get_mapping(mnt, arg);
	} else if (strcmp(opt, "--concurrent-test") == 0) {
		int nproc = arg ? atoi(arg) : 4;

		if (nproc <= 0)
			nproc = 4;
		concurrent_test(mnt, nproc);
	} else if (strcmp(opt, "--ioctl-invalid") == 0) {
		ioctl_invalid(mnt);
	} else if (strcmp(opt, "--ioctl-mapping-bad") == 0) {
		ioctl_mapping_notfound(mnt);
	} else if (strcmp(opt, "--edge-test") == 0) {
		edge_test(mnt);
	} else if (strcmp(opt, "--verify-zero") == 0) {
		verify_zero(mnt);
	} else if (strcmp(opt, "--write-pattern") == 0) {
		uint32_t pattern = arg ? (uint32_t)strtoul(arg, NULL, 0) : 0x12345678;

		write_pattern(mnt, pattern);
	} else if (strcmp(opt, "--verify-pattern") == 0) {
		uint32_t pattern = arg ? (uint32_t)strtoul(arg, NULL, 0) : 0x12345678;

		verify_pattern(mnt, pattern);
	} else if (strcmp(opt, "--partial-sector") == 0) {
		partial_sector_test(mnt);
	} else if (strcmp(opt, "--seek-test") == 0) {
		seek_test(mnt);
	} else if (strcmp(opt, "--multi-write") == 0) {
		int n = arg ? atoi(arg) : 10;

		if (n <= 0)
			n = 10;
		multi_write_test(mnt, n);
	} else if (strcmp(opt, "--read-unwritten") == 0) {
		read_unwritten(mnt);
	} else if (strcmp(opt, "--verify-hashes") == 0) {
		verify_hashes(mnt);
	} else if (strcmp(opt, "--verify-mappings") == 0) {
		verify_all_mappings(mnt);
	} else {
		usage(argv[0]);
		return 1;
	}

	return 0;
}
