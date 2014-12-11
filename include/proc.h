/*
 * Copyright (c) 2013
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

#ifndef _PROC_H_
#define _PROC_H_

/*
 * Definition of a process.
 *
 * Note: curproc is defined by <current.h>.
 */
#include "opt-A3.h"

#include <spinlock.h>
#include <thread.h> /* required for struct threadarray */
#include <kern/types.h>
#include <synch.h>
#include <wchan.h>


//process status codes  
#define PROCESS_RUNNING 0
#define PROCESS_WAITING 1
#define PROCESS_BLOCKED 2
#define PROCESS_READY 3
#define PROCESS_SWAPPED_WAITING 3
#define PROCESS_SWAPPED_BLOCKED 4
#define PROCESS_TERMINATED 5

//exit codes -- note that an exit code can also be 
/*
IRQ (0) -- Interrupt
MOD (1) -- "Modify", TLB read-only fault
TLBL (2) -- TLB miss on load
TLBS (3) -- TLB miss on store
ADEL (4) -- Address error on load
ADES (5) -- Address error on store
IBE (6) -- Bus error on instruction fetch
DBE (7) -- Bus error on data access
SYS (8) -- System call
BP (9) -- Breakpoint instruction
RI (10) -- Reserved (illegal) instruction
CPU (11) -- Coprocessor unusable
OVF (12) -- Integer overflow
*/
#define EXIT_SUCCESS 13
#define EXIT_FAILURE 14


struct addrspace;
struct vnode;
#ifdef UW
struct semaphore;
#endif // UW

struct process_list_entry* process_list_head; // first element of list
struct process_list_entry* process_list_tail;// last element of the list 
struct lock* list_lock; // list lock control access to the list 


/*
 * Process structure.
 */
struct proc {
	char *p_name;			/* Name of this process */
	struct spinlock p_lock;		/* Lock for this structure */
	struct threadarray p_threads;	/* Threads in this process */
	struct process_list_entry* list_ptr; 	/*points to the process list */
	/* VM */
	struct addrspace *p_addrspace;	/* virtual address space */
#if OPT_A2
	pid_t p_pid; 			/* Pid for this process*/
#endif
	/* VFS */
	struct vnode *p_cwd;		/* current working directory */

#ifdef UW
  /* a vnode to refer to the console device */
  /* this is a quick-and-dirty way to get console writes working */
  /* you will probably need to change this when implementing file-related
     system calls, since each process will need to keep track of all files
     it has opened, not just the console. */
  struct vnode *console;                /* a vnode for the console device */
#endif
	struct lock *p_children_lock;
	struct filetable *p_filetable;

	struct vnode * p_prog;
};

struct process_list_entry {
  pid_t pid; 						// the pid of the process this entry references
  pid_t parent_pid; 				// the parent pid of the process this entry references 
  volatile int status; 				// current status of this process
  struct proc* process_ptr; 		// points to the process in this entry references 
  volatile int exitcode; 			// the exit code for this process
  struct process_list_entry* next;	// points to the next process in the list 
  struct cv* wait_cv;  				// wait condition 
  volatile bool p_parent_exited; 		//
  struct lock * p_exit_lock; 		//
  struct cv * p_exit_cv;			//
};

struct process_list_entry* get_process_entry (pid_t pid);

/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc *kproc;

/* Semaphore used to signal when there are no more processes */
#ifdef UW
extern struct semaphore *no_proc_sem;
#endif // UW

/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);

/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

/* Destroy a process. */
void proc_destroy(struct proc *proc);

/* Attach a thread to a process. Must not already have a process. */
int proc_addthread(struct proc *proc, struct thread *t);

/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

void proc_remallthreads(struct proc *p);

/* Fetch the address space of the current process. */
struct addrspace *curproc_getas(void);

/* Change the address space of the current process, and return the old one. */
struct addrspace *curproc_setas(struct addrspace *);

//finds the child process of the current process, if any. 
//assuming a linked list structure
/*
[kproc]
	\	
	[proc 1]
	       \
		   [proc 2] child of proc 1
		          \
				   ...
*/
struct proc * get_child_proc(struct proc * proc);

/*given a proccess thats ending updates the exit code for the current processes children */
void proc_signal_children(struct proc* p);
/*
exits the current process sends the exit code to children
*/
void process_exit(void);

void proc_cleanup(struct proc *proc);

void proc_clear(struct proc *proc);

/*
Meant to switch between to processes and define the new state the switching process 
will be in this function is heavily modelled after the thread switch function in 
thread.c. This function is incomplete and can not be uses as written at current.
currentproc --switch--to--> next child process or kernel process if no other process 
are available.
steps to switch a process  
1. Change protection domain (user to supervisor[kernel]).
2. Change stacks: switch from using the user-level stack to
using a kernel stack.
3. Save execution state (on kernel’s stack).
4. Do kernel stuff
5. Kernel thread switch
6. Restore user-level execution state
7. Change protection domain (from supervisor[kernel] to user)      
*/
//static void process_switch(struct proc processToSwitch ,int newstate, struct wchan *wc);

#if OPT_A2
/* Returns the next usable or unassigned pid. In the original design of our os 
we wanted to implement some type of pid reclaiming system but we ran out of time
and in the end this was a simple and effective method. and the chances of this os 
creating 10,000 processes in any given run of the os is small so this was a calculated
risk.     
*/
pid_t get_next_pid(void);
#endif
#endif /* _PROC_H_ */

