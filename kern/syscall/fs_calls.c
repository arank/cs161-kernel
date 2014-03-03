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
#include <uio.h>
#include <kern/iovec.h>
#include <vnode.h>
#include <lib.h>

int sys_open(const_userptr_t filename, int flags, mode_t mode, int *file_desc_pos) {
    // Could walk off the end of the user world
	char path[PATH_MAX];
    struct vnode *node;
    int err;
    err = copyinstr(filename, path, sizeof(*path), NULL);
    if (err != 0) goto out;
    
    // path and flags checked in vfs_open and fd_init
    // TODO should we be derefing node?
    err = vfs_open(path, flags, mode, &node);
    if (err != 0 || node == NULL) goto path_out;

    // Don't have to lock the process to check that we haven't exceeded the number of open files
    // as this is single threaded
    int i;
    for (i = 0; i < OPEN_MAX; i++) 
    	if(curproc->fd_table[i] == NULL) break;
    
    // We looped around, max number of files are open
    if (i == OPEN_MAX) {
    	err = EMFILE;
    	goto node_out;
    }

    struct file_desc *fd = fd_init(node, mode, flags);
    if (fd == NULL) {
    	err = ENOMEM;
    	goto node_out;
    }
    curproc->fd_table[i]=fd;
    *file_desc_pos = i;
    return 0;

// TODO translate err codes
node_out:
	kfree(node);
path_out:
	kfree(path);
out:
	return err;
}

ssize_t sys_read(int fd, userptr_t buf, size_t buflen, ssize_t *bread) {
	int err;
	struct iovec vec;
	vec.iov_ubase = buf;
	vec.iov_len = buflen;

	struct file_desc *fd_ptr=curproc->fd_table[fd];
	if(fd_ptr==NULL){
		err = EBADF;
		goto out;
	}

	lock_acquire(fd_ptr->lock);
	struct uio io;
	// TODO possibly check flags
	io.uio_iov = &vec;
	io.uio_iovcnt = 1;
	io.uio_offset = fd_ptr->offset;
	io.uio_resid = buflen;
	io.uio_segflg = UIO_USERSPACE;
	io.uio_rw = UIO_READ;
	io.uio_space = curproc->p_addrspace;
	err = VOP_READ(fd_ptr->vn, &io);
	if (err != 0) {
		goto lock_out;
	}
	// set offset to new value got by delta in offset
	*bread = io.uio_offset - fd_ptr->offset;
	fd_ptr->offset=io.uio_offset;
	lock_release(fd_ptr->lock);
    return 0;

// TODO translate err codes
lock_out:
	lock_release(fd_ptr->lock);
out:
	return err;
}

/**
 *
 * EBADF    fd is not a valid file descriptor, or was not opened for writing.
 * EFAULT   Part or all of the address space pointed to by buf is invalid.
 * ENOSPC   There is no free space remaining on the filesystem containing the file.
 * EIO      doesn't return this (mentioned in manpages)
 */
ssize_t sys_write(int fd, const_userptr_t buf, size_t nbytes, ssize_t *bwritten) {
    if (fd < 0 || fd > OPEN_MAX || !curproc->fd_table[fd]) return EBADF;
     
    lock_acquire(curproc->fd_table[fd]->lock);

    struct iovec iov;
    iov.iov_ubase = (userptr_t)buf;
    iov.iov_len = nbytes;

    struct uio uio;
    uio.uio_iov = &iov;
    uio.uio_iovcnt = 1;
    uio.uio_segflg = UIO_USERSPACE;
    uio.uio_offset = curproc->fd_table[fd]->offset;
    uio.uio_resid = nbytes;
    uio.uio_rw = UIO_WRITE; 
    uio.uio_space = curproc->p_addrspace;

    int rv = VOP_WRITE(curproc->fd_table[fd]->vn, &uio);
    if (rv == ENOSPC || rv == EIO || rv == EFAULT) { 
        lock_release(curproc->fd_table[fd]->lock);
        return rv;
    } 

    *bwritten = uio.uio_offset - curproc->fd_table[fd]->offset;
    lock_release(curproc->fd_table[fd]->lock);

    return 0;
}

off_t sys_lseek (int fd, off_t pos, int whence, off_t *ret_pos) {
    if (fd < 0 || fd > OPEN_MAX || !curproc->fd_table[fd]) return EBADF;
    (void)pos;
    (void)ret_pos;
    (void)whence;
#if 0
    
    if (whence != SEEK_SET || whence != SEEK_CUR || whence != SEEK_END) 
        return EINVAL;
    lock_acquire(curproc->fd_table[fd]->lock);

    struct stat stat;
    if (VOP_STAT(curproc->fd_table[fd]->vn, &stat)) /* get the end of file */
        return -1;  /* ASK David about VOP_STAT return value */

    int new_pos;
    if (whence == SEEK_SET) new_pos = pos;
    else if (whence == SEEK_CUR) new_pos = curproc->fd_table[fd]->offset + pos;
    else new_pos = stat.st_size + pos;

    int rv = VOP_TRYSEEK(curproc->fd_table[fd]->vn, pos);
    if (rv == ESPIPE) return rv;
#endif
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
 *  EIO     doesn't return (mentioned in manpages)
 */
int sys_close(int fd) {
    if (fd < 0 || fd > OPEN_MAX || !curproc->fd_table[fd]) return EBADF;

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

int sys_dup2(int oldfd , int newfd, int *retval) {
	int err;
	struct file_desc *old = curproc->fd_table[oldfd];
	if(oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX || old == NULL ){
		err = EBADF;
		goto out;
	}
	if(curproc->fd_table[newfd]!=NULL){
		fd_dec_or_destroy(newfd);
	}
	lock_acquire(old->lock);
	curproc->fd_table[newfd]=old;
	lock_release(old->lock);

	*retval=newfd;

    return 0;

out:
	return err;
}

int sys_chdir (const_userptr_t pathname) {
	char *path;
    int err;
    err=copyin(pathname, path, sizeof(char*));
    if(err!=0||path==NULL){
    	goto out;
    }
    err=vfs_chdir(path);
    if(err!=0){
    	goto path_out;
    }

    kfree(path);
    return 0;

// TODO translate error codes for vfs_chdir
path_out:
	kfree(path);
out:
	return err;
}

int sys___getcwd(userptr_t buf , size_t buflen, int *bwritten) {
	int err;
	struct iovec iov;
	iov.iov_ubase = (userptr_t)buf;
	iov.iov_len = buflen;

	struct uio io;
	io.uio_iov=&iov;
	io.uio_iovcnt=1;
	io.uio_offset=0;
	io.uio_resid=buflen;
	io.uio_segflg=UIO_USERSPACE;
	io.uio_rw=UIO_WRITE;
	io.uio_space=curproc->p_addrspace;

	err=vfs_getcwd(&io);
	if(err!=0){
		goto out;
	}
	*bwritten=io.uio_offset;
    return 0;

// TODO translate error codes
out:
	return err;
}
