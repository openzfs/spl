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

#include <sys/debug.h>
#include <sys/vmem.h>
#include <linux/module.h>

vmem_t *heap_arena = NULL;
EXPORT_SYMBOL(heap_arena);

vmem_t *zio_alloc_arena = NULL;
EXPORT_SYMBOL(zio_alloc_arena);

vmem_t *zio_arena = NULL;
EXPORT_SYMBOL(zio_arena);

size_t
vmem_size(vmem_t *vmp, int typemask)
{
	ASSERT3P(vmp, ==, NULL);
	ASSERT3S(typemask & VMEM_ALLOC, ==, VMEM_ALLOC);
	ASSERT3S(typemask & VMEM_FREE, ==, VMEM_FREE);

	return (VMALLOC_TOTAL);
}
EXPORT_SYMBOL(vmem_size);

/*
 * Memory allocation interfaces and debugging for basic kmem_*
 * and vmem_* style memory allocation.  When DEBUG_KMEM is enabled
 * the SPL will keep track of the total memory allocated, and
 * report any memory leaked when the module is unloaded.
 */
#ifdef DEBUG_KMEM

/* Shim layer memory accounting */
#ifdef HAVE_ATOMIC64_T
atomic64_t vmem_alloc_used = ATOMIC64_INIT(0);
unsigned long long vmem_alloc_max = 0;
#else  /* HAVE_ATOMIC64_T */
atomic_t vmem_alloc_used = ATOMIC_INIT(0);
unsigned long long vmem_alloc_max = 0;
#endif /* HAVE_ATOMIC64_T */

EXPORT_SYMBOL(vmem_alloc_used);
EXPORT_SYMBOL(vmem_alloc_max);

/*
 * When DEBUG_KMEM_TRACKING is enabled not only will total bytes be tracked
 * but also the location of every alloc and free.  When the SPL module is
 * unloaded a list of all leaked addresses and where they were allocated
 * will be dumped to the console.  Enabling this feature has a significant
 * impact on performance but it makes finding memory leaks straight forward.
 *
 * Not surprisingly with debugging enabled the xmem_locks are very highly
 * contended particularly on xfree().  If we want to run with this detailed
 * debugging enabled for anything other than debugging  we need to minimize
 * the contention by moving to a lock per xmem_table entry model.
 */
#ifdef DEBUG_KMEM_TRACKING

#define	VMEM_HASH_BITS		10
#define	VMEM_TABLE_SIZE		(1 << VMEM_HASH_BITS)

typedef struct kmem_debug {
	struct hlist_node kd_hlist;	/* Hash node linkage */
	struct list_head kd_list;	/* List of all allocations */
	void *kd_addr;			/* Allocation pointer */
	size_t kd_size;			/* Allocation size */
	const char *kd_func;		/* Allocation function */
	int kd_line;			/* Allocation line */
} kmem_debug_t;

spinlock_t vmem_lock;
struct hlist_head vmem_table[VMEM_TABLE_SIZE];
struct list_head vmem_list;

EXPORT_SYMBOL(vmem_lock);
EXPORT_SYMBOL(vmem_table);
EXPORT_SYMBOL(vmem_list);

