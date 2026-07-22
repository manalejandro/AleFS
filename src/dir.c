#include "alefs.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>

static uint64_t make_key(uint64_t parent, const char *name)
{
    uint32_t h = (uint32_t)parent;
    for (const char *p = name; *p; p++)
        h = h * 31 + (unsigned char)*p;
    return (parent << 32) | h;
}

int alefs_dir_lookup(struct alefs_dev *dev, uint64_t dir_ino,
                     const char *name, uint64_t *ino)
{
    uint64_t key = make_key(dir_ino, name);

    uint64_t result = 0;
    int ret = alefs_btree_lookup(dev, dev->sb.btree_root, key, &result);
    if (ret) return ret;
    *ino = result;
    return 0;
}

int alefs_dir_add_entry(struct alefs_dev *dev, uint64_t dir_ino,
                        uint64_t child_ino, const char *name, uint8_t file_type)
{
    (void)file_type;
    uint64_t key = make_key(dir_ino, name);

    uint64_t existing;
    if (alefs_btree_lookup(dev, dev->sb.btree_root, key, &existing) == 0)
        return -EEXIST;

    int ret = alefs_btree_insert(dev, dev->sb.btree_root, key, child_ino);
    if (ret) return ret;

    struct alefs_inode dir_inode;
    ret = alefs_inode_read(dev, dir_ino, &dir_inode);
    if (ret) return ret;

    struct alefs_inode child_inode;
    ret = alefs_inode_read(dev, child_ino, &child_inode);
    if (ret == 0) {
        child_inode.links++;
        ret = alefs_inode_write(dev, child_ino, &child_inode);
        if (ret) return ret;
    }

    dir_inode.links++;
    dir_inode.mtime = time(NULL);
    return alefs_inode_write(dev, dir_ino, &dir_inode);
}

int alefs_dir_remove_entry(struct alefs_dev *dev, uint64_t dir_ino,
                           const char *name)
{
    uint64_t key = make_key(dir_ino, name);

    uint64_t child_ino;
    int ret = alefs_btree_lookup(dev, dev->sb.btree_root, key, &child_ino);
    if (ret) return ret;

    ret = alefs_btree_delete(dev, dev->sb.btree_root, key);
    if (ret) return ret;

    struct alefs_inode dir_inode;
    ret = alefs_inode_read(dev, dir_ino, &dir_inode);
    if (ret) return ret;

    if (dir_inode.links > 0)
        dir_inode.links--;
    dir_inode.mtime = time(NULL);
    ret = alefs_inode_write(dev, dir_ino, &dir_inode);
    if (ret) return ret;

    struct alefs_inode child_inode;
    ret = alefs_inode_read(dev, child_ino, &child_inode);
    if (ret == 0) {
        if (child_inode.links > 0)
            child_inode.links--;
        alefs_inode_write(dev, child_ino, &child_inode);
    }

    return 0;
}

struct list_ctx {
    struct alefs_dev *dev;
    bool show_all;
};

static int list_cb(uint64_t key, uint64_t value, void *arg)
{
    struct list_ctx *ctx = (struct list_ctx *)arg;
    struct alefs_inode inode;
    (void)key;

    if (alefs_inode_read(ctx->dev, value, &inode) != 0) {
        printf("  [%lu] (unknown)\n", (unsigned long)value);
        return 0;
    }

    char mode_str[11] = "----------";
    if (S_ISDIR(inode.mode)) mode_str[0] = 'd';
    else if (S_ISREG(inode.mode)) mode_str[0] = '-';
    if (inode.mode & S_IRUSR) mode_str[1] = 'r';
    if (inode.mode & S_IWUSR) mode_str[2] = 'w';
    if (inode.mode & S_IXUSR) mode_str[3] = 'x';
    if (inode.mode & S_IRGRP) mode_str[4] = 'r';
    if (inode.mode & S_IWGRP) mode_str[5] = 'w';
    if (inode.mode & S_IXGRP) mode_str[6] = 'x';
    if (inode.mode & S_IROTH) mode_str[7] = 'r';
    if (inode.mode & S_IWOTH) mode_str[8] = 'w';
    if (inode.mode & S_IXOTH) mode_str[9] = 'x';

    char time_buf[64];
    time_t mt = (time_t)inode.mtime;
    struct tm *tm = localtime(&mt);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm);

    printf("%s %3lu %5u %5u %8lu %s ino=%lu\n",
           mode_str, (unsigned long)inode.links,
           inode.uid, inode.gid,
           (unsigned long)inode.size,
           time_buf, (unsigned long)value);

    return 0;
}

int alefs_dir_list(struct alefs_dev *dev, uint64_t dir_ino, bool show_all)
{
    uint64_t start_key = dir_ino << 32;
    uint64_t end_key = ((dir_ino + 1) << 32) - 1;

    struct list_ctx ctx;
    ctx.dev = dev;
    ctx.show_all = show_all;

    printf("total %lu\n", (unsigned long)dev->sb.total_blocks);

    struct list_ctx *c = &ctx;
    return alefs_btree_iterate(dev, dev->sb.btree_root,
                                start_key, end_key, list_cb, c);
}

struct tree_entry {
    uint64_t ino;
    char name[ALEFS_MAX_NAME + 1];
};

struct tree_ctx {
    struct alefs_dev *dev;
    int depth;
};

static int tree_cb(uint64_t key, uint64_t value, void *arg)
{
    struct tree_ctx *ctx = (struct tree_ctx *)arg;
    struct alefs_inode inode;
    (void)key;

    if (alefs_inode_read(ctx->dev, value, &inode) != 0)
        return 0;

    for (int i = 0; i < ctx->depth; i++)
        printf("    ");

    const char *type = S_ISDIR(inode.mode) ? "d" : "f";
    printf("├── [%lu] %s (ino=%lu, size=%lu)\n",
           (unsigned long)inode.blocks,
           type, (unsigned long)value, (unsigned long)inode.size);

    return 0;
}

int alefs_dir_tree(struct alefs_dev *dev, uint64_t dir_ino, int depth, const char *path)
{
    (void)path;
    struct alefs_inode dir_inode;
    int ret = alefs_inode_read(dev, dir_ino, &dir_inode);
    if (ret) return ret;

    uint64_t start_key = dir_ino << 32;
    uint64_t end_key = ((dir_ino + 1) << 32) - 1;

    struct tree_ctx ctx;
    ctx.dev = dev;
    ctx.depth = depth;

    return alefs_btree_iterate(dev, dev->sb.btree_root,
                                start_key, end_key, tree_cb, &ctx);
}

int alefs_dir_create(struct alefs_dev *dev, uint64_t parent_ino,
                     const char *name, uint16_t mode, uint64_t *ino)
{
    uint64_t child_ino;
    int ret = alefs_inode_alloc(dev, &child_ino);
    if (ret) return ret;

    struct alefs_inode child;
    memset(&child, 0, sizeof(child));
    child.mode   = mode;
    child.uid    = getuid();
    child.gid    = getgid();
    child.links  = 1;
    child.atime  = child.mtime = child.ctime = time(NULL);

    ret = alefs_inode_write(dev, child_ino, &child);
    if (ret) return ret;

    ret = alefs_dir_add_entry(dev, parent_ino, child_ino, name,
                               S_ISDIR(mode) ? ALEFS_FT_DIR : ALEFS_FT_FILE);
    if (ret) {
        alefs_inode_free(dev, child_ino);
        return ret;
    }

    *ino = child_ino;
    return 0;
}
