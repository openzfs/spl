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

#ifndef _SPL_MUTEX_H
#define	_SPL_MUTEX_H

#include <sys/types.h>
#include <linux/mutex.h>
#include <linux/compiler_compat.h>
#include <linux/lockdep.h>
#include <spl-lockdbg.h>

typedef enum {
	MUTEX_DEFAULT    = 0,      /* Normal */
	MUTEX_NOLOCKDEP  = 1 << 0, /* No LockDep */
	MUTEX_NOTRACKING = 1 << 1, /* No Tracking */
} kmutex_type_t;

typedef struct {
	atomic_t		m_flags; /* use lower bits for sync counter */
	struct mutex		m_mutex;
	kthread_t		*m_owner;
} kmutex_t;

/* Use upper portion of m_flags to store flags */
#define	MUTEX_FLAG_NOLOCKDEP	((u32)1 << 31)
#define	MUTEX_FLAG_NOTRACKING	((u32)1 << 30)
#define	MUTEX_FLAG_MASK		(~(((u32)1 << 30) - 1))

#define	MUTEX(mp)		(&((mp)->m_mutex))
#define	MUTEX_FLAGS(mp)		(atomic_read(&(mp)->m_flags) & MUTEX_FLAG_MASK)
#define	MUTEX_COUNTER(mp)	(&((mp)->m_flags))
#define	MUTEX_COUNTER_VAL(mp)	(atomic_read(&(mp)->m_flags) & ~MUTEX_FLAG_MASK)

static inline void
spl_mutex_set_owner(kmutex_t *mp)
{
	mp->m_owner = current;
}

static inline void
spl_mutex_clear_owner(kmutex_t *mp)
{
	mp->m_owner = NULL;
}

static inline void
spl_mutex_set_type(kmutex_t *mp, kmutex_type_t type)
{
	u32 t = 0;

	ASSERT((type == MUTEX_DEFAULT) ||
		((type & (MUTEX_NOLOCKDEP | MUTEX_NOTRACKING)) != 0));
	/*
	 * Mutex m_flags flag/counter bits:
	 * bit 31 : NOLOCKDEP
	 * bit 30 : NODEBUG
	 * bits [29 - 0]: mutex_exit counter (m_flags & ~MUTEX_FLAG_MASK)
	 */
	if (type & MUTEX_NOLOCKDEP)
		t |= MUTEX_FLAG_NOLOCKDEP;

	if (type & MUTEX_NOTRACKING)
		t |= MUTEX_FLAG_NOTRACKING;

	atomic_set(&mp->m_flags, t & MUTEX_FLAG_MASK);
}

static inline kmutex_type_t
spl_mutex_get_type(kmutex_t *mp)
{
	kmutex_type_t type = MUTEX_DEFAULT;
	u32 flags = 0;

	ASSERT3P(mp, !=, NULL);

	flags = MUTEX_FLAGS(mp);

	if (flags & MUTEX_FLAG_NOLOCKDEP)
		type |= MUTEX_NOLOCKDEP;

	if (flags & MUTEX_FLAG_NOTRACKING)
		type |= MUTEX_NOTRACKING;

	return type;
}

#define	mutex_owner(mp)		(ACCESS_ONCE((mp)->m_owner))
#define	mutex_owned(mp)		(mutex_owner(mp) == current)
#define	MUTEX_HELD(mp)		(mutex_owned(mp))
#define	MUTEX_NOT_HELD(mp)	(!MUTEX_HELD(mp))

#ifdef CONFIG_LOCKDEP
static inline void
spl_mutex_lockdep_off_maybe(kmutex_t *mp)
{
	if (mp && (spl_mutex_get_type(mp) & MUTEX_NOLOCKDEP))
		lockdep_off();
}

static inline void
spl_mutex_lockdep_on_maybe(kmutex_t *mp)
{
	if (mp && (spl_mutex_get_type(mp) & MUTEX_NOLOCKDEP))
		lockdep_on();
}
#else  /* CONFIG_LOCKDEP */
#define spl_mutex_lockdep_off_maybe(mp)
#define spl_mutex_lockdep_on_maybe(mp)
#endif /* CONFIG_LOCKDEP */

