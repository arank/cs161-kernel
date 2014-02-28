#include <types.h>
#include <limits.h>
#include <fs_calls.h>
#include <syscall.h>
#include <fd.h>
#include <current.h>

int sys_open(const_userptr_t filename , int flags){
    char *path;
    struct vnode *node;
    // TODO what should max copyin size be
    if(copyin(filename, path, 200)!=0||path==NULL){
    	goto out;
    }
    // path and flags checked in vfs_open and fd_init
    // TODO what mode_t should I use currently null
    if(vfs_open(path, flags, NULL, node)!=0||node==NULL){
    	goto path_out;
    }

    // Lock the process and check that we haven't exceeded the number of open files
    spinlock_acquire(curproc->p_lock);
    int i;
    for (i = 0; i < OPEN_MAX; i++){
    	if(curproc->fd_table[i]==NULL)
    		break;
    }
    // We looped around, max number of files are open
    if(i==OPEN_MAX){
    	goto lock_out;
    }
    // TODO what mode_t should I use currently null
    struct file_desc *fd = fd_init(node, NULL, flags);
    if(fd==NULL){
    	goto lock_out;
    }
    curproc->fd_table[i]=fd;
    // Unlock after it is set
    spinlock_release(curproc->p_lock);
    return i;

lock_out:
    spinlock_release(curproc->p_lock);
node_out:
	kfree(node);
path_out:
	kfree(path);
out:
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
