// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "%s:%s(): " fmt, KBUILD_MODNAME, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/crc32.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include "filesystem.h"

static char *disk_name = "vda";
module_param(disk_name, charp, 0444);
MODULE_PARM_DESC(disk_name, "Block device name (e.g., vda)");

static int sb_first;
module_param(sb_first, int, 0444);
MODULE_PARM_DESC(sb_first, "Sector offset for first superblock");

static int sb_second = 10;
module_param(sb_second, int, 0444);
MODULE_PARM_DESC(sb_second, "Sector offset for second superblock");

static int max_filename_len = 16;
module_param(max_filename_len, int, 0444);
MODULE_PARM_DESC(max_filename_len, "Maximum file name length");

static int max_file_size_sectors = 1;
module_param(max_file_size_sectors, int, 0444);
MODULE_PARM_DESC(max_file_size_sectors, "Maximum file size in sectors (all files use this size)");

static int auto_format = 1;
module_param(auto_format, int, 0444);
MODULE_PARM_DESC(auto_format, "Auto-format disk on module load if not formatted");

struct myfs_sb_info {
	struct myfs_superblock sb;
	struct block_device *bdev;
	struct mutex sb_lock;	/* protects sb */
};

static u32 myfs_compute_sb_checksum(const struct myfs_superblock *sb)
{
	struct myfs_superblock tmp;

	memcpy(&tmp, sb, sizeof(tmp));
	tmp.checksum = 0;
	return crc32(0, (void *)&tmp, sizeof(tmp));
}

static int myfs_bio_rw_page(struct block_device *bdev, sector_t sector,
			    struct page *page, blk_opf_t op)
{
	struct bio *bio;
	int ret;

	bio = bio_alloc(bdev, 1, op, GFP_KERNEL);
	if (!bio)
		return -ENOMEM;

	bio->bi_iter.bi_sector = sector;
	ret = bio_add_page(bio, page, MYFS_SECTOR_SIZE, 0);
	if (ret != MYFS_SECTOR_SIZE) {
		bio_put(bio);
		return -EIO;
	}

	ret = submit_bio_wait(bio);
	bio_put(bio);
	return ret;
}

static u32 myfs_compute_file_hash(struct super_block *sb,
				  struct myfs_file_entry *entry)
{
	sector_t start = le32_to_cpu(entry->offset);
	unsigned int size = le32_to_cpu(entry->size);
	unsigned int i;
	u32 crc = 0;
	struct page *page;
	int ret;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return 0;

	for (i = 0; i < size; i++) {
		ret = myfs_bio_rw_page(sb->s_bdev, start + i, page, REQ_OP_READ);
		if (ret) {
			pr_err("Failed to read sector %llu for hash\n",
			       (unsigned long long)(start + i));
			put_page(page);
			return 0;
		}
		crc = crc32(crc, page_address(page), MYFS_SECTOR_SIZE);
	}
	put_page(page);
	return crc;
}

static int myfs_bio_rw_sector(struct block_device *bdev, sector_t sector,
			      void *data, size_t len, blk_opf_t op)
{
	struct bio *bio;
	struct page *page;
	void *page_addr;
	int ret;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	page_addr = page_address(page);
	if (op & REQ_OP_WRITE) {
		memset(page_addr, 0, MYFS_SECTOR_SIZE);
		memcpy(page_addr, data, len);
	}

	bio = bio_alloc(bdev, 1, op, GFP_KERNEL);
	if (!bio) {
		put_page(page);
		return -ENOMEM;
	}

	bio->bi_iter.bi_sector = sector;
	ret = bio_add_page(bio, page, MYFS_SECTOR_SIZE, 0);
	if (ret != MYFS_SECTOR_SIZE) {
		bio_put(bio);
		put_page(page);
		return -EIO;
	}

	ret = submit_bio_wait(bio);
	if (ret == 0 && !(op & REQ_OP_WRITE))
		memcpy(data, page_addr, len);

	bio_put(bio);
	put_page(page);
	return ret;
}

static inline int myfs_bio_read_sector(struct block_device *bdev, sector_t sector,
				       void *data, size_t len)
{
	return myfs_bio_rw_sector(bdev, sector, data, len, REQ_OP_READ);
}