void *
vmem_alloc_track(size_t size, int flags, const char *func, int line)
{
	void *ptr = NULL;
	kmem_debug_t *dptr;
	unsigned long irq_flags;

	ASSERT(flags & KM_SLEEP);

	/* Function may be called with KM_NOSLEEP so failure is possible */
	dptr = (kmem_debug_t *) kmalloc_nofail(sizeof (kmem_debug_t),
	    flags & ~__GFP_ZERO);
	if (unlikely(dptr == NULL)) {
		printk(KERN_WARNING "debug vmem_alloc(%ld, 0x%x) "
		    "at %s:%d failed (%lld/%llu)\n",
		    sizeof (kmem_debug_t), flags, func, line,
		    vmem_alloc_used_read(), vmem_alloc_max);
	} else {
		/*
		 * We use __strdup() below because the string pointed to by
		 * __FUNCTION__ might not be available by the time we want
		 * to print it, since the module might have been unloaded.
		 * This can never fail because we have already asserted
		 * that flags is KM_SLEEP.
		 */
		dptr->kd_func = __strdup(func, flags & ~__GFP_ZERO);
		if (unlikely(dptr->kd_func == NULL)) {
			kfree(dptr);
			printk(KERN_WARNING "debug __strdup() at %s:%d "
			    "failed (%lld/%llu)\n", func, line,
			    vmem_alloc_used_read(), vmem_alloc_max);
			goto out;
		}

		/* Use the correct allocator */
		if (flags & __GFP_ZERO) {
			ptr = vzalloc_nofail(size, flags & ~__GFP_ZERO);
		} else {
			ptr = vmalloc_nofail(size, flags);
		}

		if (unlikely(ptr == NULL)) {
			kfree(dptr->kd_func);
			kfree(dptr);
			printk(KERN_WARNING "vmem_alloc (%llu, 0x%x) "
			    "at %s:%d failed (%lld/%llu)\n",
			    (unsigned long long) size, flags, func, line,
			    vmem_alloc_used_read(), vmem_alloc_max);
			goto out;
		}

		vmem_alloc_used_add(size);
		if (unlikely(vmem_alloc_used_read() > vmem_alloc_max))
			vmem_alloc_max = vmem_alloc_used_read();

		INIT_HLIST_NODE(&dptr->kd_hlist);
		INIT_LIST_HEAD(&dptr->kd_list);

		dptr->kd_addr = ptr;
		dptr->kd_size = size;
		dptr->kd_line = line;

		spin_lock_irqsave(&vmem_lock, irq_flags);
		hlist_add_head(&dptr->kd_hlist,
		    &vmem_table[hash_ptr(ptr, VMEM_HASH_BITS)]);
		list_add_tail(&dptr->kd_list, &vmem_list);
		spin_unlock_irqrestore(&vmem_lock, irq_flags);
	}
out:
	return (ptr);
}
EXPORT_SYMBOL(vmem_alloc_track);

void
vmem_free_track(const void *ptr, size_t size)
{
	kmem_debug_t *dptr;

	ASSERTF(ptr || size > 0, "ptr: %p, size: %llu", ptr,
	    (unsigned long long) size);

	/* Must exist in hash due to vmem_alloc() */
	dptr = kmem_del_init(&vmem_lock, vmem_table, VMEM_HASH_BITS, ptr);
	ASSERT(dptr);

	/* Size must match */
	ASSERTF(dptr->kd_size == size, "kd_size (%llu) != size (%llu), "
	    "kd_func = %s, kd_line = %d\n", (unsigned long long) dptr->kd_size,
	    (unsigned long long) size, dptr->kd_func, dptr->kd_line);

	vmem_alloc_used_sub(size);
	kfree(dptr->kd_func);

	memset((void *)dptr, 0x5a, sizeof (kmem_debug_t));
	kfree(dptr);

	memset((void *)ptr, 0x5a, size);
	vfree(ptr);
}
EXPORT_SYMBOL(vmem_free_track);

#else /* DEBUG_KMEM_TRACKING */

void *
vmem_alloc_debug(size_t size, int flags, const char *func, int line)
{
	void *ptr;

	ASSERT(flags & KM_SLEEP);

	/* Use the correct allocator */
	if (flags & __GFP_ZERO) {
		ptr = vzalloc_nofail(size, flags & (~__GFP_ZERO));
	} else {
		ptr = vmalloc_nofail(size, flags);
	}

	if (unlikely(ptr == NULL)) {
		printk(KERN_WARNING
		    "vmem_alloc(%llu, 0x%x) at %s:%d failed (%lld/%llu)\n",
		    (unsigned long long)size, flags, func, line,
		    (unsigned long long)vmem_alloc_used_read(), vmem_alloc_max);
	} else {
		vmem_alloc_used_add(size);
		if (unlikely(vmem_alloc_used_read() > vmem_alloc_max))
			vmem_alloc_max = vmem_alloc_used_read();
	}

	return (ptr);
}
EXPORT_SYMBOL(vmem_alloc_debug);

