/*
 *  linux/mm/vmalloc.c
 *
 *  Copyright (C) 1993  Linus Torvalds
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *  SMP-safe vmalloc/vfree/ioremap, Tigran Aivazian <tigran@veritas.com>, May 2000
 *  Major rework to support vmap/vunmap, Christoph Hellwig, SGI, August 2002
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include <linux/vmalloc.h>

#include <asm/uaccess.h>
#include <asm/tlbflush.h>


DEFINE_RWLOCK(vmlist_lock);
struct vm_struct *vmlist;

static void unmap_area_pte(pmd_t *pmd, unsigned long address,
				  unsigned long size)
{
	unsigned long end;
	pte_t *pte;

	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		return;
	}

	pte = pte_offset_kernel(pmd, address);
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;

	do {
		pte_t page;
		page = ptep_get_and_clear(pte);
		address += PAGE_SIZE;
		pte++;
		if (pte_none(page))
			continue;
		if (pte_present(page))
			continue;
		printk(KERN_CRIT "Whee.. Swapped out page in kernel page table\n");
	} while (address < end);
}

static void unmap_area_pmd(pud_t *pud, unsigned long address,
				  unsigned long size)
{
	unsigned long end;
	pmd_t *pmd;

	if (pud_none(*pud))
		return;
	if (pud_bad(*pud)) {
		pud_ERROR(*pud);
		pud_clear(pud);
		return;
	}

	pmd = pmd_offset(pud, address);
	address &= ~PUD_MASK;
	end = address + size;
	if (end > PUD_SIZE)
		end = PUD_SIZE;

	do {
		unmap_area_pte(pmd, address, end - address);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
}

static void unmap_area_pud(pgd_t *pgd, unsigned long address,
			   unsigned long size)
{
	pud_t *pud;
	unsigned long end;

	if (pgd_none(*pgd))
		return;
	if (pgd_bad(*pgd)) {
		pgd_ERROR(*pgd);
		pgd_clear(pgd);
		return;
	}

	pud = pud_offset(pgd, address);
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;

	do {
		unmap_area_pmd(pud, address, end - address);
		address = (address + PUD_SIZE) & PUD_MASK;
		pud++;
	} while (address && (address < end));
}

static int map_area_pte(pte_t *pte, unsigned long address,
			       unsigned long size, pgprot_t prot,
			       struct page ***pages)
{
	unsigned long end;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;

	do {
		struct page *page = **pages;
		WARN_ON(!pte_none(*pte));
		if (!page)
			return -ENOMEM;

		set_pte(pte, mk_pte(page, prot));
		address += PAGE_SIZE;
		pte++;
		(*pages)++;
	} while (address < end);
	return 0;
}

static int map_area_pmd(pmd_t *pmd, unsigned long address,
			       unsigned long size, pgprot_t prot,
			       struct page ***pages)
{
	unsigned long base, end;

	base = address & PUD_MASK;
	address &= ~PUD_MASK;
	end = address + size;
	if (end > PUD_SIZE)
		end = PUD_SIZE;

	do {
		pte_t * pte = pte_alloc_kernel(&init_mm, pmd, base + address);
		if (!pte)
			return -ENOMEM;
		if (map_area_pte(pte, address, end - address, prot, pages))
			return -ENOMEM;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);

	return 0;
}

static int map_area_pud(pud_t *pud, unsigned long address,
			       unsigned long end, pgprot_t prot,
			       struct page ***pages)
{
	do {
		pmd_t *pmd = pmd_alloc(&init_mm, pud, address);
		if (!pmd)
			return -ENOMEM;
		if (map_area_pmd(pmd, address, end - address, prot, pages))
			return -ENOMEM;
		address = (address + PUD_SIZE) & PUD_MASK;
		pud++;
	} while (address && address < end);

	return 0;
}

void unmap_vm_area(struct vm_struct *area)
{
	unsigned long address = (unsigned long) area->addr;
	unsigned long end = (address + area->size);
	unsigned long next;
	pgd_t *pgd;
	int i;

	pgd = pgd_offset_k(address); // ページグローバルディレクトリのエントリを取得
	flush_cache_vunmap(address, end);
	for (i = pgd_index(address); i <= pgd_index(end-1); i++) {
		next = (address + PGDIR_SIZE) & PGDIR_MASK;
		if (next <= address || next > end)
			next = end;
		unmap_area_pud(pgd, address, next - address);
		address = next;
	        pgd++;
	}
	flush_tlb_kernel_range((unsigned long) area->addr, end);
}

/** 
 * 連続するリニアアドレスを非連続なページフレームに割り当てる。
 * 
 * @area: 非連続メモリディスクリプタ
 * @prot: 割り当てたページの保護bit
 * @pages: ページディスクリプタへのポインタ配列を指すポインタ変数のアドレス
 */

