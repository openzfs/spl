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
 *  Solaris Porting Layer (SPL) Multi-Buffer Hash Thread Pool Implementation.
\*****************************************************************************/

#if defined(__x86_64) && defined(__KERNEL__) && defined(HAVE_HASH_MB)

#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <sys/kmem.h>
#include <sys/mulbuf_thdpool.h>

static int mbtp_thread_routine(void *args)
{
	DECLARE_WAITQUEUE(wait, current);

	mbtp_thread_t *tpt = (mbtp_thread_t *) args;
	sigset_t blocked;
	unsigned long flags;

	current->flags |= PF_NOFREEZE;

	sigfillset(&blocked);
	sigprocmask(SIG_BLOCK, &blocked, NULL);
	flush_signals(current);

	spin_lock_irqsave(&tpt->thd_lock, flags);
	tpt->next_state = THREAD_SETUP;

	while (!kthread_should_stop()) {
		tpt->curr_state = tpt->next_state;

		switch (tpt->next_state) {
		case THREAD_SETUP:
			tpt->next_state = THREAD_READY;

			/* Inform thread pool, thead is going to be ready */
			wake_up(&tpt->thread_waitq);
			break;

		case THREAD_READY:
			/* tqt->next_state is determined by queue or pool during wait */
			add_wait_queue_exclusive(&tpt->thread_waitq, &wait);
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irqrestore(&tpt->thd_lock, flags);

			schedule();

			spin_lock_irqsave(&tpt->thd_lock, flags);
			__set_current_state(TASK_RUNNING);
			remove_wait_queue(&tpt->thread_waitq, &wait);

			break;

		case THREAD_RUNNING:
			/* hash mb will change it after its end */
			tpt->next_state = THREAD_RUNNING;
			spin_unlock_irqrestore(&tpt->thd_lock, flags);

			tpt->fn(tpt->arg);

			spin_lock_irqsave(&tpt->thd_lock, flags);
			break;

		case THREAD_EXIT:
		default:
			/* Exit notice to destroy function */
			wake_up(&tpt->thread_waitq);
			spin_unlock_irqrestore(&tpt->thd_lock, flags);

			return 0;
		}
	}

	/* Exit notice to destroy function */
	tpt->curr_state = THREAD_EXIT;
	wake_up(&tpt->thread_waitq);
	spin_unlock_irqrestore(&tpt->thd_lock, flags);

	return 0;

}

static int mbtp_thread_create(mbtp_thread_t ** tpt_r, mulbuf_thdpool_t * pool)
{
	mbtp_thread_t *tpt;

	tpt = kmem_alloc(sizeof(*tpt), KM_PUSHPAGE | KM_ZERO);
	*tpt_r = tpt;

	if (!tpt)
		return 1;

	tpt->fn = NULL;
	tpt->curr_state = THREAD_SETUP;
	tpt->queue = NULL;
	tpt->pool = pool;

	/* tpt facilities initialize */
	spin_lock_init(&tpt->thd_lock);
	init_waitqueue_head(&tpt->thread_waitq);

	tpt->tp_thread = spl_kthread_create(mbtp_thread_routine, tpt, "%s", pool->pool_name);
	if (tpt->tp_thread == NULL) {
		kmem_free(tpt, sizeof(mbtp_thread_t));
		*tpt_r = NULL;
		return 1;
	}

	wake_up_process(tpt->tp_thread);

	return 0;
}

static void mbtp_thread_destroy(mbtp_thread_t * tpt)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;

	/* wait for thread's exit */
	spin_lock_irqsave(&tpt->thd_lock, flags);
	if (tpt->curr_state != THREAD_EXIT) {
		add_wait_queue_exclusive(&tpt->thread_waitq, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&tpt->thd_lock, flags);

		schedule();

		spin_lock_irqsave(&tpt->thd_lock, flags);
		__set_current_state(TASK_RUNNING);
		remove_wait_queue(&tpt->thread_waitq, &wait);
	}
	spin_unlock_irqrestore(&tpt->thd_lock, flags);

	kmem_free(tpt, sizeof(*tpt));

	return;
}

int mulbuf_thdpool_create(mulbuf_thdpool_t ** pool_r, const char *name, int threadcnt,
			  int max_threadcnt)
{
	DECLARE_WAITQUEUE(pool_wait, current);
	unsigned long flags;
	int i;
	mbtp_thread_t *tpt;
	mulbuf_thdpool_t *pool;

	pool = kmem_alloc(sizeof(*pool), KM_PUSHPAGE);
	*pool_r = pool;

	/* element initialize */
	pool->pool_name = strdup(name);
	pool->curr_threadcnt = threadcnt;
	pool->idle_threadcnt = 0;
	pool->max_threadcnt = max_threadcnt;

	/* pool facilities initialize */
	init_waitqueue_head(&pool->pool_waitq);
	spin_lock_init(&pool->pool_lock);

	INIT_LIST_HEAD(&pool->plthread_idle_list);
	INIT_LIST_HEAD(&pool->plthread_busy_list);

	/* threads create and run */
	for (i = 0; i < pool->curr_threadcnt; i++) {
		if (mbtp_thread_create(&tpt, pool))
			continue;	/* if failed, continue to generate next one */

		/* wait for thread's ready */
		spin_lock_irqsave(&tpt->thd_lock, flags);
		if (tpt->curr_state == THREAD_SETUP) {
			add_wait_queue_exclusive(&tpt->thread_waitq, &pool_wait);
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irqrestore(&tpt->thd_lock, flags);

			schedule();

			spin_lock_irqsave(&tpt->thd_lock, flags);
			__set_current_state(TASK_RUNNING);
			remove_wait_queue(&tpt->thread_waitq, &pool_wait);
		}
		spin_unlock_irqrestore(&tpt->thd_lock, flags);

		/* new thread is attached to idle list */
		list_add_tail(&tpt->pool_entry, &pool->plthread_idle_list);
		pool->idle_threadcnt++;
	}

	/* adjust based on thread creation */
	pool->curr_threadcnt = pool->idle_threadcnt;
	if (pool->curr_threadcnt == 0) {
		kmem_free(pool, sizeof(*pool));
		*pool_r = NULL;
		return 1;

	}
	return 0;
}

