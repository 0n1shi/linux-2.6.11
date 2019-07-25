#ifndef _LINUX_VMALLOC_H
#define _LINUX_VMALLOC_H

#include <linux/spinlock.h>
#include <asm/page.h>		/* pgprot_t */

/* bits in vm_struct->flags */
#define VM_IOREMAP	0x00000001	/* ioremap()または同類関数により割り当てられたハードウェア上にあるオンボードメモリをマッピングしたメモリ領域 */
#define VM_ALLOC	0x00000002	/* vmalloc()によって割り当てられたページ */
#define VM_MAP		0x00000004	/* vmap()でマッピングされたページ */
/* bits [20..32] reserved for arch specific ioremap internals */

struct vm_struct {
	void			*addr; // 領域内の先頭ページフレームのリニアアドレス
	unsigned long		size; // メモリ領域のサイズ
	unsigned long		flags; // マッピングしているメモリの種類
	struct page		**pages; // ページディスクリプタのポインタ配列
	unsigned int		nr_pages; // ページディスクリプタのポインタ配列の数
	unsigned long		phys_addr; // ハードウェアデバイスのI/O共有メモリとして使用しない場合は0
	struct vm_struct	*next; // 次のvm_struct構造体を指すポインタ
};

/*
 *	Highlevel APIs for driver use
 */
extern void *vmalloc(unsigned long size);
extern void *vmalloc_exec(unsigned long size);
extern void *vmalloc_32(unsigned long size);
extern void *__vmalloc(unsigned long size, int gfp_mask, pgprot_t prot);
extern void vfree(void *addr);

extern void *vmap(struct page **pages, unsigned int count,
			unsigned long flags, pgprot_t prot);
extern void vunmap(void *addr);
 
/*
 *	Lowlevel-APIs (not for driver use!)
 */
extern struct vm_struct *get_vm_area(unsigned long size, unsigned long flags);
extern struct vm_struct *__get_vm_area(unsigned long size, unsigned long flags,
					unsigned long start, unsigned long end);
extern struct vm_struct *remove_vm_area(void *addr);
extern int map_vm_area(struct vm_struct *area, pgprot_t prot,
			struct page ***pages);
extern void unmap_vm_area(struct vm_struct *area);

/*
 *	Internals.  Dont't use..
 */
extern rwlock_t vmlist_lock;
extern struct vm_struct *vmlist;

#endif /* _LINUX_VMALLOC_H */
