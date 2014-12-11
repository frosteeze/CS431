/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define SIFINLINE

#include <types.h>

#include <uio.h>
#include <current.h>
#include <spinlock.h>
#include <mips/tlb.h>
#include <coremap.h>
#include <swapfile.h>
#include <addrspace.h>
#include <vm.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <vnode.h>
#include <uw-vmstats.h>
#include <pt.h>
#ifdef UW
#include <proc.h>
#endif
#include "opt-A3.h"


#if OPT_A3

DEFFLAGS(page_seg_info, psi, psiflag);

/*Create address space by allocating space for the size of as make sure
that you actually have an address space.
*/
struct addrspace *as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

  page_seg_infoarray_init(&as->as_psi);

  // we should eventually initialize the page table on prepare_load or something
  as->as_pt = NULL;

  as->as_stack_size = 0;

	return as;
}

void as_destroy(struct addrspace *as)
{
/*	
   *get page table size for this address space.
   *get the three pt_indirection_level 
   *get the internal pagetable size
   *per page entry:
   
      -zero the frame
      -kfree this page
      -invalidate tlb
  */
  
  KASSERT(as);

  if (!as->as_pt)
  {
    return;
  }

  struct page_table * pt = as->as_pt;
  
  pt_destroy(pt);

  kfree(as->as_pt);

  page_seg_infoarray_remove(&as->as_psi, 0);
  page_seg_infoarray_remove(&as->as_psi, 0);
  page_seg_infoarray_cleanup(&as->as_psi);
  
	kfree(as);
}

static struct addrspace* as_prev = NULL;

void as_activate(void)
{
	int i, spl;
	struct addrspace *as;
  

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

  if (as == as_prev) {
    return;
  }

	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	// update vm stat: TLB Invalidation
  vmstats_inc(VMSTAT_TLB_INVALIDATE);

	splx(spl);
}


/*
	never got around to fully implementing this function 
*/
void as_deactivate(void)
{
	/* nothing */
}

void as_set_prev(void) {
  as_prev = curproc_getas();
}

int as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable, off_t file_offset, size_t filesz){
	size_t npages; 

  struct page_seg_info* psi;
  psi = kmalloc(sizeof(struct page_seg_info));
  if (psi == NULL) {
    return ENOMEM;
  }

  // should we be checking this?
  KASSERT((vaddr & PAGE_FRAME) == vaddr);

  psi->psi_seg_base = vaddr;
  psi->psi_seg_size = filesz;

	npages = (sz + PAGE_SIZE - 1) / PAGE_SIZE;

  psi->psi_base = vaddr;
  psi->psi_size = npages;

  psi_set_flag(psi, r, readable);
  psi_set_flag(psi, w, writeable);
  psi_set_flag(psi, x, executable);

  psi->psi_offset = file_offset;

  page_seg_infoarray_add(&as->as_psi, psi, NULL);

  return 0;
}

//static int min(int a, int b) {
  //return a < b ? a : b;
//}

//static int maxf(int a, int b) {
  //return a < b ? b : a;
//}

