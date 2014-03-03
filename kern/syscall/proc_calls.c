#include <types.h>
#include <proc.h>
#include <syscall.h>
#include <synch.h>
#include <kern/errno.h>
#include <current.h>
#include <limits.h>

pid_t sys_waitpid(pid_t pid, userptr_t status, int options) {
    (void)pid;
    (void)status;
    (void)options;

    return 0;
}

pid_t sys_getpid(){
	return curproc->pid;
}


void sys__exit(int exitcode) {
	if(curproc->parent != NULL){
		curproc->parent->exit_code=exitcode;
		// TODO Special case for parent is -1 define elsewhere
		shared_link_destroy(-1);
	}
	cleanup_data(curproc);
	thread_exit();
}

