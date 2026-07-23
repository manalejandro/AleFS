#include "aleqfs.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

static unsigned int aleqfs_dentry_size(unsigned int name_len)
{
    return sizeof(struct aleqfs_direntry) + name_len;
}

static int aleqfs_dir_find_in_block(const uint8_t *block, const char *name,
                                    unsigned int name_len, unsigned int *off_out)
{
    unsigned int off = 0;
    while (off + sizeof(struct aleqfs_direntry) <= ALEQFS_BLOCK_SIZE) {
        struct aleqfs_direntry *de = (struct aleqfs_direntry *)(block + off);
        if (de->name_len == 0) {
            *off_out = off;
            return -ENOENT;
        }
        unsigned int sz = aleqfs_dentry_size(de->name_len);
        if (off + sz > ALEQFS_BLOCK_SIZE)
            break;
        if (de->name_len == name_len &&
            memcmp(de->name, name, name_len) == 0)
            return (int)off;
        off += sz;
    }
    *off_out = ALEQFS_BLOCK_SIZE;
    return -ENOSPC;
}

static unsigned int aleqfs_dir_end_in_block(const uint8_t *block)
{
    unsigned int off = 0;
    while (off + sizeof(struct aleqfs_direntry) <= ALEQFS_BLOCK_SIZE) {
        struct aleqfs_direntry *de = (struct aleqfs_direntry *)(block + off);
        if (de->name_len == 0)
            return off;
        unsigned int sz = aleqfs_dentry_size(de->name_len);
        if (off + sz > ALEQFS_BLOCK_SIZE)
            return ALEQFS_BLOCK_SIZE;
        off += sz;
    }
    return ALEQFS_BLOCK_SIZE;
}

int aleqfs_dir_lookup(struct aleqfs_dev *dev, uint64_t dir_ino,
                      const char *name, uint64_t *ino)
{
    struct aleqfs_inode dir_inode;
    int ret = aleqfs_inode_read(dev, dir_ino, &dir_inode);
    if (ret) return ret;
    if (!S_ISDIR(dir_inode.mode)) return -ENOTDIR;

    unsigned int name_len = strlen(name);
    if (name_len > ALEQFS_MAX_NAME) return -ENAMETOOLONG;

    for (unsigned int e = 0; e < dir_inode.extent_count; e++) {
        uint64_t start = dir_inode.extents[e].start;
        uint64_t count = dir_inode.extents[e].count;

        for (uint64_t b = 0; b < count; b++) {
            uint8_t buf[ALEQFS_BLOCK_SIZE];
            ret = aleqfs_dev_read(dev, start + b, buf);
            if (ret) continue;

            unsigned int off;
            int found = aleqfs_dir_find_in_block(buf, name, name_len, &off);
            if (found >= 0) {
                struct aleqfs_direntry *de = (struct aleqfs_direntry *)(buf + found);
                *ino = de->ino;
                return 0;
            }
        }
    }
    return -ENOENT;
}