void
vmem_free_debug(const void *ptr, size_t size)
{
	ASSERT(ptr || size > 0);
	vmem_alloc_used_sub(size);
	vfree(ptr);
}
EXPORT_SYMBOL(vmem_free_debug);

#endif /* DEBUG_KMEM_TRACKING */
#endif /* DEBUG_KMEM */

#if defined(DEBUG_KMEM) && defined(DEBUG_KMEM_TRACKING)
static char *
spl_sprintf_addr(kmem_debug_t *kd, char *str, int len, int min)
{
	int size = ((len - 1) < kd->kd_size) ? (len - 1) : kd->kd_size;
	int i, flag = 1;

	ASSERT(str != NULL && len >= 17);
	memset(str, 0, len);

	/*
	 * Check for a fully printable string, and while we are at
	 * it place the printable characters in the passed buffer.
	 */
	for (i = 0; i < size; i++) {
		str[i] = ((char *)(kd->kd_addr))[i];
		if (isprint(str[i])) {
			continue;
		} else {
			/*
			 * Minimum number of printable characters found
			 * to make it worthwhile to print this as ascii.
			 */
			if (i > min)
				break;

			flag = 0;
			break;
		}
	}

	if (!flag) {
		sprintf(str, "%02x%02x%02x%02x%02x%02x%02x%02x",
		    *((uint8_t *)kd->kd_addr),
		    *((uint8_t *)kd->kd_addr + 2),
		    *((uint8_t *)kd->kd_addr + 4),
		    *((uint8_t *)kd->kd_addr + 6),
		    *((uint8_t *)kd->kd_addr + 8),
		    *((uint8_t *)kd->kd_addr + 10),
		    *((uint8_t *)kd->kd_addr + 12),
		    *((uint8_t *)kd->kd_addr + 14));
	}

	return (str);
}

static int
spl_kmem_init_tracking(struct list_head *list, spinlock_t *lock, int size)
{
	int i;

	spin_lock_init(lock);
	INIT_LIST_HEAD(list);

	for (i = 0; i < size; i++)
		INIT_HLIST_HEAD(&kmem_table[i]);

	return (0);
}

static void
spl_kmem_fini_tracking(struct list_head *list, spinlock_t *lock)
{
	unsigned long flags;
	kmem_debug_t *kd;
	char str[17];

	spin_lock_irqsave(lock, flags);
	if (!list_empty(list))
		printk(KERN_WARNING "%-16s %-5s %-16s %s:%s\n", "address",
		    "size", "data", "func", "line");

	list_for_each_entry(kd, list, kd_list)
		printk(KERN_WARNING "%p %-5d %-16s %s:%d\n", kd->kd_addr,
		    (int)kd->kd_size, spl_sprintf_addr(kd, str, 17, 8),
		    kd->kd_func, kd->kd_line);

	spin_unlock_irqrestore(lock, flags);
}
#else /* DEBUG_KMEM && DEBUG_KMEM_TRACKING */
#define	spl_kmem_init_tracking(list, lock, size)
#define	spl_kmem_fini_tracking(list, lock)
#endif /* DEBUG_KMEM && DEBUG_KMEM_TRACKING */

int
spl_vmem_init(void)
{
	int rc = 0;

#ifdef DEBUG_KMEM
	vmem_alloc_used_set(0);
	spl_kmem_init_tracking(&vmem_list, &vmem_lock, VMEM_TABLE_SIZE);
#endif

	return (rc);
}

void
spl_vmem_fini(void)
{
#ifdef DEBUG_KMEM
	/*
	 * Display all unreclaimed memory addresses, including the
	 * allocation size and the first few bytes of what's located
	 * at that address to aid in debugging.  Performance is not
	 * a serious concern here since it is module unload time.
	 */
	if (vmem_alloc_used_read() != 0)
		printk(KERN_WARNING "vmem leaked %ld/%llu bytes\n",
		    vmem_alloc_used_read(), vmem_alloc_max);

	spl_kmem_fini_tracking(&vmem_list, &vmem_lock);
#endif /* DEBUG_KMEM */
}
