#ifndef _COREMAP_H
#define _COREMAP_H

#include <addrspace.h>
#include <pt.h>
#include "opt-A3.h"


#if OPT_A3

// fm_state
// FRAME_FREE, FRAME_USER, FRAME_FIXED

// fm_used
// NOT USED, USED

// fm_modified
// NOT MODIFIED, MODIFIED

// page structure: stores the mapping and its states, used in 
// page table structure and coremap structure


/*
the coremap is made to keep track of free physical frames in ram memory 
*/

struct frame;
struct page;

#define FRAME_FREE 0
#define FRAME_USER 1
#define FRAME_FIXED 2

struct frame
{
  // page used for mapping
  struct page * fm_page;

  /*
    0 - FRAME_FREE
    1 - FRAME_USER
    2 - FRAME_FIXED
  */
  int fm_state;
  
  // whether the frame has recently been used
  bool fm_used;

  // whether the frame has been modified
  bool fm_modified;
};

void coremap_init(void);

paddr_t coremap_alloc_frame(struct page *new_page);

paddr_t coremap_alloc_kframes(size_t npages);

void coremap_free_kframes(paddr_t addr);

void coremap_set_modified(paddr_t paddr);

void coremap_set_used(paddr_t paddr);

void coremap_zero_frame(paddr_t paddr);
#endif
#endif //_COREMAP_H