int map_vm_area(struct vm_struct *area, pgprot_t prot, struct page ***pages)
{
	unsigned long address = (unsigned long) area->addr; // 領域の開始アドレス
	unsigned long end = address + (area->size-PAGE_SIZE); // 終端アドレス
	unsigned long next;
	pgd_t *pgd;
	int err = 0;
	int i;

	pgd = pgd_offset_k(address); // ページグローバルディレクトリのエントリを取得
	spin_lock(&init_mm.page_table_lock); // スピンロックで保護
	for (i = pgd_index(address); i <= pgd_index(end-1); i++) {
		// ページアッパーディレクトリを作成し
		// ページグローバルディレクトリに対応するエントリに
		// ページアッパーディレクトリの物理アドレスを書き込む
		pud_t *pud = pud_alloc(&init_mm, pgd, address); 
		// 作成に失敗
		if (!pud) {
			err = -ENOMEM;
			break;
		}
		next = (address + PGDIR_SIZE) & PGDIR_MASK;
		if (next < address || next > end)
			next = end;

		// ページアッパーディレクトリに対応する全てのページテーブルを割り当てる
		if (map_area_pud(pud, address, next, prot, pages)) {
			err = -ENOMEM;
			break;
		}

		address = next;
		pgd++;
	}

	spin_unlock(&init_mm.page_table_lock);
	flush_cache_vmap((unsigned long) area->addr, end);
	return err;
}

#define IOREMAP_MAX_ORDER	(7 + PAGE_SHIFT)	/* 128 pages */

struct vm_struct *__get_vm_area(unsigned long size, unsigned long flags,
				unsigned long start, unsigned long end)
{
	struct vm_struct **p, *tmp, *area;
	unsigned long align = 1;
	unsigned long addr;

	// ioremap時のバリデーション
	if (flags & VM_IOREMAP) {
		int bit = fls(size);

		if (bit > IOREMAP_MAX_ORDER)
			bit = IOREMAP_MAX_ORDER;
		else if (bit < PAGE_SHIFT)
			bit = PAGE_SHIFT;

		align = 1ul << bit;
	}
	addr = ALIGN(start, align); // アラインメント

	area = kmalloc(sizeof(*area), GFP_KERNEL); // 汎用キャッシュからメモリ領域の取得
	if (unlikely(!area))
		return NULL;

	/*
	 * 保護用のページを割り当てる
	 */
	size += PAGE_SIZE;
	if (unlikely(!size)) {
		kfree (area);
		return NULL;
	}

	write_lock(&vmlist_lock); // vm_structリスト用のスピンロックを取得
	// リストを先頭(vmlist)からトラバースし空き領域を探す
	for (p = &vmlist; (tmp = *p) != NULL ;p = &tmp->next) {
		if ((unsigned long)tmp->addr < addr) {
			if((unsigned long)tmp->addr + tmp->size >= addr)
				addr = ALIGN(tmp->size + 
					     (unsigned long)tmp->addr, align);
			continue;
		}
		if ((size + addr) < addr)
			goto out;
		if (size + addr <= (unsigned long)tmp->addr)
			goto found;
		addr = ALIGN(tmp->size + (unsigned long)tmp->addr, align);
		if (addr > end - size)
			goto out;
	}

// 見つかった場合
found:
	// リストにvm_structを繋ぐ
	area->next = *p;
	*p = area;
	// vm_struct構造体を初期化
	area->flags = flags;
	area->addr = (void *)addr;
	area->size = size;
	area->pages = NULL;
	area->nr_pages = 0;
	area->phys_addr = 0;
	write_unlock(&vmlist_lock); // スピンロックを解放

	return area;

out:
	write_unlock(&vmlist_lock); // スピンロックを解放
	kfree(area); // 取得したメモリ領域を開放する
	if (printk_ratelimit())
		printk(KERN_WARNING "allocation failed: out of vmalloc space - use vmalloc=<size> to increase size.\n");
	return NULL;
}

