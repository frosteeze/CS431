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
#include <synch.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <file.h>
#include <copyinout.h>
#include <vfs.h>


  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

#define PATH_MAX 1024

#if OPT_A2
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
	

	int result = as_copy(p->p_addrspace, &child_proc->p_addrspace);/* copy the address space from the 
	parent to the child process lot of problems can happen here but the most common problem is the os 
	running out of memory to give to the vm, which causes the vm to not be able to return any pages to 
	ram_stealmem which causes as_prepare_load not to return an addresspace to as_copy which final fails 
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


int
sys_getpid(pid_t *retval)
{
#if OPT_A2
    struct proc *p = curproc;
    *retval = p->p_pid;
#endif // OPT_A2
    return(0);

}

int sys__exit (int exitcode) {
  struct process_list_entry* temp = curproc->list_ptr;
  temp->status = PROCESS_TERMINATED;
  temp->exitcode = exitcode;
  process_exit();//should be process_exit() still working on this!!!!! 
  return 0;
}

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;
  
	KASSERT(status != NULL);
	if (((int) status) % 4 != 0) {
        //misaligned memory address
        return EFAULT;
    }
  
	struct process_list_entry* child = NULL;
	child = get_process_entry(pid);
	
  	if (child == NULL) return EINVAL;
  	if (options != 0) return EINVAL;
  	if (status == NULL) return EINVAL;

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

/* Execv implementation */
int sys_execv(char *progname, char **args) { /* execv will take in the arguments of a char pointer to the program name and an array of characters for the program's arguments */
	struct vnode *v;
	vaddr_t entrypoint, stackprt;
	int result;
	int argc, spl;
	int i = 0;
	size_t bufferlen;
	spl=splhigh(); // Switch to kernel mode
	char *prog = (char*)kmalloc(PATH_MAX); // copy the name of the program to the kernel
	if(prog == NULL) {
		// Make sure that the program isn't empty
		return ENOMEM;
	}	
	copyinstr((userptr_t)progname,prog,PATH_MAX,&bufferlen);
	
	//verify that all arguments are valid
	while(args[i]!=NULL) {
		i++;
	}
	argc = i;
	char **argv;
	argv = (char**)kmalloc(sizeof(char*));
	if(argv == NULL) {
		kfree(prog);
		return ENOMEM;
	}
	
	int j;
	for(i=0; i <= argc; i++) {
		if(i<argc) {
			int length;
			length = strlen(args[i]);
			length = length + 1;
			argv[i]=(char*)kmalloc(length+1);
			if(argv[i]==NULL) {
				// If we are out of memory
				for(j=0; j < i; j++) {
					kfree(argv[j]);
				}
				// deallocate previous args we allocated and used before
				kfree(argv);
				kfree(prog);
				return ENOMEM;
			}
				// We have deallocated all previous arguments and the copy of the program name from the kernel
			copyinstr((userptr_t)args[i], argv[i], length, &bufferlen);
		}	
		else {
		argv[i] = NULL;
		}
	}
	/* We will now open the file */
	result = vfs_open(prog, O_RDONLY, 0, &v);
	if(result) {
		return result;
	}	

	if(curproc->p_addrspace) {
		struct addrspace *as = curproc->p_addrspace;	
		curproc->p_addrspace = NULL;
		as_destroy(as);
	}
	/* It should create a new process and be NULL */
	//kassert(curproc->p_addrspace == NULL);
	
	/* Create an address space for the process */
	curproc->p_addrspace = as_create();
	if(curproc->p_addrspace == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	// Allocate space for the current process into VM
	//as_activate(curproc->p_addrspace);
	
	/* We will now load the executable file by invoking load_elf */
	result = load_elf(v, &entrypoint);
	if(result) {
		// If result returns true, destroy the current process
		vfs_close(v);
		return result;
	}
	
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curproc->p_addrspace, &stackprt);
	
	if(result) {
		return result;
	}


	/* We are now going to push the arugments onto the stack and align the arguments to be byte addressable similar to how we did it for runprog */
	int argvptr[argc];
	int totaloffset = 0;
	for(i = argc-1; i >= 0; i--) {
		int length = strlen(argv[i])+1;
		if(length%4 != 0) {
			int remainder = (length%4);
			totaloffset = (length + (4-remainder));
			stackprt = stackprt - totaloffset;
		}
		else {
			stackprt = stackprt - length;
		}
		copyoutstr(argv[i],(userptr_t)stackprt, length, &bufferlen);
		argvptr[i] = stackprt;
	}
	
	argvptr[argc] = 0;
	
	for(i = argc; i >= 0; i--) {
		stackprt = stackprt-4;
		copyout(&argvptr[i], (userptr_t)stackprt, sizeof(argvptr[i]));
	}

	kfree(argv);
	kfree(prog);

	/* Switch to user mode */
	splx(spl);

	enter_new_process(argc, (userptr_t)stackprt, stackprt, entrypoint);
	
	/* Usermod does not return, panic something went wrong */
	panic("md_usermode returned\n");
	return EINVAL;
}

