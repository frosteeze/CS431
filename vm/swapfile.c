#include <types.h>
#include <pt.h>
#include <swapfile.h>
#include <current.h>
#include <lib.h>
#include <vnode.h>
#include <spinlock.h>
#include <uio.h>
#include <vfs.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <vm.h>
#include <proc.h>
#include <uw-vmstats.h>
#include "opt-A3.h"

#if OPT_A3

#define SWAPFILE_FRAMES (SWAPFILE_SIZE / PAGE_SIZE) // default 9MB / 4KB = 2250KB or 2.50MB
const char * swapf_name = "/swapfile"; //file name for the swapfile 
/*At the time we wrote this code we hadn't seen the queue file build in the O.S. so we
made one which is less generic and only used for swapfile  */
struct queue {
  int vals[SWAPFILE_FRAMES]; // 
  volatile int ptr;
};
/*inits the queue for the swapfile allow the queue to be use.*/
static struct queue* queue_create(void) {
  struct queue * qu = kmalloc(sizeof(struct queue));
  bzero(qu, sizeof(struct queue));
  qu->ptr = 0;
  return qu;
}


/*push a value on to the queue */
static void qu_push(int val, struct queue * qu) {
  if (qu->ptr >= SWAPFILE_FRAMES) {
    panic("Swapfile full");
  }
  qu->vals[qu->ptr] = val;
  qu->ptr++;
}

/*pop the queue for the value in the queue*/
static int qu_pop(struct queue * qu) {
  if (qu == NULL) {
    return 0;
  }
  
/* checks if the queue is empty */
static bool qu_empty(struct queue * qu) {
  return qu->ptr <= 0;
}
  KASSERT(!qu_empty(qu));

  qu->ptr--;
  int val = qu->vals[qu->ptr];

  return val;
}

/*Holds the pages swapped out of memory by the pagetable/vm this struct hold the
 must hold the vnode to the file system where the swapfile data in stored, must have 
 the page queue so storing and re 
*/
struct swapfile {
  struct vnode* swapf_vn; // vnode to the   
  struct queue* swapf_fl; // queue for the swapfile 
  volatile unsigned int swapf_next_frame;
  struct spinlock swapf_lock; //controls internal access to parts of the swap file  
  struct lock* swapf_vnlock; // controls access to the swapfile 
};

struct swapfile swapf; //should be a singleton not sure how to implement this in c code


/*The swapfile initialization function creates the internal parts of the swapfile 
 */
void swapfile_bootstrap(void) {
  bzero(&swapf, sizeof(struct swapfile));

  char file[100]; //buffer
  strcpy(file, swapf_name);

  int result = vfs_open(file, O_RDWR | O_CREAT | O_TRUNC, 0664, &swapf.swapf_vn);
  if (result) {
    panic ("failed to initialize swapfile!\n");
  }

  spinlock_init(&swapf.swapf_lock);
  swapf.swapf_vnlock = lock_create("swapfile lock");

  swapf.swapf_fl = queue_create();
}


/*Get the index of file */
static int swapfile_get_next(struct page* pg) {
  int idx;
  
  spinlock_acquire(&swapf.swapf_lock);
  if (!qu_empty(swapf.swapf_fl)) {
    idx = qu_pop(swapf.swapf_fl);
    pg->pg_offset = idx;
    spinlock_release(&swapf.swapf_lock);
    return idx;
  }

  if (swapf.swapf_next_frame < SWAPFILE_FRAMES) {
    idx = swapf.swapf_next_frame;
    swapf.swapf_next_frame++;
    pg->pg_offset = idx;
    spinlock_release(&swapf.swapf_lock);
    return idx;
  }

  spinlock_release(&swapf.swapf_lock);
  return -1;
}
/*returns the page in the queue  */
static void swapfile_return(int idx, struct page* pg) {
  spinlock_acquire(&swapf.swapf_lock);
  qu_push(idx, swapf.swapf_fl);
  pg->pg_offset = -1;
  spinlock_release(&swapf.swapf_lock);
}


/*writes a page down to the swapfile.   */
int swapfile_write_page(struct page* pg) {
  KASSERT(pg->pg_offset < 0);

  int idx;
  int result;
  off_t offset;
  struct iovec iov;
  struct uio u;

  idx = swapfile_get_next(pg);
  vmstats_inc(VMSTAT_SWAP_FILE_WRITE);
  if (idx < 0) {
    panic("Out of swap space");
  }

  KASSERT(pg->pg_paddr != 0);

  lock_acquire(swapf.swapf_vnlock);

  offset = idx * PAGE_SIZE;
  iov.iov_ubase = (void*)PADDR_TO_KVADDR(pg->pg_paddr);
  iov.iov_len = PAGE_SIZE;
  u.uio_iov = &iov;
  u.uio_iovcnt = 1;
  u.uio_resid = PAGE_SIZE;
  u.uio_segflg = UIO_SYSSPACE;
  u.uio_rw = UIO_WRITE;
  u.uio_space = NULL;
  u.uio_offset = offset;

  result = VOP_WRITE(swapf.swapf_vn, &u);

  lock_release(swapf.swapf_vnlock);

  if (result) {
    swapfile_return(idx, pg);

    KASSERT(pg->pg_offset < 0);
    return result;
  }

  return 0;
}
/*load a page from the swapfile into  memory */
int swapfile_load_page(struct page* pg) {
  int idx;
  int result;
  off_t offset;
  struct iovec iov;
  struct uio u;

  idx = pg->pg_offset;

  if (idx < 0) {
    return EINVAL;
  }

  KASSERT(pg->pg_paddr != 0);

  lock_acquire(swapf.swapf_vnlock);

  offset = idx * PAGE_SIZE;
  iov.iov_ubase = (void*)PADDR_TO_KVADDR(pg->pg_paddr);
  iov.iov_len = PAGE_SIZE;
  u.uio_iov = &iov;
  u.uio_iovcnt = 1;
  u.uio_resid = PAGE_SIZE;
  u.uio_segflg = UIO_SYSSPACE;
  u.uio_rw = UIO_READ;
  u.uio_space = NULL;
  u.uio_offset = offset;

  result = VOP_READ(swapf.swapf_vn, &u);

  if (result) {
    return result;
  }

  lock_release(swapf.swapf_vnlock);

  swapfile_return(idx, pg);

  KASSERT(pg->pg_offset < 0);

  return 0;
}

#endif