/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>
#define CLOSED_FILE  -1
/*
 * Put your function declarations and data types here ...
 */

struct open_file {
    int refCount;
    int flags;
    off_t offset;
    struct vnode *vn_ptr;
    struct lock *oft_lock;
};

struct open_file *oft; 
int creat_table(void);
void destroy_open_file(void);
int valid_fd_fp(int fpt_index);

int sys_open(userptr_t filename, int flags, mode_t mode, int *retval);
int sys_close(int fd);

ssize_t sys_read(int fd, userptr_t buffer, size_t bufflen, int* retval);

ssize_t sys_write(int fd, userptr_t buffer, size_t nbytes, int* retval);

int sys_dup2(int oldfd, int newfd, int* retval);

int sys_lseek(int fd, off_t pos, int whence, int64_t* retval);

#endif /* _FILE_H_ */