void mulbuf_thdpool_destroy(mulbuf_thdpool_t * pool)
{
	mbtp_thread_t *tpt;
	unsigned long flags;

	/* if not empty, there is error */
	ASSERT(list_empty(&pool->plthread_busy_list));

	/* notify threads to leave */
	while (!list_empty(&pool->plthread_idle_list)) {
		tpt = list_entry(pool->plthread_idle_list.next, mbtp_thread_t, pool_entry);
		list_del(&tpt->pool_entry);
		/* check and wait whether this tpt is left */
		spin_lock_irqsave(&tpt->thd_lock, flags);
		if (tpt->next_state != THREAD_EXIT) {
			tpt->next_state = THREAD_EXIT;
			wake_up(&tpt->thread_waitq);
		}
		spin_unlock_irqrestore(&tpt->thd_lock, flags);

		mbtp_thread_destroy(tpt);
		pool->curr_threadcnt--;
		pool->idle_threadcnt--;
	}

	/* free pool */
	strfree(pool->pool_name);
	kmem_free(pool, sizeof(pool));

	return;
}

/*
 * Get a valid thread from pool; return 0 if success
 */
int mulbuf_thdpool_get_thread(mulbuf_thdpool_t * pool, mbtp_thread_t ** tpt_r)
{
	DECLARE_WAITQUEUE(pool_wait, current);
	mbtp_thread_t *tpt;
	unsigned long flags;

	spin_lock_irqsave(&pool->pool_lock, flags);
	/* check whether need to add a thread */
	if (pool->idle_threadcnt == 0) {

		/* there is no idle thread */
		if (pool->curr_threadcnt < pool->max_threadcnt) {
			spin_unlock_irqrestore(&pool->pool_lock, flags);

			/* create one new thread */
			if (!mbtp_thread_create(&tpt, pool)) {
				/* wait for thread's ready */
				spin_lock_irqsave(&tpt->thd_lock, flags);
				if (tpt->curr_state == THREAD_SETUP) {
					add_wait_queue_exclusive(&tpt->thread_waitq,
								 &pool_wait);
					set_current_state(TASK_INTERRUPTIBLE);
					spin_unlock_irqrestore(&tpt->thd_lock, flags);

					schedule();

					spin_lock_irqsave(&tpt->thd_lock, flags);
					__set_current_state(TASK_RUNNING);
					remove_wait_queue(&tpt->thread_waitq, &pool_wait);
				}
				spin_unlock_irqrestore(&tpt->thd_lock, flags);

				spin_lock_irqsave(&pool->pool_lock, flags);

				list_add_tail(&tpt->pool_entry, &pool->plthread_idle_list);
				pool->idle_threadcnt++;
				pool->curr_threadcnt++;
				spin_unlock_irqrestore(&pool->pool_lock, flags);
			}

			spin_lock_irqsave(&pool->pool_lock, flags);
		}
	}

	/* there is at least one idle thread */
	if (pool->idle_threadcnt > 0) {
		pool->idle_threadcnt--;

		tpt = list_entry(pool->plthread_idle_list.next, mbtp_thread_t, pool_entry);
		list_del(&tpt->pool_entry);
		list_add_tail(&tpt->pool_entry, &pool->plthread_busy_list);
		spin_unlock_irqrestore(&pool->pool_lock, flags);

		*tpt_r = tpt;
		return 0;
	} else {
		/* there no idle thread */
		spin_unlock_irqrestore(&pool->pool_lock, flags);

		*tpt_r = NULL;
		return 1;
	}

}

/*
 * Return a valid thread to its pool
 */
void mulbuf_thdpool_put_thread(mbtp_thread_t * tpt)
{
	mulbuf_thdpool_t *pool = tpt->pool;
	unsigned long flags;

	spin_lock_irqsave(&pool->pool_lock, flags);
	pool->idle_threadcnt++;
	list_del(&tpt->pool_entry);
	list_add_tail(&tpt->pool_entry, &pool->plthread_idle_list);
	spin_unlock_irqrestore(&pool->pool_lock, flags);

	return;
}

/*
 * Get a valid thread from pool
 */
void mbtp_thread_run_fn(mbtp_thread_t * tpt, threadp_func_t fn, void *arg)
{
	unsigned long flags;

	spin_lock_irqsave(&tpt->thd_lock, flags);

	tpt->fn = fn;
	tpt->arg = arg;
	tpt->next_state = THREAD_RUNNING;
	/* trigger tpt to run fn */
	wake_up(&tpt->thread_waitq);

	spin_unlock_irqrestore(&tpt->thd_lock, flags);

	return;
}

#endif /* __KERNEL__ && __x86_64 && HAVE_HASH_MB */