static inline int myfs_bio_write_sector(struct block_device *bdev, sector_t sector,
					const void *data, size_t len)
{
	return myfs_bio_rw_sector(bdev, sector, (void *)data, len, REQ_OP_WRITE);
}

static int myfs_write_sb_sector(struct super_block *sb, sector_t sector,
				const void *data, size_t len)
{
	return myfs_bio_write_sector(sb->s_bdev, sector, data, len);
}

static int myfs_zero_sb_sector(struct super_block *sb, sector_t sector)
{
	char buf[MYFS_SECTOR_SIZE];

	memset(buf, 0, MYFS_SECTOR_SIZE);
	return myfs_bio_write_sector(sb->s_bdev, sector, buf, MYFS_SECTOR_SIZE);
}

static int myfs_format_super(struct super_block *sb)
{
	struct myfs_superblock sb_disk;
	sector_t total_sectors, next_sector;
	unsigned int i, num_files;
	u32 crc;
	int ret;

	total_sectors = bdev_nr_sectors(sb->s_bdev);

	if (sb_first < 0 || sb_first >= total_sectors) {
		pr_err("Invalid sb_first %d (total sectors %llu)\n",
		       sb_first, (unsigned long long)total_sectors);
		return -EINVAL;
	}
	if (sb_second < 0 || sb_second >= total_sectors || sb_second == sb_first) {
		pr_err("Invalid sb_second %d (total sectors %llu, sb_first=%d)\n",
		       sb_second, (unsigned long long)total_sectors, sb_first);
		return -EINVAL;
	}
	if (max_file_size_sectors <= 0) {
		pr_err("Invalid max_file_size_sectors %d\n", max_file_size_sectors);
		return -EINVAL;
	}
	if (max_filename_len <= 0 || max_filename_len > MYFS_NAME_LEN) {
		pr_err("Invalid max_filename_len %d (max %d)\n",
		       max_filename_len, MYFS_NAME_LEN);
		return -EINVAL;
	}

	num_files = (total_sectors - 2) / max_file_size_sectors;
	if (num_files > MYFS_MAX_FILES)
		num_files = MYFS_MAX_FILES;

	memset(&sb_disk, 0, sizeof(sb_disk));
	sb_disk.magic = cpu_to_le32(MYFS_MAGIC);
	sb_disk.version = cpu_to_le32(MYFS_VERSION);
	sb_disk.num_files = cpu_to_le32(num_files);
	sb_disk.max_filename_len = cpu_to_le32(max_filename_len);
	sb_disk.max_file_size_sectors = cpu_to_le32(max_file_size_sectors);

	next_sector = sb_first + 1;
	for (i = 0; i < num_files; i++) {
		if (next_sector <= sb_second &&
		    sb_second < next_sector + max_file_size_sectors)
			next_sector = sb_second + 1;

		if (next_sector + max_file_size_sectors > total_sectors) {
			sb_disk.num_files = cpu_to_le32(i);
			num_files = i;
			break;
		}

		snprintf(sb_disk.files[i].name, MYFS_NAME_LEN, "file%u", i);
		sb_disk.files[i].offset = cpu_to_le32(next_sector);
		sb_disk.files[i].size = cpu_to_le32(max_file_size_sectors);
		sb_disk.files[i].hash = 0;
		next_sector += max_file_size_sectors;
	}

	crc = myfs_compute_sb_checksum(&sb_disk);
	sb_disk.checksum = cpu_to_le32(crc);

	ret = myfs_write_sb_sector(sb, sb_second, &sb_disk, sizeof(sb_disk));
	if (ret) {
		pr_err("Failed to write backup superblock\n");
		return ret;
	}

	ret = myfs_write_sb_sector(sb, sb_first, &sb_disk, sizeof(sb_disk));
	if (ret) {
		pr_err("Failed to write primary superblock\n");
		return ret;
	}

	for (i = 0; i < num_files; i++) {
		sector_t start = le32_to_cpu(sb_disk.files[i].offset);
		unsigned int j;

		for (j = 0; j < max_file_size_sectors; j++) {
			ret = myfs_zero_sb_sector(sb, start + j);
			if (ret) {
				pr_err("Failed to zero sector %llu\n",
				       (unsigned long long)(start + j));
				return ret;
			}
		}
	}

	pr_info("Formatted disk with %u files, sb_second=%d\n",
		num_files, sb_second);
	return 0;
}

