
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
	struct openfile *file;
	int err;

	/* better be a valid file descriptor */

	err = filetable_findfile(fd, &file);
	if (err) {
		return err;
	}

	lock_acquire(file->of_lock);

	/* Dirs shouldn't be openable for write at all, but be safe... */
	if (file->of_accmode == O_WRONLY) {
		lock_release(file->of_lock);
		return EBADF;
	}

	/* set up a uio with the buffer, its size, and the current offset */
	mk_useruio(&iov, &useruio, buf, buflen, file->of_offset, UIO_READ);

	/* does the read */
	err = VOP_GETDIRENTRY(file->of_vnode, &useruio);
	if (err) {
		lock_release(file->of_lock);
		return err;
	}

	/* set the offset to the updated offset in the uio */
	file->of_offset = useruio.uio_offset;

	lock_release(file->of_lock);

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
	struct openfile *file;
	int err;

	err = filetable_findfile(fd, &file);
	if (err) {
		return err;
	}

	/*
	 * No need to lock the openfile - it cannot disappear under us,
	 * and we're not using any of its non-constant fields.
	 */

	err = VOP_STAT(file->of_vnode, &kbuf);
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
	struct openfile *file;
	int err;

	err = filetable_findfile(fd, &file);
	if (err) {
		return err;
	}

	/*
	 * No need to lock the openfile - it cannot disappear under us,
	 * and we're not using any of its non-constant fields.
	 */

	return VOP_FSYNC(file->of_vnode);
}

/*
 * ftruncate - call VOP_TRUNCATE
 */
int
sys_ftruncate(int fd, off_t len)
{
	struct openfile *file;
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

	return VOP_TRUNCATE(file->of_vnode, len);
}
