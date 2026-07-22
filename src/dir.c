#include "alefs.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* Directory entry storage in data blocks.
 * Each directory inode has extents pointing to data blocks.
 * Blocks contain packed entries:
 *   [ino:8][name_len:1][name:name_len bytes]
 * name_len == 0 marks end of entries in a block.
 */

static unsigned int alefs_dentry_size(unsigned int name_len)
{
    return sizeof(struct alefs_direntry) + name_len;
}

static int alefs_dir_find_in_block(const uint8_t *block, const char *name,
                                    unsigned int name_len, unsigned int *off_out)
{
    unsigned int off = 0;
    while (off + sizeof(struct alefs_direntry) <= ALEFS_BLOCK_SIZE) {
        struct alefs_direntry *de = (struct alefs_direntry *)(block + off);
        if (de->name_len == 0) {
            *off_out = off;
            return -ENOENT;
        }
        unsigned int sz = alefs_dentry_size(de->name_len);
        if (off + sz > ALEFS_BLOCK_SIZE)
            break;
        if (de->name_len == name_len &&
            memcmp(de->name, name, name_len) == 0)
            return (int)off;
        off += sz;
    }
    *off_out = ALEFS_BLOCK_SIZE;
    return -ENOSPC;
}

static unsigned int alefs_dir_end_in_block(const uint8_t *block)
{
    unsigned int off = 0;
    while (off + sizeof(struct alefs_direntry) <= ALEFS_BLOCK_SIZE) {
        struct alefs_direntry *de = (struct alefs_direntry *)(block + off);
        if (de->name_len == 0)
            return off;
        unsigned int sz = alefs_dentry_size(de->name_len);
        if (off + sz > ALEFS_BLOCK_SIZE)
            return ALEFS_BLOCK_SIZE;
        off += sz;
    }
    return ALEFS_BLOCK_SIZE;
}

int alefs_dir_lookup(struct alefs_dev *dev, uint64_t dir_ino,
                     const char *name, uint64_t *ino)
{
    struct alefs_inode dir_inode;
    int ret = alefs_inode_read(dev, dir_ino, &dir_inode);
    if (ret) return ret;
    if (!S_ISDIR(dir_inode.mode)) return -ENOTDIR;

    unsigned int name_len = strlen(name);
    if (name_len > ALEFS_MAX_NAME) return -ENAMETOOLONG;

    for (unsigned int e = 0; e < dir_inode.extent_count; e++) {
        uint64_t start = dir_inode.extents[e].start;
        uint64_t count = dir_inode.extents[e].count;

        for (uint64_t b = 0; b < count; b++) {
            uint8_t buf[ALEFS_BLOCK_SIZE];
            ret = alefs_dev_read(dev, start + b, buf);
            if (ret) continue;

            unsigned int off;
            int found = alefs_dir_find_in_block(buf, name, name_len, &off);
            if (found >= 0) {
                struct alefs_direntry *de = (struct alefs_direntry *)(buf + found);
                *ino = de->ino;
                return 0;
            }
        }
    }
    return -ENOENT;
}

