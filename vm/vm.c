#ifdef UW

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <coremap.h>
#include <pt.h>
#include <uw-vmstats.h>
#include "opt-A3.h"

static
paddr_t
getppages(unsigned long npages)
{
  return coremap_alloc_kframes(npages);
}

//round robin tlb code 
int
tlb_get_rr_victim()
{
    int victim;
    static unsigned int next_victim = 0;
    victim = next_victim;
    next_victim = (next_victim + 1) % NUM_TLB;
    return victim;
}

// initialize vm, coremap do this in the main with other bootstrap stuff
void
vm_bootstrap(void)
{
  coremap_init();
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
  paddr_t pa;
  pa = getppages(npages);
  if (pa == 0) {
      return 0;
  }
  return PADDR_TO_KVADDR(pa);
}

//free up memory when you are done using it 
void 
free_kpages(vaddr_t addr)
{
  paddr_t pa;
  pa = KVADDR_TO_PADDR(addr);
  coremap_free_kframes(pa);
}

/*Junk that doesn't do anything because we don't know what it was suppose to do */
void
vm_tlbshootdown_all(void)
{
    panic("Not implemented yet.\n");
}
  
void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
    (void)ts;
    panic("Not implemented yet.\n");
}

/*handle a tlb miss both the hard misses and the soft misses */
int
vm_fault(int faulttype, vaddr_t faultaddress) {
  vaddr_t stackbase, stacktop;
  paddr_t paddr;
  int i;
  uint32_t ehi, elo;
  struct addrspace *as;
  int spl;
  int result;
  bool readonly, write;

  write = false;

  faultaddress &= PAGE_FRAME;

  switch (faulttype) {
      case VM_FAULT_READONLY:
      readonly = true;
      break;
      case VM_FAULT_WRITE:
      write = true;
      case VM_FAULT_READ:
      readonly = false;
      break;
      default:
      return EINVAL;
  }

  if (curproc == NULL) {
      /*
       * No process. This is probably a kernel fault early
       * in boot. Return EFAULT so as to panic instead of
       * getting into an infinite faulting loop.
       */
      return EFAULT;
  }

  as = curproc_getas();
  if (as == NULL) {
      /*
       * No address space set up. This is probably also a
       * kernel fault early in boot.
       */
      return EFAULT;
  }

  KASSERT(as_num_stackpages(as) == 12);

  stackbase = USERSTACK - as_num_stackpages(as) * PAGE_SIZE;
  stacktop = USERSTACK;

  bool is_text_segment = false;
  bool in_segment = false;
  bool in_stack = false;

  int max = seg_infoarray_num(&as->as_si);
  KASSERT(max == 2);
  for (int i = 0; i < max; i++) {
    struct seg_info * si = seg_infoarray_get(&as->as_si, i);
    if (si_in_segment(si, faultaddress)) {
      if (!si_get_flag(si, w)) {
        is_text_segment = true;
      }
      in_segment = true;
    }
  }

  in_stack = (faultaddress < stacktop) && (faultaddress >= stackbase);

  if (!in_segment && !in_stack) {
    return EFAULT;
  }

  
  bool needs_load = false;
  struct page * pg = as_get_page(as, faultaddress, &needs_load);
  if (pg == NULL) {
    return ENOMEM;
  }
  spl = splhigh();
  
  
  
  int idx = tlb_find(pg->pg_vaddr);
  if (idx >= 0) {
    uint32_t ehi, elo;
    tlb_read(&ehi, &elo, idx);
    if (readonly) {
      if (is_text_segment) {
        splx(spl);
        return EFAULT;
      }

      coremap_set_modified(pg->pg_paddr);
      pg_set_flag(pg, swap, true);
      elo |= TLBLO_DIRTY;
    }

    if (write) {
      if (is_text_segment) {
        splx(spl);
        return EFAULT;
      }
      coremap_set_modified(pg->pg_paddr);
      pg_set_flag(pg, swap, true);
      // we can write now.
      elo |= TLBLO_DIRTY;
    }

    coremap_set_used(pg->pg_paddr);
    // entry is actually valid.
    elo |= TLBLO_VALID;

    tlb_write(ehi, elo, idx);

    splx(spl);
    return 0;
  }
  splx(spl);

  paddr = pg->pg_paddr;

  vmstats_inc(VMSTAT_TLB_FAULT);

  KASSERT((paddr & PAGE_FRAME) == paddr);
  KASSERT(paddr != 0);

  /* Disable interrupts  */
  spl = splhigh();

  bool found_space = false;
  idx = -1;
  for (i=0; i<NUM_TLB; i++) {
      tlb_read(&ehi, &elo, i);
      if (elo & TLBLO_VALID) {
          continue;
      }
      ehi = faultaddress;
      
      elo = paddr | TLBLO_DIRTY | TLBLO_VALID;

      tlb_write(ehi, elo, i);
      found_space = true;
      idx = i;

      vmstats_inc(VMSTAT_TLB_FAULT_FREE);

      break;
  }

  // Ran out space in the TLB, remove an entry for a  new entry
  if (!found_space) {
    int victim = tlb_get_rr_victim();
    ehi = faultaddress;
    idx = victim;
    

    elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
    
    tlb_write(ehi, elo, victim);

    vmstats_inc(VMSTAT_TLB_FAULT_REPLACE);
  }
  splx(spl);

  if (needs_load) {
    result = as_load_page(as, pg);
    if (result) {
      return result;
    }
  }
  else {
    vmstats_inc(VMSTAT_TLB_RELOAD);
  }


  spl = splhigh();
  ehi = faultaddress;
  elo = paddr;
  tlb_write(ehi, elo, idx);
  splx(spl);

  return 0;
}

void tlb_evict(vaddr_t vaddr) {
  int spl = splhigh();

  int idx = tlb_probe(vaddr, 0);
  if (idx >= 0) {
    tlb_write(TLBHI_INVALID(idx), TLBLO_INVALID(), idx);
  }

  splx(spl);
}

void tlb_invalidate(vaddr_t vaddr) {
  int spl = splhigh();

  int idx = tlb_probe(vaddr, 0);
  if (idx >= 0) {
    uint32_t ehi, elo;

    tlb_read(&ehi, &elo, idx);
    tlb_write(ehi, elo & (~TLBLO_VALID), idx);
  }

  splx(spl);
}

#endif /* UW */