/**
 *	get_vm_area  -  連続したカーネルの仮想領域を予約する
 *
 *	@size:		領域のサイズ
 *	@flags:		I/O mappingsのためのVM_IOREMAP もしくは VM_ALLOC
 *
 *  カーネルの仮想マッピングされた領域内のsizeで指定したサイズの領域を探し予約する
 *  成功時はvm_structのポインタを返し、失敗時にはNULLを返す
 */
struct vm_struct *get_vm_area(unsigned long size, unsigned long flags)
{
	return __get_vm_area(size, flags, VMALLOC_START, VMALLOC_END);
}

/**
 *	remove_vm_area  -  find and remove a contingous kernel virtual area
 *
 *	@addr:		base address
 *
 *	Search for the kernel VM area starting at @addr, and remove it.
 *	This function returns the found VM area, but using it is NOT safe
 *	on SMP machines.
 */
struct vm_struct *remove_vm_area(void *addr)
{
	struct vm_struct **p, *tmp;

	write_lock(&vmlist_lock);
	for (p = &vmlist ; (tmp = *p) != NULL ;p = &tmp->next) {
		 if (tmp->addr == addr)
			 goto found;
	}
	write_unlock(&vmlist_lock);
	return NULL;

found:
	unmap_vm_area(tmp);
	*p = tmp->next;
	write_unlock(&vmlist_lock);
	return tmp;
}

void __vunmap(void *addr, int deallocate_pages)
{
	struct vm_struct *area;

	// バリデーション
	if (!addr)
		return;
	if ((PAGE_SIZE-1) & (unsigned long)addr) {
		printk(KERN_ERR "Trying to vfree() bad address (%p)\n", addr);
		WARN_ON(1);
		return;
	}

	// 領域に対応するカーネル用ページテーブル内エントリを削除
	area = remove_vm_area(addr);

	// 存在しない領域を解放対象とした場合
	if (unlikely(!area)) {
		printk(KERN_ERR "Trying to vfree() nonexistent vm area (%p)\n",
				addr);
		WARN_ON(1);
		return;
	}
	
	// 当該フラグがセットされている場合はページフレームを開放する(ページフレームアロケータに返す)
	if (deallocate_pages) {
		int i;

		// ページ毎に処理
		for (i = 0; i < area->nr_pages; i++) {
			if (unlikely(!area->pages[i]))
				BUG();
			// どこからも参照されていない場合は当該ページを開放する
			__free_page(area->pages[i]);
		}

		if (area->nr_pages > PAGE_SIZE/sizeof(struct page *))
			vfree(area->pages);
		else
			kfree(area->pages); // ポインタ配列自体を解放
	}

	kfree(area); // vm_structを解放
	return;
}

/**
 *	vfree  -  vmalloc()で割り当てたメモリを開放する
 *
 *	@addr:		メモリのベースアドレス
 *
 *  仮想的に連続している領域(addrで開始する)を解放する。
 *	おそらくこの関数は割り込みコンテキストからは呼び出されない
 */