int alefs_dir_add_entry(struct alefs_dev *dev, uint64_t dir_ino,
                        uint64_t child_ino, const char *name, uint8_t file_type)
{
    (void)file_type;
    unsigned int name_len = strlen(name);
    if (name_len == 0 || name_len > ALEFS_MAX_NAME)
        return -EINVAL;

    struct alefs_inode dir_inode;
    int ret = alefs_inode_read(dev, dir_ino, &dir_inode);
    if (ret) return ret;

    unsigned int esize = alefs_dentry_size(name_len);

    for (unsigned int e = 0; e < dir_inode.extent_count; e++) {
        uint64_t start = dir_inode.extents[e].start;
        uint64_t count = dir_inode.extents[e].count;

        for (uint64_t b = 0; b < count; b++) {
            uint8_t buf[ALEFS_BLOCK_SIZE];
            ret = alefs_dev_read(dev, start + b, buf);
            if (ret) continue;

            unsigned int off;
            int found = alefs_dir_find_in_block(buf, name, name_len, &off);
            if (found >= 0) {
                /* Update existing entry */
                struct alefs_direntry *de = (struct alefs_direntry *)(buf + found);
                de->ino = child_ino;
                return alefs_dev_write(dev, start + b, buf);
            }

            /* Check if there's room at the end */
            unsigned int end = alefs_dir_end_in_block(buf);
            if (end + esize <= ALEFS_BLOCK_SIZE) {
                struct alefs_direntry *de = (struct alefs_direntry *)(buf + end);
                de->ino = child_ino;
                de->name_len = (uint8_t)name_len;
                memcpy(de->name, name, name_len);
                return alefs_dev_write(dev, start + b, buf);
            }
        }
    }

    /* Allocate a new block */
    uint64_t new_block;
    ret = alefs_bitmap_alloc(dev, &new_block);
    if (ret) return ret;

    uint8_t zbuf[ALEFS_BLOCK_SIZE];
    memset(zbuf, 0, sizeof(zbuf));
    struct alefs_direntry *de = (struct alefs_direntry *)zbuf;
    de->ino = child_ino;
    de->name_len = (uint8_t)name_len;
    memcpy(de->name, name, name_len);
    ret = alefs_dev_write(dev, new_block, zbuf);
    if (ret) return ret;

    /* Add extent to dir inode */
    if (dir_inode.extent_count > 0) {
        struct alefs_extent *last = &dir_inode.extents[dir_inode.extent_count - 1];
        if (last->start + last->count == new_block) {
            last->count++;
        } else if (dir_inode.extent_count < ALEFS_NR_EXTENTS) {
            dir_inode.extents[dir_inode.extent_count].start = new_block;
            dir_inode.extents[dir_inode.extent_count].count = 1;
            dir_inode.extent_count++;
        } else {
            return -ENOSPC;
        }
    } else {
        dir_inode.extents[0].start = new_block;
        dir_inode.extents[0].count = 1;
        dir_inode.extent_count = 1;
    }
    dir_inode.mtime = time(NULL);
    return alefs_inode_write(dev, dir_ino, &dir_inode);
}

int alefs_dir_remove_entry(struct alefs_dev *dev, uint64_t dir_ino,
                           const char *name)
{
    unsigned int name_len = strlen(name);
    if (name_len > ALEFS_MAX_NAME) return -EINVAL;

    struct alefs_inode dir_inode;
    int ret = alefs_inode_read(dev, dir_ino, &dir_inode);
    if (ret) return ret;

    for (unsigned int e = 0; e < dir_inode.extent_count; e++) {
        uint64_t start = dir_inode.extents[e].start;
        uint64_t count = dir_inode.extents[e].count;

        for (uint64_t b = 0; b < count; b++) {
            uint8_t buf[ALEFS_BLOCK_SIZE];
            ret = alefs_dev_read(dev, start + b, buf);
            if (ret) continue;

            unsigned int off;
            int found = alefs_dir_find_in_block(buf, name, name_len, &off);
            if (found >= 0) {
                uint64_t child_ino;
                {
                    struct alefs_direntry *de = (struct alefs_direntry *)(buf + found);
                    child_ino = de->ino;
                }
                /* Mark deleted */
                struct alefs_direntry *de = (struct alefs_direntry *)(buf + found);
                de->name_len = 0;
                ret = alefs_dev_write(dev, start + b, buf);
                if (ret) return ret;

                /* Update link counts */
                dir_inode.mtime = time(NULL);
                if (dir_inode.links > 0) dir_inode.links--;
                alefs_inode_write(dev, dir_ino, &dir_inode);

                struct alefs_inode child_inode;
                if (alefs_inode_read(dev, child_ino, &child_inode) == 0) {
                    if (child_inode.links > 0) child_inode.links--;
                    alefs_inode_write(dev, child_ino, &child_inode);
                }
                return 0;
            }
        }
    }
    return -ENOENT;
}

