#include <types.h>
#include <proc.h>
#include <syscall.h>
#include <synch.h>
#include <kern/errno.h>
#include <current.h>
#include <limits.h>
#include <pid_table.h>

pid_t sys_waitpid(pid_t pid, userptr_t status, int options) {
    int err;
    if(options != 0){
    	err = EINVAL;
    	goto out;
    }
    if(pid_in_use(pid)){
    	err = ESRCH;
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
    if(status != NULL)
    	*(int*)status = shared->exit_code;

    return 0;

out:
	return err;
}

pid_t sys_getpid(){
	return curproc->pid;
}


void sys__exit(int exitcode) {
	struct proc *proc = curproc;
	// If there is a parent, set the exit code in the parent
	if(proc->parent != NULL){
		proc->parent->exit_code=exitcode;
		shared_link_destroy(PARENT, proc);
	}
	// TODO is it safe to free pid here
	pid_destroy(proc->pid);
	// Add thread to the kernel
	proc_remthread(curthread);
	proc_addthread(kproc, curthread);
	// Destroy the old process
	proc_destroy(proc);
	thread_exit();
}

