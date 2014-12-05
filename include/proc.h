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
#include "opt-A2.h"

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

//exit codes 
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1


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

	struct filetable *p_filetable;
};

struct process_list_entry {
  pid_t pid; 				// the pid of the process this entry references
  pid_t parent_pid; 		// the parent pid of the process this entry references 
  volatile int status; 				// current status of this process
  struct proc* process_ptr; // points to the process in this entry references 
  int exitcode; 			// the exit code for this process
  struct process_list_entry* next;// points to the next process in the list 
  struct cv* wait_cv; 		// wait condition for waitpid
};



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

void process_exit(void);

#if OPT_A2
/* Returns the next usable or unassigned pid.*/
pid_t get_next_pid(void);
#endif
#endif /* _PROC_H_ */