int as_load_page(struct addrspace* as, struct page* pg) {
  if (pg == NULL) {
    return EINVAL;
  }

  KASSERT(pg_get_flag(pg, valid));
  KASSERT(pg->pg_paddr != 0);
  vaddr_t vaddr = pg->pg_vaddr;
  paddr_t paddr = pg->pg_paddr;
  // if we need to load from swap file, then load from swapfile
  if (pg_get_flag(pg, swap)) {
    int result = swapfile_load_page(pg);
    vmstats_inc(VMSTAT_SWAP_FILE_READ);
    vmstats_inc(VMSTAT_PAGE_FAULT_DISK);
    return result;
  }

  int max = page_seg_infoarray_num(&as->as_psi);
  bool in_seg = false;

  for (int i = 0; i < max; i++) {
    struct page_seg_info * psi = page_seg_infoarray_get(&as->as_psi, i);
    if (psi_in_segment(psi, vaddr)) {
      struct iovec iov;
      struct uio u;
      int res;

      vaddr_t startvaddr = vaddr;
      int len = MIN(MAXF((int)psi->psi_seg_size - ((int)startvaddr - (int)psi->psi_seg_base), 0), PAGE_SIZE);
      
      if (len > 0) {
        iov.iov_ubase = (void*)PADDR_TO_KVADDR(paddr);
        iov.iov_len = len;
        u.uio_iov = &iov;
        u.uio_iovcnt = 1;
        u.uio_resid = len; // amount to read
        u.uio_offset = psi->psi_offset + (startvaddr - psi->psi_seg_base);
        u.uio_segflg = UIO_SYSSPACE;
        u.uio_rw = UIO_READ;
        u.uio_space = NULL;

        // are we in the current process?
        struct vnode * vn = curproc->p_prog;

        res = VOP_READ(vn, &u);
        if (res) {
          return res;
        }
        vmstats_inc(VMSTAT_ELF_FILE_READ);
        vmstats_inc(VMSTAT_PAGE_FAULT_DISK);

        // zero out the remaining stuff
        paddr_t endpaddr = paddr + len - u.uio_resid;
        int readlen = (int)paddr + PAGE_SIZE - (int)endpaddr;
        if (readlen > 0) {
          bzero((void*)PADDR_TO_KVADDR(endpaddr), readlen);
        }
      }
      else {
        vmstats_inc(VMSTAT_PAGE_FAULT_ZERO);

        // zero out the whole page
        bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
      }
      
      in_seg = true;
      break;
    }
  }

  if (!in_seg) {
    vmstats_inc(VMSTAT_PAGE_FAULT_ZERO);
    bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
  }

  return 0;
}

int as_prepare_load(struct addrspace *as)
{
  struct page_table * pt;
  pt = pt_create(as);

  as->as_pt = pt;
  
  if (pt == NULL) {
    return ENOMEM;
  }

  return 0;
}

int as_complete_load(struct addrspace *as)
{
  // we never really complete loading
	(void)as;
	return 0;
}

int as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
  as->as_stack_size = MAX_STACK_SIZE;
	*stackptr = USERSTACK;
	return 0;
}

int as_copy(struct addrspace *old, struct addrspace **ret)
{
  struct addrspace *new;

  new = as_create();
  if (new==NULL) {
  	return ENOMEM;
  }

  // TODO
  (void)old;

	*ret = new;
	return 0;
}

//should be 
int as_num_stackpages(struct addrspace * as) {
  return as->as_stack_size;
}

bool psi_in_segment(struct page_seg_info * psi, vaddr_t vaddr) {
  KASSERT(vaddr % PAGE_SIZE == 0);

  return (vaddr >= psi->psi_base && vaddr < psi->psi_base + PAGE_SIZE * psi->psi_size);
}

struct page * as_get_page(struct addrspace * as, vaddr_t vaddr, bool * ret) {
  struct page * pg;
  pg = pt_find_page(as->as_pt, vaddr);

  if (pg == NULL || !pg_get_flag(pg, valid)) {
    *ret = true;
  }

  if (pg == NULL) {
    return NULL;
  }

  KASSERT(pg != NULL);

  if (!pg_get_flag(pg, valid)) {
    paddr_t paddr = coremap_alloc_frame(pg);
    pg->pg_paddr = paddr;

    pg_set_flag(pg, valid, true);
    KASSERT(pg->pg_paddr < USERSPACETOP);
    KASSERT(pg->pg_paddr % PAGE_SIZE == 0);
  }

  KASSERT(pg_get_flag(pg, valid));

  return pg;
}

#else

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */

	(void)old;
	
	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */
	
	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/*
	 * Write this.
	 */
}

void
#ifdef UW
as_deactivate(void)
#else
as_dectivate(void)
#endif
{
	
}


int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;
	
	return 0;
}
#endif
