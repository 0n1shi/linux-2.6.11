#ifndef _LINUX_FS_STRUCT_H
#define _LINUX_FS_STRUCT_H

struct dentry;
struct vfsmount;

struct fs_struct {
	atomic_t count; // 共有するプロセス数
	rwlock_t lock; // 読み書きスピンロック
	int umask; // ファイル権限のためのビットマスク

	// ルートディレクトリ、ワーキングディレクトリのdエントリ
	struct dentry * root, * pwd, * altroot;
	// ルートディレクトリ、ワーキングディレクトリのファイルシステムオブジェクト
	struct vfsmount * rootmnt, * pwdmnt, * altrootmnt;
};

#define INIT_FS {				\
	.count		= ATOMIC_INIT(1),	\
	.lock		= RW_LOCK_UNLOCKED,	\
	.umask		= 0022, \
}

extern void exit_fs(struct task_struct *);
extern void set_fs_altroot(void);
extern void set_fs_root(struct fs_struct *, struct vfsmount *, struct dentry *);
extern void set_fs_pwd(struct fs_struct *, struct vfsmount *, struct dentry *);
extern struct fs_struct *copy_fs_struct(struct fs_struct *);
extern void put_fs_struct(struct fs_struct *);

#endif /* _LINUX_FS_STRUCT_H */
