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
\*****************************************************************************/

#ifndef _SPL_SNAPSHOTS_AUTOMOUNT_H
#define _SPL_SNAPSHOTS_AUTOMOUNT_H

#include <linux/mount.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/version.h>


extern struct vfsmount *linux_kern_mount(struct file_system_type *type,
                                      int flags, const char *name,
                                      void *data);

extern int
linux_add_mount(struct vfsmount *newmnt, struct nameidata *nd,
                int mnt_flags, struct list_head *fslist);

#endif /* _SPL_SNAPSHOTS_AUTOMOUNT_H */

