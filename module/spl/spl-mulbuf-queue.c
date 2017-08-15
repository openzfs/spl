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
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Multi-Buffer Hash Queue Basic Implementation.
\*****************************************************************************/

#if defined(__x86_64) && defined(__KERNEL__) && defined(HAVE_HASH_MB)

#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <sys/kmem.h>
#include <sys/mulbuf_queue.h>

int mbtp_queue_create(mbtp_queue_t ** queue_r, const char *name, mulbuf_thdpool_t * pool,
		      int min_threadcnt, int max_threadcnt, threadp_func_t hash_mb_fn)
{
	int i;
	mbtp_thread_t *tpt;
	mbtp_queue_t *queue;

	queue = kmem_alloc(sizeof(*queue), KM_PUSHPAGE);
	*queue_r = queue;
	if (queue == NULL)
		return 1;

	queue->queue_name = strdup(name);
	queue->curr_threadcnt = 0;
	queue->idle_threadcnt = 0;
	queue->max_threadcnt = max_threadcnt;
	queue->min_threadcnt = min_threadcnt;

	queue->pool = pool;
	queue->leave = 0;
	queue->thread_fn = hash_mb_fn;

	INIT_LIST_HEAD(&queue->plthread_list);
	INIT_LIST_HEAD(&queue->task_list);

	queue->curr_taskcnt = 0;
	queue->proc_taskcnt = 0;
	queue->total_taskcnt = 0;

	init_waitqueue_head(&queue->queue_waitq);
	spin_lock_init(&queue->queue_lock);

	for (i = 0; i < queue->min_threadcnt; i++) {
		/* get thread and attach it to list */
		if (!mulbuf_thdpool_get_thread(queue->pool, &tpt)) {
			list_add_tail(&tpt->queue_entry, &queue->plthread_list);
			tpt->queue = queue;
			mbtp_thread_run_fn(tpt, queue->thread_fn, (void *)tpt);
			queue->curr_threadcnt++;
			queue->idle_threadcnt++;
		}
	}

	if (queue->curr_threadcnt == 0)
		return 1;

	return 0;
}

void mbtp_queue_destroy(mbtp_queue_t * queue)
{
	DECLARE_WAITQUEUE(queue_wait, current);
	mbtp_thread_t *tpt;
	mbtp_task_t *tj;
	unsigned long flags;

	struct list_head *l, *l_tmp;

	spin_lock_irqsave(&queue->queue_lock, flags);
	queue->leave = 1;

	/* cancel unassigned taskjobs */
	while (!list_empty(&queue->task_list)) {
		tj = list_entry(queue->task_list.next, mbtp_task_t, queue_entry);
		list_del(&tj->queue_entry);
		/* tag this task as cancelled */
		tj->processsed = 2;
		tj->cb_fn(tj, tj->cb_arg);
		queue->curr_taskcnt--;
	}

	/* validate thread state */
	list_for_each(l, &queue->plthread_list) {
		tpt = list_entry(l, mbtp_thread_t, queue_entry);
		ASSERT(tpt->next_state == THREAD_RUNNING);
	}

	/* wake waiting threads to leave */
	wake_up_all(&queue->queue_waitq);

	spin_unlock_irqrestore(&queue->queue_lock, flags);

	/*
	 * Detach threads from queue
	 * since threads will not operate queue elements, there is no need to lock queue
	 */
	list_for_each_safe(l, l_tmp, &queue->plthread_list) {

		/* check and wait whether this tqt is left */
		tpt = list_entry(l, mbtp_thread_t, queue_entry);

		spin_lock_irqsave(&tpt->thd_lock, flags);

		/* thread may be going ready or running */
		if (tpt->next_state != THREAD_READY) {
			add_wait_queue_exclusive(&tpt->thread_waitq, &queue_wait);

			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irqrestore(&tpt->thd_lock, flags);

			schedule();

			spin_lock_irqsave(&tpt->thd_lock, flags);
			__set_current_state(TASK_RUNNING);
			remove_wait_queue(&tpt->thread_waitq, &queue_wait);
		}

		spin_unlock_irqrestore(&tpt->thd_lock, flags);

		mbtp_queue_shrink_thread(queue, tpt);
	}

	strfree(queue->queue_name);
	kmem_free(queue, sizeof(*queue));

	return;
}

/*
 * Assign tasks to thread
 *
 * process_num process_num number of jobs thread is processing
 * concurrent_num thread's concurrent capability of maximum jobs
 * return how many tasks should be taken
 */