void vfree(void *addr)
{
	BUG_ON(in_interrupt());
	__vunmap(addr, 1);
}

EXPORT_SYMBOL(vfree);

/**
 *	vunmap  -  release virtual mapping obtained by vmap()で取得された仮想的なマッピングを開放する
 *	@addr:		ベースになるメモリアドレス
 *
 *  (addrで開始する)vmap()から渡されたページ配列から作成された仮想的に連続するメモリ領域を開放する
 *	割り込みコンテキストからは呼び出されないだろう
 */
void vunmap(void *addr)
{
	BUG_ON(in_interrupt());
	__vunmap(addr, 0);
}

EXPORT_SYMBOL(vunmap);

/**
 *	vmap  -  map an array of pages into virtually contiguous space
 *
 *	@pages:		array of page pointers
 *	@count:		number of pages to map
 *	@flags:		vm_area->flags
 *	@prot:		page protection for the mapping
 *
 *	Maps @count pages from @pages into contiguous kernel virtual
 *	space.
 */
void *vmap(struct page **pages, unsigned int count,
		unsigned long flags, pgprot_t prot)
{
	struct vm_struct *area;

	if (count > num_physpages)
		return NULL;

	area = get_vm_area((count << PAGE_SHIFT), flags);
	if (!area)
		return NULL;
	if (map_vm_area(area, prot, &pages)) {
		vunmap(area->addr);
		return NULL;
	}

	return area->addr;
}

EXPORT_SYMBOL(vmap);

/**
 *	__vmalloc  -  仮想的に連続しているメモリを予約する
 *
 *	@size:		割り当てサイズ
 *	@gfp_mask:	ページアロケータのためのフラグ
 *	@prot:		割り当てたページの保護マスク
 *
 *  サイズをカバーできるページをgfp_maskフラグと共にページアロケータから割り当てる。
 *  protのページテーブル保護を用いて割り当てたページは連続する仮想領域にマップする。
 */
void *__vmalloc(unsigned long size, int gfp_mask, pgprot_t prot)
{
	struct vm_struct *area;
	struct page **pages;
	unsigned int nr_pages, array_size, i;
	
	// サイズのアライメント及びバリデーション
	size = PAGE_ALIGN(size); // アライメント
	if (!size || (size >> PAGE_SHIFT) > num_physpages)
		return NULL;

	// 空き領域を取得
	area = get_vm_area(size, VM_ALLOC);
	if (!area)
		return NULL;

	nr_pages = size >> PAGE_SHIFT; // ページ数に変換
	array_size = (nr_pages * sizeof(struct page *)); // ページ数のページディスクリプタ配列のサイズ

	area->nr_pages = nr_pages; // ページ数を更新
	
	/* この再帰呼び出しは厳しく制限されている */
	if (array_size > PAGE_SIZE) // ページサイズより大きい場合
		pages = __vmalloc(array_size, gfp_mask, PAGE_KERNEL); // 再帰呼び出し
	else
		pages = kmalloc(array_size, (gfp_mask & ~__GFP_HIGHMEM)); // 汎用キャッシュから割り当てる
	area->pages = pages; // 取得したページディスクリプタのポインタ配列
	
	// 取得できなかった場合
	if (!area->pages) {
		remove_vm_area(area->addr);
		kfree(area);
		return NULL;
	}
	memset(area->pages, 0, array_size); // 当該領域を0クリア

	// 取得した領域にページを割り当てる
	for (i = 0; i < area->nr_pages; i++) {
		// ページを割り当て、メモリディスクリプタのメンバであるリストにページディスクリプタを設定する
		area->pages[i] = alloc_page(gfp_mask);

		// 割り当てに失敗した場合
		if (unlikely(!area->pages[i])) {
			/* いくつかのページの割り当てに成功していた場合には__vunmap()関数でそれらを開放する */
			area->nr_pages = i;
			goto fail;
		}
	}

	// 連続するリニアアドレスを非連続なページフレームに割り当てる。
	if (map_vm_area(area, prot, &pages))
		goto fail;
	return area->addr;

fail: // 割り当てに失敗した場合にはそれを開放する
	vfree(area->addr);
	return NULL;
}

