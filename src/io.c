#include "aleqfs.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

int aleqfs_dev_open(struct aleqfs_dev *dev, const char *path, int flags)
{
    struct stat st;
    memset(dev, 0, sizeof(*dev));

    if (stat(path, &st) == 0 && S_ISBLK(st.st_mode)) {
        dev->fd = open(path, O_RDWR);
        if (dev->fd < 0)
            return -errno;
        off_t size = lseek(dev->fd, 0, SEEK_END);
        if (size < 0) {
            close(dev->fd);
            return -errno;
        }
        dev->num_blocks = (uint64_t)size / ALEQFS_BLOCK_SIZE;
    } else {
        dev->fd = open(path, flags, 0644);
        if (dev->fd < 0)
            return -errno;
        off_t size = lseek(dev->fd, 0, SEEK_END);
        if (size > 0)
            dev->num_blocks = (uint64_t)size / ALEQFS_BLOCK_SIZE;
    }

    dev->path = strdup(path);
    dev->block_size = ALEQFS_BLOCK_SIZE;
    if (!dev->path) {
        close(dev->fd);
        return -ENOMEM;
    }
    return 0;
}

int aleqfs_dev_close(struct aleqfs_dev *dev)
{
    if (dev->dirty)
        aleqfs_super_sync(dev);
    if (dev->fd >= 0)
        close(dev->fd);
    free(dev->path);
    memset(dev, 0, sizeof(*dev));
    return 0;
}

int aleqfs_dev_read(struct aleqfs_dev *dev, uint64_t block, void *buf)
{
    off_t offset = (off_t)block * (off_t)dev->block_size;
    ssize_t n = pread(dev->fd, buf, dev->block_size, offset);
    if (n != (ssize_t)dev->block_size)
        return -EIO;
    return 0;
}

int aleqfs_dev_write(struct aleqfs_dev *dev, uint64_t block, const void *buf)
{
    off_t offset = (off_t)block * (off_t)dev->block_size;
    ssize_t n = pwrite(dev->fd, buf, dev->block_size, offset);
    if (n != (ssize_t)dev->block_size)
        return -EIO;
    dev->dirty = true;
    return 0;
}

int aleqfs_dev_flush(struct aleqfs_dev *dev)
{
    return fsync(dev->fd);
}

int aleqfs_dev_sync(struct aleqfs_dev *dev)
{
    if (dev->dirty) {
        int ret = aleqfs_super_sync(dev);
        if (ret) return ret;
    }
    return fsync(dev->fd);
}

int aleqfs_dev_create(const char *path, uint64_t size_mb)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISBLK(st.st_mode))
        return 0;

    uint64_t size = size_mb * 1024ULL * 1024ULL;
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -errno;
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -errno;
    }
    close(fd);
    return 0;
}