static int myfs_read_sb(struct super_block *sb, sector_t sec)
{
	struct myfs_sb_info *sbi = sb->s_fs_info;

	return myfs_bio_read_sector(sb->s_bdev, sec, &sbi->sb, sizeof(sbi->sb));
}

static int myfs_validate_sb(struct super_block *sb, sector_t sec, const char *label)
{
	struct myfs_superblock sb_tmp;
	u32 crc;
	int ret;

	ret = myfs_bio_read_sector(sb->s_bdev, sec, &sb_tmp, sizeof(sb_tmp));
	if (ret) {
		pr_err("Failed to read %s superblock at sector %d\n", label, sec);
		return -EIO;
	}

	if (le32_to_cpu(sb_tmp.magic) != MYFS_MAGIC) {
		pr_err("%s superblock bad magic: 0x%x\n", label, le32_to_cpu(sb_tmp.magic));
		return -EINVAL;
	}

	crc = myfs_compute_sb_checksum(&sb_tmp);
	if (crc != le32_to_cpu(sb_tmp.checksum)) {
		pr_err("%s superblock checksum mismatch\n", label);
		return -EINVAL;
	}

	pr_info("%s superblock at sector %d is valid\n", label, sec);
	return 0;
}

static int myfs_write_sb(struct super_block *sb, sector_t sec)
{
	struct myfs_sb_info *sbi = sb->s_fs_info;

	return myfs_bio_write_sector(sb->s_bdev, sec, &sbi->sb, sizeof(sbi->sb));
}

static void myfs_put_super(struct super_block *sb)
{
	struct myfs_sb_info *sbi = sb->s_fs_info;

	kfree(sbi);
}

static const struct super_operations myfs_sb_ops = {
	.put_super = myfs_put_super,
};

static int myfs_iterate(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct myfs_sb_info *sbi = inode->i_sb->s_fs_info;
	unsigned int num_files = le32_to_cpu(sbi->sb.num_files);
	unsigned int idx;

	if (ctx->pos == 0) {
		if (!dir_emit(ctx, ".", 1, inode->i_ino, DT_DIR))
			return 0;
		ctx->pos++;
	}
	if (ctx->pos == 1) {
		if (!dir_emit(ctx, "..", 2, inode->i_ino, DT_DIR))
			return 0;
		ctx->pos++;
	}

	idx = ctx->pos - 2;
	while (idx < num_files) {
		if (!dir_emit(ctx, sbi->sb.files[idx].name,
			      strlen(sbi->sb.files[idx].name),
			      idx + 2, DT_REG))
			return 0;
		idx++;
		ctx->pos++;
	}
	return 0;
}

static const struct file_operations myfs_dir_fops = {
	.owner = THIS_MODULE,
	.iterate_shared = myfs_iterate,
	.llseek = generic_file_llseek,
};

static ssize_t myfs_read(struct file *filp, char __user *buf,
			 size_t len, loff_t *off)
{
	struct inode *inode = filp->f_inode;
	struct myfs_file_entry *entry = inode->i_private;
	struct super_block *sb = inode->i_sb;
	sector_t start_sector = le32_to_cpu(entry->offset);
	size_t file_size = le32_to_cpu(entry->size) * MYFS_SECTOR_SIZE;
	loff_t pos = *off;
	ssize_t total = 0;
	struct page *page;
	int ret;

	if (pos >= file_size)
		return 0;
	if (len > file_size - pos)
		len = file_size - pos;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	while (len > 0) {
		sector_t sector = start_sector + (pos / MYFS_SECTOR_SIZE);
		size_t offset_in_sector = pos % MYFS_SECTOR_SIZE;
		size_t to_read = min(len,
				     (size_t)(MYFS_SECTOR_SIZE - offset_in_sector));

		ret = myfs_bio_rw_page(sb->s_bdev, sector, page, REQ_OP_READ);
		if (ret) {
			put_page(page);
			return -EIO;
		}
		if (copy_to_user(buf + total, page_address(page) + offset_in_sector,
				 to_read)) {
			put_page(page);
			return -EFAULT;
		}
		pos += to_read;
		total += to_read;
		len -= to_read;
	}

	put_page(page);
	*off = pos;
	return total;
}