EXPORT_SYMBOL(__vmalloc);

/**
 *	vmalloc  -  仮想的に連続したメモリを割り当てる
 *
 *	@size:		割り当てサイズ
 *
 *  ページアロケータからサイズ以上のページを割り当て、それを連続するカーネルの
 *  仮想メモリ領域にマッピングする。
 *
 *  厳しいページアロケータの制御及び保護フラグのため、__vmalloc()を代わりに使用する。
 */
void *vmalloc(unsigned long size)
{
       return __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM, PAGE_KERNEL);
}

EXPORT_SYMBOL(vmalloc);

/**
 *	vmalloc_exec  -  allocate virtually contiguous, executable memory
 *
 *	@size:		allocation size
 *
 *	Kernel-internal function to allocate enough pages to cover @size
 *	the page level allocator and map them into contiguous and
 *	executable kernel virtual space.
 *
 *	For tight cotrol over page level allocator and protection flags
 *	use __vmalloc() instead.
 */

#ifndef PAGE_KERNEL_EXEC
# define PAGE_KERNEL_EXEC PAGE_KERNEL
#endif

void *vmalloc_exec(unsigned long size)
{
	return __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM, PAGE_KERNEL_EXEC);
}

/**
 *	vmalloc_32  -  allocate virtually contiguous memory (32bit addressable)
 *
 *	@size:		allocation size
 *
 *	Allocate enough 32bit PA addressable pages to cover @size from the
 *	page level allocator and map them into contiguous kernel virtual space.
 */
void *vmalloc_32(unsigned long size)
{
	return __vmalloc(size, GFP_KERNEL, PAGE_KERNEL);
}

EXPORT_SYMBOL(vmalloc_32);

long vread(char *buf, char *addr, unsigned long count)
{
	struct vm_struct *tmp;
	char *vaddr, *buf_start = buf;
	unsigned long n;

	/* Don't allow overflow */
	if ((unsigned long) addr + count < count)
		count = -(unsigned long) addr;

	read_lock(&vmlist_lock);
	for (tmp = vmlist; tmp; tmp = tmp->next) {
		vaddr = (char *) tmp->addr;
		if (addr >= vaddr + tmp->size - PAGE_SIZE)
			continue;
		while (addr < vaddr) {
			if (count == 0)
				goto finished;
			*buf = '\0';
			buf++;
			addr++;
			count--;
		}
		n = vaddr + tmp->size - PAGE_SIZE - addr;
		do {
			if (count == 0)
				goto finished;
			*buf = *addr;
			buf++;
			addr++;
			count--;
		} while (--n > 0);
	}
finished:
	read_unlock(&vmlist_lock);
	return buf - buf_start;
}

long vwrite(char *buf, char *addr, unsigned long count)
{
	struct vm_struct *tmp;
	char *vaddr, *buf_start = buf;
	unsigned long n;

	/* Don't allow overflow */
	if ((unsigned long) addr + count < count)
		count = -(unsigned long) addr;

	read_lock(&vmlist_lock);
	for (tmp = vmlist; tmp; tmp = tmp->next) {
		vaddr = (char *) tmp->addr;
		if (addr >= vaddr + tmp->size - PAGE_SIZE)
			continue;
		while (addr < vaddr) {
			if (count == 0)
				goto finished;
			buf++;
			addr++;
			count--;
		}
		n = vaddr + tmp->size - PAGE_SIZE - addr;
		do {
			if (count == 0)
				goto finished;
			*addr = *buf;
			buf++;
			addr++;
			count--;
		} while (--n > 0);
	}
finished:
	read_unlock(&vmlist_lock);
	return buf - buf_start;
}
