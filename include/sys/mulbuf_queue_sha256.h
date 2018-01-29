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

#ifndef MULBUF_QUEUE_SHA256_H_
#define MULBUF_QUEUE_SHA256_H_

#if defined(__x86_64) && defined(__KERNEL__) && defined(HAVE_HASH_MB)

#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <sys/kmem.h>
#include <sys/mulbuf_queue.h>

void mulbuf_sha256_fn(void *args);

int mulbuf_suite_sha256_init(void);
void mulbuf_suite_sha256_fini(void);

int mulbuf_sha256_queue_choose(void *buffer, size_t size,
		unsigned char *digest, mbtp_queue_t *queue);
int mulbuf_sha256(void *buffer, size_t size, unsigned char *digest);

#else

static inline int mulbuf_suite_sha256_init(void)
{
	return 0;
}
static inline void mulbuf_suite_sha256_fini(void)
{
	return;
}

#endif /* __KERNEL__ && __x86_64 && HAVE_HASH_MB */

#endif /* MULBUF_QUEUE_SHA256_H_ */
