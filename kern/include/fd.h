#ifndef _FD_H_
#define _FD_H_

#include <vnode.h>
struct file_desc *fd_init(struct vnode *vn, mode_t mode, int flags);
void fd_destroy(struct file_desc *fd);

#endif /* _FD_H_ */
