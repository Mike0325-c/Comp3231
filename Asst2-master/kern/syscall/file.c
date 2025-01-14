#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>

/*
 * Add your file-related functions here ...
 */


int creat_table(void)
{
    oft = kmalloc(sizeof(struct open_file) * __OPEN_MAX);
    if (oft == NULL) {
        return ENOMEM;
    }

    for (int i = 3; i < __OPEN_MAX; i++)
    {
        oft[i].flags = 0;
        oft[i].refCount = 0;
        oft[i].vn_ptr = NULL;
        oft[i].offset = 0;
    }
    char conname[5];
    // stdin
    strcpy(conname,"con:");
    oft[0].flags = O_RDONLY;
    oft[0].refCount = 1;
    oft[0].offset = 0;
    vfs_open(conname, O_RDONLY, 0, &oft[0].vn_ptr);
    // stdout
    strcpy(conname,"con:");
    oft[1].flags = O_WRONLY;
    oft[1].refCount = 1;
    oft[1].offset = 0;
    vfs_open(conname, O_WRONLY, 0, &oft[1].vn_ptr);
    // stderr
    strcpy(conname,"con:");
    oft[2].flags = O_WRONLY;
    oft[2].refCount = 1;
    oft[2].offset = 0;
    vfs_open(conname, O_WRONLY, 0, &oft[2].vn_ptr);

    oft->oft_lock = lock_create("oft_lock");
    if ((oft->oft_lock) == NULL) {
        return ENOMEM;
    }

    return 0;


}

void destroy_open_file() {
    //check if open file table is null
    if(oft != NULL) {
        for(int i = 3; i < __OPEN_MAX; i++) {
            if(oft->vn_ptr != NULL) {
                vfs_close(oft->vn_ptr);
            }
        }
        lock_destroy(oft->oft_lock);
        kfree(oft);
    }
}

// check valid fd and fp
int valid_fd_fp(int fd) {

    if (fd < 0 || fd >= __OPEN_MAX) {
        return EBADF;
    }
    int fpt_index = curproc->fd_table[fd];
    if (fpt_index == CLOSED_FILE || fpt_index >= __OPEN_MAX)
    {
        return EBADF;
    }
    return fpt_index;
}

int sys_open(userptr_t filename, int flags, mode_t mode, int *retval)
{   

    int result = 0;
    int fpt_index;
    // check filename then copy in
    char* sfilename = kmalloc(__NAME_MAX * sizeof(char));
    
    if (sfilename == NULL) {
        return ENOMEM;
    }
    lock_acquire(oft->oft_lock);
    result = copyinstr(filename, sfilename, __NAME_MAX, NULL);

    if (result)
    {
        lock_release(oft->oft_lock);
        kfree(sfilename);
        return result;
    }

    // find the first free global index in open_file_table
    for (fpt_index = 0; fpt_index < __OPEN_MAX; fpt_index++)
    {
        if (oft[fpt_index].vn_ptr == 0)
        {
            break;
        }
    }

    if (fpt_index == __OPEN_MAX)
    {
        lock_release(oft->oft_lock);
        kfree(sfilename);
        return ENFILE;
    }

    // find the first free process index in fd_table
    int fd_index;
    for (fd_index = 0; fd_index < __OPEN_MAX; fd_index++)
    {
        if (curproc->fd_table[fd_index] == CLOSED_FILE)
        {
            break;
        }
    }

    if (fd_index == __OPEN_MAX)
    {
        lock_release(oft->oft_lock);
        kfree(sfilename);
        return EMFILE;
    }
    // open the file
    result = vfs_open(sfilename, flags, mode, &oft[fpt_index].vn_ptr);
    if (result) 
    {
        lock_release(oft->oft_lock);
        kfree(sfilename);
        return result;

    }

    // setup the open file table data
    oft[fpt_index].refCount = 1;
    oft[fpt_index].offset = 0;
    oft[fpt_index].flags = flags;
    lock_release(oft->oft_lock);

    // setup fd table
    curproc->fd_table[fd_index] = fpt_index;
    kfree(sfilename);
    // assign retval
    *retval = fd_index;

    return result;
}

ssize_t sys_read(int fd, userptr_t buffer, size_t buflen, int *retval)
{

    // int fpt_index = valid_fd_fp(fd);
    if (fd < 0 || fd >= __OPEN_MAX)
    {
        return EBADF;
    }
    int fpt_index = curproc->fd_table[fd];

    if (fpt_index == CLOSED_FILE || fpt_index >= __OPEN_MAX)
    {
        return EBADF;
    }

    if (buffer == NULL)
    {
        return EFAULT;
    }

    if ((oft[fpt_index].flags & O_ACCMODE) == O_WRONLY)
    {
        return EBADF;
    }

    // lock_acquire(oft->oft_lock);
    struct iovec iov;
    struct uio u;
    // void *valid_buf[buflen];

    uio_uinit(&iov, &u, buffer, buflen, oft[fpt_index].offset, UIO_READ);

    int err = VOP_READ(oft[fpt_index].vn_ptr, &u);

    if (err)
    {
        return err;
    }

    lock_acquire(oft->oft_lock);
    oft[fpt_index].offset = u.uio_offset;
    lock_release(oft->oft_lock);
    *retval = buflen - u.uio_resid;

    // kfree(iov);
    // kfree(u);
    return 0;
}

