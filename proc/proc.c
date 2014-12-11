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

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */
#include "opt-A3.h"

#include <types.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <addrspace.h>
#include <vnode.h>
#include <vfs.h>
#include <synch.h>
#include <kern/fcntl.h>  

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/*
 * Mechanism for making the kernel menu thread sleep while processes are running
 */
#ifdef UW
/* count of the number of processes, excluding kproc */
static unsigned int proc_count;
/* provides mutual exclusion for proc_count */
/* it would be better to use a lock here, but we use a semaphore because locks are not implemented in the base kernel */ 
static struct semaphore *proc_count_mutex;
/* used to signal the kernel menu thread when there are no processes */
struct semaphore *no_proc_sem;   
#endif  // UW

static pid_t next_pid;

/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	threadarray_init(&proc->p_threads);
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

#ifdef UW
	proc->console = NULL;
#endif // UW
#if OPT_A2
proc->p_filetable = NULL;
proc->p_pid = 0;
proc->list_ptr = NULL;




#endif
	return proc;
}

/*
 * Destroy a proc structure.
 */
void
proc_destroy(struct proc *proc)
{
	/*
         * note: some parts of the process structure, such as the address space,
         *  are destroyed in sys_exit, before we get here
         *
         * note: depending on where this function is called from, curproc may not
         * be defined because the calling thread may have already detached itself
         * from the process.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}


#ifndef UW  // in the UW version, space destruction occurs in sys_exit, not here
	if (proc->p_addrspace) {
		/*
		 * In case p is the currently running process (which
		 * it might be in some circumstances, or if this code
		 * gets moved into exit as suggested above), clear
		 * p_addrspace before calling as_destroy. Otherwise if
		 * as_destroy sleeps (which is quite possible) when we
		 * come back we'll be calling as_activate on a
		 * half-destroyed address space. This tends to be
		 * messily fatal.
		 */
		struct addrspace *as;

		as_deactivate();
		as = curproc_setas(NULL);
		as_destroy(as);
	}
#endif // UW

#ifdef UW
	if (proc->console) {
	  vfs_close(proc->console);
	}
#endif // UW

	threadarray_cleanup(&proc->p_threads);
	spinlock_cleanup(&proc->p_lock);

	kfree(proc->p_name);
	kfree(proc);

#ifdef UW
	/* decrement the process count */
        /* note: kproc is not included in the process count, but proc_destroy
	   is never called on kproc (see KASSERT above), so we're OK to decrement
	   the proc_count unconditionally here */
	P(proc_count_mutex); 
	KASSERT(proc_count > 0);
	proc_count--;
	/* signal the kernel menu thread if the process count has reached zero */
	if (proc_count == 0) {
	  V(no_proc_sem);
	}
	V(proc_count_mutex);
#endif // UW
	/*struct process_list_entry *temp = proc->list_ptr;
	
  if (temp->wait_cv != NULL) {
    cv_destroy(temp->wait_cv);
  }*/
	
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
  kproc = proc_create("[kernel]");
  if (kproc == NULL) {
    panic("proc_create for kproc failed\n");
  }
#ifdef UW
  proc_count = 0;
  proc_count_mutex = sem_create("proc_count_mutex",1);
  if (proc_count_mutex == NULL) {
    panic("could not create proc_count_mutex semaphore\n");
  }
  no_proc_sem = sem_create("no_proc_sem",0);
  if (no_proc_sem == NULL) {
    panic("could not create no_proc_sem semaphore\n");
  }
#endif // UW 

 if (list_lock == NULL) {
    list_lock = lock_create("list_lock");
    if (list_lock == NULL) {
      panic("process_bootstrap: lock_create failed\n");
    }
  }



//init the process list
  struct process_list_entry* temp = kmalloc(sizeof(struct process_list_entry));
  if (temp == NULL) {
    kfree(temp);
    panic("process_bootstrap: Out of memory\n");
  } 
  
  temp->wait_cv = cv_create("wait_cv");
  if (temp->wait_cv == NULL) {
    kfree(temp);
    panic("process_bootstrap: create_cv failed\n");
  }
  
  //This is a dummy it should point to the kernel process but doesn't because no one should ever need
  //access the kernel process pid this way. 
   temp->pid = 101;
   temp->parent_pid = 100;
   temp->status = PROCESS_RUNNING;
   temp->process_ptr = NULL;
   temp->exitcode = EXIT_SUCCESS;
   temp->process_ptr = kproc;
   temp->next = NULL;	
   kproc->list_ptr = temp;
   kproc->p_pid = 101;
   
   process_list_head = temp; 
   process_list_tail = temp;

}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *proc;
	char *console_path;

	proc = proc_create(name);
	if (proc == NULL) {
		return NULL;
	}

