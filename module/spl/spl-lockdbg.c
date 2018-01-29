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

#ifdef DEBUG_LOCK_TRACKING

#include <sys/types.h>
#include <spl-lockdbg.h>

spl_lock_tracking_t lock_tracker = {
	.slt_rwlock = __RW_LOCK_UNLOCKED(slt_rwlock),
	.slt_buckets = { [0 ... (SLT_BCKT_SIZE - 1)] = RB_ROOT },
	.slt_locks =
	    { [0 ... (SLT_BCKT_SIZE - 1)] = __SPIN_LOCK_UNLOCKED(slt_locks) },
	.slt_cnt = ATOMIC_INIT(0),
	.slt_mismatched = RB_ROOT,
	.slt_mismatched_lock = __SPIN_LOCK_UNLOCKED(slt_mismatched_lock),
	.slt_mismatched_cnt = ATOMIC_INIT(0)
};

static const char *lock_type_names[] = {
	"kmutex_t",
	"krwlock_t",
	"kcondvar_t"
};

static const char *
slt_lock_name(lock_type_t lock)
{
	return (lock_type_names[lock]);
}

/* Stack trace helpers */
static void
slt_record_stack_trace(lock_tracking_node_t *ltnp)
{
	struct stack_trace trace = { 0 };

	trace.nr_entries = 0;
	trace.max_entries = LT_STACK_TRACE_SIZE;
	trace.entries = ltnp->lt_trace;
	trace.skip = 2;

	save_stack_trace(&trace);
	ltnp->lt_trace_nr = MAX((int)trace.nr_entries - 1, 0);
}

/* Tracker */
typedef int (*slt_cmp_fn)(const void *, const void *);

static int
slt_ptr_cmp(const void *a, const void *b)
{
	uintptr_t _a = (uintptr_t)a;
	uintptr_t _b = (uintptr_t)b;

	return ((_a > _b) - (_a < _b));
}

static int
slt_str_cmp(const void *a, const void *b)
{
	return (strncmp(a, b, LT_LOC_SIZE));
}

static inline lock_tracking_node_t *
slt_lookup(struct rb_root *root, slt_cmp_fn cmp_fn, const void *val)
{
	struct rb_node *n = root->rb_node;
	lock_tracking_node_t *mtp;
	int cmp;

	while (n) {
		mtp = rb_entry(n, lock_tracking_node_t, lt_rbnode);

		if (cmp_fn == slt_ptr_cmp)
			cmp = cmp_fn(val, (void *)mtp->lt_ptr);
		else
			cmp = cmp_fn(val, (void *)mtp->lt_loc);

		if (cmp < 0)
			n = n->rb_left;
		else if (cmp > 0)
			n = n->rb_right;
		else
			return (mtp);
	}
	return (NULL);
}

static inline void
slt_insert(struct rb_root *root, slt_cmp_fn cmp_fn,
    lock_tracking_node_t *mtp)
{
	struct rb_node **p = &(root->rb_node);
	struct rb_node *parent = NULL;
	lock_tracking_node_t *mtp_tmp;
	int cmp;
	void *cmp_arg1 = (cmp_fn == slt_ptr_cmp) ? (void *)mtp->lt_ptr :
	    (void *)mtp->lt_loc;
	void *cmp_arg2 = NULL;

	while (*p) {
		mtp_tmp = rb_entry(*p, lock_tracking_node_t, lt_rbnode);
		parent = *p;

		cmp_arg2 = (cmp_fn == slt_ptr_cmp) ? (void *)mtp_tmp->lt_ptr :
		    (void *)mtp_tmp->lt_loc;
		cmp = cmp_fn(cmp_arg1, cmp_arg2);

		if (cmp < 0)
			p = &((*p)->rb_left);
		else if (cmp > 0)
			p = &((*p)->rb_right);
		else
			return;
	}

	rb_link_node(&mtp->lt_rbnode, parent, p);
	rb_insert_color(&mtp->lt_rbnode, root);
}

static inline void
slt_remove(struct rb_root *root, lock_tracking_node_t *mtp)
{
	rb_erase(&mtp->lt_rbnode, root);
}

