/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://github.com/behlendorf/spl/>.
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
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Proc Implementation.
\*****************************************************************************/

#include <linux/snapshots_automount.h>


struct vfsmount *
linux_kern_mount(struct file_system_type *type, int flags, const char *name, void *data)
{
	struct vfsmount *vfs_mnt = NULL;
	vfs_mnt = vfs_kern_mount(type, flags, name, data);
	return vfs_mnt;
}
EXPORT_SYMBOL(linux_kern_mount);


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
int 
linux_add_mount(struct vfsmount *newmnt, struct nameidata *nd,
		int mnt_flags, struct list_head *fslist)
{
	int ret;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	ret = do_add_mount(newmnt, nd, mnt_flags, fslist);
#else
	ret = do_add_mount(newmnt, &nd->path, mnt_flags, fslist);
#endif
	
	return ret;
}
#endif