/*
 * The following functions must be a #define and not static inline.
 * This ensures that the native linux mutex functions (lock/unlock)
 * will be correctly located in the users code which is important
 * for the built in kernel lock analysis tools
 */

#undef mutex_init
#define	mutex_init(mp, name, type, ibc)					\
do {									\
	static struct lock_class_key __key;				\
									\
	VERIFY3P((mp), !=, NULL);					\
	__mutex_init(MUTEX(mp), name, &__key);				\
	spl_mutex_clear_owner(mp);					\
	spl_mutex_set_type(mp, type);					\
									\
	spl_lock_tracking_record_init(SLT_MUTEX, (ulong_t)mp,		\
	    OBJ_INIT_LOC);						\
} while(0)

#define	mutex_tryenter(mp)						\
({									\
	int _rc_;							\
									\
	spl_mutex_lockdep_off_maybe(mp);				\
	if ((_rc_ = mutex_trylock(MUTEX(mp))) == 1)			\
		spl_mutex_set_owner(mp);				\
	spl_mutex_lockdep_on_maybe(mp);					\
									\
	_rc_;								\
})

#ifdef CONFIG_DEBUG_LOCK_ALLOC
#define	mutex_enter_nested(mp, subclass)				\
do {									\
	ASSERT3P(mutex_owner(mp), !=, current);				\
	spl_mutex_lockdep_off_maybe(mp);				\
	mutex_lock_nested(MUTEX(mp), (subclass));			\
	spl_mutex_lockdep_on_maybe(mp);					\
	spl_mutex_set_owner(mp);					\
} while(0)
#else /* CONFIG_DEBUG_LOCK_ALLOC */
#define	mutex_enter_nested(mp, subclass)				\
do {									\
	ASSERT3P(mutex_owner(mp), !=, current);				\
	spl_mutex_lockdep_off_maybe(mp);				\
	mutex_lock(MUTEX(mp));						\
	spl_mutex_lockdep_on_maybe(mp);					\
	spl_mutex_set_owner(mp);					\
} while(0)
#endif /*  CONFIG_DEBUG_LOCK_ALLOC */

#define	mutex_enter(mp) mutex_enter_nested((mp), 0)

#define	mutex_exit(mp)							\
do {									\
	spl_mutex_clear_owner(mp);					\
	atomic_inc(MUTEX_COUNTER(mp));					\
	spl_mutex_lockdep_off_maybe(mp);				\
	mutex_unlock(MUTEX(mp));					\
	spl_mutex_lockdep_on_maybe(mp);					\
	atomic_dec(MUTEX_COUNTER(mp));					\
} while(0)

/*
 * The reason for the spinning in mutex_destroy():
 *
 * The Linux mutex is designed with a fast-path/slow-path design such that it
 * does not guarantee serialization upon itself, allowing a race where latter
 * acquirers finish mutex_unlock before former ones.
 *
 * The race renders it unsafe to be used for serializing the freeing of an
 * object in which the mutex is embedded, where the latter acquirer could go
 * on to free the object while the former one is still doing mutex_unlock and
 * causing memory corruption.
 *
 * However, there are many places in ZFS where the mutex is used for
 * serializing object freeing, and the code is shared among other OSes without
 * this issue. Thus, we need to spin in mutex_destroy() until we're sure that
 * all threads have finished slow-path in mutex_exit(), avoiding use-after-free.
 *
 * See http://lwn.net/Articles/575477/ for the information about the race.
 */

#undef mutex_destroy
#define mutex_destroy(mp)						\
{									\
	VERIFY3P(mutex_owner(mp), ==, NULL);				\
									\
	while (MUTEX_COUNTER_VAL(mp) != 0)				\
		cpu_relax();						\
									\
	spl_lock_tracking_record_destroy(SLT_MUTEX, (ulong_t)mp,	\
	    OBJ_INIT_LOC);						\
} while(0)

int spl_mutex_init(void);
void spl_mutex_fini(void);

#endif /* _SPL_MUTEX_H */
