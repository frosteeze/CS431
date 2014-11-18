#include <types.h>
#include <kern/errno.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/unistd.h>
#include <kern/fcntl.h>
#include <file.h>
#include <syscall.h>
#include <lib.h>
#include <vfs.h>
#include <current.h>
#include <spinlock.h>
#include "opt-A2.h"

#if OPT_A2
int
file_open(char *filename, int flags, int mode, int *retfd)
{
    
    //Check the flags if they are valid
    int how = flags & O_ACCMODE;
    if ((how != O_RDONLY) && (how != O_WRONLY) && (how != O_RDWR)) {
        return EINVAL;
    }
    
    //Find empty entry in the filetable
    int fd;
    struct filetable *filetable = curthread->t_filetable;
    
	spinlock_acquire(&filetable->filetable_spinlock);
    for (fd = 0; fd < __OPEN_MAX; fd++) {
        if (filetable->filetable_entries[fd] == NULL) {
            break;
        }
    }
    
    //If the filetable is full return an error
    if (fd == __OPEN_MAX) {
        spinlock_release(&filetable->filetable_spinlock);
        return EMFILE;
    }
    
    //open the actual file
    struct vnode *new_vnode = NULL;
    int result = vfs_open(filename, flags, mode, &new_vnode);
    if (result > 0) {
        spinlock_release(&filetable->filetable_spinlock);
        return result;
    }
    
    filetable->filetable_entries[fd] = (struct filetable_entry *)kmalloc(sizeof(struct filetable_entry));
    filetable->filetable_entries[fd]->filetable_vnode = new_vnode;
    filetable->filetable_entries[fd]->filetable_pos = 0;
    filetable->filetable_entries[fd]->filetable_flags = flags;
    filetable->filetable_entries[fd]->filetable_count = 1;
    
    *retfd = fd;
    
    spinlock_release(&filetable->filetable_spinlock);
    return 0;
}


//This function closes the file
int
file_close(int fd)
{
    DEBUG(DB_VFS, "*** Closing fd %d\n", fd);
    
    struct filetable *filetable = curthread->t_filetable;
    spinlock_acquire(&filetable->filetable_spinlock);
    
    //Check if the file descriptor is valid
    if ((fd < 0) || (fd >= __OPEN_MAX) || (filetable->filetable_entries[fd] == NULL) ||
            (filetable->filetable_entries[fd]->filetable_vnode == NULL)) {
        spinlock_release(&filetable->filetable_spinlock);
        return EBADF;
    }
    
    //There is no other application using the file so free the memory
    filetable->filetable_entries[fd]->filetable_count--;
    if (filetable->filetable_entries[fd]->filetable_count == 0) {
        vfs_close(filetable->filetable_entries[fd]->filetable_vnode);
        kfree(filetable->filetable_entries[fd]);
    }
    
    //Remove the entry 
    filetable->filetable_entries[fd] = NULL;
    spinlock_release(&filetable->filetable_spinlock);
    
    return 0;
}


int
filetable_init(void)
{
    struct filetable *filetable = (struct filetable *)kmalloc(sizeof(struct filetable));
    
    int result;
    char path[5];
    strcpy(path, "con:");
    
    filetable->filetable_entries[0] = (struct filetable_entry *)kmalloc(sizeof(struct filetable_entry));
    struct vnode *cons_vnode = NULL;
    result = vfs_open(path, O_RDWR, 0, &cons_vnode);
    filetable->filetable_entries[0]->filetable_vnode = cons_vnode;
    filetable->filetable_entries[0]->filetable_pos = 0;
    filetable->filetable_entries[0]->filetable_flags = O_RDWR;
    filetable->filetable_entries[0]->filetable_count = 3;
    
    filetable->filetable_entries[1] = filetable->filetable_entries[0];
    filetable->filetable_entries[2] = filetable->filetable_entries[0];
    
    //initialize filetable entries with null
    int fd;
    for (fd = 3; fd < __OPEN_MAX; fd++) {
        filetable->filetable_entries[fd] = NULL;
    }
    
	spinlock_init(&filetable->filetable_spinlock);
    
    /* Update current thread's filetable field. */
    curthread->t_filetable = filetable;
    
    return 0;
}	
#endif