void
spl_lockdbg_fini()
{
	spl_lock_tracking_t *sltp = &lock_tracker;
	lock_tracking_node_t *mtp;
	struct rb_node *n;
	struct rb_root *root;
	int i;

	/* Print 'header' for mismatched calls */
	if (SLT_MISMATCHED_NUM(sltp) > 0)
		printk(KERN_WARNING
		    "\n******************************************************\n"
		    "SPL Lock Tracker: missing destroy\n"
		    "******************************************************\n");

	root = SLT_MISMATCHED(sltp);
	while ((n = rb_first(root)) != NULL) {
		mtp = rb_entry(n, lock_tracking_node_t, lt_rbnode);

		slt_remove(root, mtp);

		/* Report mismatch */
		printk(KERN_WARNING "missing destroy: object (%s) [0x%p]\n"
		    "  init location = %s", slt_lock_name(mtp->lt_type),
		    (void *)mtp->lt_ptr, mtp->lt_loc);
		kfree(mtp);
	}

	/* Print 'header' if lock tracking is not empty */
	if (SLT_TRACKED_NUM(sltp) > 0)
		printk(KERN_WARNING
		    "\n******************************************************\n"
		    "SPL Lock Tracker: missing teardown\n"
		    "******************************************************\n");

	for (i = 0; i < SLT_BCKT_SIZE; i++) {
		root = SLT_BUCKET(sltp, i);

		while ((n = rb_first(root)) != NULL) {
			mtp = rb_entry(n, lock_tracking_node_t, lt_rbnode);

			slt_remove(root, mtp);

			/* Report mismatch */
			printk(KERN_WARNING "missing teardown: "
			    "object (%s) [0x%p]\n"
			    "  init location = %s",
			    slt_lock_name(mtp->lt_type),
			    (void *)mtp->lt_ptr, mtp->lt_loc);
			kfree(mtp);
		}
	}
}

void
spl_lock_tracking_record_init(lock_type_t type, ulong_t lp, char *loc)
{
	spl_lock_tracking_t *sltp = &lock_tracker;
	lock_tracking_node_t *ltp;
	ulong_t flags, rwflags, mis_flags;
	const ulong_t idx = SLT_HASH(lp);

	/* get filename if string starts with full path */
	loc = SLT_FILE_LOC(loc);

	read_lock_irqsave(SLT_RWLOCK(sltp), rwflags);
	spin_lock_irqsave(SLT_BUCKET_LOCK(sltp, idx), flags);

	/* Check if the lock already exists */
	ltp = slt_lookup(SLT_BUCKET(sltp, idx), slt_ptr_cmp, (void *)lp);
	if (ltp != NULL) {
		slt_remove(SLT_BUCKET(sltp, idx), ltp);
		SLT_TRACKED_DEC(sltp);

		spin_lock_irqsave(SLT_MISMATCHED_LOCK(sltp), mis_flags);
		if (slt_lookup(SLT_MISMATCHED(sltp), slt_str_cmp, ltp->lt_loc)
		    == NULL) {
			/* insert into the mismatched tree */
			slt_insert(SLT_MISMATCHED(sltp), slt_str_cmp, ltp);
			SLT_MISMATCHED_INC(sltp);
		} else
			kfree(ltp); /* already known */
		spin_unlock_irqrestore(SLT_MISMATCHED_LOCK(sltp), mis_flags);
	}

	ltp = kzalloc(sizeof (*ltp), GFP_ATOMIC);
	if (ltp) {
		ltp->lt_ptr = (ulong_t)lp;
		ltp->lt_type = type;
		strlcpy(ltp->lt_loc, loc, LT_LOC_SIZE);
		slt_record_stack_trace(ltp);

		slt_insert(SLT_BUCKET(sltp, idx), slt_ptr_cmp, ltp);
		SLT_TRACKED_INC(sltp);
	}

	spin_unlock_irqrestore(SLT_BUCKET_LOCK(sltp, idx), flags);
	read_unlock_irqrestore(SLT_RWLOCK(sltp), rwflags);
}

/*
 * Limit the number of stack traces dumped to not more than 1 every 60 seconds
 * to prevent denial-of-service attacks from debug code. Lock destroy mismatch
 * might be caused by low RAM condition.
 */
static DEFINE_RATELIMIT_STATE(lock_destroy_ratelimit_state, 60 * HZ, 1);

void
spl_lock_tracking_record_destroy(lock_type_t type, ulong_t lp, char *loc)
{
	spl_lock_tracking_t *sltp = &lock_tracker;
	lock_tracking_node_t *mtp;
	ulong_t flags, rwflags;
	const ulong_t idx = SLT_HASH(lp);

	read_lock_irqsave(SLT_RWLOCK(sltp), rwflags);
	spin_lock_irqsave(SLT_BUCKET_LOCK(sltp, idx), flags);

	/* Check if exists */
	mtp = slt_lookup(SLT_BUCKET(sltp, idx), slt_ptr_cmp, (void *)lp);
	if (mtp == NULL) {
		/* get filename if string starts with full path */
		loc = SLT_FILE_LOC(loc);
		if (__ratelimit(&lock_destroy_ratelimit_state)) {
			printk(KERN_WARNING "SPL Lock Tracker [%s]: possible "
			    "spurious destroy() at: %s",
			    slt_lock_name(type), loc);
			dump_stack();
		}
		goto out;
	}

	slt_remove(SLT_BUCKET(sltp, idx), mtp);
	SLT_TRACKED_DEC(sltp);
	kfree(mtp);

out:
	spin_unlock_irqrestore(SLT_BUCKET_LOCK(sltp, idx), flags);
	read_unlock_irqrestore(SLT_RWLOCK(sltp), rwflags);
}



