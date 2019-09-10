#ifndef _LINUX_NAMEI_H
#define _LINUX_NAMEI_H

#include <linux/linkage.h>

struct vfsmount;

struct open_intent {
	int	flags;
	int	create_mode;
};

enum { MAX_NESTED_LINKS = 5 };

/**
 * パス名検索の結果
 */
struct nameidata {
	struct dentry	*dentry; // dエントリ
	struct vfsmount *mnt; // ファイルシステムオブジェクトのアドレス
	struct qstr	last; // パス名の最後の要素
	unsigned int	flags; // 検索のためのフラグ
	int		last_type; // パス名の最後の要素の種類
	unsigned	depth; // シンボリックリンクのネスト数(<=5)
	char *saved_names[MAX_NESTED_LINKS + 1]; // シンボリックリンクのネストしたパス名の配列

	/* Intent data */
	union {
		struct open_intent open; // ファイルがアクセスされる方法を指定
	} intent;
};

/*
 * Type of the last component on LOOKUP_PARENT
 */
enum {LAST_NORM, LAST_ROOT, LAST_DOT, LAST_DOTDOT, LAST_BIND};

/*
 * The bitmask for a lookup event:
 *  - follow links at the end
 *  - require a directory
 *  - ending slashes ok even for nonexistent files
 *  - internal "there are more path compnents" flag
 *  - locked when lookup done with dcache_lock held
 */
#define LOOKUP_FOLLOW		 1 // 検索結果がシンボリックリンクである場合に検索を続行
#define LOOKUP_DIRECTORY	 2 // 最後の要素はディレクトリでなければならない
#define LOOKUP_CONTINUE		 4 // パス名に調べるべきファイル名が残っている
#define LOOKUP_PARENT		16 // パス名の最後の要素を含むディレクトリを検索
#define LOOKUP_NOALT		32 // エミュレートされたルートディレクトリを考慮しない
/*
 * Intent data
 */
#define LOOKUP_OPEN		(0x0100) // ファイルをオープンする目的での検索
#define LOOKUP_CREATE		(0x0200) // ファイルを生成する目的での検索
#define LOOKUP_ACCESS		(0x0400) // ファイルに対するユーザ権限を調べる目的での検索

extern int FASTCALL(__user_walk(const char __user *, unsigned, struct nameidata *));
#define user_path_walk(name,nd) \
	__user_walk(name, LOOKUP_FOLLOW, nd)
#define user_path_walk_link(name,nd) \
	__user_walk(name, 0, nd)
extern int FASTCALL(path_lookup(const char *, unsigned, struct nameidata *));
extern int FASTCALL(path_walk(const char *, struct nameidata *));
extern int FASTCALL(link_path_walk(const char *, struct nameidata *));
extern void path_release(struct nameidata *);
extern void path_release_on_umount(struct nameidata *);

extern struct dentry * lookup_one_len(const char *, struct dentry *, int);
extern struct dentry * lookup_hash(struct qstr *, struct dentry *);

extern int follow_down(struct vfsmount **, struct dentry **);
extern int follow_up(struct vfsmount **, struct dentry **);

extern struct dentry *lock_rename(struct dentry *, struct dentry *);
extern void unlock_rename(struct dentry *, struct dentry *);

static inline void nd_set_link(struct nameidata *nd, char *path)
{
	nd->saved_names[nd->depth] = path;
}

static inline char *nd_get_link(struct nameidata *nd)
{
	return nd->saved_names[nd->depth];
}

#endif /* _LINUX_NAMEI_H */
