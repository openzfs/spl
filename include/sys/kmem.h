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

#ifndef _SPL_KMEM_H
#define	_SPL_KMEM_H

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/hash.h>
#include <linux/rbtree.h>
#include <linux/ctype.h>
#include <asm/atomic.h>
#include <sys/types.h>
#include <sys/vmem.h>
#include <sys/vmsystm.h>
#include <sys/kstat.h>
#include <sys/taskq.h>

/*
 * Memory allocation interfaces
 */
#define	KM_SLEEP	0x0000	/* can block for memory; success guaranteed */
#define	KM_NOSLEEP	0x0001	/* cannot block for memory; may fail */
#define	KM_PUSHPAGE	0x0004	/* can block for memory; may use reserve */
#define	KM_ZERO		0x1000	/* zero the allocation */

#define KM_PUBLIC_MASK	(KM_SLEEP | KM_NOSLEEP | KM_PUSHPAGE)

/* XXX: Modify the code to stop using these */
#define	KM_NODEBUG	0x0

/*
 * We use a special process flag to avoid recursive callbacks into
 * the filesystem during transactions.  We will also issue our own
 * warnings, so we explicitly skip any generic ones (silly of us).
 */
static inline gfp_t
kmem_flags_convert(int flags)
{
	gfp_t lflags = __GFP_NOWARN;

	if (flags & KM_NOSLEEP) {
		lflags |= GFP_ATOMIC | __GFP_NORETRY;
	} else {
		lflags |= GFP_KERNEL;
		if ((current->flags & PF_FSTRANS))
			lflags &= ~(__GFP_IO|__GFP_FS);
	}

	if (flags & KM_PUSHPAGE)
		lflags |= __GFP_HIGH;

	if (flags & KM_ZERO)
		lflags |= __GFP_ZERO;

	return (lflags);
}

typedef struct {
	struct task_struct *fstrans_thread;
	unsigned int saved_flags;
} fstrans_cookie_t;

static inline fstrans_cookie_t
spl_fstrans_mark(void)
{
	fstrans_cookie_t cookie;

	VERIFY(cookie.fstrans_thread = current);

	cookie.saved_flags = current->flags & PF_FSTRANS;
	current->flags |= PF_FSTRANS;

	return (cookie);
}

static inline void
spl_fstrans_unmark(fstrans_cookie_t cookie)
{
	VERIFY(cookie.fstrans_thread == current);
	VERIFY(current->flags & PF_FSTRANS);

	current->flags &= ~(PF_FSTRANS);
	current->flags |= cookie.saved_flags;
}

/*
 * This is a version of vmalloc() that hooks into PF_FSTRANS.
 */
extern void *spl_vmalloc (unsigned long size, gfp_t lflags, pgprot_t prot);

extern void *spl_kmem_alloc(size_t size, int flags);
extern void *spl_kmem_zalloc(size_t size, int flags);
extern void spl_kmem_free(const void *buf, size_t size);

#ifdef DEBUG_KMEM

/*
 * Memory accounting functions to be used only when DEBUG_KMEM is set.
 */
# ifdef HAVE_ATOMIC64_T

# define kmem_alloc_used_add(size)      atomic64_add(size, &kmem_alloc_used)
# define kmem_alloc_used_sub(size)      atomic64_sub(size, &kmem_alloc_used)
# define kmem_alloc_used_read()         atomic64_read(&kmem_alloc_used)
# define kmem_alloc_used_set(size)      atomic64_set(&kmem_alloc_used, size)

extern atomic64_t kmem_alloc_used;
extern unsigned long long kmem_alloc_max;

# else  /* HAVE_ATOMIC64_T */

# define kmem_alloc_used_add(size)      atomic_add(size, &kmem_alloc_used)
# define kmem_alloc_used_sub(size)      atomic_sub(size, &kmem_alloc_used)
# define kmem_alloc_used_read()         atomic_read(&kmem_alloc_used)
# define kmem_alloc_used_set(size)      atomic_set(&kmem_alloc_used, size)

