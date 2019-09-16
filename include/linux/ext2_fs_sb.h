/*
 *  linux/include/linux/ext2_fs_sb.h
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/include/linux/minix_fs_sb.h
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#ifndef _LINUX_EXT2_FS_SB
#define _LINUX_EXT2_FS_SB

#include <linux/blockgroup_lock.h>
#include <linux/percpu_counter.h>

/*
 * second extended-fs super-block data in memory
 */
struct ext2_sb_info {
	unsigned long s_frag_size;	/* フラグメントのサイズ(単位: バイト) */
	unsigned long s_frags_per_block;/* ブロック内にあるフラグメント数 */
	unsigned long s_inodes_per_block;/* ブロック内のiノード数 */
	unsigned long s_frags_per_group;/* グループ内のフラグメント数 */
	unsigned long s_blocks_per_group;/* グループ内のブロック数 */
	unsigned long s_inodes_per_group;/* グループ内のiノード数 */
	unsigned long s_itb_per_group;	/* グループ内のiノードテーブルに使用されるブロック数 */
	unsigned long s_gdb_count;	/* グループディスクリプタ用のブロック数 */
	unsigned long s_desc_per_block;	/* ブロック内のグループディスクリプタ数 */
	unsigned long s_groups_count;	/* ブロックグループ数 */
	struct buffer_head * s_sbh;	/* バッファが保持しているスーパーブロック */
	struct ext2_super_block * s_es;	/* バッファ内のスーパーブロックを参照するポインタ */
	struct buffer_head ** s_group_desc; /* グループディスクリプタ */
	unsigned long  s_mount_opt; /* マウントオプション */
	uid_t s_resuid; /* UID */
	gid_t s_resgid; /* GID */
	unsigned short s_mount_state; /* マウントの状態 */
	unsigned short s_pad; /* パディング */
	int s_addr_per_block_bits; /* ブロックアドレスのアラインメント */
	int s_desc_per_block_bits; /* ブロック内のディスクリプタ数 */
	int s_inode_size; /* iノードサイズ */
	int s_first_ino; /* 予約されていない最初のiノード番号 */
	spinlock_t s_next_gen_lock; /* ロック用メンバ */
	u32 s_next_generation; /* 次のiノードバージョン番号 */
	unsigned long s_dir_count; /* ディレクトリ数 */
	u8 *s_debts; 
	struct percpu_counter s_freeblocks_counter; /* 空きブロック数 */
	struct percpu_counter s_freeinodes_counter; /* 空きiノード数 */
	struct percpu_counter s_dirs_counter; /* ディレクトリ数 */
	struct blockgroup_lock s_blockgroup_lock; /* ブロックグループロック */
};

#endif	/* _LINUX_EXT2_FS_SB */
