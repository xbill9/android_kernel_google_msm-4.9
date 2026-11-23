/*
 * Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Android passthrough interface (the one libfuse and MediaProvider use):
 * the daemon hands the kernel an open lower file with
 * FUSE_DEV_IOC_PASSTHROUGH_OPEN_V2 and puts the returned id in
 * fuse_open_out.passthrough_fh; reads, writes and mmap of that FUSE file
 * then go straight to the lower file, with the daemon's credentials.
 */

#include "fuse_passthrough.h"

#include <linux/cred.h>
#include <linux/fs_stack.h>
#include <linux/fsnotify.h>
#include <linux/idr.h>
#include <linux/uio.h>

int fuse_passthrough_open(struct fuse_conn *fc, int fd)
{
	struct file *filp;
	struct inode *inode;
	int id;

	if (!fc->passthrough)
		return -EPERM;

	filp = fget(fd);
	if (!filp)
		return -EBADF;

	inode = file_inode(filp);
	if (!S_ISREG(inode->i_mode) || !filp->f_op->read_iter ||
	    !filp->f_op->write_iter) {
		fput(filp);
		return -EINVAL;
	}
	/* If we reached the stacking limit go through regular io */
	if (inode->i_sb->s_stack_depth >= FILESYSTEM_MAX_STACK_DEPTH) {
		fput(filp);
		pr_err("FUSE: maximum fs stacking depth exceeded, cannot use passthrough for this file\n");
		return -ELOOP;
	}

	idr_preload(GFP_KERNEL);
	spin_lock(&fc->lock);
	id = idr_alloc(&fc->passthrough_req, filp, 1, 0, GFP_ATOMIC);
	spin_unlock(&fc->lock);
	idr_preload_end();

	if (id < 0)
		fput(filp);
	return id;
}

static int fuse_passthrough_put(int id, void *p, void *data)
{
	fput(p);
	return 0;
}

void fuse_passthrough_free_unclaimed(struct fuse_conn *fc)
{
	idr_for_each(&fc->passthrough_req, fuse_passthrough_put, NULL);
	idr_destroy(&fc->passthrough_req);
}

void fuse_setup_passthrough(struct fuse_conn *fc, struct fuse_req *req)
{
	unsigned int open_out_index;
	struct fuse_open_out *open_out;
	struct file *filp;
	u32 id;

	req->passthrough_filp = NULL;

	if (!(fc->passthrough))
		return;

	if ((req->in.h.opcode != FUSE_OPEN) &&
	    (req->in.h.opcode != FUSE_CREATE))
		return;

	if (req->out.h.error)
		return;

	open_out_index = req->in.numargs - 1;

	if (WARN_ON(open_out_index != 0 && open_out_index != 1) ||
	    WARN_ON(req->out.args[open_out_index].size != sizeof(*open_out)))
		return;

	open_out = req->out.args[open_out_index].value;

	id = open_out->passthrough_fh;
	if (!id || id > INT_MAX)
		return;

	/* Each id is good for one open */
	spin_lock(&fc->lock);
	filp = idr_find(&fc->passthrough_req, id);
	if (filp)
		idr_remove(&fc->passthrough_req, id);
	spin_unlock(&fc->lock);

	req->passthrough_filp = filp;
}

ssize_t fuse_passthrough_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *fuse_filp = iocb->ki_filp;
	struct fuse_file *ff = fuse_filp->private_data;
	struct file *passthrough_filp = ff->passthrough_filp;
	const struct cred *old_cred;
	ssize_t ret;

	if (!(passthrough_filp->f_mode & FMODE_READ))
		return -EBADF;
	if (!iov_iter_count(to))
		return 0;

	old_cred = override_creds(passthrough_filp->f_cred);
	ret = vfs_iter_read(passthrough_filp, to, &iocb->ki_pos);
	revert_creds(old_cred);

	if (ret >= 0)
		fsstack_copy_attr_atime(file_inode(fuse_filp),
					file_inode(passthrough_filp));
	return ret;
}

ssize_t fuse_passthrough_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *fuse_filp = iocb->ki_filp;
	struct fuse_file *ff = fuse_filp->private_data;
	struct file *passthrough_filp = ff->passthrough_filp;
	struct inode *fuse_inode = file_inode(fuse_filp);
	struct inode *passthrough_inode = file_inode(passthrough_filp);
	struct fuse_conn *fc = ff->fc;
	const struct cred *old_cred;
	struct kiocb kiocb;
	ssize_t ret;

	if (!(passthrough_filp->f_mode & FMODE_WRITE))
		return -EBADF;
	if (!iov_iter_count(from))
		return 0;

	inode_lock(fuse_inode);
	old_cred = override_creds(passthrough_filp->f_cred);
	/*
	 * The lower file may have been opened without O_APPEND or O_SYNC:
	 * carry this write's own flags over, so an append lands at the
	 * lower file's end.
	 */
	init_sync_kiocb(&kiocb, passthrough_filp);
	kiocb.ki_flags |= iocb->ki_flags & (IOCB_APPEND | IOCB_DSYNC | IOCB_SYNC);
	kiocb.ki_pos = iocb->ki_pos;
	file_start_write(passthrough_filp);
	ret = passthrough_filp->f_op->write_iter(&kiocb, from);
	file_end_write(passthrough_filp);
	revert_creds(old_cred);
	if (ret > 0) {
		iocb->ki_pos = kiocb.ki_pos;
		fsnotify_modify(passthrough_filp);
	}

	if (ret > 0) {
		spin_lock(&fc->lock);
		fsstack_copy_inode_size(fuse_inode, passthrough_inode);
		spin_unlock(&fc->lock);
		fsstack_copy_attr_times(fuse_inode, passthrough_inode);
		fuse_invalidate_attr(fuse_inode);
	}
	inode_unlock(fuse_inode);

	/* the lower write_iter did any O_SYNC flush itself */
	return ret;
}

/* Map the lower file itself, so its page cache is the only one */
int fuse_passthrough_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct fuse_file *ff = file->private_data;
	struct file *passthrough_filp = ff->passthrough_filp;
	const struct cred *old_cred;
	int ret;

	if (!passthrough_filp->f_op->mmap)
		return -ENODEV;
	if (WARN_ON(file != vma->vm_file))
		return -EIO;

	vma->vm_file = get_file(passthrough_filp);
	old_cred = override_creds(passthrough_filp->f_cred);
	ret = passthrough_filp->f_op->mmap(passthrough_filp, vma);
	revert_creds(old_cred);

	if (ret) {
		/* mmap_region drops its own reference to file */
		vma->vm_file = file;
		fput(passthrough_filp);
	} else {
		fput(file);
		file_accessed(file);
	}
	return ret;
}

void fuse_passthrough_release(struct fuse_file *ff)
{
	if (!(ff->passthrough_filp))
		return;

	/* Release the passthrough file. */
	fput(ff->passthrough_filp);
	ff->passthrough_filp = NULL;
}