extern atomic_t kmem_alloc_used;
extern unsigned long long kmem_alloc_max;

# endif /* HAVE_ATOMIC64_T */

# ifdef DEBUG_KMEM_TRACKING
/*
 * DEBUG_KMEM && DEBUG_KMEM_TRACKING
 *
 * The maximum level of memory debugging.  All memory will be accounted
 * for and each allocation will be explicitly tracked.  Any allocation
 * which is leaked will be reported on module unload and the exact location
 * where that memory was allocation will be reported.  This level of memory
 * tracking will have a significant impact on performance and should only
 * be enabled for debugging.  This feature may be enabled by passing
 * --enable-debug-kmem-tracking to configure.
 */
#  define kmem_alloc(sz, fl)            kmem_alloc_track((sz), (fl),           \
                                             __FUNCTION__, __LINE__, \
					     NUMA_NO_NODE)
#  define kmem_zalloc(sz, fl)           kmem_alloc_track((sz), (fl)|KM_ZERO,\
                                             __FUNCTION__, __LINE__, \
					     NUMA_NO_NODE)
#  define kmem_free(ptr, sz)            kmem_free_track((ptr), (sz))

extern void *kmem_alloc_track(size_t, int, const char *, int, int);
extern void kmem_free_track(const void *, size_t);

# else /* DEBUG_KMEM_TRACKING */
/*
 * DEBUG_KMEM && !DEBUG_KMEM_TRACKING
 *
 * The default build will set DEBUG_KEM.  This provides basic memory
 * accounting with little to no impact on performance.  When the module
 * is unloaded in any memory was leaked the total number of leaked bytes
 * will be reported on the console.  To disable this basic accounting
 * pass the --disable-debug-kmem option to configure.
 */
#  define kmem_alloc(sz, fl)            kmem_alloc_debug((sz), (fl),           \
                                             __FUNCTION__, __LINE__, \
					     NUMA_NO_NODE)
#  define kmem_zalloc(sz, fl)           kmem_alloc_debug((sz), (fl)|KM_ZERO,\
                                             __FUNCTION__, __LINE__, \
					     NUMA_NO_NODE)
#  define kmem_free(ptr, sz)            kmem_free_debug((ptr), (sz))

extern void *kmem_alloc_debug(size_t, int, const char *, int, int);
extern void kmem_free_debug(const void *, size_t);

# endif /* DEBUG_KMEM_TRACKING */
#else /* DEBUG_KMEM */
/*
 * !DEBUG_KMEM && !DEBUG_KMEM_TRACKING
 *
 * All debugging is disabled.  There will be no overhead even for
 * minimal memory accounting.  To enable basic accounting pass the
 * --enable-debug-kmem option to configure.
 */
# define kmem_alloc(sz, fl)             spl_kmem_alloc((sz), (fl))
# define kmem_zalloc(sz, fl)            spl_kmem_zalloc((sz), (fl))
# define kmem_free(ptr, sz)             spl_kmem_free((ptr), (sz))

#endif /* DEBUG_KMEM */

extern int kmem_debugging(void);
extern char *kmem_vasprintf(const char *fmt, va_list ap);
extern char *kmem_asprintf(const char *fmt, ...);
extern char *strdup(const char *str);
extern void strfree(char *str);


/*
 * Slab allocation interfaces.  The SPL slab differs from the standard
 * Linux SLAB or SLUB primarily in that each cache may be backed by slabs
 * allocated from the physical or virtal memory address space.  The virtual
 * slabs allow for good behavior when allocation large objects of identical
 * size.  This slab implementation also supports both constructors and
 * destructions which the Linux slab does not.
 */
