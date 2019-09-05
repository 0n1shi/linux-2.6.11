#ifndef _NAMESPACE_H_
#define _NAMESPACE_H_
#ifdef __KERNEL__

#include <linux/mount.h>
#include <linux/sched.h>

struct namespace {
	atomic_t		count; /* 参照カウンタ */
	struct vfsmount *	root; /* この名前空間のルートファイルシステムディスクリプタ */
	struct list_head	list; /* マウント済みの全てのファイルシステムディスクリプタリストの先頭 */
	struct rw_semaphore	sem; /* この構造体を保護するための読み書きセマフォ */
};

extern void umount_tree(struct vfsmount *);
extern int copy_namespace(int, struct task_struct *);
extern void __put_namespace(struct namespace *namespace);

static inline void put_namespace(struct namespace *namespace)
{
	if (atomic_dec_and_test(&namespace->count))
		__put_namespace(namespace);
}

static inline void exit_namespace(struct task_struct *p)
{
	struct namespace *namespace = p->namespace;
	if (namespace) {
		task_lock(p);
		p->namespace = NULL;
		task_unlock(p);
		put_namespace(namespace);
	}
}

static inline void get_namespace(struct namespace *namespace)
{
	atomic_inc(&namespace->count);
}

#endif
#endif
