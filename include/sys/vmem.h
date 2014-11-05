/*****************************************************************************\
 *  Copyright (C) 2014 Zettabyte Software LLC.
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

#ifndef _SPL_VMEM_H
#define	_SPL_VMEM_H

#include <sys/kmem.h>

/*
 * vmem_* is an interface to a low level arena-based memory allocator on
 * Illumos that is used to allocate virtual address space. The kmem SLAB
 * allocator allocates slabs from it. Then the generic
 * kmem_{alloc,zalloc,free}() are layered on top of SLAB allocators.
 *
 * On Linux, the primary means of doing allocations is via kmalloc(), which is
 * similarly layered on top of something called the bump allocator. The bump
 * allocator is not available to kernel modules, uses physical memory addresses
 * rather than virtual memory addresses and is prone to fragmentation.
 *
 * Linux sets aside a relatvely small address space for in-kernel virtual
 * memory allocations on Linux from which they can be done using vmalloc(). It
 * might seem like a good idea to use vmalloc() to implement something similar
 * to Illumos' allocator. However, these have the following problems:
 *
 * 0. Page directory table allocations are hard coded as being done with
 * GFP_KERNEL. Consequently, any KM_PUSHPAGE or KM_NOSLEEP allocations done
 * using vmalloc() will not have proper semantics.
 *
 * 1. Address space exhaustion is a real issue on 32-bit x86 Linux where only
 * 100MB are available. The kernel will handle it by spinning when it runs out
 * of address space.
 *
 * 2. All vmalloc() allocations and frees are protected by a single global
 * spinlock.
 *
 * 3. Accessing /proc/meminfo and /proc/vmallocinfo will iterate the entire
 * list. The former will sum the allocations while the latter will print them
 * to userspace in a way that userspace can keep the lock held indefinitely.
 * When the total number of allocations reaches about 250,000, as much as 75%
 * of system time will be spent in spin locks and interactive response will be
 * attrocious.
 *
 * 4. Linux has a wait_on_bit() locking primitive that assumes physical memory
 * is used and simply does not work on virtual memory. Certain Linux structures
 * (e.g. the superblock) use them and might be embedded into a structure from
 * Illumos, which makes using Linux virtual memory unsafe.
 *
 * It follows that we cannot obtain identical semantics to those on Illumos.
 * Consequently, we implement the kmem_{alloc,zalloc,free}() functions in such
 * a way that they can be used as drop-in replacements for small vmem_*
 * allocations (32MB in size or smaller) and map vmem_{alloc,zalloc,free}() to
 * them. This produces the strange situation where sys/vmem.h includes
 * sys/kmem.h and sys/kmem.h must also include sys/vmem.h for Illumos
 * compatibility.
 */
#define vmem_alloc(sz, fl)		kmem_alloc((sz), (fl))
#define vmem_zalloc(sz, fl)		kmem_zalloc((sz), (fl))
#define vmem_free(sz, fl)		kmem_free((sz), (fl))

#endif	/* _SPL_VMEM_H */
