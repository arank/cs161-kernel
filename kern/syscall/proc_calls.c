#include <types.h>
#include <proc_calls.h>

int __getcwd(char *buf, size_t buflen) {
    (void)buf;
    (void)buflen;
    return 0;
}

pid_t waitpid(pid_t pid, int *status, int options) {
    (void)pid;
    (void)status;
    (void)options;
    return 0;
}

void _exit(int exitcode) {
    (void)exitcode;
    return;
}