int aleqfs_dir_add_entry(struct aleqfs_dev *dev, uint64_t dir_ino,
                         uint64_t child_ino, const char *name, uint8_t file_type)
{
    (void)file_type;
    unsigned int name_len = strlen(name);
    if (name_len == 0 || name_len > ALEQFS_MAX_NAME)
        return -EINVAL;

    struct aleqfs_inode dir_inode;
    int ret = aleqfs_inode_read(dev, dir_ino, &dir_inode);
    if (ret) return ret;

    unsigned int esize = aleqfs_dentry_size(name_len);

    for (unsigned int e = 0; e < dir_inode.extent_count; e++) {
        uint64_t start = dir_inode.extents[e].start;
        uint64_t count = dir_inode.extents[e].count;

        for (uint64_t b = 0; b < count; b++) {
            uint8_t buf[ALEQFS_BLOCK_SIZE];
            ret = aleqfs_dev_read(dev, start + b, buf);
            if (ret) continue;

            unsigned int off;
            int found = aleqfs_dir_find_in_block(buf, name, name_len, &off);
            if (found >= 0) {
                struct aleqfs_direntry *de = (struct aleqfs_direntry *)(buf + found);
                de->ino = child_ino;
                return aleqfs_dev_write(dev, start + b, buf);
            }

            unsigned int end = aleqfs_dir_end_in_block(buf);
            if (end + esize <= ALEQFS_BLOCK_SIZE) {
                struct aleqfs_direntry *de = (struct aleqfs_direntry *)(buf + end);
                de->ino = child_ino;
                de->name_len = (uint8_t)name_len;
                de->amplitude_real = 32767;
                de->amplitude_imag = 0;
                memcpy(de->name, name, name_len);
                return aleqfs_dev_write(dev, start + b, buf);
            }
        }
    }

    uint64_t new_block;
    ret = aleqfs_bitmap_alloc(dev, &new_block);
    if (ret) return ret;

    uint8_t zbuf[ALEQFS_BLOCK_SIZE];
    memset(zbuf, 0, sizeof(zbuf));
    struct aleqfs_direntry *de = (struct aleqfs_direntry *)zbuf;
    de->ino = child_ino;
    de->name_len = (uint8_t)name_len;
    de->amplitude_real = 32767;
    de->amplitude_imag = 0;
    memcpy(de->name, name, name_len);
    ret = aleqfs_dev_write(dev, new_block, zbuf);
    if (ret) return ret;

    if (dir_inode.extent_count > 0) {
        struct aleqfs_extent *last = &dir_inode.extents[dir_inode.extent_count - 1];
        if (last->start + last->count == new_block) {
            last->count++;
        } else if (dir_inode.extent_count < ALEQFS_NR_EXTENTS) {
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
    return aleqfs_inode_write(dev, dir_ino, &dir_inode);
}

int aleqfs_dir_remove_entry(struct aleqfs_dev *dev, uint64_t dir_ino,
                            const char *name)
{
    unsigned int name_len = strlen(name);
    if (name_len > ALEQFS_MAX_NAME) return -EINVAL;

    struct aleqfs_inode dir_inode;
    int ret = aleqfs_inode_read(dev, dir_ino, &dir_inode);
    if (ret) return ret;

    for (unsigned int e = 0; e < dir_inode.extent_count; e++) {
        uint64_t start = dir_inode.extents[e].start;
        uint64_t count = dir_inode.extents[e].count;

        for (uint64_t b = 0; b < count; b++) {
            uint8_t buf[ALEQFS_BLOCK_SIZE];
            ret = aleqfs_dev_read(dev, start + b, buf);
            if (ret) continue;

            unsigned int off;
            int found = aleqfs_dir_find_in_block(buf, name, name_len, &off);
            if (found >= 0) {
                uint64_t child_ino;
                {
                    struct aleqfs_direntry *de = (struct aleqfs_direntry *)(buf + found);
                    child_ino = de->ino;
                }
                struct aleqfs_direntry *de = (struct aleqfs_direntry *)(buf + found);
                de->name_len = 0;
                ret = aleqfs_dev_write(dev, start + b, buf);
                if (ret) return ret;

                dir_inode.mtime = time(NULL);
                if (dir_inode.links > 0) dir_inode.links--;
                aleqfs_inode_write(dev, dir_ino, &dir_inode);

                struct aleqfs_inode child_inode;
                if (aleqfs_inode_read(dev, child_ino, &child_inode) == 0) {
                    if (child_inode.links > 0) child_inode.links--;
                    aleqfs_inode_write(dev, child_ino, &child_inode);
                }
                return 0;
            }
        }
    }
    return -ENOENT;
}

int aleqfs_dir_list(struct aleqfs_dev *dev, uint64_t dir_ino, bool show_all,
                    bool quantum_collapse)
{
    (void)show_all;
    struct aleqfs_inode dir_inode;
    int ret = aleqfs_inode_read(dev, dir_ino, &dir_inode);
    if (ret) return ret;
    if (!S_ISDIR(dir_inode.mode)) return -ENOTDIR;

    printf("total %lu\n", (unsigned long)dev->sb.total_blocks);

    double threshold = 0.0;
    if (quantum_collapse)
        threshold = (double)rand() / (double)RAND_MAX;

    for (unsigned int e = 0; e < dir_inode.extent_count; e++) {
        uint64_t start = dir_inode.extents[e].start;
        uint64_t count = dir_inode.extents[e].count;

        for (uint64_t b = 0; b < count; b++) {
            uint8_t buf[ALEQFS_BLOCK_SIZE];
            ret = aleqfs_dev_read(dev, start + b, buf);
            if (ret) continue;

            unsigned int off = 0;
            while (off + sizeof(struct aleqfs_direntry) <= ALEQFS_BLOCK_SIZE) {
                struct aleqfs_direntry *de = (struct aleqfs_direntry *)(buf + off);
                if (de->name_len == 0) break;
                unsigned int sz = aleqfs_dentry_size(de->name_len);
                if (off + sz > ALEQFS_BLOCK_SIZE) break;

                double prob = aleqfs_amplitude_probability(de->amplitude_real,
                                                           de->amplitude_imag);

                if (quantum_collapse && prob < threshold) {
                    off += sz;
                    continue;
                }

                printf("%s%*s  [%lu]  amp=(%4d,%4d)  p=%.3f\n",
                       "  ", (int)de->name_len, (const char *)de->name,
                       (unsigned long)de->ino,
                       de->amplitude_real, de->amplitude_imag, prob);
                off += sz;
            }
        }
    }
    return 0;
}

static void aleqfs_dir_tree_recursive(struct aleqfs_dev *dev, uint64_t dir_ino,
                                      int depth, bool quantum_collapse);

static void aleqfs_dir_tree_block(struct aleqfs_dev *dev, const uint8_t *buf,
                                  int depth, bool quantum_collapse)
{
    double threshold = 0.0;
    if (quantum_collapse)
        threshold = (double)rand() / (double)RAND_MAX;

    unsigned int off = 0;
    while (off + sizeof(struct aleqfs_direntry) <= ALEQFS_BLOCK_SIZE) {
        struct aleqfs_direntry *de = (struct aleqfs_direntry *)(buf + off);
        if (de->name_len == 0) break;
        unsigned int sz = aleqfs_dentry_size(de->name_len);
        if (off + sz > ALEQFS_BLOCK_SIZE) break;

        double prob = aleqfs_amplitude_probability(de->amplitude_real,
                                                   de->amplitude_imag);

        if (quantum_collapse && prob < threshold) {
            off += sz;
            continue;
        }

        for (int i = 0; i < depth; i++) printf("    ");

        struct aleqfs_inode child;
        const char *type = "?";
        if (aleqfs_inode_read(dev, de->ino, &child) == 0) {
            type = S_ISDIR(child.mode) ? "d" : "f";
        }

        if (quantum_collapse) {
            printf("├── [%lu] %.*s (ino=%lu) [p=%.3f]\n",
                   (unsigned long)(depth ? 0UL : 0UL),
                   (int)de->name_len, (const char *)de->name,
                   (unsigned long)de->ino, prob);

            if (type[0] == 'd')
                aleqfs_dir_tree_recursive(dev, de->ino, depth + 1, quantum_collapse);
        } else {
            printf("├── [%lu] %.*s (ino=%lu)  amp=(%4d,%4d)  p=%.3f\n",
                   (unsigned long)(depth ? 0UL : 0UL),
                   (int)de->name_len, (const char *)de->name,
                   (unsigned long)de->ino,
                   de->amplitude_real, de->amplitude_imag, prob);

            if (type[0] == 'd')
                aleqfs_dir_tree_recursive(dev, de->ino, depth + 1, quantum_collapse);
        }

        off += sz;
    }
}

static void aleqfs_dir_tree_recursive(struct aleqfs_dev *dev, uint64_t dir_ino,
                                      int depth, bool quantum_collapse)
{
    struct aleqfs_inode dir_inode;
    if (aleqfs_inode_read(dev, dir_ino, &dir_inode) != 0) return;

    for (unsigned int e = 0; e < dir_inode.extent_count; e++) {
        uint64_t start = dir_inode.extents[e].start;
        uint64_t count = dir_inode.extents[e].count;
        for (uint64_t b = 0; b < count; b++) {
            uint8_t buf[ALEQFS_BLOCK_SIZE];
            if (aleqfs_dev_read(dev, start + b, buf)) continue;
            aleqfs_dir_tree_block(dev, buf, depth, quantum_collapse);
        }
    }
}

int aleqfs_dir_tree(struct aleqfs_dev *dev, uint64_t dir_ino, int depth,
                    const char *path, bool quantum_collapse)
{
    (void)path;
    if (depth == 0) {
        printf("/\n");
    }
    aleqfs_dir_tree_recursive(dev, dir_ino, depth, quantum_collapse);
    return 0;
}

int aleqfs_dir_create(struct aleqfs_dev *dev, uint64_t parent_ino,
                      const char *name, uint16_t mode, uint64_t *ino)
{
    uint64_t child_ino;
    int ret = aleqfs_inode_alloc(dev, &child_ino);
    if (ret) return ret;

    struct aleqfs_inode child;
    memset(&child, 0, sizeof(child));
    child.mode   = mode;
    child.uid    = getuid();
    child.gid    = getgid();
    child.links  = 1;
    child.atime  = child.mtime = child.ctime = time(NULL);
    child.amplitude.real = 32767;
    child.amplitude.imag = 0;

    ret = aleqfs_inode_write(dev, child_ino, &child);
    if (ret) return ret;

    ret = aleqfs_dir_add_entry(dev, parent_ino, child_ino, name,
                               S_ISDIR(mode) ? ALEQFS_FT_DIR : ALEQFS_FT_FILE);
    if (ret) {
        aleqfs_inode_free(dev, child_ino);
        return ret;
    }

    *ino = child_ino;
    return 0;
}
