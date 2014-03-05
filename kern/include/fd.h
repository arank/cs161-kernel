#ifndef _FD_H_
#define _FD_H_

#include <vnode.h>

struct proc;

struct file_desc {
    struct vnode *vn;
    off_t offset;
    unsigned ref_count;
    mode_t mode;
    int flags;
    struct lock *lock;
};

struct file_desc *fd_init(struct vnode *vn, mode_t mode, int flags);
void fd_dec_or_destroy(int index, struct proc *proc);

#endif /* _FD_H_ */