#ifdef UW
	/* open the console - this should always succeed */
	console_path = kstrdup("con:");
	if (console_path == NULL) {
	  panic("unable to copy console path name during process creation\n");
	}
	if (vfs_open(console_path,O_WRONLY,0,&(proc->console))) {
	  panic("unable to open the console during process creation\n");
	}
	kfree(console_path);
#endif // UW
	  
	/* VM fields */

	proc->p_addrspace = NULL;

	/* VFS fields */

#ifdef UW
	/* we do not need to acquire the p_lock here, the running thread should
           have the only reference to this process */
        /* also, acquiring the p_lock is problematic because VOP_INCREF may block */
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		proc->p_cwd = curproc->p_cwd;
	}
#else // UW
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		proc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);
#endif // UW

#ifdef UW
	/* increment the count of processes */
        /* we are assuming that all procs, including those created by fork(),
           are created using a call to proc_create_runprogram  */
	P(proc_count_mutex); 
	proc_count++;
	V(proc_count_mutex);
#endif // UW

struct process_list_entry* temp = kmalloc(sizeof(struct process_list_entry));
  if (temp == NULL) {
    kfree(proc->p_name);
    kfree(proc);
    return NULL;;
  }
  temp->wait_cv = cv_create("wait_cv");
  if (temp->wait_cv == NULL) {
    kfree(temp);
    kfree(proc->p_name);
    kfree(proc);
    return NULL;;
  }
  lock_acquire(list_lock);
  temp->pid = process_list_tail->pid+1;
  temp->parent_pid = curproc->p_pid;
  temp->status = PROCESS_RUNNING;
  temp->exitcode = EXIT_SUCCESS;
  temp->process_ptr = proc;
  temp->next = NULL;
  process_list_tail->next = temp;
  process_list_tail = process_list_tail->next;
  proc->list_ptr = temp;
  proc->p_pid = temp->pid;
  lock_release(list_lock);
	return proc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int result;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	result = threadarray_add(&proc->p_threads, t, NULL);
	spinlock_release(&proc->p_lock);
	if (result) {
		return result;
	}
	t->t_proc = proc;
	return 0;
}



/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	unsigned i, num;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	/* ugh: find the thread in the array */
	num = threadarray_num(&proc->p_threads);
	for (i=0; i<num; i++) {
		if (threadarray_get(&proc->p_threads, i) == t) {
			threadarray_remove(&proc->p_threads, i);
			spinlock_release(&proc->p_lock);
			t->t_proc = NULL;
			return;
		}
	}
	/* Did not find it. */
	spinlock_release(&proc->p_lock);
	panic("Thread (%p) has escaped from its process (%p)\n", t, proc);
}

void
proc_remallthreads(struct proc *p)
{
	struct proc *proc;
	unsigned i, num;

	proc = p;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	/* ugh: find the thread in the array */
	num = threadarray_num(&proc->p_threads);
	for (i=0; i<num; i++) {
			threadarray_remove(&proc->p_threads, i);
			spinlock_release(&proc->p_lock);
			return;
	}
	spinlock_release(&proc->p_lock);
}


/*
 * Fetch the address space of the current process. Caution: it isn't
 * refcounted. If you implement multithreaded processes, make sure to
 * set up a refcount scheme or some other method to make this safe.
 */
struct addrspace *
curproc_getas(void)
{
	struct addrspace *as;
#ifdef UW
        /* Until user processes are created, threads used in testing 
         * (i.e., kernel threads) have no process or address space.
         */
	if (curproc == NULL) {
		return NULL;
	}
#endif

	spinlock_acquire(&curproc->p_lock);
	as = curproc->p_addrspace;
	spinlock_release(&curproc->p_lock);
	return as;
}

/*
 * Change the address space of the current process, and return the old
 * one.
 */
struct addrspace *
curproc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}

