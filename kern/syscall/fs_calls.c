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
#include <kern/seek.h>
#include <kern/stat.h>
#include <kern/fcntl.h>

static 
int 
filetable_findfile(int fd, struct file_desc **file) {
    if (fd >= OPEN_MAX) return EBADF;

    *file = curproc->fd_table[fd]; 
    if (*file == NULL) return EBADF;

    return 0;
}

int sys_open(const_userptr_t filename, int flags, mode_t mode, int *file_desc_pos) {
    // Could walk off the end of the user world
	char path[PATH_MAX];
    struct vnode *node;
    int err;
    err = copyinstr(filename, path, sizeof path, NULL);
    if (err != 0) goto out;
    
    // path and flags checked in vfs_open and fd_init
    // TODO should we be derefing node?
    err = vfs_open(path, flags, mode, &node);
    if (err != 0 || node == NULL) goto out;

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
out:
	return err;
}

ssize_t sys_read(int fd, userptr_t buf, size_t buflen, ssize_t *bread) {
    if (fd < 0 || fd >= OPEN_MAX || !curproc->fd_table[fd]) return EBADF;

	struct file_desc *fd_ptr = curproc->fd_table[fd];
	lock_acquire(fd_ptr->lock);

	struct iovec vec;
	vec.iov_ubase = buf;
	vec.iov_len = buflen;

	struct uio io;
	io.uio_iov = &vec;
	io.uio_iovcnt = 1;
	io.uio_segflg = UIO_USERSPACE;
	io.uio_offset = fd_ptr->offset;
	io.uio_resid = buflen;
	io.uio_rw = UIO_READ;
	io.uio_space = curproc->p_addrspace;

	int err = VOP_READ(fd_ptr->vn, &io);
	if (err) { 
        lock_release(fd_ptr->lock);
        return EFAULT;
    } 
    
	// set offset to new value got by delta in offset
	*bread = io.uio_offset - fd_ptr->offset;
	fd_ptr->offset = io.uio_offset;
	lock_release(fd_ptr->lock);

    return 0;
}

/**
 * EBADF    fd is not a valid file descriptor, or was not opened for writing.
 * EFAULT   Part or all of the address space pointed to by buf is invalid.
 * ENOSPC   There is no free space remaining on the filesystem containing the file.
 * EIO      doesn't return this (mentioned in manpages)
 */
ssize_t sys_write(int fd, const_userptr_t buf, size_t nbytes, ssize_t *bwritten) {
    if (fd < 0 || fd >= OPEN_MAX || !curproc->fd_table[fd]) return EBADF;
     
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
	curproc->fd_table[fd]->offset = uio.uio_offset;
    lock_release(curproc->fd_table[fd]->lock);

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
    if (fd < 0 || fd >= OPEN_MAX || !curproc->fd_table[fd]) return EBADF;

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

off_t sys_lseek (int fd, off_t pos, int whence, off_t *ret_pos) {
    if (fd < 0 || fd >= OPEN_MAX || !curproc->fd_table[fd]) return EBADF;

    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) 
        return EINVAL;

    lock_acquire(curproc->fd_table[fd]->lock);

    struct stat stat;
    if (VOP_STAT(curproc->fd_table[fd]->vn, &stat)) {/* get the end of file */
        lock_release(curproc->fd_table[fd]->lock);
        return ESPIPE; 
    }

    off_t new_pos;
    if (whence == SEEK_SET) new_pos = pos;
    else if (whence == SEEK_CUR) new_pos = curproc->fd_table[fd]->offset + pos;
    else new_pos = stat.st_size + pos;
    
    if (new_pos < 0) {
        lock_release(curproc->fd_table[fd]->lock);
        return EINVAL;
    }

    if (VOP_TRYSEEK(curproc->fd_table[fd]->vn, new_pos)) {
        lock_release(curproc->fd_table[fd]->lock);
        return ESPIPE;
    }

    curproc->fd_table[fd]->offset = new_pos;
    *ret_pos = curproc->fd_table[fd]->offset;
    lock_release(curproc->fd_table[fd]->lock);

    return 0;
}

int sys_dup2(int oldfd , int newfd, int *retval) {
	if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX 
                || curproc->fd_table[oldfd] == NULL ) return EBADF;
    
    if (oldfd == newfd || curproc->fd_table[oldfd] == curproc->fd_table[newfd]) {
        *retval = newfd;
        return 0;
    }

	if(curproc->fd_table[newfd] != NULL)
        sys_close(newfd);   /* OK not to check for return value */

    KASSERT(curproc->fd_table[newfd] == NULL);
	curproc->fd_table[newfd] = curproc->fd_table[oldfd];

	lock_acquire(curproc->fd_table[newfd]->lock);
    curproc->fd_table[newfd]->ref_count++;
	lock_release(curproc->fd_table[newfd]->lock);

	*retval = newfd;

    return 0;
}

int sys_chdir (const_userptr_t pathname) {
	char path[PATH_MAX];

    int err = copyinstr(pathname, path, sizeof path, NULL);
    if (err != 0 || path == NULL) return EFAULT;

    err = vfs_chdir(path);
    if (err != 0) return err;

    return 0;
}

int sys___getcwd(userptr_t buf, size_t buflen, int *bwritten) {
	int err;
    char path[PATH_MAX];
	struct uio kio;
	struct iovec iov;
	uio_kinit(&iov, &kio, path, sizeof path, 0, UIO_READ);

	err = vfs_getcwd(&kio);
	if (err) return ENOENT;

    err = copyout((const char *)path, buf, buflen);
    if (err) return EFAULT;

	*bwritten = kio.uio_offset;
    return 0;
}

/*
 * sync - call vfs_sync
 */
