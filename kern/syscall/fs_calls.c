#include <types.h>
#include <fs_calls.h>
#include <syscall.h>

int sys_open(const_userptr_t filename , int flags){
    (void)filename;
    (void)flags;

    return 0;
}

ssize_t sys_read(int fd , userptr_t buf , size_t buflen) {
    (void)fd;
    (void)buf;
    (void)buflen;

    return 0;
}

ssize_t sys_write(int fd , const_userptr_t buf , size_t nbytes) {
    (void)fd;
    (void)buf;
    (void)nbytes;
    return 0;
}

off_t sys_lseek ( int fd , off_t pos , int whence) {
    (void)fd;
    (void)pos;
    (void)whence;
    return 0;
}

int sys_close(int fd) {
    (void)fd;
    return 0;
}

int sys_dup2(int oldfd , int newfd) {
    (void)oldfd;
    (void)newfd;
    return 0;
}

int sys_chdir (const_userptr_t pathname) {
    (void)pathname;
    return 0;
}

int sys___getcwd(userptr_t buf , size_t buflen) {
    (void)buf;
    (void)buflen;
    return 0;
}
