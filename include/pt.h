#ifndef _PT_H
#define _PT_H

#include <types.h>

enum pgflag {swap=0, valid=1};
/*this implementation of the pagetable uses a multilevel pagetable where you have 2 levels 
of indirection.
1. Page Directory(10)  the index in the root page table
2. Page Table    (10)  the index in the sub-page table
3. Memory Page   (12)  the offset in that page
 
*/

struct addrspace;
/*A page is a mapping of the virtual address to the physical address in ram. Flags 
have been add for ease of implementation.*/
struct page
{
  paddr_t     pg_paddr;
  vaddr_t     pg_vaddr; 

  // swapfile offset
  volatile int pg_offset;
  DEFFLAGVAR(pg); 
};

DECLFLAGS(page, pg, pgflag);

struct pt_internal {
  struct page * pti_pages;

  vaddr_t pti_base;
  size_t pti_size;
};

struct page_table {
  struct pt_internal * pt_pt[3];
};

struct page_table* pt_create(struct addrspace* as);
struct page* pt_find_page(struct page_table* pt, vaddr_t vaddr);
void pt_destroy(struct page_table* pt);

#endif
