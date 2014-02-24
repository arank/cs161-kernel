#include <types.h>
#include <fs_calls.h>

int open(const char *filename , int flags){ 
    (void)filename;
    (void)flags;

    return 0;
}

ssize_t read(int fd , void *buf , size_t buflen) {
    (void)fd;
    (void)buf;
    (void)buflen;

    return 0;
}

ssize_t write(int fd , const void *buf , size_t nbytes) {
    (void)fd;
    (void)buf;
    (void)nbytes;
    return 0;
}

off_t lseek ( int fd , off_t pos , int whence) {
    (void)fd;
    (void)pos;
    (void)whence;
    return 0;
}

int close(int fd) {
    (void)fd;
    return 0;
}

int dup2(int oldfd , int newfd) {
    (void)oldfd;
    (void)newfd;
    return 0;
}

int chdir (const char *pathname) {
    (void)pathname;
    return 0;
}

int __getcwd(char *buf , size_t buflen) {
    (void)buf;
    (void)buflen;
    return 0;
}
