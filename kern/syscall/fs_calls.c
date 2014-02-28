#include <types.h>
#include <limits.h>
#include <syscall.h>
#include <fd.h>
#include <current.h>
#include <synch.h>
#include <kern/errno.h>
#include <current.h>
#include <proc.h>
#include <vfs.h>
#include <copyinout.h>

int sys_open(const_userptr_t filename , int flags, mode_t mode, int *file_desc_pos){
    char *path;
    struct vnode *node;
    int err;
    // TODO what should the size be, is this right?
    err=copyin(filename, path, sizeof(char*));
    if(err!=0||path==NULL){
    	goto out;
    }
    // path and flags checked in vfs_open and fd_init
    // TODO should we be derefing node?
    err=vfs_open(path, flags, mode, &node);
    if(err!=0||node==NULL){
    	goto path_out;
    }

    // Don't lock the process and check that we haven't exceeded the number of open files
    // as this is single threaded
    int i;
    for (i = 0; i < OPEN_MAX; i++){
    	if(curproc->fd_table[i]==NULL)
    		break;
    }
    // We looped around, max number of files are open
    if(i==OPEN_MAX){
    	err = EMFILE;
    	goto node_out;
    }
    struct file_desc *fd = fd_init(node, mode, flags);
    if(fd==NULL){
    	err=ENOMEM;
    	goto node_out;
    }
    curproc->fd_table[i]=fd;
    *file_desc_pos = i;
    return 0;

node_out:
	kfree(node);
path_out:
	kfree(path);
out:
	return err;
}

ssize_t sys_read(int fd, userptr_t buf, size_t buflen, ssize_t *bread) {

	(void)fd;
    (void)buf;
    (void)buflen;
    (void)bread;
    return 0;
}

/**
 *
 * EBADF    fd is not a valid file descriptor, or was not opened for writing.
 * EFAULT   Part or all of the address space pointed to by buf is invalid.
 * ENOSPC   There is no free space remaining on the filesystem containing the file.
 * EIO      A hardware I/O error occurred writing the data.
 */
ssize_t sys_write(int fd, const_userptr_t buf, size_t nbytes, ssize_t *bwritten) {
    (void)fd;
    (void)buf;
    (void)nbytes;
    (void)bwritten;
    return 0;
}

off_t sys_lseek (int fd, off_t pos, int whence) {
    (void)fd;
    (void)pos;
    (void)whence;
    return 0;
}

/**
 * sys_close(fd) - closes file by getting the lock on the fd and decrementing 
 * the reference counter; if the counter is 0, release and free the lock,
 * iterate over other fds of the process and set to NULL those that point to
 * this file_desc structure, free fd_table entry and set it to NULL to indicate
 * that the index is free to be reused.
 *
 * return 0 on success (either closed the file or just decremented the refcnt)
 *  EBADF   fd is not within the legal range or wasn't open
 *  EIO     hard IO error occured
 */
int sys_close(int fd) {
    if (fd < 0 || fd > OPEN_MAX) return EBADF;
    if (curproc->fd_table[fd] == NULL) return EBADF; 

    lock_acquire(curproc->fd_table[fd]->lock);  
    if (--curproc->fd_table[fd]->ref_count != 0)    
        lock_release(curproc->fd_table[fd]->lock);  
    else {                                          
        vfs_close(curproc->fd_table[fd]->vn);
        lock_release(curproc->fd_table[fd]->lock);
        lock_destroy(curproc->fd_table[fd]->lock);
        kfree(curproc->fd_table[fd]);
    }

    curproc->fd_table[fd] = NULL;
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
