/*
  File: linux/ext2_xattr.h

  On-disk format of extended attributes for the ext2 filesystem.

  (C) 2001 Andreas Gruenbacher, <a.gruenbacher@computer.org>
*/

#include <linux/config.h>
#include <linux/init.h>
#include <linux/xattr.h>

/* Magic value in attribute blocks */
#define EXT2_XATTR_MAGIC		0xEA020000

/* Maximum number of references to one attribute block */
#define EXT2_XATTR_REFCOUNT_MAX		1024

/* Name indexes */
#define EXT2_XATTR_INDEX_USER			1
#define EXT2_XATTR_INDEX_POSIX_ACL_ACCESS	2
#define EXT2_XATTR_INDEX_POSIX_ACL_DEFAULT	3
#define EXT2_XATTR_INDEX_TRUSTED		4
#define	EXT2_XATTR_INDEX_LUSTRE			5
#define EXT2_XATTR_INDEX_SECURITY	        6

struct ext2_xattr_header {
	__le32	h_magic;	/* magic number for identification */
	__le32	h_refcount;	/* reference count */
	__le32	h_blocks;	/* number of disk blocks used */
	__le32	h_hash;		/* hash value of all attributes */
	__u32	h_reserved[4];	/* zero right now */
};

struct ext2_xattr_entry {
	__u8	e_name_len;	/* 名前の長さ */
	__u8	e_name_index;	/* 拡張属性名のインデックス */
	__le16	e_value_offs;	/* ディスクブロック内にある値の位置を示すオフセット */
	__le32	e_value_block;	/* ディスクブロック属性が保存されているブロック */
	__le32	e_value_size;	/* 値のデータサイズ */
	__le32	e_hash;		/* 要素名と値のハッシュ */
	char	e_name[0];	/* 可変長の要素名 */
};

#define EXT2_XATTR_PAD_BITS		2
#define EXT2_XATTR_PAD		(1<<EXT2_XATTR_PAD_BITS)
#define EXT2_XATTR_ROUND		(EXT2_XATTR_PAD-1)
#define EXT2_XATTR_LEN(name_len) \
	(((name_len) + EXT2_XATTR_ROUND + \
	sizeof(struct ext2_xattr_entry)) & ~EXT2_XATTR_ROUND)
#define EXT2_XATTR_NEXT(entry) \
	( (struct ext2_xattr_entry *)( \
	  (char *)(entry) + EXT2_XATTR_LEN((entry)->e_name_len)) )
#define EXT2_XATTR_SIZE(size) \
	(((size) + EXT2_XATTR_ROUND) & ~EXT2_XATTR_ROUND)

# ifdef CONFIG_EXT2_FS_XATTR

extern struct xattr_handler ext2_xattr_user_handler;
extern struct xattr_handler ext2_xattr_trusted_handler;
extern struct xattr_handler ext2_xattr_acl_access_handler;
extern struct xattr_handler ext2_xattr_acl_default_handler;
extern struct xattr_handler ext2_xattr_security_handler;

extern ssize_t ext2_listxattr(struct dentry *, char *, size_t);

extern int ext2_xattr_get(struct inode *, int, const char *, void *, size_t);
extern int ext2_xattr_set(struct inode *, int, const char *, const void *, size_t, int);

extern void ext2_xattr_delete_inode(struct inode *);
extern void ext2_xattr_put_super(struct super_block *);

extern int init_ext2_xattr(void);
extern void exit_ext2_xattr(void);

extern struct xattr_handler *ext2_xattr_handlers[];

# else  /* CONFIG_EXT2_FS_XATTR */

static inline int
ext2_xattr_get(struct inode *inode, int name_index,
	       const char *name, void *buffer, size_t size)
{
	return -EOPNOTSUPP;
}

static inline int
ext2_xattr_set(struct inode *inode, int name_index, const char *name,
	       const void *value, size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static inline void
ext2_xattr_delete_inode(struct inode *inode)
{
}

static inline void
ext2_xattr_put_super(struct super_block *sb)
{
}

static inline int
init_ext2_xattr(void)
{
	return 0;
}

static inline void
exit_ext2_xattr(void)
{
}

#define ext2_xattr_handlers NULL

# endif  /* CONFIG_EXT2_FS_XATTR */

