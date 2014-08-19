/*
 * Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 * Copyright (C) 2007 The Regents of the University of California.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 * UCRL-CODE-235197
 *
 * This file is part of the SPL, Solaris Porting Layer.
 * For details, see <http://zfsonlinux.org/>.
 *
 * The SPL is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * The SPL is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef	_SPL_RWLOCK_H
#define	_SPL_RWLOCK_H

#include <sys/types.h>
#include <linux/rwsem.h>

typedef enum {
	RW_DRIVER  = 2,
	RW_DEFAULT = 4
} krw_type_t;

typedef enum {
	RW_NONE   = 0,
	RW_WRITER = 1,
	RW_READER = 2
} krw_t;

typedef struct {
	struct rw_semaphore rw_rwlock;
	kthread_t *rw_owner;
} krwlock_t;

#define	SEM(rwp)	((struct rw_semaphore *)(rwp))

static inline kthread_t *
rw_owner(krwlock_t *rwp)
{
	return (rwp->rw_owner);
}

static inline int
RW_READ_HELD(krwlock_t *rwp)
{
	return (rwsem_is_locked(SEM(rwp)) && rw_owner(rwp) == NULL);
}

static inline int
RW_WRITE_HELD(krwlock_t *rwp)
{
	return (rwsem_is_locked(SEM(rwp)) && rw_owner(rwp) == current);
}

static inline int
RW_LOCK_HELD(krwlock_t *rwp)
{
	return (rwsem_is_locked(SEM(rwp)));
}

/*
 * The following functions must be a #define and not static inline.
 * This ensures that the native linux semaphore functions (down/up)
 * will be correctly located in the users code which is important
 * for the built in kernel lock analysis tools
 */
#define	rw_init(rwp, name, type, arg)				\
{								\
	static struct lock_class_key __key;			\
								\
	__init_rwsem(SEM(rwp), #rwp, &__key);			\
	((krwlock_t *)rwp)->rw_owner = NULL;			\
}

#define	rw_destroy(rwp)						\
{								\
	VERIFY(!RW_LOCK_HELD(rwp));				\
}

#define	rw_tryenter(rwp, rw)					\
({								\
	int _rc_ = 0;						\
								\
	switch (rw) {						\
	case RW_READER:						\
		_rc_ = down_read_trylock(SEM(rwp));		\
		break;						\
	case RW_WRITER:						\
		if ((_rc_ = down_write_trylock(SEM(rwp))))	\
			((krwlock_t *)rwp)->rw_owner = current;	\
		break;						\
	default:						\
		VERIFY(0);					\
	}							\
	_rc_;							\
})

#define	rw_enter(rwp, rw)					\
{								\
	switch (rw) {						\
	case RW_READER:						\
		down_read(SEM(rwp));				\
		break;						\
	case RW_WRITER:						\
		down_write(SEM(rwp));				\
		((krwlock_t *)rwp)->rw_owner = current;		\
		break;						\
	default:						\
		VERIFY(0);					\
	}							\
}

#define	rw_exit(rwp)						\
{								\
	if (RW_WRITE_HELD(rwp)) {				\
		((krwlock_t *)rwp)->rw_owner = NULL;		\
		up_write(SEM(rwp));				\
	} else {						\
		ASSERT(RW_READ_HELD(rwp));			\
		up_read(SEM(rwp));				\
	}							\
}

#define	rw_downgrade(rwp)					\
{								\
	((krwlock_t *)rwp)->rw_owner = NULL;			\
	downgrade_write(SEM(rwp));				\
}

#define	rw_tryupgrade(rwp)	(0)

int spl_rw_init(void);
void spl_rw_fini(void);

#endif /* _SPL_RWLOCK_H */
