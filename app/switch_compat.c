/* POSIX *at() directory functions the Geargrafx core references (zip media
 * extraction cleanup) but devkitA64's newlib does not provide. rondo-nx never
 * loads zip files, so these just report "not supported". */
#include <errno.h>
#include <dirent.h>
#include <stddef.h>

int openat(int dirfd_, const char* path, int flags, ...)
{
    (void)dirfd_; (void)path; (void)flags;
    errno = ENOSYS;
    return -1;
}

DIR* fdopendir(int fd)
{
    (void)fd;
    errno = ENOSYS;
    return NULL;
}

int dirfd(DIR* d)
{
    (void)d;
    errno = ENOSYS;
    return -1;
}

int unlinkat(int dirfd_, const char* path, int flags)
{
    (void)dirfd_; (void)path; (void)flags;
    errno = ENOSYS;
    return -1;
}
