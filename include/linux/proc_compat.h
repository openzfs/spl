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

#ifndef _SPL_PROC_H
#define _SPL_PROC_H

#include <linux/proc_fs.h>

#ifdef CONFIG_SYSCTL
#ifdef HAVE_2ARGS_REGISTER_SYSCTL
#define spl_register_sysctl_table(t, a)	register_sysctl_table(t, a)
#else
#define spl_register_sysctl_table(t, a)	register_sysctl_table(t)
#endif /* HAVE_2ARGS_REGISTER_SYSCTL */
#define spl_unregister_sysctl_table(t)	unregister_sysctl_table(t)
#endif /* CONFIG_SYSCTL */

#ifdef HAVE_CTL_NAME
#define CTL_NAME(cname)                 .ctl_name = (cname),
#else
#define CTL_NAME(cname)
#endif

#ifndef HAVE_PROC_DIR_ENTRY
struct proc_dir_entry {
	unsigned int low_ino;
	umode_t mode;
	nlink_t nlink;
	kuid_t uid;
	kgid_t gid;
	loff_t size;
	const struct inode_operations *proc_iops;
	const struct file_operations *proc_fops;
	struct proc_dir_entry *next, *parent, *subdir;
	void *data;
	atomic_t count;         /* use count */
	atomic_t in_use;        /* number of callers into module in progress; */
	/* negative -> it's going away RSN */
	struct completion *pde_unload_completion;
	struct list_head pde_openers;   /* who did ->open, but not ->release */
	spinlock_t pde_unload_lock; /* proc_fops checks and pde_users bumps */
	u8 namelen;
	char name[];
};
#endif


extern struct proc_dir_entry *proc_spl_kstat;
struct proc_dir_entry *proc_dir_entry_find(struct proc_dir_entry *root,
					   const char *str);
int proc_dir_entries(struct proc_dir_entry *root);

#ifndef HAVE_PROC_CREATE_DATA
static inline struct proc_dir_entry *proc_create_data(
	const char *name, umode_t mode, struct proc_dir_entry *parent,
	const struct file_operations *proc_fops, void *data)
{
	struct proc_dir_entry *de = create_proc_entry(name, mode, parent);
	if (de != NULL){
		de->proc_fops = proc_fops;
		de->data = data;
	}
	return de;
}

static inline struct proc_dir_entry *proc_create(
	const char *name, umode_t mode, struct proc_dir_entry *parent,
	const struct file_operations *proc_fops)
{
	return proc_create_data(name, mode, parent, proc_fops, NULL);
}
#endif

int spl_proc_init(void);
void spl_proc_fini(void);

#endif /* SPL_PROC_H */
