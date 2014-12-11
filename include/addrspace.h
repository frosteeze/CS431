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

#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

/*
 * Address space structure and operations.
 */

#include <types.h>
#include <array.h>
#include <vm.h>
#include <pt.h>
#include "opt-A3.h"

struct vnode;


/* 
 * Address space - data structure associated with the virtual memory
 * space of a process.
 *
 * You write this.
 */
 
 /*This enum defines whether this segment or page can be read from, written to or 
even executed. */
 enum psiflag {r=0,w=1,x=2};

struct page_seg_info {
  // the page aligned base and size
  vaddr_t psi_base;
  size_t psi_size; // in number of pages

  // the original base and size
  vaddr_t psi_seg_base;
  size_t psi_seg_size; // in bytes

  off_t psi_offset;

  DEFFLAGVAR(psi);
};

DECLFLAGS(page_seg_info, psi, psiflag);

bool si_in_segment(struct seg_info * si, vaddr_t vaddr);

#ifndef SIFINLINE
#define SIFINLINE INLINE
#endif

DECLARRAY(page_seg_info);
DEFARRAY(page_seg_info, SIFINLINE);

struct addrspace {
  //vaddr_t as_vbase1;
  //paddr_t as_pbase1;
  //size_t as_npages1;
  //vaddr_t as_vbase2;
  //paddr_t as_pbase2;
  //size_t as_npages2;
  //paddr_t as_stackpbase;
  
  /*
  Pagetable for this addressspace these are initalized on a by process basis so one for each
  process is needed.
  */ 
  struct page_table * as_pt;

  // for storing vbases for each segment so we can load them
  struct page_seg_infoarray as_psi;

  size_t as_stack_size;
  
  
};

/*
 * Functions in addrspace.c:
 *
 *    as_create - create a new empty address space. You need to make 
 *                sure this gets called in all the right places. You
 *                may find you want to change the argument list. May
 *                return NULL on out-of-memory error.
 *
 *    as_copy   - create a new address space that is an exact copy of
 *                an old one. Probably calls as_create to get a new
 *                empty address space and fill it in, but that's up to
 *                you.
 *
 *    as_activate - make curproc's address space the one currently
 *                "seen" by the processor.
 *
 *    as_deactivate - unload curproc's address space so it isn't
 *                currently "seen" by the processor.
 *
 *    as_destroy - dispose of an address space. You may need to change
 *                the way this works if implementing user-level threads.
 *
 *    as_define_region - set up a region of memory within the address
 *                space.
 *
 *    as_prepare_load - this is called before actually loading from an
 *                executable into the address space.
 *
 *    as_complete_load - this is called when loading from an executable
 *                is complete.
 *
 *    as_define_stack - set up the stack region in the address space.
 *                (Normally called *after* as_complete_load().) Hands
 *                back the initial stack pointer for the new process.
 */

struct addrspace *as_create(void);
int               as_copy(struct addrspace *src, struct addrspace **ret);
void              as_activate(void);
void              as_deactivate(void);
void              as_destroy(struct addrspace *);

int               as_define_region(struct addrspace *as, 
                                   vaddr_t vaddr, size_t sz,
                                   int readable, 
                                   int writeable,
                                   int executable,
				   off_t file_offset, 
				   size_t filesz);
int               as_prepare_load(struct addrspace *as);
int               as_complete_load(struct addrspace *as);
int               as_define_stack(struct addrspace *as, vaddr_t *initstackptr);

int               as_num_stackpages(struct addrspace * as);

// this assumes that the given page is valid.
// this also assumes that page has been loaded into the TLB.
int         	  as_load_page(struct addrspace * as, struct page * pg);

bool              as_is_userptr(struct addrspace * as, vaddr_t vaddr);

// if the page was invalid, it returns true in ret
struct page*      as_get_page(struct addrspace * as, vaddr_t vaddr, bool * ret);

// call this on a context switch
void              as_set_prev(void);

//int               as_num_stackpages(void);



/*
 * Functions in loadelf.c
 *    load_elf - load an ELF user program executable into the current
 *               address space. Returns the entry point (initial PC)
 *               in the space pointed to by ENTRYPOINT.
 */

int load_elf(struct vnode *v, vaddr_t *entrypoint);

#endif
#endif /* _ADDRSPACE_H_ */