enum {
	KMC_BIT_NOTOUCH		= 0,	/* Don't update ages */
	KMC_BIT_NODEBUG		= 1,	/* Default behavior */
	KMC_BIT_NOMAGAZINE	= 2,	/* XXX: Unsupported */
	KMC_BIT_NOHASH		= 3,	/* XXX: Unsupported */
	KMC_BIT_QCACHE		= 4,	/* XXX: Unsupported */
	KMC_BIT_KMEM		= 5,	/* Use kmem cache */
	KMC_BIT_VMEM		= 6,	/* Use vmem cache */
	KMC_BIT_SLAB		= 7,	/* Use Linux slab cache */
	KMC_BIT_OFFSLAB		= 8,	/* Objects not on slab */
	KMC_BIT_NOEMERGENCY	= 9,	/* Disable emergency objects */
	KMC_BIT_DEADLOCKED      = 14,	/* Deadlock detected */
	KMC_BIT_GROWING         = 15,   /* Growing in progress for KM_SLEEP */
	KMC_BIT_GROWING_HIGH	= 16,	/* Growing in progress */
	KMC_BIT_REAPING		= 17,	/* Reaping in progress */
	KMC_BIT_DESTROY		= 18,	/* Destroy in progress */
	KMC_BIT_TOTAL		= 19,	/* Proc handler helper bit */
	KMC_BIT_ALLOC		= 20,	/* Proc handler helper bit */
	KMC_BIT_MAX		= 21,	/* Proc handler helper bit */
};

/* kmem move callback return values */
typedef enum kmem_cbrc {
	KMEM_CBRC_YES		= 0,	/* Object moved */
	KMEM_CBRC_NO		= 1,	/* Object not moved */
	KMEM_CBRC_LATER		= 2,	/* Object not moved, try again later */
	KMEM_CBRC_DONT_NEED	= 3,	/* Neither object is needed */
	KMEM_CBRC_DONT_KNOW	= 4,	/* Object unknown */
} kmem_cbrc_t;

#define KMC_NOTOUCH		(1 << KMC_BIT_NOTOUCH)
#define KMC_NODEBUG		(1 << KMC_BIT_NODEBUG)
#define KMC_NOMAGAZINE		(1 << KMC_BIT_NOMAGAZINE)
#define KMC_NOHASH		(1 << KMC_BIT_NOHASH)
#define KMC_QCACHE		(1 << KMC_BIT_QCACHE)
#define KMC_KMEM		(1 << KMC_BIT_KMEM)
#define KMC_VMEM		(1 << KMC_BIT_VMEM)
#define KMC_SLAB		(1 << KMC_BIT_SLAB)
#define KMC_OFFSLAB		(1 << KMC_BIT_OFFSLAB)
#define KMC_NOEMERGENCY		(1 << KMC_BIT_NOEMERGENCY)
#define KMC_DEADLOCKED		(1 << KMC_BIT_DEADLOCKED)
#define KMC_GROWING		(1 << KMC_BIT_GROWING)
#define KMC_GROWING_HIGH	(1 << KMC_BIT_GROWING_HIGH)
#define KMC_REAPING		(1 << KMC_BIT_REAPING)
#define KMC_DESTROY		(1 << KMC_BIT_DESTROY)
#define KMC_TOTAL		(1 << KMC_BIT_TOTAL)
#define KMC_ALLOC		(1 << KMC_BIT_ALLOC)
#define KMC_MAX			(1 << KMC_BIT_MAX)

#define KMC_REAP_CHUNK		INT_MAX
#define KMC_DEFAULT_SEEKS	1

#define KMC_EXPIRE_AGE		0x1     /* Due to age */
#define KMC_EXPIRE_MEM		0x2     /* Due to low memory */

#define	KMC_RECLAIM_ONCE	0x1	/* Force a single shrinker pass */

extern unsigned int spl_kmem_cache_expire;
extern struct list_head spl_kmem_cache_list;
extern struct rw_semaphore spl_kmem_cache_sem;

#define SKM_MAGIC			0x2e2e2e2e
#define SKO_MAGIC			0x20202020
#define SKS_MAGIC			0x22222222
#define SKC_MAGIC			0x2c2c2c2c

