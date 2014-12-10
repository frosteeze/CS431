#include <types.h>
#include <lib.h>
#include <pt.h>
#include <vm.h>
#include <coremap.h>
#include <swapfile.h>
#include <kern/errno.h>
#include <addrspace.h>
#include "opt-A3.h"

#if OPT_A3



DEFFLAGS(page, pg, pgflag);

static struct pt_internal * pti_create(vaddr_t base, size_t sz) {
  struct pt_internal * pti = kmalloc(sizeof(struct pt_internal));
  if (pti == NULL) {
    return NULL;
  }

  KASSERT((base & PAGE_FRAME) == base);

  pti->pti_base = base;

  pti->pti_size = sz;

  pti->pti_pages = kmalloc(sizeof(struct page) * pti->pti_size);
  if (pti->pti_pages == NULL) {
    kfree(pti);
    return NULL;
  }

  for (unsigned int i = 0; i < pti->pti_size; i++) {
    vaddr_t vaddr = pti->pti_base + i * PAGE_SIZE;
    pti->pti_pages[i].pg_vaddr = vaddr;
    pti->pti_pages[i].pg_paddr = 0;

    pg_reset_flags(&pti->pti_pages[i]);

    pti->pti_pages[i].pg_offset = -1;

    KASSERT(pti->pti_pages[i].pg_paddr == 0);
  }

  return pti;
}

static bool pti_contains(struct pt_internal * pti, vaddr_t vaddr) {
  return (pti->pti_base <= vaddr && (pti->pti_base + pti->pti_size * PAGE_SIZE) >
      vaddr);
}

static struct page * pti_find(struct pt_internal * pti, vaddr_t vaddr) {
  if (pti == NULL) {
    return NULL;
  }

  if (pti->pti_pages == NULL) {
    return NULL;
  }

  off_t offset = (vaddr - pti->pti_base) / PAGE_SIZE;

  return &pti->pti_pages[offset];
}

struct page_table* pt_create(struct addrspace * as) {
  if (as == NULL) {
    return NULL;
  }

  if (seg_infoarray_num(&as->as_si) != 2) {
    return NULL;
  }

  struct page_table * pt;
  pt = kmalloc(sizeof(*pt));
  if (pt == NULL) {
    return NULL;
  }

  struct seg_info * si;
  // code - probably.
  si = seg_infoarray_get(&as->as_si, 0);
  pt->pt_pt[0] = pti_create(si->si_base, si->si_size);
  if (pt->pt_pt[0] == NULL) {
    kfree(pt);
    return NULL;
  }

  // data - probably.
  si = seg_infoarray_get(&as->as_si, 1);
  pt->pt_pt[1] = pti_create(si->si_base, si->si_size);
  if (pt->pt_pt[1] == NULL) {
    kfree(pt->pt_pt[0]);
    kfree(pt);
    return NULL;
  }

  // stack is fixed size
  pt->pt_pt[2] = pti_create(USERSTACK - (MAX_STACK_SIZE * PAGE_SIZE),
      MAX_STACK_SIZE);
  if (pt->pt_pt[2] == NULL) {
    kfree(pt->pt_pt[1]);
    kfree(pt->pt_pt[0]);
    kfree(pt);
    return NULL;
  }

  return pt;
}

void pt_destroy(struct page_table * pt)
{
	KASSERT(pt);

  for (int i = 0; i < 3; i++)
  {
    struct pt_internal * pti = pt->pt_pt[i];

    KASSERT(pti);

    size_t pti_size = pti->pti_size;
    for (unsigned int j = 0; j < pti_size; j++)
    {
      if (pti->pti_pages[j].pg_vaddr)
      {
        tlb_evict(pti->pti_pages[j].pg_vaddr);
      }

      if (pti->pti_pages[j].pg_paddr)
      {
        coremap_zero_frame(pti->pti_pages[j].pg_paddr);
      }
    }
  }

  kfree(pt->pt_pt[2]->pti_pages);
  kfree(pt->pt_pt[1]->pti_pages);
  kfree(pt->pt_pt[0]->pti_pages);
  kfree(pt->pt_pt[2]);
  kfree(pt->pt_pt[1]);
  kfree(pt->pt_pt[0]);
}


struct page * pt_find_page(struct page_table * pt, vaddr_t vaddr) {
  if (pt == NULL) {
    return NULL;
  }

  struct page * pg = NULL;

  if (pti_contains(pt->pt_pt[0], vaddr)) {
    pg = pti_find(pt->pt_pt[0], vaddr);
  }
  else if (pti_contains(pt->pt_pt[1], vaddr)) {
    pg = pti_find(pt->pt_pt[1], vaddr);
  }
  else if (pti_contains(pt->pt_pt[2], vaddr)) {
    pg = pti_find(pt->pt_pt[2], vaddr);
  }

  return pg;
}

#endif