int
sys_sync(void)
{
	int err;

	err = vfs_sync();
	if (err==EIO) {
		/* This is the only likely failure case */
		kprintf("Warning: I/O error during sync\n");
	}
	else if (err) {
		kprintf("Warning: sync: %s\n", strerror(err));
	}
	/* always succeed */
	return 0;
}

/*
 * mkdir - call vfs_mkdir
 */
int
sys_mkdir(userptr_t path, mode_t mode)
{
	char pathbuf[PATH_MAX];
	int err;

	(void) mode;

	err = copyinstr(path, pathbuf, sizeof(pathbuf), NULL);
	if (err) {
		return err;
	}
	else {
		return vfs_mkdir(pathbuf, mode);
	}
}

/*
 * rmdir - call vfs_rmdir
 */
int
sys_rmdir(userptr_t path)
{
	char pathbuf[PATH_MAX];
	int err;

	err = copyinstr(path, pathbuf, sizeof(pathbuf), NULL);
	if (err) {
		return err;
	}
	else {
		return vfs_rmdir(pathbuf);
	}
}

/*
 * remove - call vfs_remove
 */
int
sys_remove(userptr_t path)
{
	char pathbuf[PATH_MAX];
	int err;

	err = copyinstr(path, pathbuf, sizeof(pathbuf), NULL);
	if (err) {
		return err;
	}
	else {
		return vfs_remove(pathbuf);
	}
}

/*
 * link - call vfs_link
 */
int
sys_link(userptr_t oldpath, userptr_t newpath)
{
	char *oldbuf;
	char *newbuf;
	int err;

	oldbuf = kmalloc(PATH_MAX);
	if (oldbuf == NULL) {
		return ENOMEM;
	}

	newbuf = kmalloc(PATH_MAX);
	if (newbuf == NULL) {
		kfree(oldbuf);
		return ENOMEM;
	}

	err = copyinstr(oldpath, oldbuf, PATH_MAX, NULL);
	if (err) {
		goto fail;
	}

	err = copyinstr(newpath, newbuf, PATH_MAX, NULL);
	if (err) {
		goto fail;
	}

	err = vfs_link(oldbuf, newbuf);
 fail:
	kfree(newbuf);
	kfree(oldbuf);
	return err;
}

/*
 * rename - call vfs_rename
 */
int
sys_rename(userptr_t oldpath, userptr_t newpath)
{
	char *oldbuf;
	char *newbuf;
	int err;

	oldbuf = kmalloc(PATH_MAX);
	if (oldbuf == NULL) {
		return ENOMEM;
	}

	newbuf = kmalloc(PATH_MAX);
	if (newbuf == NULL) {
		kfree(oldbuf);
		return ENOMEM;
	}

	err = copyinstr(oldpath, oldbuf, PATH_MAX, NULL);
	if (err) {
		goto fail;
	}

	err = copyinstr(newpath, newbuf, PATH_MAX, NULL);
	if (err) {
		goto fail;
	}

	err = vfs_rename(oldbuf, newbuf);
 fail:
	kfree(newbuf);
	kfree(oldbuf);
	return err;
}

/*
 * getdirentry - call VOP_GETDIRENTRY
 */
int
sys_getdirentry(int fd, userptr_t buf, size_t buflen, int *retval)
{
	struct iovec iov;
	struct uio useruio;
	struct file_desc *file;
	int err;

	/* better be a valid file descriptor */

	err = filetable_findfile(fd, &file);
	if (err) {
		return err;
	}

	lock_acquire(file->lock);

	/* Dirs shouldn't be openable for write at all, but be safe... */
	if (file->mode == O_WRONLY) {
		lock_release(file->lock);
		return EBADF;
	}

	/* set up a uio with the buffer, its size, and the current offset */
	mk_useruio(&iov, &useruio, buf, buflen, file->offset, UIO_READ);

	/* does the read */
	err = VOP_GETDIRENTRY(file->vn, &useruio);
	if (err) {
		lock_release(file->lock);
		return err;
	}

	/* set the offset to the updated offset in the uio */
	file->offset = useruio.uio_offset;

	lock_release(file->lock);

	/*
	 * the amount read is the size of the buffer originally, minus
	 * how much is left in it. Note: it is not correct to use
	 * uio_offset for this!
	 */
	*retval = buflen - useruio.uio_resid;

	return 0;
}

/*
 * fstat - call VOP_FSTAT
 */
int
sys_fstat(int fd, userptr_t statptr)
{
	struct stat kbuf;
	struct file_desc *file;
	int err;

	err = filetable_findfile(fd, &file);
	if (err) {
		return err;
	}

	/*
	 * No need to lock the openfile - it cannot disappear under us,
	 * and we're not using any of its non-constant fields.
	 */

	err = VOP_STAT(file->vn, &kbuf);
	if (err) {
		return err;
	}

	return copyout(&kbuf, statptr, sizeof(struct stat));
}

/*
 * fsync - call VOP_FSYNC
 */
int
sys_fsync(int fd)
{
	struct file_desc *file;
	int err;

	err = filetable_findfile(fd, &file);
	if (err) {
		return err;
	}

	/*
	 * No need to lock the openfile - it cannot disappear under us,
	 * and we're not using any of its non-constant fields.
	 */

	return VOP_FSYNC(file->vn);
}

/*
 * ftruncate - call VOP_TRUNCATE
 */
int
sys_ftruncate(int fd, off_t len)
{
	struct file_desc *file;
	int err;

	if (len < 0) {
		return EINVAL;
	}

	err = filetable_findfile(fd, &file);
	if (err) {
		return err;
	}

	/*
	 * No need to lock the openfile - it cannot disappear under us,
	 * and we're not using any of its non-constant fields.
	 */

	return VOP_TRUNCATE(file->vn, len);
}
