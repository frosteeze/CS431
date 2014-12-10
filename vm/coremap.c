#include <coremap.h>
#include <types.h>
#include <pt.h>
#include <swapfile.h>
#include <vm.h>
#include <synch.h>
#include "opt-A3.h"

#if OPT_A3
// coremap is an array of frames. it's initialized with vm.
// Initially the coremap is an array of frames with state 
// FRAME_FREE. We store the kernel mapping with state 
// FRAME_FIXED to identify kernel mappings and prevent them
// from being swapped. 

static paddr_t        basepaddr;
static paddr_t        toppaddr;
struct frame *        coremap;
static volatile int   next_victim;
struct spinlock       cm_lock;
int                   mem_frames;
int                   avail_frames;
int                   free_frames;
int                   coremapframes;

void coremap_init(void)
{
  // ram_getsize returns the paddrs, however it also
  // destroys the copy of paddrs used for kmalloc, so we
  // need to declare fixed size variables first, call ram_getsize,
  // then manually allocate coremap in kernel space
  paddr_t firstpaddr;
  paddr_t lastpaddr;


  spinlock_init(&cm_lock);
  free_frames = 0;

  ram_getsize(&firstpaddr, &lastpaddr);

  KASSERT(firstpaddr % PAGE_SIZE == 0);

  basepaddr = firstpaddr;
  toppaddr  = lastpaddr;

  // Calculate the total number of frames in RAM
  size_t roundmemsz = (lastpaddr - firstpaddr + PAGE_SIZE - 1) & PAGE_FRAME;
  mem_frames = roundmemsz / PAGE_SIZE;
  
  // calculate how many frames the coremap will take up
  coremapframes = (sizeof(struct frame)*mem_frames + PAGE_SIZE - 1) / PAGE_SIZE;
      
  // Manually allocate paddr space for coremap
  // paddr[firstpaddr, firstpaddr + coremapframes * PAGE_SIZE] stores the
  // coremap, so paddr mapping should start from firstpaddr + coremapframes * PAGE_SIZE.

  // set up the coremap here
  coremap = (struct frame *)PADDR_TO_KVADDR(firstpaddr);

  bzero(coremap, coremapframes * PAGE_SIZE);

  // set our initial frames to be fixed so we don't try to use them.
  for (int i = 0; i < coremapframes; i++) {
    coremap[i].fm_state = FRAME_FIXED;
  }

  avail_frames = mem_frames - coremapframes;
  
  // replacement algo will increment it to 0 for first check
  next_victim = 0;
}


/*
  Decide what frame to work with next; may be a victim of eviction,
  hence the name "next_victim"
*/
static void choose_next_victim()
{
  KASSERT(spinlock_do_i_hold(&cm_lock));

  next_victim++;

  if (next_victim >= mem_frames)
  {
    next_victim = 0;
  }
  
  // skip 'fixed' frames when doing coremap operations
  while (coremap[next_victim].fm_state == FRAME_FIXED) next_victim++;

  if (next_victim >= mem_frames)
  {
    next_victim = 0;
  }

  KASSERT(next_victim < mem_frames);
}

// Populates frame for user
static void coremap_populate_frame(struct page *new_page)
{
  KASSERT(spinlock_do_i_hold(&cm_lock));

  coremap[next_victim].fm_state = FRAME_USER;
  coremap[next_victim].fm_page = new_page;
  coremap[next_victim].fm_used = true;
  coremap[next_victim].fm_modified = false;
}

// Populates frames for kernel
static void coremap_populate_kframes(int start_frame, int end_frame, int npages)
{
  KASSERT(spinlock_do_i_hold(&cm_lock));

  for (int i = start_frame; i <= end_frame; i++) {
    coremap[i].fm_state = FRAME_FIXED;
    
    // since we don't have kernel pages, might as well
    // use this pointer to keep track of number of pages
    // hacky? yes. very yes.
    coremap[i].fm_page = (struct page *) npages;
  }
}

/*
  Used to allocate frame for a new page.
  Try to find any free frame.
*/
static paddr_t coremap_first_sweep(struct page *new_page)
{
  paddr_t availpaddr = 0;

  // Note here that we are using coremap[next_victim] rather than coremap[i];
  // this is to prevent having to iterate from frame 0 every time since it'd
  // be more likely to find a victim after where we left off last time
  for (int i = 0; i < avail_frames; i++) {
    if (coremap[next_victim].fm_state == FRAME_FREE) {
      availpaddr = basepaddr + PAGE_SIZE * next_victim;

      coremap_populate_frame(new_page);
      choose_next_victim();
      break;
    }

    choose_next_victim();
  }

  return availpaddr;
}

/*
  Used to allocate frame for a new page.
  Couldn't find any free frame.
  Look for used = 0, modified = 0  
*/
static paddr_t coremap_second_sweep(struct page *new_page)
{
  paddr_t availpaddr = 0;

  // ditto, coremap_first_sweep
  for (int i = 0; i < avail_frames; i++) {
    if (!coremap[next_victim].fm_used && !coremap[next_victim].fm_modified) {
      availpaddr = basepaddr + PAGE_SIZE * next_victim;

      struct page *old_page = coremap[next_victim].fm_page;
      pg_set_flag(old_page, valid, false);
      coremap_populate_frame(new_page);

      // store victim for when we come back; another process may change
      // next_victim while we are doing the swapfile operation
      int victim = next_victim;

      // for swapping the old page in case it's been modified
      // we set the specific frame to be fixed because we need to
      // release the cm_lock before doing the actual coremap operation
      // and without the lock, another process may use next_victim and we
      // want next_victim to ignore this frame until we are done here
      if (pg_get_flag(old_page, swap)) {
        coremap[next_victim].fm_state = FRAME_FIXED;

        spinlock_release(&cm_lock);

        /* actual swapfile operation */
        int error = swapfile_write_page(old_page);

        if (error) {
          panic("I/O Error in Swapfile: %d\n", error);
        }

        spinlock_acquire(&cm_lock);
      }

      // reset the frame state back to FRAME_USER, as it should be
      coremap[victim].fm_state = FRAME_USER;
      old_page->pg_paddr = 0;

      tlb_evict(old_page->pg_vaddr);

      break;
    }

    choose_next_victim();
  }

  return availpaddr;
}