static ssize_t myfs_write(struct file *filp, const char __user *buf,
			  size_t len, loff_t *off)
{
	struct inode *inode = filp->f_inode;
	struct myfs_file_entry *entry = inode->i_private;
	struct super_block *sb = inode->i_sb;
	sector_t start_sector = le32_to_cpu(entry->offset);
	size_t file_size = le32_to_cpu(entry->size) * MYFS_SECTOR_SIZE;
	loff_t pos = *off;
	ssize_t total = 0;
	struct page *page;
	int ret;

	if (pos >= file_size)
		return -ENOSPC;
	if (len > file_size - pos)
		len = file_size - pos;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	while (len > 0) {
		sector_t sector = start_sector + (pos / MYFS_SECTOR_SIZE);
		size_t offset_in_sector = pos % MYFS_SECTOR_SIZE;
		size_t to_write = min(len,
				      (size_t)(MYFS_SECTOR_SIZE - offset_in_sector));

		ret = myfs_bio_rw_page(sb->s_bdev, sector, page, REQ_OP_READ);
		if (ret) {
			put_page(page);
			return -EIO;
		}
		if (copy_from_user(page_address(page) + offset_in_sector,
				   buf + total, to_write)) {
			put_page(page);
			return -EFAULT;
		}
		ret = myfs_bio_rw_page(sb->s_bdev, sector, page, REQ_OP_WRITE);
		if (ret) {
			put_page(page);
			return -EIO;
		}
		pos += to_write;
		total += to_write;
		len -= to_write;
	}

	put_page(page);

	/* Update file hash and sb copies */
	struct myfs_sb_info *sbi = sb->s_fs_info;

	mutex_lock(&sbi->sb_lock);
	entry->hash = cpu_to_le32(myfs_compute_file_hash(sb, entry));
	sbi->sb.checksum = cpu_to_le32(myfs_compute_sb_checksum(&sbi->sb));
	ret = myfs_write_sb(sb, sb_first);
	if (ret == 0)
		ret = myfs_write_sb(sb, sb_second);
	mutex_unlock(&sbi->sb_lock);
	if (ret)
		return ret;

	*off = pos;
	return total;
}

static long myfs_file_ioctl(struct file *filp, unsigned int cmd,
			    unsigned long arg)
{
	struct inode *inode = filp->f_inode;
	struct super_block *sb = inode->i_sb;
	struct myfs_sb_info *sbi = sb->s_fs_info;
	unsigned int num_files = le32_to_cpu(sbi->sb.num_files);
	unsigned int i;
	int ret = 0;

	switch (cmd) {
	case MYFS_IOCTL_ZERO_FILES: {
		struct page *page = alloc_page(GFP_KERNEL);

		if (!page) {
			ret = -ENOMEM;
			break;
		}
		memset(page_address(page), 0, MYFS_SECTOR_SIZE);
		mutex_lock(&sbi->sb_lock);
		for (i = 0; i < num_files; i++) {
			sector_t start = le32_to_cpu(sbi->sb.files[i].offset);
			unsigned int j, size = le32_to_cpu(sbi->sb.files[i].size);

			for (j = 0; j < size; j++) {
				ret = myfs_bio_rw_page(sb->s_bdev, start + j, page, REQ_OP_WRITE);
				if (ret) {
					pr_err("ZERO_FILES: failed to zero sector %llu\n",
					       (unsigned long long)(start + j));
					break;
				}
			}
			if (ret)
				break;
			sbi->sb.files[i].hash = 0;
		}
		if (ret == 0) {
			sbi->sb.checksum = cpu_to_le32(myfs_compute_sb_checksum(&sbi->sb));
			ret = myfs_write_sb(sb, sb_first);
			if (ret == 0)
				ret = myfs_write_sb(sb, sb_second);
		}
		mutex_unlock(&sbi->sb_lock);
		put_page(page);
		break;
	}

	case MYFS_IOCTL_ERASE_FS: {
		sector_t total_sectors = bdev_nr_sectors(sbi->bdev);
		struct page *page = alloc_page(GFP_KERNEL);

		if (!page) {
			ret = -ENOMEM;
			break;
		}
		memset(page_address(page), 0, MYFS_SECTOR_SIZE);
		for (i = 0; i < total_sectors; i++) {
			ret = myfs_bio_rw_page(sb->s_bdev, i, page, REQ_OP_WRITE);
			if (ret) {
				pr_err("ERASE_FS: failed to erase sector %u\n", i);
				break;
			}
		}
		put_page(page);
		break;
	}

	case MYFS_IOCTL_LIST_HASHES: {
		struct myfs_hlist list;

		list.count = num_files;
		for (i = 0; i < num_files; i++) {
			strscpy(list.hashes[i].name, sbi->sb.files[i].name,
				MYFS_NAME_LEN);
			list.hashes[i].hash = le32_to_cpu(sbi->sb.files[i].hash);
		}
		if (copy_to_user((void __user *)arg, &list, sizeof(list)))
			ret = -EFAULT;
		break;
	}

	case MYFS_IOCTL_GET_MAPPING: {
		struct myfs_mapping_req req;

		if (copy_from_user(&req, (void __user *)arg, sizeof(req))) {
			ret = -EFAULT;
			break;
		}
		req.name[MYFS_NAME_LEN - 1] = '\0';
		for (i = 0; i < num_files; i++) {
			if (strcmp(sbi->sb.files[i].name, req.name) == 0) {
				req.mapping.offset = le32_to_cpu(sbi->sb.files[i].offset);
				req.mapping.size = le32_to_cpu(sbi->sb.files[i].size);
				if (copy_to_user((void __user *)arg, &req,
						 sizeof(req)))
					ret = -EFAULT;
				goto out;
			}
		}
		ret = -ENOENT;
		break;
	}

	default:
		ret = -ENOTTY;
	}
out:
	return ret;
}

