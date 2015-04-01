/*
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
 */

#ifdef KMEM_DEBUG_DIRECT_RECLAIM

#include <sys/kmem.h>
#include <linux/atomic.h>

static spl_kmem_debug_direct_t debug_reclaim_func = NULL;
static atomic_t rcount = ATOMIC_INIT(0);

void spl_kmem_debug_direct_reclaim_register(spl_kmem_debug_direct_t f)
{
	debug_reclaim_func = f;
}
EXPORT_SYMBOL(spl_kmem_debug_direct_reclaim_register);

void spl_kmem_debug_direct_reclaim_unregister(void)
{
	debug_reclaim_func = NULL;
	smp_mb();

	/* wait until the last user exits */
	while (atomic_read(&rcount))
		schedule_timeout(1);
}
EXPORT_SYMBOL(spl_kmem_debug_direct_reclaim_unregister);

void spl_kmem_debug_direct_reclaim(int flags, unsigned long *t, const char *c)
{
	spl_kmem_debug_direct_t f;

	/* not allowed to do direct reclaim */
	if (flags & KM_NOSLEEP || current->flags & (PF_FSTRANS|PF_MEMALLOC))
		return;
	/* not yet registered or unregistered */
	if (!debug_reclaim_func)
		return;

	atomic_inc(&rcount);
	smp_mb();
	/* check again in case it's being unregistered */
	f = ACCESS_ONCE(debug_reclaim_func);
	if (!f)
		goto out;
	f(flags, t, c);
out:
	atomic_dec(&rcount);
}
EXPORT_SYMBOL(spl_kmem_debug_direct_reclaim);
#endif	/* KMEM_DEBUG_DIRECT_RECLAIM */
