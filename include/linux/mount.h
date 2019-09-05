/*
 *
 * Definitions for mount interface. This describes the in the kernel build 
 * linkedlist with mounted filesystems.
 *
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 *
 * Version: $Id: mount.h,v 2.0 1996/11/17 16:48:14 mvw Exp mvw $
 *
 */
#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H
#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

#define MNT_NOSUID	1 /* setuid及びsetgidを禁止 */
#define MNT_NODEV	2 /* デバイスファイルへのアクセスを禁止 */
#define MNT_NOEXEC	4 /* プログラム */

struct vfsmount
{
	struct list_head mnt_hash; /* ハッシュテーブルリスト用のポインタ */
	struct vfsmount *mnt_parent;	/* マウントされる親ファイルシステムへのポインタ */
	struct dentry *mnt_mountpoint;	/* マウントポイントのdエントリ */
	struct dentry *mnt_root;	/* マウントするファイルシステムのルートディレクトリに対応するdエントリ */
	struct super_block *mnt_sb;	/* マウントするファイルシステムのスーパーブロックオブジェクト */
	struct list_head mnt_mounts;	/* マウントするファイルシステムの全てのファイルシステムディスクリプタリストの先頭 */
	struct list_head mnt_child;	/* マウント済みファイルシステムディスクリプタのmnt_mountsリストへのポインタ */
	atomic_t mnt_count; /* 利用カウンタ(ファイルシステムのアンマウントをきんしするために増加させる) */
	int mnt_flags; /* フラグ */
	int mnt_expiry_mark; /* ファイルシステムが有効期限切れかどうか */
	char *mnt_devname;		/* デバイスファイル名 */
	struct list_head mnt_list; /* マウント済みのファイルシステムディスクリプタの名前空間リストへのポインタ */
	struct list_head mnt_fslink;	/* ファイルシステム固有の期限管理リストのポインタ */
	struct namespace *mnt_namespace; /* ファイルシステムをマウントしたプロセスの名前空間へのポインタ */
};

static inline struct vfsmount *mntget(struct vfsmount *mnt)
{
	if (mnt)
		atomic_inc(&mnt->mnt_count);
	return mnt;
}

extern void __mntput(struct vfsmount *mnt);

static inline void _mntput(struct vfsmount *mnt)
{
	if (mnt) {
		if (atomic_dec_and_test(&mnt->mnt_count))
			__mntput(mnt);
	}
}

static inline void mntput(struct vfsmount *mnt)
{
	if (mnt) {
		mnt->mnt_expiry_mark = 0;
		_mntput(mnt);
	}
}

extern void free_vfsmnt(struct vfsmount *mnt);
extern struct vfsmount *alloc_vfsmnt(const char *name);
extern struct vfsmount *do_kern_mount(const char *fstype, int flags,
				      const char *name, void *data);

struct nameidata;

extern int do_add_mount(struct vfsmount *newmnt, struct nameidata *nd,
			int mnt_flags, struct list_head *fslist);

extern void mark_mounts_for_expiry(struct list_head *mounts);

extern spinlock_t vfsmount_lock;

#endif
#endif /* _LINUX_MOUNT_H */