static const struct file_operations myfs_file_fops = {
	.owner = THIS_MODULE,
	.read = myfs_read,
	.write = myfs_write,
	.unlocked_ioctl = myfs_file_ioctl,
	.llseek = generic_file_llseek,
};

static const struct inode_operations myfs_inode_ops;

static struct inode *myfs_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;
	struct myfs_sb_info *sbi = sb->s_fs_info;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	if (ino == 1) {
		inode->i_mode = S_IFDIR | 0755;
		inode->i_op = &myfs_inode_ops;
		inode->i_fop = &myfs_dir_fops;
	} else if (ino >= 2 && ino - 2 < le32_to_cpu(sbi->sb.num_files)) {
		struct myfs_file_entry *entry = &sbi->sb.files[ino - 2];

		inode->i_mode = S_IFREG | 0644;
		inode->i_op = &myfs_inode_ops;
		inode->i_fop = &myfs_file_fops;
		inode->i_size = le32_to_cpu(entry->size) * MYFS_SECTOR_SIZE;
		inode->i_private = entry;
	} else {
		unlock_new_inode(inode);
		iput(inode);
		return ERR_PTR(-EINVAL);
	}

	unlock_new_inode(inode);
	return inode;
}

static struct dentry *myfs_lookup(struct inode *dir, struct dentry *dentry,
				  unsigned int flags)
{
	struct myfs_sb_info *sbi = dir->i_sb->s_fs_info;
	unsigned int i, num_files = le32_to_cpu(sbi->sb.num_files);

	if (dir->i_ino != 1)
		return ERR_PTR(-ENOTDIR);

	for (i = 0; i < num_files; i++) {
		if (strcmp(sbi->sb.files[i].name, dentry->d_name.name) == 0) {
			struct inode *inode = myfs_iget(dir->i_sb, i + 2);

			if (IS_ERR(inode))
				return ERR_CAST(inode);
			d_add(dentry, inode);
			return NULL;
		}
	}

	d_add(dentry, NULL);
	return NULL;
}

static const struct inode_operations myfs_inode_ops = {
	.lookup = myfs_lookup,
};