static void alefs_print_inode_info(struct alefs_dev *dev, uint64_t ino)
{
    struct alefs_inode inode;
    if (alefs_inode_read(dev, ino, &inode) != 0) {
        printf("  [%lu] (unknown)\n", (unsigned long)ino);
        return;
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

    printf("%s %3u %5u %5u %8lu %s %s\n",
           mode_str, inode.links,
           inode.uid, inode.gid,
           (unsigned long)inode.size,
           time_buf, "?");
}

int alefs_dir_list(struct alefs_dev *dev, uint64_t dir_ino, bool show_all)
{
    (void)show_all;
    struct alefs_inode dir_inode;
    int ret = alefs_inode_read(dev, dir_ino, &dir_inode);
    if (ret) return ret;
    if (!S_ISDIR(dir_inode.mode)) return -ENOTDIR;

    printf("total %lu\n", (unsigned long)dev->sb.total_blocks);

    for (unsigned int e = 0; e < dir_inode.extent_count; e++) {
        uint64_t start = dir_inode.extents[e].start;
        uint64_t count = dir_inode.extents[e].count;

        for (uint64_t b = 0; b < count; b++) {
            uint8_t buf[ALEFS_BLOCK_SIZE];
            ret = alefs_dev_read(dev, start + b, buf);
            if (ret) continue;

            unsigned int off = 0;
            while (off + sizeof(struct alefs_direntry) <= ALEFS_BLOCK_SIZE) {
                struct alefs_direntry *de = (struct alefs_direntry *)(buf + off);
                if (de->name_len == 0) break;
                unsigned int sz = alefs_dentry_size(de->name_len);
                if (off + sz > ALEFS_BLOCK_SIZE) break;

                printf("%s%*s  [%lu]\n",
                       "  ", (int)de->name_len, (const char *)de->name,
                       (unsigned long)de->ino);
                off += sz;
            }
        }
    }
    return 0;
}

struct tree_ctx {
    struct alefs_dev *dev;
    int depth;
};

static void alefs_dir_tree_recursive(struct alefs_dev *dev, uint64_t dir_ino,
                                      int depth);

static void alefs_dir_tree_block(struct alefs_dev *dev, const uint8_t *buf,
                                  uint64_t block_ino, int depth)
{
    (void)block_ino;
    unsigned int off = 0;
    while (off + sizeof(struct alefs_direntry) <= ALEFS_BLOCK_SIZE) {
        struct alefs_direntry *de = (struct alefs_direntry *)(buf + off);
        if (de->name_len == 0) break;
        unsigned int sz = alefs_dentry_size(de->name_len);
        if (off + sz > ALEFS_BLOCK_SIZE) break;

        for (int i = 0; i < depth; i++) printf("    ");

        struct alefs_inode child;
        const char *type = "?";
        if (alefs_inode_read(dev, de->ino, &child) == 0) {
            type = S_ISDIR(child.mode) ? "d" : "f";
        }

        printf("├── [%lu] %.*s (ino=%lu)\n",
               (unsigned long)(depth ? 0UL : 0UL),
               (int)de->name_len, (const char *)de->name,
               (unsigned long)de->ino);

        if (type[0] == 'd') {
            alefs_dir_tree_recursive(dev, de->ino, depth + 1);
        }

        off += sz;
    }
}

static void alefs_dir_tree_recursive(struct alefs_dev *dev, uint64_t dir_ino,
                                      int depth)
{
    struct alefs_inode dir_inode;
    if (alefs_inode_read(dev, dir_ino, &dir_inode) != 0) return;

    for (unsigned int e = 0; e < dir_inode.extent_count; e++) {
        uint64_t start = dir_inode.extents[e].start;
        uint64_t count = dir_inode.extents[e].count;
        for (uint64_t b = 0; b < count; b++) {
            uint8_t buf[ALEFS_BLOCK_SIZE];
            if (alefs_dev_read(dev, start + b, buf)) continue;
            alefs_dir_tree_block(dev, buf, start + b, depth);
        }
    }
}

int alefs_dir_tree(struct alefs_dev *dev, uint64_t dir_ino, int depth,
                   const char *path)
{
    (void)path;
    if (depth == 0) {
        printf("/\n");
    }
    alefs_dir_tree_recursive(dev, dir_ino, depth);
    return 0;
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
