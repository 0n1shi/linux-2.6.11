/*
 * include/linux/journal-head.h
 *
 * buffer_head fields for JBD
 *
 * 27 May 2001 Andrew Morton <akpm@digeo.com>
 *	Created - pulled out of fs.h
 */

#ifndef JOURNAL_HEAD_H_INCLUDED
#define JOURNAL_HEAD_H_INCLUDED

typedef unsigned int		tid_t;		/* Unique transaction ID */
typedef struct transaction_s	transaction_t;	/* Compound transaction type */
struct buffer_head;

struct journal_head {
	/*
	 * Points back to our buffer_head. [jbd_lock_bh_journal_head()]
	 * バッファヘッダのポインタ群
	 */
	struct buffer_head *b_bh;

	/*
	 * 参照カウンタ - see description in journal.c
	 * [jbd_lock_bh_journal_head()]
	 */
	int b_jcount;

	/*
	 * このバッファのジャーナリングリスト [jbd_lock_bh_state()]
	 */
	unsigned b_jlist;

	/*
	 * ログに書き込むためのバッファデータのコピー
	 * [jbd_lock_bh_state()]
	 */
	char *b_frozen_data;

	/*
	 * コミットされていないデアロケーション参照を保持しない保存されたバッファのコピー
	 * アロケーションはコミットされていない削除をオーパライトすることを回避できる
	 */
	char *b_committed_data;

	/*
	 * バッファのメタデータを保持するトランザクションを参照するポインタ
	 * 動作中のトランザクションもしくはコミット中のトランザクション。
	 * トランザクションのデータもしくはメタデータのジャーナリングリスト上の
	 * バッファに対して適応される。
	 * 
	 * [j_list_lock] [jbd_lock_bh_state()]
	 */
	transaction_t *b_transaction;

	/*
	 * [t_list_lock] [jbd_lock_bh_state()]
	 * 現在バッファのメタデータを変更している動作中のトランザクションを参照するポインタ
	 * もしコミット中のトランザクションが存在した場合新たなトランザクションがそれを処理する。
	 */
	transaction_t *b_next_transaction;

	/*
	 * [t_list_lock] [jbd_lock_bh_state()]
	 * トランザクションデータ上のバッファの双方向リスト。メタデータもしくは忘れられたキュー
	 */
	struct journal_head *b_tnext, *b_tprev;

	/*
	 * [j_list_lock]
	 * このバッファがチェックポイントのトランザクションを参照するポインタ
	 * ダーティのバッファのみチェックポイントになることが可能
	 */
	transaction_t *b_cp_transaction;

	/*
	 * [j_list_lock]
	 * 古いトランザクションがチェックポイントになることができる前の
	 * フラッシュ待ちのバッファの双方向リスト。
	 * 
	 */
	struct journal_head *b_cpnext, *b_cpprev;
};

#endif		/* JOURNAL_HEAD_H_INCLUDED */