void proc_clear(struct proc *proc)
{
  proc_cleanup(proc);

  lock_release(proc->list_ptr->p_exit_lock);
}

void proc_cleanup(struct proc * proc) {

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		struct addrspace *as;

		as_deactivate();
		as = curproc_setas(NULL);
		as_destroy(as);
	}

  if (proc->p_prog != NULL) {
    vfs_close(proc->p_prog);
    proc->p_prog = NULL;
  }

}

void process_exit()
{
	// parent process has not exited
  lock_acquire(curproc->list_ptr->p_exit_lock);
  
  
  //assure that the parent of this porcess has not exited   
  if (!curproc->list_ptr->p_parent_exited) {
    //curproc->list_ptr->exitcode = 
    cv_broadcast(curproc->list_ptr->p_exit_cv, curproc->list_ptr->p_exit_lock);
    proc_signal_children(curproc);
    proc_clear(curproc);
  }
  // parent process has already exited
  else {
    proc_signal_children(curproc);
    lock_release(curproc->list_ptr->p_exit_lock);
    proc_destroy(curproc);
  }

  // exit kernel thread
  thread_exit();

  // Something went wrong very very wrong!!!!!!
  panic("process_exit: couldn't exit process\n");
	
	
}

void proc_signal_children(struct proc* proc)
{	
	lock_acquire(proc->p_children_lock);
	struct proc * child  = get_child_proc(proc);
    
    lock_acquire(child->list_ptr->p_exit_lock);
    if (child->list_ptr->p_parent_exited) {
      lock_release(child->list_ptr->p_exit_lock);
      proc_destroy(child);
    } else {
      child->list_ptr->p_parent_exited = true;
      lock_release(child->list_ptr->p_exit_lock);
    }
  

  lock_release(proc->p_children_lock);
}


struct proc * get_child_proc(struct proc * proc) {
  if (proc == NULL) {
    return NULL;
  }

  lock_acquire(proc->p_children_lock);

  if(proc->list_ptr->next){
		return proc->list_ptr->next->process_ptr;
		}
		
  lock_release(proc->p_children_lock);

  return NULL;
}

//not done! not done! not done! not done! not done! not done! not done! not done! 
//static void process_switch(struct proc processToSwitch ,int newstate, struct wchan *wc)
//{
/*
1. Change protection domain (user to supervisor[kernel]).
2. Change stacks: switch from using the user-level stack to
using a kernel stack.
3. Save execution state (on kernel’s stack).
4. Do kernel stuff
5. Kernel thread switch
6. Restore user-level execution state
7. Change protection domain (from supervisor[kernel] to user) 
*/
//	struct proc *cur, *next;
//	int spl;
//
//	DEBUGASSERT(curcpu->c_curthread == curthread);
//	DEBUGASSERT(curthread->t_cpu == curcpu->c_self);
//
//	/* Explicitly disable interrupts on this processor */
//	spl = splhigh();
//
//	cur = curproc;
//
//	/*
//	 * If we're idle, return without doing anything. This happens
//	 * when the timer interrupt interrupts the idle loop.
//	 */
//	if (curcpu->c_isidle) {
//		splx(spl);
//		return;
//	}//
//
//	/* Check the stack guard band. */
//	thread_checkstack(cur);
//
//	/* Lock the run queue. */
//	spinlock_acquire(&curcpu->c_runqueue_lock);
//
//	/* Micro-optimization: if nothing to do, just return */
//	if (newstate == S_READY && threadlist_isempty(&curcpu->c_runqueue)) {
//		spinlock_release(&curcpu->c_runqueue_lock);
//		splx(spl);
//		return;
//	}
//
//	/* Put the thread in the right place. */
//	switch (newstate) {
//	    case S_RUN:
//		panic("Illegal S_RUN in thread_switch\n");//
//	    case S_READY://
//		thread_make_runnable(cur, true /*have lock*/);
//		break;
//	    case S_SLEEP:
//		cur->t_wchan_name = wc->wc_name;
		/*
		 * Add the thread to the list in the wait channel, and
		 * unlock same. To avoid a race with someone else
		 * calling wchan_wake*, we must keep the wchan locked
		 * from the point the caller of wchan_sleep locked it
		 * until the thread is on the list.
		 *
		 * (We could for symmetry relock the channel before
		 * returning from wchan_sleep, but we don't, for two
		 * reasons. One is that the caller is unlikely to need
		 * or want it locked and if it does can lock it itself
		 * without racing. Exercise: what's the other?)
		 */
