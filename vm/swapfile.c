#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <spinlock.h>
#include <uio.h>
#include <vfs.h>
#include <vnode.h>
#include <vm.h>
#include <pt.h>
#include <swapfile.h>
#include <current.h>
#include <proc.h>
#include <uw-vmstats.h>
#include "opt-A3.h"

#if OPT_A3

#define SWAPFILE_FRAMES (SWAPFILE_SIZE / PAGE_SIZE)
const char * sf_name = "/swapfile";

struct ilist {
  int vals[SWAPFILE_FRAMES];
  volatile int ptr;
};

static struct ilist * ilist_create(void) {
  struct ilist * il = kmalloc(sizeof(struct ilist));

  bzero(il, sizeof(struct ilist));

  il->ptr = 0;

  return il;
}

static bool il_empty(struct ilist * il) {
  return il->ptr <= 0;
}

static void il_push(int val, struct ilist * il) {
  if (il->ptr >= SWAPFILE_FRAMES) {
    panic("Swapfile full");
  }

  il->vals[il->ptr] = val;
  il->ptr++;
}

static int il_pop(struct ilist * il) {
  if (il == NULL) {
    return 0;
  }

  KASSERT(!il_empty(il));

  il->ptr--;
  int val = il->vals[il->ptr];

  return val;
}

struct swapfile {
  struct vnode * sf_vn;

  struct ilist * sf_fl;

  volatile unsigned int sf_next_frame;

  struct spinlock sf_lock;
  struct lock * sf_vnlock;
};

struct swapfile sf;

void swapfile_bootstrap(void) {
  bzero(&sf, sizeof(struct swapfile));

  char file[100];
  strcpy(file, sf_name);

  int result = vfs_open(file, O_RDWR | O_CREAT | O_TRUNC, 0664, &sf.sf_vn);
  if (result) {
    panic ("failed to initialize swapfile!\n");
  }

  spinlock_init(&sf.sf_lock);
  sf.sf_vnlock = lock_create("swapfile lock");

  sf.sf_fl = ilist_create();
}

static int swapfile_get_next(struct page * pg) {
  int idx;
  spinlock_acquire(&sf.sf_lock);
  if (!il_empty(sf.sf_fl)) {
    idx = il_pop(sf.sf_fl);
    pg->pg_offset = idx;
    spinlock_release(&sf.sf_lock);
    return idx;
  }

  if (sf.sf_next_frame < SWAPFILE_FRAMES) {
    idx = sf.sf_next_frame;
    sf.sf_next_frame++;
    pg->pg_offset = idx;
    spinlock_release(&sf.sf_lock);
    return idx;
  }

  spinlock_release(&sf.sf_lock);
  return -1;
}

static void swapfile_return(int idx, struct page * pg) {
  spinlock_acquire(&sf.sf_lock);
  il_push(idx, sf.sf_fl);
  pg->pg_offset = -1;
  spinlock_release(&sf.sf_lock);
}

int swapfile_write_page(struct page * pg) {
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

  lock_acquire(sf.sf_vnlock);

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

  result = VOP_WRITE(sf.sf_vn, &u);

  lock_release(sf.sf_vnlock);

  if (result) {
    swapfile_return(idx, pg);

    KASSERT(pg->pg_offset < 0);
    return result;
  }

  return 0;
}

int swapfile_load_page(struct page * pg) {
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

  lock_acquire(sf.sf_vnlock);

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

  result = VOP_READ(sf.sf_vn, &u);

  if (result) {
    return result;
  }

  lock_release(sf.sf_vnlock);

  swapfile_return(idx, pg);

  KASSERT(pg->pg_offset < 0);

  return 0;
}

#endif
