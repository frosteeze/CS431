#ifndef _FILE_H_
#define _FILE_H_

#include <kern/limits.h>
#include <spinlock.h>
#include "opt-A2.h"

#if OPT_A2
struct vnode;

//Filetable struct
struct filetable_entry {
    struct vnode *filetable_vnode; //Make the vnode for the file
    int filetable_pos; //Shows position of the file
    int filetable_flags; //Flags for the file
    int filetable_count; //How many file descriptors is in the file
};

//Filetable will hold the files that are open. Stores an int and other things
struct filetable {
	struct filetable_entry *filetable_entries[__OPEN_MAX];
	struct spinlock filetable_spinlock;
};

//Starts the filetable. Should start it during runprogram
int filetable_init(void);


//Opens the file, but has to go through file syscall first
int file_open(char *filename, int flags, int mode, int *retfd);

//Closes the file
int file_close(int fd);


#endif //file_H
#endif