ssize_t sys_write(int fd, userptr_t buffer, size_t nbytes, int *retval)
{

    // int fpt_index = valid_fd_fp(fd);
    // check fd and fpt
    if (fd < 0 || fd >= __OPEN_MAX)
    {
        return EBADF;
    }
    int fpt_index = curproc->fd_table[fd];
    if (fpt_index == CLOSED_FILE || fpt_index >= __OPEN_MAX)
    {
        return EBADF;
    }

    if (buffer == NULL)
    {
        return EFAULT;
    }

    // check if the file is opened for writing
    if ((oft[fpt_index].flags & O_ACCMODE) == O_RDONLY)
    {
        return EBADF;
    }

    struct iovec iov;

    struct uio u;

    void *valid_buf[nbytes];
    // char *buffer = kmalloc(nbytes);
    // copyin(buffer, buffer, nbytes);
    // // initialize the uio structure
    uio_uinit(&iov, &u, buffer, nbytes, oft[fpt_index].offset, UIO_WRITE);

    // use VOP_WRITE to write to the file
    int err = VOP_WRITE(oft[fpt_index].vn_ptr, &u);
    if (err)
    {
        return err;
    }

    int result = copyin((userptr_t)buffer, valid_buf, nbytes);
    if (result)
    {
        return result;
    }
    
    lock_acquire(oft->oft_lock);

    // return the retval of the number of bytes it written
    oft[fpt_index].offset = u.uio_offset;
    lock_release(oft->oft_lock);
    *retval = nbytes - u.uio_resid;

    // kfree(iov);
    // kfree(&u);
    return 0;
}

int sys_close(int fd)
{
    // check fd/fp
    int fpt_index = valid_fd_fp(fd);
      
    // update reference counter
    oft[fpt_index].refCount--;

    if (oft[fpt_index].refCount == 0)
    {
        vfs_close(oft[fpt_index].vn_ptr);
        // update open_file_table
        lock_acquire(oft->oft_lock);
        oft[fpt_index].offset = 0;
        oft[fpt_index].flags = -1;
        oft[fpt_index].vn_ptr = NULL;
        lock_release(oft->oft_lock);
        curproc->fd_table[fd] = -1;
    }
    return fpt_index;
}

int sys_dup2(int oldfd, int newfd, int *retval)
{
    *retval = -1;

    if (oldfd < 0 || oldfd >= __OPEN_MAX || newfd < 0 || newfd >= __OPEN_MAX)
    {
        return EBADF;
    }

    int old_oft = curproc->fd_table[oldfd];
    int new_oft = curproc->fd_table[newfd];

    if (old_oft < 0 || old_oft >= __OPEN_MAX || oft[old_oft].vn_ptr == NULL)
    {
        return EBADF;
    }

    if (new_oft < 0 || new_oft >= __OPEN_MAX)
    {
        return EBADF;
    }

    lock_acquire(oft->oft_lock);
    if (oldfd == newfd)
    {
        lock_release(oft->oft_lock);
        return 0;
    }

    if (oft[new_oft].vn_ptr != NULL)
    {
        if (sys_close(newfd))
        {
            lock_release(oft->oft_lock);
            return EBADF;
        }
    }
    
    curproc->fd_table[newfd] = old_oft;
    lock_acquire(oft->oft_lock);
    oft[old_oft].refCount++;
    lock_release(oft->oft_lock);
    *retval = newfd;
    return 0;
}

// //TODO
int sys_lseek(int fd, off_t pos, int whence, int64_t *retval)
{
    if (fd < 0 || fd >= __OPEN_MAX)
    {
        return EBADF;
    }
    int fpt_index = curproc->fd_table[fd];
    if (fpt_index < 0 || fpt_index >= __OPEN_MAX || oft[fpt_index].refCount <= 0)
    {
        return EBADF;
    }

    lock_acquire(oft->oft_lock);

    bool seekable = VOP_ISSEEKABLE(oft[fpt_index].vn_ptr);
    if (!seekable)
    {
        lock_release(oft->oft_lock);
        return ESPIPE;
    }

    struct stat *statbuf = kmalloc(sizeof(struct stat));
    
    if (whence == SEEK_CUR) {
        oft[fpt_index].offset += pos;
        if (oft[fpt_index].offset < 0) {
            lock_release(oft->oft_lock);
            return EINVAL;
        }
    } else if (whence == SEEK_SET) {
        //pos can not be negative
        if (pos < 0) {
            lock_release(oft->oft_lock);
            return EINVAL;
        }
        oft[fpt_index].offset = pos;
    } else if (whence == SEEK_END) {
        VOP_STAT(oft[fpt_index].vn_ptr, statbuf);
        off_t file_size = statbuf->st_size;
        oft[fpt_index].offset = file_size + pos;
    } else {
        lock_release(oft->oft_lock);
        return EINVAL;
    }

    lock_release(oft->oft_lock);
    *retval = oft[fpt_index].offset;
    return 0;
}


