/*
 * Wrapper functions for accessing the file_struct fd array.
 */

#ifndef __LINUX_FILE_H
#define __LINUX_FILE_H

#include <asm/atomic.h>
#include <linux/posix_types.h>
#include <linux/compiler.h>
#include <linux/spinlock.h>

/*
 * The default fd array needs to be at least BITS_PER_LONG,
 * as this is the granularity returned by copy_fdset().
 */
#define NR_OPEN_DEFAULT BITS_PER_LONG

/*
 * Open file table structure
 */
struct files_struct {
        atomic_t count; /* この構造体を共有するプロセス数*/
        spinlock_t file_lock;　/* この構造体用のスピンロック */
        int max_fds; /* ファイルオブジェクトの最大数 */
        int max_fdset; /* ファイルディスクリプタの現在の最大数 */
        int next_fd; /* 次に割り当てるのディスクリプタ番号 */
        struct file ** fd; /* ファイルオブジェクトポインタ配列へのポインタ */
        fd_set *close_on_exec; /* exec()時にクローズする必要のあるファイルディスクリプタ */
        fd_set *open_fds; /* オープン中のファイルディスクリプタへのポインタ */
        fd_set close_on_exec_init; /* exec()の際にクローズすべきファイルディスクリプタ集合の初期値 */
        fd_set open_fds_init; /* ファイルディスクリプタ集合の初期値 */
        struct file * fd_array[NR_OPEN_DEFAULT]; /* ファイルオブジェクトへのポインタを格納する初期配列 */
};

extern void FASTCALL(__fput(struct file *));
extern void FASTCALL(fput(struct file *));

static inline void fput_light(struct file *file, int fput_needed)
{
	if (unlikely(fput_needed))
		fput(file);
}

extern struct file * FASTCALL(fget(unsigned int fd));
extern struct file * FASTCALL(fget_light(unsigned int fd, int *fput_needed));
extern void FASTCALL(set_close_on_exec(unsigned int fd, int flag));
extern void put_filp(struct file *);
extern int get_unused_fd(void);
extern void FASTCALL(put_unused_fd(unsigned int fd));
struct kmem_cache_s;
extern void filp_ctor(void * objp, struct kmem_cache_s *cachep, unsigned long cflags);
extern void filp_dtor(void * objp, struct kmem_cache_s *cachep, unsigned long dflags);

extern struct file ** alloc_fd_array(int);
extern void free_fd_array(struct file **, int);

extern fd_set *alloc_fdset(int);
extern void free_fdset(fd_set *, int);

extern int expand_files(struct files_struct *, int nr);

static inline struct file * fcheck_files(struct files_struct *files, unsigned int fd)
{
	struct file * file = NULL;

	if (fd < files->max_fds)
		file = files->fd[fd];
	return file;
}

/*
 * Check whether the specified fd has an open file.
 */
#define fcheck(fd)	fcheck_files(current->files, fd)

extern void FASTCALL(fd_install(unsigned int fd, struct file * file));

struct task_struct;

struct files_struct *get_files_struct(struct task_struct *);
void FASTCALL(put_files_struct(struct files_struct *fs));

#endif /* __LINUX_FILE_H */
