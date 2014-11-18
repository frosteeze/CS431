#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <lib.h>
#include <uio.h>
#include <syscall.h>
#include <vnode.h>
#include <vfs.h>
#include <current.h>
#include <proc.h>
#include <file.h>
#include <copyinout.h>
#include "opt-A2.h"
#if OPT_A2
static
void
mk_useruio(struct iovec *iov, struct uio *u, userptr_t buf, 
	   size_t len, off_t offset, enum uio_rw rw)
{

	iov->iov_ubase = buf;
	iov->iov_len = len;
	u->uio_iov = iov;
	u->uio_iovcnt = 1;
	u->uio_offset = offset;
	u->uio_resid = len;
	u->uio_segflg = UIO_USERSPACE;
	u->uio_rw = rw;
	u->uio_space = curproc->p_addrspace;
}

int
sys_write(int fd,userptr_t buf,size_t len,int *retval)
{
	struct uio user_uio;
	struct iovec user_iov;
	int result;
	int offset = 0;

	//Check the filetable if it exists
	struct filetable *filetable = curthread->t_filetable;
	spinlock_acquire(&filetable->filetable_spinlock);

	//Checks if the file is valid or has write permission
	if ((fd < 0) || (fd >= __OPEN_MAX) || (filetable->filetable_entries[fd] == NULL) ||
		    (filetable->filetable_entries[fd]->filetable_vnode == NULL)) {
		spinlock_release(&filetable->filetable_spinlock);
		return EBADF;
	}

	//Enters the userio info needed
	offset = filetable->filetable_entries[fd]->filetable_pos;
	mk_useruio(&user_iov, &user_uio, buf, len, offset, UIO_WRITE);

	//Actual write being done here
	result = VOP_WRITE(filetable->filetable_entries[fd]->filetable_vnode, &user_uio);
	if (result) {
		spinlock_release(&filetable->filetable_spinlock);
		return result;
	}

	//Calculate how much left is in the lifetable
	*retval = len - user_uio.uio_resid;

	filetable->filetable_entries[fd]->filetable_pos += *retval;
	spinlock_release(&filetable->filetable_spinlock);
	return 0;
}

//Sys_open implementation to open a file. Has a function that overloads.
int
sys_open(userptr_t filename, int flags, int mode, int *retval)
{
	char *fname;
	int result;

	//Allocates memory to open the file
	if ( (fname = (char *)kmalloc(__PATH_MAX)) == NULL) {
		return ENOMEM;
	}

	//Turns the full path file name into string
	result = copyinstr(filename, fname, __PATH_MAX, NULL);
	if (result) {
		kfree(fname);
		return result;
	}
	result = file_open(fname, flags, mode, retval);

	kfree(fname);
	return result;
}


int
sys_read(int fd, userptr_t buf, size_t size, int *retval)
{

    
    struct uio user_uio;
	struct iovec user_iov;
	int result;
	int offset = 0;

    //Check if the filetable exists
    struct filetable *filetable = curthread->t_filetable;
    spinlock_acquire(&filetable->filetable_spinlock);
    
    //Check if the fd is valid and if the file can be read or not
    if ((fd < 0) || (fd >= __OPEN_MAX) || (filetable->filetable_entries[fd] == NULL) ||
            (filetable->filetable_entries[fd]->filetable_vnode == NULL)) {
        spinlock_release(&filetable->filetable_spinlock);
        return EBADF;
    }

	//Enter the userio info needed
    offset = filetable->filetable_entries[fd]->filetable_pos;
	mk_useruio(&user_iov, &user_uio, buf, size, offset, UIO_READ);

	//Does the actual read using VOP_read
    spinlock_release(&filetable->filetable_spinlock);
	result = VOP_READ(filetable->filetable_entries[fd]->filetable_vnode, &user_uio);
	if (result) {
		return result;
	}

	//The amount of the buffer is subtracted to the amount that is still left
	*retval = size - user_uio.uio_resid;
	spinlock_acquire(&filetable->filetable_spinlock);
    filetable->filetable_entries[fd]->filetable_pos += *retval;

    spinlock_release(&filetable->filetable_spinlock);
	return 0;
}

//Just passes the file descriptor to close the file
int
sys_close(int fd)
{
	return file_close(fd);
}
#endif

