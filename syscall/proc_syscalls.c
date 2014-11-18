#include "opt-A2.h"

#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <mips/trapframe.h>
#include <lib.h>
#include <syscall.h>
#include <spl.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <file.h>
#include <copyinout.h>

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}
#if OPT_A2
int sys_fork(struct trapframe *tf, pid_t *retval)
{
	int s;
	s = splhigh();
	struct proc *p = curproc; // get the current process
	struct trapframe *child_trapframe; // define a trapframe pointer 

	child_trapframe = kmalloc(sizeof(struct trapframe));// allocate space for a trapframe.
	if(child_trapframe == NULL)
	{
		*retval = -1;
		return ENOMEM;//assume you ran out of memory
	}
	
	//KASSERT(curproc->p_addrspace != NULL); // check of a null addrspace  
	
	struct proc *child_proc = proc_create_runprogram(p->p_name); // create a new process with the same 		//name
	if(child_proc == NULL)
	{
		*retval = -1;
		return ENOMEM;
	}
	

	int result = as_copy(p->p_addrspace, &child_proc->p_addrspace);// copy the address space from the parent to the child process
	/*lot of problems can happen here but the most common problem is the os running out of memory to give to the vm, which causes the vm to not be able to return any pages to ram_stealmem which causes as_prepare_load not to return an addresspace to as_copy which final fails to create or copy the address space from the parent to child in sys_fork here. */

	//KASSERT(child_proc->p_addrspace != NULL);
	if(result) {
		panic("\nNo address space defined %d (sys_fork)this probably means the os ran out of memory.\n",result);
		*retval = -1;
		return ENOMEM;
	}
	


	memcpy(child_trapframe, tf, sizeof(struct trapframe));// copy the contence of the parent tf to the child_tf
	struct filetable *ft; 
	
	ft = kmalloc(sizeof(struct filetable));
	if(ft == NULL)
	{
		panic("\nCould not create space for FileTable\n");
		*retval = -1;
		return ENOMEM;
	}
	//ft = curthread->t_filetable;
	//kprintf("Handling filetable now!!!!!\n");
	memcpy(ft,curthread->t_filetable, sizeof(struct filetable));

	
	int err = thread_fork(curthread->t_name, child_proc, (void*)enter_forked_process, child_trapframe, (unsigned long)ft); // fork a new thread for the child process with the same name as the parents threads pass the child_tf
	//and pass the enter forked process.
	splx(s);
	if (err){
		*retval = -1;
		return err;
	}else{
	*retval = child_proc->p_pid;//return the childs pid 
	//kprintf("This is the id of process: %d\n", (int)*retval);
	return(0);
	}
}

#endif


int
sys_getpid(pid_t *retval)
{
#if OPT_A2
    struct proc *p = curproc;
    *retval = p->p_pid;
#else
    /* for now, this is just a stub that always returns a PID of 1 */
    /* you need to fix this to make it work properly */
   // *retval = 1;
#endif // OPT_A2
    return(0);

}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}