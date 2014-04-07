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

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <pid_table.h>
#include <fd.h>
#include <synch.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <vfs.h>
#include <uio.h>
#include <fd.h>
#include <kern/fcntl.h>

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;
static void console_init(struct proc *proc);

/*
 * Create a proc structure.
 */
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	// memory is set to deadbeef after free, but we need to reset to NULL
    memset(proc, 0, sizeof *proc);

	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	threadarray_init(&proc->p_threads);
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = as_create();

	/* VFS fields */
	proc->p_cwd = NULL;

	if(curthread){
		proc->pid = pid_get();
        if (proc->pid == -1) {      /* ran out of pids */
            proc_destroy(proc);
            return NULL;
        }
	}else{
		proc->pid = 0;  /* kernel thread, 0 is set in pid_table in pid_init */
	}
	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
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

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	// TODO this design choice allows for memory waste, consider a proc
	// that forks 1M children and one exits, its shared struct won't be cleared
    cleanup_data(proc);
	threadarray_cleanup(&proc->p_threads);
	spinlock_cleanup(&proc->p_lock);

	kfree(proc->p_name);
	kfree(proc);
}


struct
proc_link *
shared_link_create(pid_t pid) {
    struct proc_link *link = kmalloc(sizeof *link);
    if (link == NULL) goto out;

    link->lock = lock_create("shared_lock");;
    if (link->lock == NULL) goto link_out;

    link->cv = cv_create("shared_cv");
    if (link->cv == NULL) goto cv_out;

    link->ref_count = 0;
    link->exit_code = -1;
    link->child_pid = pid;

    return link;

cv_out:
    lock_destroy(link->lock);
link_out:
    kfree(link);
out:
    return NULL;
}

void shared_link_destroy(int index, struct proc* proc) {
	if(-1 > index || index >= MAX_CLD){
		return;
	}

	struct proc_link *link = (index == PARENT)
                                ? proc->parent
                                : proc->children[index];
	if (link == NULL) return;

    lock_acquire(link->lock);
    if (index == PARENT)
    	cv_signal(link->cv, link->lock);

    // In this state of the world there is no parent
    if (link->ref_count == 1) {
        pid_destroy(link->child_pid);
        lock_release(link->lock);
        lock_destroy(link->lock);
        cv_destroy(link->cv);
        kfree(link);
        if (index == PARENT)
            proc->parent = NULL;
        else
            proc->children[index] = NULL;
    } else {
        link->ref_count--;
        /*
        for (int i = 0; i < MAX_CLD; i++)
            if (proc->children[i] != NULL)
                proc->children[i]->ref_count--;
        */
        lock_release(link->lock);
    }
}

// TODO: think once again where to do the clearing
void cleanup_data(struct proc *proc) {
    int i;
    for (i = 0; i < MAX_CLD; i++)
    	shared_link_destroy(i, proc);

    for (i = 0; i < OPEN_MAX; i++)
        fd_dec_or_destroy(i, proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	if (init_pid_table())
		panic("init_pid_table failed\n");

	kproc = proc_create("[kernel]");
    extern struct lock *exec_lock;
    exec_lock = lock_create("exec-lock");
    if (exec_lock == NULL)
        panic("exec-lock failed");

	if (kproc == NULL)
		panic("proc_create for kproc failed\n");
}

static void console_init(struct proc *proc) {
    char *con_read = kstrdup("con:");
    char *con_write = kstrdup("con:");
    char *con_error = kstrdup("con:");
    if (con_read == NULL || con_write == NULL || con_error == NULL)
        panic("proc init: could not connect to console\n");

    struct vnode *out;
    struct vnode *in;
    struct vnode *err;
    int rv1 = vfs_open(con_read, O_RDONLY, 0, &in);
    int rv2 = vfs_open(con_write, O_WRONLY, 0, &out);
    int rv3 = vfs_open(con_error, O_WRONLY, 0, &err);
    if (rv1 | rv2 | rv3)
        panic("proc init: could not connect to console\n");

    kfree(con_read);
    kfree(con_write);
    kfree(con_error);

    struct file_desc *stdin = kmalloc(sizeof *stdin);
    struct file_desc *stdout = kmalloc(sizeof *stdout);
    struct file_desc *stderr = kmalloc(sizeof *stderr);
    if (stdin == NULL || stdout == NULL || stderr == NULL)
        panic("proc init: out of memory\n");

    stdin->flags = O_RDONLY;
    stdin->ref_count = 1;
    stdin->offset = 0;
    stdin->vn = in;
    stdin->mode = 0;
    stdin->lock = lock_create("stdin");

    stdout->flags = O_WRONLY;
    stdout->ref_count = 1;
    stdout->offset = 0;
    stdout->vn = out;
    stdout->mode = 0;
    stdout->lock = lock_create("stdout");

    stderr->flags = O_WRONLY;
    stderr->ref_count = 1;
    stderr->offset = 0;
    stderr->vn = err;
    stderr->mode = 0;
    stderr->lock = lock_create("stderr");

    if (stdin->lock == NULL || stdout->lock == NULL || stderr->lock == NULL)
        panic("proc init: stdin, stdout, or stderr lock couldn't be allocated\n");

    proc->fd_table[STDIN_FILENO] = stdin;
    proc->fd_table[STDOUT_FILENO] = stdout;
    proc->fd_table[STDERR_FILENO] = stderr;
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

	proc = proc_create(name);
	if (proc == NULL) {
		return NULL;
	}

	/* VM fields */
	proc->p_addrspace = NULL;

    /* Console devices */
    console_init(proc);

	/* VFS fields */

	spinlock_acquire(&curproc->p_lock);
	/* we don't need to lock proc->p_lock as we have the only reference */
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		proc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);
	return proc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int result;
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	result = threadarray_add(&proc->p_threads, t, NULL);
	spinlock_release(&proc->p_lock);
	if (result) {
		return result;
	}
	spl = splhigh();
	t->t_proc = proc;
	splx(spl);
	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	unsigned i, num;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	/* ugh: find the thread in the array */
	num = threadarray_num(&proc->p_threads);
	for (i=0; i<num; i++) {
		if (threadarray_get(&proc->p_threads, i) == t) {
			threadarray_remove(&proc->p_threads, i);
			spinlock_release(&proc->p_lock);
			spl = splhigh();
			t->t_proc = NULL;
			splx(spl);
			return;
		}
	}
	/* Did not find it. */
	spinlock_release(&proc->p_lock);
	panic("Thread (%p) has escaped from its process (%p)\n", t, proc);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}
