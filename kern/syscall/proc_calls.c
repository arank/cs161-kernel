#include <types.h>
#include <proc_calls.h>
#include <syscall.h>


pid_t sys_waitpid(pid_t pid, userptr_t status, int options) {
    (void)pid;
    (void)status;
    (void)options;
    return 0;
}

void sys__exit(int exitcode) {
    (void)exitcode;
    return;
}