#define SPL_KMEM_CACHE_DELAY		15	/* Minimum slab release age */
#define SPL_KMEM_CACHE_REAP		0	/* Default reap everything */
#define SPL_KMEM_CACHE_OBJ_PER_SLAB	16	/* Target objects per slab */
#define SPL_KMEM_CACHE_OBJ_PER_SLAB_MIN	8	/* Minimum objects per slab */
#define SPL_KMEM_CACHE_ALIGN		8	/* Default object alignment */

#define POINTER_IS_VALID(p)		0	/* Unimplemented */
#define POINTER_INVALIDATE(pp)			/* Unimplemented */

typedef int (*spl_kmem_ctor_t)(void *, void *, int);
typedef void (*spl_kmem_dtor_t)(void *, void *);
typedef void (*spl_kmem_reclaim_t)(void *);

typedef struct spl_kmem_magazine {
	uint32_t		skm_magic;	/* Sanity magic */
	uint32_t		skm_avail;	/* Available objects */
	uint32_t		skm_size;	/* Magazine size */
	uint32_t		skm_refill;	/* Batch refill size */
	struct spl_kmem_cache	*skm_cache;	/* Owned by cache */
	unsigned long		skm_age;	/* Last cache access */
	unsigned int		skm_cpu;	/* Owned by cpu */
	void			*skm_objs[0];	/* Object pointers */
} spl_kmem_magazine_t;

typedef struct spl_kmem_obj {
        uint32_t		sko_magic;	/* Sanity magic */
	void			*sko_addr;	/* Buffer address */
	struct spl_kmem_slab	*sko_slab;	/* Owned by slab */
	struct list_head	sko_list;	/* Free object list linkage */
} spl_kmem_obj_t;

typedef struct spl_kmem_slab {
        uint32_t		sks_magic;	/* Sanity magic */
	uint32_t		sks_objs;	/* Objects per slab */
	struct spl_kmem_cache	*sks_cache;	/* Owned by cache */
	struct list_head	sks_list;	/* Slab list linkage */
	struct list_head	sks_free_list;	/* Free object list */
	unsigned long		sks_age;	/* Last modify jiffie */
	uint32_t		sks_ref;	/* Ref count used objects */
} spl_kmem_slab_t;

typedef struct spl_kmem_alloc {
	struct spl_kmem_cache	*ska_cache;	/* Owned by cache */
	int			ska_flags;	/* Allocation flags */
	taskq_ent_t		ska_tqe;	/* Task queue entry */
} spl_kmem_alloc_t;

typedef struct spl_kmem_emergency {
	struct rb_node		ske_node;	/* Emergency tree linkage */
	void			*ske_obj;	/* Buffer address */
} spl_kmem_emergency_t;