static int myfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct myfs_sb_info *sbi;
	struct inode *root;
	sector_t valid_sec = sb_first;
	int ret;

	pr_info("start\n");
	{
		size_t sbi_size = sizeof(*sbi);

		sbi = kzalloc(sbi_size, GFP_KERNEL);
	}
	if (!sbi)
		return -ENOMEM;

	mutex_init(&sbi->sb_lock);
	sb->s_fs_info = sbi;
	sb->s_magic = MYFS_MAGIC;
	sb->s_blocksize = MYFS_SECTOR_SIZE;
	sb->s_blocksize_bits = 9;
	sb->s_maxbytes = max_file_size_sectors * MYFS_SECTOR_SIZE;
	sb->s_op = &myfs_sb_ops;

	sbi->bdev = sb->s_bdev;

	pr_info("reading primary superblock\n");
	ret = myfs_read_sb(sb, sb_first);
	if (ret) {
		pr_err("Failed to read primary superblock at sector %d\n", sb_first);
		goto fail;
	}
	pr_info("primary superblock magic=0x%x\n",
		le32_to_cpu(sbi->sb.magic));

	if (le32_to_cpu(sbi->sb.magic) == MYFS_MAGIC) {
		ret = myfs_validate_sb(sb, sb_first, "primary");
		if (ret == 0)
			goto sb_ok;
	}

	if (sb_second != sb_first) {
		pr_info("primary invalid, trying backup superblock\n");
		ret = myfs_read_sb(sb, sb_second);
		if (ret) {
			pr_err("Failed to read backup superblock at sector %d\n", sb_second);
			goto fail;
		}
		pr_info("backup superblock magic=0x%x\n",
			le32_to_cpu(sbi->sb.magic));
		if (le32_to_cpu(sbi->sb.magic) == MYFS_MAGIC) {
			ret = myfs_validate_sb(sb, sb_second, "backup");
			if (ret == 0) {
				valid_sec = sb_second;
				goto sb_ok;
			}
		}
	}

	if (auto_format) {
		pr_info("Disk not formatted, auto-formatting...\n");
		ret = myfs_format_super(sb);
		if (ret) {
			pr_err("Auto-format failed: %d\n", ret);
			goto fail;
		}
		ret = myfs_read_sb(sb, sb_first);
		if (ret) {
			pr_err("Failed to re-read superblock after format\n");
			goto fail;
		}
		if (le32_to_cpu(sbi->sb.magic) != MYFS_MAGIC) {
			pr_err("Bad magic after format\n");
			ret = -EINVAL;
			goto fail;
		}
		ret = myfs_validate_sb(sb, sb_first, "primary");
		if (ret)
			goto fail;
	} else {
		pr_err("No valid superblock found\n");
		ret = -EINVAL;
		goto fail;
	}

sb_ok:
	if (valid_sec == sb_first && sb_second != sb_first) {
		ret = myfs_validate_sb(sb, sb_second, "backup");
		if (ret)
			goto fail;
	}

	pr_info("checksum ok\n");

	pr_info("creating root inode\n");
	root = myfs_iget(sb, 1);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto fail;
	}
	pr_info("root inode created\n");

	pr_info("creating root dentry\n");
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		ret = -ENOMEM;
		iput(root);
		goto fail;
	}
	pr_info("root dentry created\n");

	pr_info("myfs mounted: %u files, blocksize=%lu\n",
		le32_to_cpu(sbi->sb.num_files), sb->s_blocksize);
	return 0;

fail:
	kfree(sbi);
	return ret;
}

static struct dentry *myfs_mount(struct file_system_type *fs_type,
				 int flags,
				 const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, myfs_fill_super);
}

static struct file_system_type myfs_type = {
	.owner = THIS_MODULE,
	.name = "myfs",
	.mount = myfs_mount,
	.kill_sb = kill_block_super,
};

static int __init myfs_init(void)
{
	int ret;

	ret = register_filesystem(&myfs_type);
	if (ret) {
		pr_err("Failed to register filesystem\n");
		return ret;
	}

	pr_info("myfs filesystem registered\n");
	return 0;
}

static void __exit myfs_exit(void)
{
	unregister_filesystem(&myfs_type);
	pr_info("myfs filesystem unregistered\n");
}

module_init(myfs_init);
module_exit(myfs_exit);

MODULE_AUTHOR("dmfrpro <dmfr2021y@gmail.com>");
MODULE_DESCRIPTION("Home Assignment");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