int mbtp_queue_assign_taskjobcnt(mbtp_queue_t * queue, int process_num, int concurrent_num)
{
	int idle_threadcnt = queue->idle_threadcnt;
	int curr_taskjobcnt = queue->curr_taskcnt;
	int take_num;

	if (curr_taskjobcnt == 0) {
		/* take 0 if no task */
		take_num = 0;
	} else if (curr_taskjobcnt + process_num >= idle_threadcnt - 1 + concurrent_num) {
		/* take Concurrent_num if other idle thread can take at least one task job */
		take_num = concurrent_num - process_num;
	} else if (idle_threadcnt > 1) {
		/* not the last thread, take 1 task */
		take_num = 1;
	} else {
		/* the last one idle thread */
		if (curr_taskjobcnt + process_num >= concurrent_num) {
			take_num = concurrent_num - process_num;
		} else {
			take_num = curr_taskjobcnt;
		}
	}

	return take_num;
}

/*
 * Add thread into queue
 * requires under lock of queue
 * returns	0 add thread successfully; 1 failed to add
 */
int mbtp_queue_add_thread(mbtp_queue_t * queue)
{
	mbtp_thread_t *new_tpt;
	unsigned long flags;

	if (!mulbuf_thdpool_get_thread(queue->pool, &new_tpt)) {
		/* add threadcnt before run thread */
		spin_lock_irqsave(&queue->queue_lock, flags);
		list_add_tail(&new_tpt->queue_entry, &queue->plthread_list);
		new_tpt->queue = queue;
		queue->curr_threadcnt++;
		queue->idle_threadcnt++;
		spin_unlock_irqrestore(&queue->queue_lock, flags);

		mbtp_thread_run_fn(new_tpt, queue->thread_fn, (void *)new_tpt);

		return 0;
	}
	return 1;
}

/*
 * Shrink thread into queue
 * Requires under lock of queue;
 * Called by thread itself if thread is running hash_mb_fn
 */
void mbtp_queue_shrink_thread(mbtp_queue_t * queue, mbtp_thread_t * tpt)
{
	queue->curr_threadcnt--;
	queue->idle_threadcnt--;

	tpt->queue = NULL;
	list_del(&tpt->queue_entry);
	mulbuf_thdpool_put_thread(tpt);
}

/*
 * Check and add thread if needed
 *
 * return 0 if need to add thread
 * return 1 if no need
 */
int mbtp_queue_check_add_thread(mbtp_queue_t * queue, int concurrent_num)
{
	int curr_threadcnt = queue->curr_threadcnt;
	int idle_threadcnt = queue->idle_threadcnt;
	int max_threadcnt = queue->max_threadcnt;
	int proc_taskjobcnt = queue->proc_taskcnt;

	/*
	 * add_by_proc is 1 if thread need to add from processing job's view
	 * add_by_thread is 1 if adding is not limited by max_threadcnt
	 */
	int add_by_proc = 0, add_by_thread = 0;

	if (curr_threadcnt < max_threadcnt && idle_threadcnt == 0) {
		add_by_thread = 1;
	}

	if (proc_taskjobcnt == (curr_threadcnt - idle_threadcnt) * concurrent_num) {
		add_by_proc = 1;
	}

	if (add_by_proc && add_by_thread) {
		return 0;
	}

	return 1;
}

/*
 * Check and shrink thread if needed
 *
 * return 0 if need to shrink thread
 * return 1 if no need
 */
int mbtp_queue_check_shrink_thread(mbtp_queue_t * queue, int concurrent_num)
{
	int idle_threadcnt = queue->idle_threadcnt;
	int proc_taskjobcnt = queue->proc_taskcnt;
	int curr_threadcnt = queue->curr_threadcnt;
	int min_threadcnt = queue->min_threadcnt;

	/*
	 * shrink_by_proc is 1 if thread need to shrink from processing job's view
	 * shrink_by_thread is 1 if shrinking is not limited by min_threadcnt
	 */
	int shrink_by_proc = 0, shrink_by_thread = 0;

	/* current threadcnt should be at least equal to min_threadcnt */
	if (curr_threadcnt > min_threadcnt)
		shrink_by_thread = 1;

	/* at least, there is one thread doing snoop */
	if (proc_taskjobcnt < (curr_threadcnt - idle_threadcnt) * concurrent_num)
		shrink_by_proc = 1;

	if (shrink_by_proc && shrink_by_thread) {
		return 0;
	}

	return 1;
}

void mbtp_queue_submit_job(mbtp_task_t * mb_task, mbtp_queue_t * queue)
{
	unsigned long flags;

	spin_lock_irqsave(&queue->queue_lock, flags);

	queue->curr_taskcnt++;
	queue->total_taskcnt++;
	list_add_tail(&mb_task->queue_entry, &queue->task_list);

	/* signal queue to wake thread if before there is no other jobs in queue */
	if (queue->curr_taskcnt == 1)
		wake_up(&queue->queue_waitq);

	spin_unlock_irqrestore(&queue->queue_lock, flags);

	return;
}

#endif /* __KERNEL__ && __x86_64 && HAVE_HASH_MB */
