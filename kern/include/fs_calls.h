int sys_open(const_userptr_t filename , int flags);

ssize_t sys_read(int fd , userptr_t buf , size_t buflen);

ssize_t sys_write(int fd , const_userptr_t buf , size_t nbytes);

off_t sys_lseek (int fd , off_t pos , int whence);

int sys_close(int fd);

int sys_dup2(int oldfd , int newfd);

int sys_chdir ( const_userptr_t pathname);

int sys___getcwd(userptr_t buf , size_t buflen);