typedef struct spl_kmem_cache {
	uint32_t		skc_magic;	/* Sanity magic */
	uint32_t		skc_name_size;	/* Name length */
	char			*skc_name;	/* Name string */
	spl_kmem_magazine_t	*skc_mag[NR_CPUS]; /* Per-CPU warm cache */
	uint32_t		skc_mag_size;	/* Magazine size */
	uint32_t		skc_mag_refill;	/* Magazine refill count */
	spl_kmem_ctor_t		skc_ctor;	/* Constructor */
	spl_kmem_dtor_t		skc_dtor;	/* Destructor */
	spl_kmem_reclaim_t	skc_reclaim;	/* Reclaimator */
	void			*skc_private;	/* Private data */
	void			*skc_vmp;	/* Unused */
	struct kmem_cache	*skc_linux_cache; /* Linux slab cache if used */
	unsigned long		skc_flags;	/* Flags */
	uint32_t		skc_obj_size;	/* Object size */
	uint32_t		skc_obj_align;	/* Object alignment */
	uint32_t		skc_slab_objs;	/* Objects per slab */
	uint32_t		skc_slab_size;	/* Slab size */
	uint32_t		skc_delay;	/* Slab reclaim interval */
	uint32_t		skc_reap;	/* Slab reclaim count */
	atomic_t		skc_ref;	/* Ref count callers */
	taskqid_t		skc_taskqid;	/* Slab reclaim task */
	struct list_head	skc_list;	/* List of caches linkage */
	struct list_head	skc_complete_list;/* Completely alloc'ed */
	struct list_head	skc_partial_list; /* Partially alloc'ed */
	struct rb_root		skc_emergency_tree; /* Min sized objects */
	spinlock_t		skc_lock;	/* Cache lock */
	wait_queue_head_t	skc_waitq;	/* Allocation waiters */
	uint64_t		skc_slab_fail;	/* Slab alloc failures */
	uint64_t		skc_slab_create;/* Slab creates */
	uint64_t		skc_slab_destroy;/* Slab destroys */
	uint64_t		skc_slab_total;	/* Slab total current */
	uint64_t		skc_slab_alloc;	/* Slab alloc current */
	uint64_t		skc_slab_max;	/* Slab max historic  */
	uint64_t		skc_obj_total;	/* Obj total current */
	uint64_t		skc_obj_alloc;	/* Obj alloc current */
	uint64_t		skc_obj_max;	/* Obj max historic */
	uint64_t		skc_obj_deadlock;  /* Obj emergency deadlocks */
	uint64_t		skc_obj_emergency; /* Obj emergency current */
	uint64_t		skc_obj_emergency_max; /* Obj emergency max */
} spl_kmem_cache_t;
#define kmem_cache_t		spl_kmem_cache_t

extern spl_kmem_cache_t *spl_kmem_cache_create(char *name, size_t size,
	size_t align, spl_kmem_ctor_t ctor, spl_kmem_dtor_t dtor,
	spl_kmem_reclaim_t reclaim, void *priv, void *vmp, int flags);
extern void spl_kmem_cache_set_move(spl_kmem_cache_t *,
	kmem_cbrc_t (*)(void *, void *, size_t, void *));
extern void spl_kmem_cache_destroy(spl_kmem_cache_t *skc);
extern void *spl_kmem_cache_alloc(spl_kmem_cache_t *skc, int flags);
extern void spl_kmem_cache_free(spl_kmem_cache_t *skc, void *obj);
extern void spl_kmem_cache_reap_now(spl_kmem_cache_t *skc, int count);
extern void spl_kmem_reap(void);

int spl_kmem_init(void);
void spl_kmem_fini(void);

#define kmem_cache_create(name,size,align,ctor,dtor,rclm,priv,vmp,flags) \
        spl_kmem_cache_create(name,size,align,ctor,dtor,rclm,priv,vmp,flags)
#define kmem_cache_set_move(skc, move)	spl_kmem_cache_set_move(skc, move)
#define kmem_cache_destroy(skc)		spl_kmem_cache_destroy(skc)
#define kmem_cache_alloc(skc, flags)	spl_kmem_cache_alloc(skc, flags)
#define kmem_cache_free(skc, obj)	spl_kmem_cache_free(skc, obj)
#define kmem_cache_reap_now(skc)	\
        spl_kmem_cache_reap_now(skc, skc->skc_reap)
#define kmem_reap()			spl_kmem_reap()

/*
 * Allow custom slab allocation flags to be set for KMC_SLAB based caches.
 * One use for this function is to ensure the __GFP_COMP flag is part of
 * the default allocation mask which ensures higher order allocations are
 * properly refcounted.  This flag was added to the default ->allocflags
 * as of Linux 3.11.
 */
static inline void
kmem_cache_set_allocflags(spl_kmem_cache_t *skc, gfp_t flags)
{
	if (skc->skc_linux_cache == NULL)
		return;

#if defined(HAVE_KMEM_CACHE_ALLOCFLAGS)
	skc->skc_linux_cache->allocflags |= flags;
#elif defined(HAVE_KMEM_CACHE_GFPFLAGS)
	skc->skc_linux_cache->gfpflags |= flags;
#endif
}

#endif	/* _SPL_KMEM_H */
