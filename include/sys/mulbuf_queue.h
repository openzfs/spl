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

#ifndef _SPL_MULBUF_QUEUE_H
#define	_SPL_MULBUF_QUEUE_H

#if defined(__x86_64) && defined(__KERNEL__) && defined(HAVE_HASH_MB)

#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <sys/types.h>
#include <sys/thread.h>
#include <sys/rwlock.h>

#include "mulbuf_thdpool.h"

/* taskjob's callback function */
typedef struct mbtp_task mbtp_task_t;
typedef void (*mbtp_task_cb)(mbtp_task_t *mb_task, void *arg);


struct mbtp_task {
	void *buffer;
	size_t size;
	unsigned char *digest;

	mbtp_task_cb cb_fn; /* callback after task is completed or cancelled */
	void *cb_arg;
	int processsed; /* 1 if processed by hash fn, 2 if cancelled by queue's close */

	struct list_head queue_entry;
};

struct mbtp_queue{
	char *queue_name;		/* queue name */
	struct list_head plthread_list;
	int curr_threadcnt; /* current thread count attached to this queue */
	int idle_threadcnt; /* thread in queue, but not running hash-mb-proc-fn */
	int max_threadcnt; /* max thread count doing jobs */
	int min_threadcnt; /* min thread count waiting to do job */

	struct list_head task_list;
	int curr_taskcnt; /* task count attached to this queue, waiting to be processed */
	int proc_taskcnt; /* task count in processing */
	int total_taskcnt; /* task count processed */

	mulbuf_thdpool_t *pool;
	int leave; /* 1 means this queue is going to leave */

	threadp_func_t thread_fn; /* which function queue's thread should run */

	spinlock_t queue_lock;
	wait_queue_head_t queue_waitq;
};

int mbtp_queue_create(mbtp_queue_t **queue_r, const char *name, mulbuf_thdpool_t *pool,
		int min_threadcnt, int max_threadcnt, threadp_func_t hash_mb_fn);
void mbtp_queue_destroy(mbtp_queue_t *queue);

int mbtp_queue_assign_taskjobcnt(mbtp_queue_t *queue, int process_num, int concurrent_num);

int mbtp_queue_add_thread(mbtp_queue_t *queue);
void mbtp_queue_shrink_thread(mbtp_queue_t *queue, mbtp_thread_t *tpt);
int mbtp_queue_check_add_thread(mbtp_queue_t *queue, int concurrent_num);
int mbtp_queue_check_shrink_thread(mbtp_queue_t *queue, int concurrent_num);

void mbtp_queue_submit_job(mbtp_task_t *mb_task, mbtp_queue_t *queue);

#endif /* __KERNEL__ && __x86_64 && HAVE_HASH_MB */

#endif  /* _SPL_MULBUF_QUEUE_H */
