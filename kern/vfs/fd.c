#include <types.h>
#include <fd.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>

struct file_desc *fd_init(struct vnode *vn, mode_t mode, int flags) {
    struct file_desc *fd = kmalloc(sizeof *fd);
    if (fd == NULL) goto out;

    fd->lock = lock_create("fd_lock");
    if (fd->lock == NULL) goto lk_out;

    fd->vn = vn;
    fd->offset = 0;
    fd->mode = mode;
    fd->ref_count = 1;
    fd->flags = flags;

    return fd;

lk_out:
	kfree(fd);
out:
    return NULL;    /* effectively it means ENOMEM */
}

void fd_destroy(struct file_desc *fd) {
    lock_acquire(fd->lock);
    if (fd->ref_count == 1) {
        lock_release(fd->lock);
        lock_destroy(fd->lock);
        vfs_close(fd->vn);
        kfree(fd);
    } else {
        fd->ref_count--;
    }
    lock_release(fd->lock);
}
