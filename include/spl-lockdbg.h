/*
 *  Copyright (C) 2016 Gvozden Neskovic <neskovic@gmail.com>
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

#ifndef _SPL_LOCKDBG_H
#define	_SPL_LOCKDBG_H

#ifdef DEBUG_LOCK_TRACKING

#include <linux/types.h>
#include <linux/rbtree.h>
#include <linux/hash.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/stacktrace.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/jiffies.h>

#define	SLT_BCKT_SIZE_BITS	(9)
#define	SLT_BCKT_SIZE		(1U << SLT_BCKT_SIZE_BITS)
#define	SLT_HASH(x)		(hash_ptr((void *)x, SLT_BCKT_SIZE_BITS))

typedef struct spl_lock_tracking {
	rwlock_t		slt_rwlock;		/* global rwlock */
	struct rb_root		slt_buckets[SLT_BCKT_SIZE]; /* tracked */
	spinlock_t		slt_locks[SLT_BCKT_SIZE]; /* tracked locks */
	atomic_t		slt_cnt;		/* tracked count */
	struct rb_root		slt_mismatched;		/* mismatched */
	spinlock_t		slt_mismatched_lock;	/* mismatched locks */
	atomic_t		slt_mismatched_cnt;	/* mismatched count */
} spl_lock_tracking_t;

extern spl_lock_tracking_t lock_tracker;

#define	SLT_FILE_LOC(l)		(strrchr(l, '/') ? strrchr(l, '/') + 1 : (l))

#define	SLT_RWLOCK(sltp)		(&(sltp)->slt_rwlock)
#define	SLT_BUCKET(sltp, idx)		(&((sltp)->slt_buckets[idx]))
#define	SLT_BUCKET_LOCK(sltp, idx)	(&((sltp)->slt_locks[idx]))

#define	SLT_TRACKED_NUM(sltp)		atomic_read(&((sltp)->slt_cnt))
#define	SLT_TRACKED_INC(sltp)		atomic_inc(&((sltp)->slt_cnt))
#define	SLT_TRACKED_DEC(sltp)		atomic_dec(&((sltp)->slt_cnt))

#define	SLT_MISMATCHED(sltp)		(&((sltp)->slt_mismatched))
#define	SLT_MISMATCHED_LOCK(sltp)	(&((sltp)->slt_mismatched_lock))
#define	SLT_MISMATCHED_NUM(sltp)	atomic_read(&(sltp)->slt_mismatched_cnt)
#define	SLT_MISMATCHED_INC(sltp)	atomic_inc(&(sltp)->slt_mismatched_cnt)
#define	SLT_MISMATCHED_DEC(sltp)	atomic_dec(&(sltp)->slt_mismatched_cnt)

#define	_OBJ_INIT_LOC1(x) #x
#define	_OBJ_INIT_LOC2(x) _OBJ_INIT_LOC1(x)
#define	OBJ_INIT_LOC __FILE__ ":" _OBJ_INIT_LOC2(__LINE__)

typedef enum  lock_type {
	SLT_MUTEX	= 0,
	SLT_RWLOCK	= 1,
	SLT_CONDVAR	= 2,
} lock_type_t;

typedef struct lock_tracking_node {
	struct rb_node	lt_rbnode;
	ulong_t		lt_ptr;
	lock_type_t	lt_type;
#define	LT_LOC_SIZE	32
	char		lt_loc[LT_LOC_SIZE];
#define	LT_STACK_TRACE_SIZE	22
	ulong_t		lt_trace[LT_STACK_TRACE_SIZE];
	ulong_t		lt_trace_nr;
} lock_tracking_node_t;


extern struct file_operations proc_rwlock_tracking_operations;
extern struct file_operations proc_mutex_tracking_operations;
extern struct file_operations proc_condvar_tracking_operations;

/*
 * Tracker interface:
 * - spl_lock_tracking_record_init(lock_type_t, ulong_t, char *)
 * - spl_lock_tracking_record_destroy(lock_type_t, ulong_t, char *)
 */
void spl_lock_tracking_record_init(lock_type_t type, ulong_t lp, char *loc);
void spl_lock_tracking_record_destroy(lock_type_t type, ulong_t lp, char *loc);

void spl_lockdbg_fini(void);

#else

#define	DEFINE_LOCK_TRACKING(module)	void *module = NULL

#define	spl_lock_tracking_record_init(a, b, c)		do {} while (0)
#define	spl_lock_tracking_record_destroy(a, b, c)	do {} while (0)
#define	spl_lockdbg_fini()				do {} while (0)

#endif /* DEBUG_LOCK_TRACKING */

#endif /* _SPL_LOCKDBG_H */