/*
  Used to allocate frame for a new page.
  Look for used = 0, modified = 1
*/
static paddr_t coremap_third_sweep(struct page *new_page)
{
  paddr_t availpaddr = 0;

  // ditto, coremap_first_sweep
  for (int i = 0; i < avail_frames; i++) {
    if (!coremap[next_victim].fm_used && coremap[next_victim].fm_modified) {
      availpaddr = basepaddr + PAGE_SIZE * next_victim;

      struct page *old_page = coremap[next_victim].fm_page;
      pg_set_flag(old_page, valid, false);

      coremap_populate_frame(new_page);

      // store victim for when we come back; another process may change
      // next_victim while we are doing the swapfile operation
      int victim = next_victim;

      // for swapping the old page in case it's been modified
      // we set the specific frame to be fixed because we need to
      // release the cm_lock before doing the actual coremap operation
      // and without the lock, another process may use next_victim and we
      // want next_victim to ignore this frame until we are done here
      if (pg_get_flag(old_page, swap)) {
        coremap[next_victim].fm_state = FRAME_FIXED;

        spinlock_release(&cm_lock);

        /* actual swapfile operation */
        int error = swapfile_write_page(old_page);

        if (error) {
          panic("I/O Error in Swapfile: %d\n", error);
        }

        spinlock_acquire(&cm_lock);
      }

      // reset the frame state back to FRAME_USER, as it should be
      coremap[victim].fm_state = FRAME_USER;

      tlb_evict(old_page->pg_vaddr);
      old_page->pg_paddr = 0;
      break;
    }

    coremap[next_victim].fm_used = false;
    tlb_invalidate(coremap[next_victim].fm_page->pg_vaddr);
    choose_next_victim();
  }

  return availpaddr;
}

// For user frames request
paddr_t coremap_alloc_frame(struct page *new_page)
{
  paddr_t availpaddr = 0;

  // init is wrong if you fail here
  KASSERT(coremap != NULL);
  KASSERT(mem_frames > 0);
  KASSERT(basepaddr > 0);

  spinlock_acquire(&cm_lock);

  int num_runs = 0;

  /*
    Enhanced second chance replacement algorithm
  */
  availpaddr = coremap_first_sweep(new_page);
  while (availpaddr == 0) {
    availpaddr = coremap_second_sweep(new_page);

    if (availpaddr == 0) {
      availpaddr = coremap_third_sweep(new_page);
    }

    num_runs++;
  }

  KASSERT(num_runs < 3);
  spinlock_release(&cm_lock);

  return availpaddr;
}

// for kernel frames request (via kmalloc)
paddr_t coremap_alloc_kframes(size_t npages)
{
  paddr_t availpaddr = 0;

  // init is wrong if you fail here
  KASSERT(coremap != NULL);
  KASSERT(mem_frames > 0);
  KASSERT(basepaddr > 0);

  spinlock_acquire(&cm_lock);

  // for counting contiguous frames
  size_t avail_count = 0;

  // Try to find 'npages' free contiguous frames
  for (int i = 0; i < avail_frames; i++) {
    if (coremap[next_victim].fm_state == FRAME_FREE) {
      avail_count++;

      if (avail_count == npages) {
        int start_frame = next_victim - (npages - 1);
        coremap_populate_kframes(start_frame, start_frame + (npages - 1), npages);
        availpaddr = basepaddr + start_frame * PAGE_SIZE;
        choose_next_victim();
        break;
      }
    }
    else {
      avail_count = 0;
    }

    choose_next_victim();
  }

  spinlock_release(&cm_lock);

  return availpaddr;
}

void coremap_free_kframes(paddr_t addr)
{
  int start_frame = (addr - basepaddr) / PAGE_SIZE;
  int npages = (int) coremap[start_frame].fm_page;

  for (int i = start_frame; i <= start_frame + (npages - 1); i++) {
    coremap[i].fm_state = FRAME_FREE;
    coremap[i].fm_page = NULL;
  }
}

void coremap_set_modified(paddr_t paddr) {
  int frame = (paddr - basepaddr) / PAGE_SIZE;
  coremap[frame].fm_modified = true;
}

void coremap_set_used(paddr_t paddr) {
  int frame = (paddr - basepaddr) / PAGE_SIZE;
  coremap[frame].fm_used = true;
}

void coremap_zero_frame(paddr_t paddr)
{
  KASSERT(paddr);
  int frame = (paddr - basepaddr) / PAGE_SIZE;
  coremap[frame].fm_page = NULL;
  coremap[frame].fm_used = false;
  coremap[frame].fm_state = FRAME_FREE;
  coremap[frame].fm_modified = false;
}
#endif
