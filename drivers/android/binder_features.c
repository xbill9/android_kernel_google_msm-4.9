// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal "binder" filesystem holding only the binderfs features directory.
 *
 * This kernel has no binderfs; binder devices are misc devices. Userspace
 * (libbinder) learns which driver features exist by reading
 * /dev/binderfs/features/<name>, and init already mounts a "binder"
 * filesystem at /dev/binderfs. Mounting this filesystem there gives it the
 * features files and their SELinux labels (genfscon binder /features).
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/string.h>

#define BINDER_FEATURES_MAGIC 0x6c6f6f70

static const struct {
	const char *name;
	const char *value;
} binder_features[] = {
	{ "freeze_notification", "1\n" },
};

static ssize_t binder_features_read(struct file *file, char __user *buf,
				    size_t count, loff_t *ppos)
{
	const char *value = file_inode(file)->i_private;

	return simple_read_from_buffer(buf, count, ppos, value, strlen(value));
}

static const struct file_operations binder_features_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = binder_features_read,
	.llseek = default_llseek,
};

static struct inode *binder_features_inode(struct super_block *sb, umode_t mode)
{
	struct inode *inode = new_inode(sb);

	if (!inode)
		return NULL;
	inode->i_ino = get_next_ino();
	inode->i_mode = mode;
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	return inode;
}

static struct dentry *binder_features_dir(struct dentry *parent,
					  const char *name)
{
	struct dentry *dentry = d_alloc_name(parent, name);
	struct inode *inode;

	if (!dentry)
		return ERR_PTR(-ENOMEM);
	inode = binder_features_inode(parent->d_sb, S_IFDIR | 0755);
	if (!inode) {
		dput(dentry);
		return ERR_PTR(-ENOMEM);
	}
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	inc_nlink(inode);
	inc_nlink(d_inode(parent));
	d_add(dentry, inode);
	return dentry;
}

static int binder_features_file(struct dentry *parent, const char *name,
				const char *value)
{
	struct dentry *dentry = d_alloc_name(parent, name);
	struct inode *inode;

	if (!dentry)
		return -ENOMEM;
	inode = binder_features_inode(parent->d_sb, S_IFREG | 0444);
	if (!inode) {
		dput(dentry);
		return -ENOMEM;
	}
	inode->i_fop = &binder_features_fops;
	inode->i_private = (void *)value;
	d_add(dentry, inode);
	return 0;
}

static const struct super_operations binder_features_sops = {
	.statfs = simple_statfs,
	.drop_inode = generic_delete_inode,
};

static int binder_features_fill_super(struct super_block *sb, void *data,
				      int silent)
{
	struct dentry *features;
	struct inode *root;
	int i, ret;

	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic = BINDER_FEATURES_MAGIC;
	sb->s_op = &binder_features_sops;
	sb->s_time_gran = 1;

	root = binder_features_inode(sb, S_IFDIR | 0755);
	if (!root)
		return -ENOMEM;
	root->i_op = &simple_dir_inode_operations;
	root->i_fop = &simple_dir_operations;
	set_nlink(root, 2);
	sb->s_root = d_make_root(root);
	if (!sb->s_root)
		return -ENOMEM;

	features = binder_features_dir(sb->s_root, "features");
	if (IS_ERR(features))
		return PTR_ERR(features);
	for (i = 0; i < ARRAY_SIZE(binder_features); i++) {
		ret = binder_features_file(features, binder_features[i].name,
					   binder_features[i].value);
		if (ret)
			return ret;
	}
	return 0;
}

/* Mount options such as "stats=global" are accepted and ignored. */
static struct dentry *binder_features_mount(struct file_system_type *fs_type,
					    int flags, const char *dev_name,
					    void *data)
{
	return mount_nodev(fs_type, flags, data, binder_features_fill_super);
}

static struct file_system_type binder_features_fs_type = {
	.owner = THIS_MODULE,
	.name = "binder",
	.mount = binder_features_mount,
	.kill_sb = kill_litter_super,
};

static int __init binder_features_init(void)
{
	return register_filesystem(&binder_features_fs_type);
}
device_initcall(binder_features_init);
