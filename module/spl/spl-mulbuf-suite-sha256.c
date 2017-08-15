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
 *  Solaris Porting Layer (SPL) Multi-Buffer Hash SHA256 Suite Implementation.
\*****************************************************************************/

#if defined(__x86_64) && defined(__KERNEL__) && defined(HAVE_HASH_MB)

#include <sys/mulbuf_queue_sha256.h>

/*
 * Different queues for different hash length
 * For ZFS sha256, 7 is a proper queue number for length: 16KB and below, 32KB
 * 64KB, 128KB, 256KB, 512KB, 1MB and above.
 */
#define MULBUF_NQUEUE	7

int spl_mulbuf_queues_max_threadcnt = -50;
module_param(spl_mulbuf_queues_max_threadcnt, int, 0644);
MODULE_PARM_DESC(spl_mulbuf_queues_max_threadcnt,
		 "Maximum number of threads in each queue. (minus means percentage)");

int spl_mulbuf_queues_min_threadcnt = -25;
module_param(spl_mulbuf_queues_min_threadcnt, int, 0644);
MODULE_PARM_DESC(spl_mulbuf_queues_min_threadcnt,
		 "Maximum number of threads in each queue. (minus means percentage)");

typedef struct mulbuf_sha256_suite {
	mulbuf_thdpool_t *pool;
	mbtp_queue_t **queues_array;
	int queue_count;
} mulbuf_sha256_suite_t;

mulbuf_sha256_suite_t suite;

static int threadcnt_analyze(int ncpu, int threadcnt_param)
{
	int threadcnt;

	if (threadcnt_param < 0)
		threadcnt = ncpu * (0 - threadcnt_param) / 100;
	else
		threadcnt = threadcnt_param;

	if (threadcnt < 1)
		threadcnt = 1;

	if (threadcnt > ncpu)
		threadcnt = ncpu;

	return threadcnt;
}

int mulbuf_suite_sha256_init(void)
{
	int min_poolthreadcnt;
	int max_poolthreadcnt;
	int max_threadcnt;
	int min_threadcnt;
	int ncpu;
	char namep[] = "spl-sha256mb-pool";
	char nameq[] = "spl-sha256mb-queue";
	int rc, i;

	mulbuf_thdpool_t *pool;
	int nqueue = MULBUF_NQUEUE;
	mbtp_queue_t **queue_array;

	ncpu = num_online_cpus();

	min_threadcnt = threadcnt_analyze(ncpu, spl_mulbuf_queues_min_threadcnt);
	max_threadcnt = threadcnt_analyze(ncpu, spl_mulbuf_queues_max_threadcnt);
	min_poolthreadcnt = min_threadcnt * nqueue;
	max_poolthreadcnt = min_poolthreadcnt + max_threadcnt - min_threadcnt;

	queue_array = kmem_alloc(sizeof(mbtp_queue_t *) * nqueue, KM_PUSHPAGE);

	rc = mulbuf_thdpool_create(&pool, namep, min_poolthreadcnt, max_poolthreadcnt);

	for (i = 0; i < nqueue; i++) {
		rc += mbtp_queue_create(&queue_array[i], nameq, pool,
					min_threadcnt, max_threadcnt, mulbuf_sha256_fn);
	}

	suite.pool = pool;
	suite.queue_count = nqueue;
	suite.queues_array = queue_array;

	if (rc > 0)
		rc = 1;

	return rc;
}

void mulbuf_suite_sha256_fini(void)
{
	int i;

	mulbuf_thdpool_t *pool;
	int nqueue;
	mbtp_queue_t **queue_array;

	pool = suite.pool;
	nqueue = suite.queue_count;
	queue_array = suite.queues_array;

	for (i = 0; i < nqueue; i++) {
		if (queue_array[i])
			mbtp_queue_destroy(queue_array[i]);
	}

	kmem_free(queue_array, sizeof(mbtp_queue_t *) * nqueue);

	if (pool)
		mulbuf_thdpool_destroy(pool);

	return;
}

static void mulbuf_sha256_cb(mbtp_task_t * mb_task, void *arg)
{
	struct completion *cmp = (struct completion *)arg;
	complete(cmp);
}

int mulbuf_sha256_queue_choose(void *buffer, size_t size, unsigned char *digest,
			       mbtp_queue_t * queue)
{
	mbtp_task_t task;

	struct completion cmpt;
	init_completion(&cmpt);

	task.buffer = buffer;
	task.size = size;
	task.digest = digest;

	task.cb_fn = mulbuf_sha256_cb;
	task.cb_arg = &cmpt;
	task.processsed = 0;

	mbtp_queue_submit_job(&task, queue);

	wait_for_completion(&cmpt);

	if (task.processsed == 1) {
		return 0;
	} else {
		return 1;
	}
}

/*
 * mulbuf_sha256 is the API exported out for ZFS to compute sha256 digest
 */
int mulbuf_sha256(void *buffer, size_t size, unsigned char *digest)
{
	int rc;
	int index;
	uint64_t mask;

	/*
	 * Find queue index for specific buffer size.
	 * queues are set for lengths 16K ~ 1M. 16K is 0x1 << 14
	 */
	for (index = -1, mask = (0x1 << 14); size >= mask; mask <<= 1)
		index++;

	if (index == -1)
		index = 0;
	if (index >= MULBUF_NQUEUE)
		index = MULBUF_NQUEUE - 1;

	rc = mulbuf_sha256_queue_choose(buffer, size, digest, suite.queues_array[index]);

	return rc;
}

EXPORT_SYMBOL(mulbuf_sha256);

#endif /* __KERNEL__ && __x86_64 && HAVE_HASH_MB */
