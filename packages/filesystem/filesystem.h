/* SPDX-License-Identifier: GPL-2.0 */

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define MYFS_MAGIC		0x4D594653
#define MYFS_VERSION		1
#define MYFS_SECTOR_SIZE	512
#define MYFS_NAME_LEN		16
#define MYFS_MAX_FILES		15

struct myfs_file_entry {
	char name[MYFS_NAME_LEN];
	u32 offset;		/* starting sector */
	u32 size;		/* size in sectors */
	u32 hash;		/* crc32 of file content */
	u32 reserved;
};

struct myfs_superblock {
	u32 magic;
	u32 version;
	u32 num_files;
	u32 max_filename_len;
	u32 max_file_size_sectors;
	u32 checksum;		/* crc32 of sb excluding this field */
	struct myfs_file_entry files[MYFS_MAX_FILES];
};

/* IOCTLs */
#define MYFS_IOCTL_MAGIC	0x4D59

struct myfs_file_hash {
	char name[MYFS_NAME_LEN];
	u32 hash;
};

struct myfs_hlist {
	u32 count;
	struct myfs_file_hash hashes[MYFS_MAX_FILES];
};

struct myfs_mapping {
	u32 offset;
	u32 size;
};

struct myfs_mapping_req {
	char name[MYFS_NAME_LEN];
	struct myfs_mapping mapping;
};

#define MYFS_IOCTL_ZERO_FILES _IO(MYFS_IOCTL_MAGIC, 0)
#define MYFS_IOCTL_ERASE_FS _IO(MYFS_IOCTL_MAGIC, 1)
#define MYFS_IOCTL_LIST_HASHES _IOR(MYFS_IOCTL_MAGIC, 2, struct myfs_hlist)
#define MYFS_IOCTL_GET_MAPPING	\
	_IOWR(MYFS_IOCTL_MAGIC, 3, struct myfs_mapping_req)

#endif /* FILESYSTEM_H */
