#include "opt-A3.h"

#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <mips/trapframe.h>
#include <lib.h>
#include <syscall.h>
#include <spl.h>
#include <current.h>
#include <synch.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <file.h>
#include <copyinout.h>



#if OPT_A3
int sys_fork(struct trapframe *tf, pid_t *retval)
{
	int s;
	s = splhigh();
	struct proc *p = curproc; // get the current process
	struct trapframe *child_trapframe; // define a trapframe pointer
	struct filetable *ft; 	

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
	

	int result = as_copy(p->p_addrspace, &child_proc->p_addrspace);
	/* copy the address space from the parent to the child process lot 
	of problems can happen here but the most common problem is the os 
	running out of memory to give to the vm, which causes the vm to not 
	be able to return any pages to ram_stealmem which causes as_prepare_load 
	not to return an addressspace to as_copy which final fails 
	to create or copy the address space from the parent to child in sys_fork here. */

	//KASSERT(child_proc->p_addrspace != NULL);
	if(result) {
		panic("\nNo address space defined %d (sys_fork)this probably means the os ran out of memory.\n",result);
		*retval = -1;
		return ENOMEM;
	}
	
	memcpy(child_trapframe, tf, sizeof(struct trapframe));// copy the contence of the parent tf to the child_tf
	
	
	ft = kmalloc(sizeof(struct filetable));
	if(ft == NULL)
	{
		panic("\nCould not create space for FileTable\n");
		*retval = -1;
		return ENOMEM;
	}
	
	//kprintf("Handling filetable now!!!!!\n");
	memcpy(ft,p->p_filetable, sizeof(struct filetable));//copy the filetable data
	child_proc->p_filetable = ft; // move the file table 
	
	int err = thread_fork(curthread->t_name, child_proc, (void*)enter_forked_process, child_trapframe, 0); 
	// fork a new thread for the child process with the same name as the parents threads pass the child_tf
	//and pass the enter forked process.
	
	if (err){
		*retval = -1;
		splx(s);
		return err;
	}else{
		*retval = child_proc->p_pid;//return the childs pid 
		//kprintf("This is the id of process: %d\n", (int)*retval);
		splx(s);
		return(0);
	}
}

#endif

int sys_getpid(pid_t *retval)
{
    struct proc *p = curproc;
    *retval = p->p_pid;
    return(0);

}

int sys__exit (int exitcode) {
  curproc->list_ptr->status = PROCESS_TERMINATED;
  curproc->list_ptr->exitcode = exitcode;
  process_exit();
  return 0;
}

int sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *retval)
{
  int exitstatus = 0;
  int result;
  
	KASSERT(status != NULL);
	if (((int) status) % 4 != 0) {
        //misaligned memory address
        return EFAULT;
    }
	if (options != 0) 
		return EINVAL;
		
  	if (status == NULL) 
		return EINVAL;
		
		
	struct process_list_entry* child = NULL;
	child = get_process_entry(pid);
	
  	if (child == NULL) 
		return EINVAL;
  	

	lock_acquire(list_lock);
	
	if (child->parent_pid != curproc->p_pid) {
		lock_release(list_lock);
		return EINVAL;
  }
	//struct proc* process = child->process_ptr;
	if(child->status != PROCESS_TERMINATED) 
	{
		cv_wait(child->wait_cv,list_lock);
		exitstatus = child->exitcode;
	}
	else{
		exitstatus = child->exitcode;
	}
	lock_release(list_lock);
    
  /* for now, just pretend the exitstatus is 0 */
  //exitstatus = 0;
  *retval = pid;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  
  return(0);
}

struct process_list_entry* get_process_entry (pid_t pid) {
  if (process_list_head == NULL) {
    kprintf("get_process_entry: process_list_head is NULL\n");
    return NULL;
  }
  struct process_list_entry* traverser = process_list_head;
  while (traverser != NULL) {
    if (traverser->pid == pid) {
      return traverser;
    }
    traverser = traverser->next;
  }
  return NULL;
}