//		threadlist_addtail(&wc->wc_threads, cur);
//		wchan_unlock(wc);
//		break;
//	    case S_ZOMBIE:
//		cur->t_wchan_name = "ZOMBIE";
//		threadlist_addtail(&curcpu->c_zombies, cur);//
//		break;
//	}
//	cur->t_state = newstate;

	/*
	 * Get the next thread. While there isn't one, call md_idle().
	 * curcpu->c_isidle must be true when md_idle is
	 * called. Unlock the runqueue while idling too, to make sure
	 * things can be added to it.
	 *
	 * Note that we don't need to unlock the runqueue atomically
	 * with idling; becoming unidle requires receiving an
	 * interrupt (either a hardware interrupt or an interprocessor
	 * interrupt from another cpu posting a wakeup) and idling
	 * *is* atomic with respect to re-enabling interrupts.
	 *
	 * Note that c_isidle becomes true briefly even if we don't go
	 * idle. However, because one is supposed to hold the runqueue
	 * lock to look at it, this should not be visible or matter.
	 */

	/* The current cpu is now idle. */
//	curcpu->c_isidle = true;
//	do {
//		next = threadlist_remhead(&curcpu->c_runqueue);
//		if (next == NULL) {
//			spinlock_release(&curcpu->c_runqueue_lock);
//			cpu_idle();
//			spinlock_acquire(&curcpu->c_runqueue_lock);
//		}
//	} while (next == NULL);
//	curcpu->c_isidle = false;

	/*
	 * Note that curcpu->c_curthread may be the same variable as
	 * curthread and it may not be, depending on how curthread and
	 * curcpu are defined by the MD code. We'll assign both and
	 * assume the compiler will optimize one away if they're the
	 * same.
	 */
//	curcpu->c_curthread = next;
//	curthread = next;

	/* do the switch (in assembler in switch.S) */
//	switchframe_switch(&cur->t_context, &next->t_context);

	/*
	 * When we get to this point we are either running in the next
	 * thread, or have come back to the same thread again,
	 * depending on how you look at it. That is,
	 * switchframe_switch returns immediately in another thread
	 * context, which in general will be executing here with a
	 * different stack and different values in the local
	 * variables. (Although new threads go to thread_startup
	 * instead.) But, later on when the processor, or some
	 * processor, comes back to the previous thread, it's also
	 * executing here with the *same* value in the local
	 * variables.
	 *
	 * The upshot, however, is as follows:
	 *
	 *    - The thread now currently running is "cur", not "next",
	 *      because when we return from switchrame_switch on the
	 *      same stack, we're back to the thread that
	 *      switchframe_switch call switched away from, which is
	 *      "cur".
	 *
	 *    - "cur" is _not_ the thread that just *called*
	 *      switchframe_switch.
	 *
	 *    - If newstate is S_ZOMB we never get back here in that
	 *      context at all.
	 *
	 *    - If the thread just chosen to run ("next") was a new
	 *      thread, we don't get to this code again until
	 *      *another* context switch happens, because when new
	 *      threads return from switchframe_switch they teleport
	 *      to thread_startup.
	 *
	 *    - At this point the thread whose stack we're now on may
	 *      have been migrated to another cpu since it last ran.
	 *
	 * The above is inherently confusing and will probably take a
	 * while to get used to.
	 *
	 * However, the important part is that code placed here, after
	 * the call to switchframe_switch, does not necessarily run on
	 * every context switch. Thus any such code must be either
	 * skippable on some switches or also called from
	 * thread_startup.
	 */


	/* Clear the wait channel and set the thread state. */
//	cur->t_wchan_name = NULL;
//	cur->t_state = S_RUN;

	/* Unlock the run queue. */
//	spinlock_release(&curcpu->c_runqueue_lock);

//	/* Activate our address space in the MMU. */
//	as_activate();
//
	/* Clean up dead threads. */
//	exorcise();

	/* Turn interrupts back on. */
//	splx(spl);
//}

#if OPT_A3
pid_t
get_next_pid(void) {
    return next_pid++;
}
#endif
