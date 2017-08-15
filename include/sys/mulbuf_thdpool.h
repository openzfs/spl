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

#ifndef _SPL_MULBUF_THDPOOL_H
#define	_SPL_MULBUF_THDPOOL_H

#if defined(__x86_64) && defined(__KERNEL__) && defined(HAVE_HASH_MB)

#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <sys/types.h>
#include <sys/thread.h>
#include <sys/rwlock.h>


typedef void (*threadp_func_t)(void *);
typedef struct mulbuf_thdpool mulbuf_thdpool_t;	/* thread pool for multi-buffer crypto */
typedef struct mbtp_thread mbtp_thread_t;	/* thread for multi-buffer thread pool */
typedef struct mbtp_queue mbtp_queue_t;		/* task queue for mb thread pool */

typedef enum mbtp_thd_state {
	THREAD_SETUP,
	THREAD_READY,
	THREAD_RUNNING,
	THREAD_EXIT
} mbtp_thd_state_t;


struct mbtp_thread {
	struct list_head pool_entry;
	mulbuf_thdpool_t *pool;
	struct list_head queue_entry;
	mbtp_queue_t *queue;

	spinlock_t thd_lock;
	wait_queue_head_t thread_waitq;

	struct task_struct	*tp_thread;
	mbtp_thd_state_t curr_state;
	mbtp_thd_state_t next_state;

	threadp_func_t fn;
	void *arg;
};

struct mulbuf_thdpool {
	char *pool_name;	/* thread pool name */
	int curr_threadcnt;	/* current thread count */
	int max_threadcnt;	/* max thread count, if 0, then unlimited */
	int idle_threadcnt;	/* idle thread count */

	spinlock_t pool_lock;
	unsigned long pool_lock_flags; /* interrupt state */
	wait_queue_head_t pool_waitq;

	struct list_head plthread_idle_list; /* idle thread not attached to any queue */
	struct list_head plthread_busy_list; /* busy thread attached to one queue */
};

/* Initialize thread pool */
int mulbuf_thdpool_create(mulbuf_thdpool_t **pool_r, const char *name, int threadcnt, int max_threadcnt);

/* Destroy thread pool */
void mulbuf_thdpool_destroy(mulbuf_thdpool_t *pool);

/*
 * Get a valid thread from pool
 * return 0 if success
 */
int mulbuf_thdpool_get_thread(mulbuf_thdpool_t *pool, mbtp_thread_t **tpt_r);

/* Get a valid thread from pool */
void mbtp_thread_run_fn(mbtp_thread_t *tpt, threadp_func_t fn, void *arg);

/* Return a valid thread to its pool */
void mulbuf_thdpool_put_thread(mbtp_thread_t *tpt);

#endif /* __KERNEL__ && __x86_64 && HAVE_HASH_MB */

#endif  /* _SPL_MULBUF_THDPOOL_H */
