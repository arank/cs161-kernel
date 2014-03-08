/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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

#include <types.h>
#include <proc.h>
#include <syscall.h>
#include <synch.h>
#include <kern/errno.h>
#include <current.h>
#include <limits.h>
#include <pid_table.h>
#include <copyinout.h>
#include <kern/wait.h>

pid_t sys_waitpid(pid_t pid, userptr_t status, int options) {
    int err;
    if(options != 0){
    	err = EINVAL;
    	goto out;
    }

    if(!pid_in_use(pid)){
    	err = ESRCH;
    	goto out;
    }

    if ((int)status % ALIGN != 0) {
        err = EFAULT;
        goto out;
    }

    int i;
    struct proc_link *shared;
    for(i = 0; i < MAX_CLD; i++){
    	shared = curproc->children[i];
    	if(shared!=NULL && shared->child_pid==pid){
    		break;
    	}
    }
    if(i==MAX_CLD){
    	err = ECHILD;
    	goto out;
    }
    lock_acquire(shared->lock);
    // Handle child exits after parent
    if(shared->ref_count==2){
    	while(shared->ref_count!=1){
    		cv_wait(shared->cv, shared->lock);
    	}
    }
    lock_release(shared->lock);

    // Handle child has already exited
    if (status == NULL) return 0;

    if(copyout(&shared->exit_code, (userptr_t)status, sizeof(userptr_t))) {
        err = EFAULT;
        goto out;
    }

    return 0;

out:
	return err;
}

pid_t sys_getpid(pid_t *pid){
	*pid = curproc->pid;
    return 0;
}

void sys__exit(int exitcode) {
	struct proc *proc = curproc;
	// If there is a parent, set the exit code in the parent
	if (proc->parent != NULL) {
		proc->parent->exit_code = (exitcode == -1) 
                                    ? _MKWAIT_SIG(exitcode) 
                                    : _MKWAIT_EXIT(exitcode);
		shared_link_destroy(PARENT, proc);
	}
	// Add thread to the kernel
	proc_remthread(curthread);
	proc_addthread(kproc, curthread);
    /* calls cleanup_data, which calls shared_link_destroy to destroy children */
	proc_destroy(proc); 
	thread_exit();
}