/* Proc file */
typedef struct slt_seq_file_desc {
	spl_lock_tracking_t	*sfd_tracker;
	lock_type_t		sfd_type;
} slt_seq_file_desc_t;

static void*
slt_seq_file_start(struct seq_file *file, loff_t *pos)
{
	slt_seq_file_desc_t *desc = file->private;
	spl_lock_tracking_t *sltp = desc->sfd_tracker;
	struct rb_node *n = rb_first(SLT_MISMATCHED(sltp));
	loff_t off = *pos;

	/*
	 * NOTE: SLT_RWLOCK(sltp) is used in a non-standard way: seq-file
	 * traversal locks for WRITING and actual modification methods for
	 * READING. This is safe because modification of the tracking structure
	 * is protected by finer-grained locks.
	 */
	write_lock(SLT_RWLOCK(sltp));

	while (n != NULL && (off--) > 0)
		n = rb_next(n);

	return (n);
}

static void
slt_seq_file_stop(struct seq_file *file, void *v)
{
	slt_seq_file_desc_t *desc = file->private;
	spl_lock_tracking_t *sltp = desc->sfd_tracker;

	write_unlock(SLT_RWLOCK(sltp));
}

static void*
slt_seq_file_next(struct seq_file *file, void *p, loff_t *pos)
{
	struct rb_node *n = (struct rb_node *)p;

	(*pos)++;

	return (rb_next(n));
}

static int
slt_seq_file_show(struct seq_file *file, void *p)
{
	slt_seq_file_desc_t *desc = (slt_seq_file_desc_t *)file->private;
	struct rb_node *n = (struct rb_node *)p;
	lock_tracking_node_t *mtp;
	const char *type_name = slt_lock_name(desc->sfd_type);
	int i;

	ASSERT3P(p, !=, NULL);

	mtp = rb_entry(n, lock_tracking_node_t, lt_rbnode);
	if (mtp->lt_type != desc->sfd_type)
		return (0);

	/* Report the mismatch */
	seq_printf(file, "object (%s) [0x%p] is missing destroy\n"
	    "  init location = %s\n", type_name, (void *)mtp->lt_ptr,
	    mtp->lt_loc);
	seq_printf(file, "  backtrace:\n");
	for (i = 0; i < mtp->lt_trace_nr; i++)
		seq_printf(file, "    %pS\n", (void *)mtp->lt_trace[i]);

	return (0);
}

static struct seq_operations slt_seq_ops = {
	.show  = slt_seq_file_show,
	.start = slt_seq_file_start,
	.next  = slt_seq_file_next,
	.stop  = slt_seq_file_stop,
};

static int
proc_tracking_open(lock_type_t type, struct inode *inode, struct file *fp)
{
	slt_seq_file_desc_t *desc;
	void *p = __seq_open_private(fp, &slt_seq_ops,
	    sizeof (slt_seq_file_desc_t));
	if (p == NULL)
		return (-ENOMEM);

	desc = (slt_seq_file_desc_t *)p;
	memset(desc, 0, sizeof (slt_seq_file_desc_t));
	desc->sfd_tracker = &lock_tracker;
	desc->sfd_type = type;
	return (0);
}

static int
proc_tracking_mutex_open(struct inode *inode, struct file *fp)
{
	return (proc_tracking_open(SLT_MUTEX, inode, fp));
}

struct file_operations proc_mutex_tracking_operations = {
	.owner		= THIS_MODULE,
	.open		= proc_tracking_mutex_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

static int
proc_tracking_rwlock_open(struct inode *inode, struct file *fp)
{
	return (proc_tracking_open(SLT_RWLOCK, inode, fp));
}

struct file_operations proc_rwlock_tracking_operations = {
	.owner		= THIS_MODULE,
	.open		= proc_tracking_rwlock_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

static int
proc_tracking_condvar_open(struct inode *inode, struct file *fp)
{
	return (proc_tracking_open(SLT_CONDVAR, inode, fp));
}

struct file_operations proc_condvar_tracking_operations = {
	.owner		= THIS_MODULE,
	.open		= proc_tracking_condvar_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

EXPORT_SYMBOL(spl_lock_tracking_record_init);
EXPORT_SYMBOL(spl_lock_tracking_record_destroy);

#endif /* DEBUG_LOCK_TRACKING */
