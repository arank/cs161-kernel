int open(const char *filename , int flags);

ssize_t read(int fd , void *buf , size_t buflen);

ssize_t write(int fd , const void *buf , size_t nbytes);

off_t lseek (int fd , off_t pos , int whence);

int close(int fd);

int dup2(int oldfd , int newfd);

int chdir ( const char *pathname);

int __getcwd(char *buf , size_t buflen);
