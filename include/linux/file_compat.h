/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_FILE_COMPAT_H
#define _SPL_FILE_COMPAT_H

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define	spl_file_open(name, fl, mode)	filp_open(name, fl, mode)
#define	spl_file_close(f)		filp_close(f, NULL)
#define	spl_file_pos(f)			((f)->f_pos)
#define	spl_file_dentry(f)		((f)->f_path.dentry)

static inline ssize_t
spl_file_read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	mm_segment_t saved_fs = get_fs();
	ssize_t ret;

	set_fs(get_ds());
	ret = vfs_read(fp, buf, count, pos);
	set_fs(saved_fs);

	return (ret);
}

static inline ssize_t
spl_file_write(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	mm_segment_t saved_fs = get_fs();
	ssize_t ret;

	set_fs(get_ds());
	ret = vfs_write(fp, buf, count, pos);
	set_fs(saved_fs);

	return (ret);
}

#ifdef HAVE_2ARGS_VFS_UNLINK
#define	spl_file_unlink(ip, dp)		vfs_unlink(ip, dp)
#else
#define spl_file_unlink(ip, dp)		vfs_unlink(ip, dp, NULL)
#endif /* HAVE_2ARGS_VFS_UNLINK */

#ifdef HAVE_2ARGS_VFS_GETATTR
#define	spl_file_stat(fp, st)		vfs_getattr(&(fp)->f_path, st)
#else
#define	spl_file_stat(fp, st)		vfs_getattr((fp)->f_path.mnt, \
					     (fp)->f_dentry, st)
#endif /* HAVE_2ARGS_VFS_GETATTR */

#ifdef HAVE_2ARGS_VFS_FSYNC
#define	spl_file_fsync(fp, sync)	vfs_fsync(fp, sync)
#else
#define	spl_file_fsync(fp, sync)	vfs_fsync(fp, (fp)->f_dentry, sync)
#endif /* HAVE_2ARGS_VFS_FSYNC */

#define	spl_inode_lock(ip)		mutex_lock(&(ip)->i_mutex)
#define	spl_inode_unlock(ip)		mutex_unlock(&(ip)->i_mutex)

#endif /* SPL_FILE_COMPAT_H */

