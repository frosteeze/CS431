#ifndef _SWAPFILE_H
#define _SWAPFILE_H

#include <types.h>
#include <pt.h>
#include "opt-A3.h"
/*
Defines the swap file for a program.   
*/
// 9 MB
#define SWAPFILE_SIZE (9*1024*1024)

#if OPT_A3

//initializes the swapfile for a single process/program  
void swapfile_bootstrap(void);

//writes a single page down to a file on the hardrive.   
int swapfile_write_page(struct page* pg);

//loads a page into ram from secondary memory 
int swapfile_load_page(struct page* pg);

//checks that a page is in a file  
bool probeswapfile_At_Page(struct page* pg);  
#endif

#endif // _SWAPFILE_H
