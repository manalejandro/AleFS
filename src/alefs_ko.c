// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/time64.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/parser.h>
#include <linux/seq_file.h>
#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/mnt_idmapping.h>

#include "alefs_layout.h"

#define ALEFS_MOD_NAME  "alefs"
#define ALEFS_MOD_DESC  "Adaptive Linux Efficient Filesystem"
#define ALEFS_MOD_VER   "1.0.0"

struct alefs_sb_info {
    struct alefs_superblock *sb;
    struct buffer_head *sb_bh;
    unsigned long inode_table_start;
    unsigned long data_start;
    unsigned long total_blocks;
    unsigned long inode_count;
    unsigned long block_size;
};

static inline struct alefs_sb_info *ALEFS_SB(struct super_block *sb)
{
    return sb->s_fs_info;
}

static int alefs_read_inode_block(struct super_block *sb, uint64_t ino,
                                   struct buffer_head **bhp, unsigned long *offset)
{
    struct alefs_sb_info *sbi = ALEFS_SB(sb);
    unsigned long block = sbi->inode_table_start + (ino - 1) / ALEFS_INODES_PER_BLK;
    *offset = ((ino - 1) % ALEFS_INODES_PER_BLK) * sizeof(struct alefs_inode);

    if (block >= sbi->total_blocks)
        return -EIO;

    *bhp = sb_bread(sb, block);
    if (!*bhp)
        return -EIO;
    return 0;
}

static int alefs_super_sync(struct super_block *sb)
{
    struct alefs_sb_info *sbi = ALEFS_SB(sb);
    struct buffer_head *bh = sb_bread(sb, 0);
    if (!bh)
        return -EIO;
    memcpy(bh->b_data, sbi->sb, sizeof(*sbi->sb));
    mark_buffer_dirty(bh);
    brelse(bh);
    return 0;
}

static int alefs_bitmap_alloc(struct super_block *sb, uint64_t *block);

/* Directory entry helpers.
 * Directory entries are stored in data blocks pointed to by the directory
 * inode's extents. Each block contains packed entries:
 *   struct alefs_direntry { uint64_t ino; uint8_t name_len; uint8_t name[]; }
 * Entries follow each other with no padding. name_len == 0 marks end.
 */

static unsigned int alefs_dentry_size(unsigned int name_len)
{
    return sizeof(struct alefs_direntry) + name_len;
}

/* Find matching entry in a single buffer_head block.
 * Returns offset in block if found, negative errno otherwise. */
static int alefs_dir_find_in_bh(struct buffer_head *bh,
                                 const unsigned char *name,
                                 unsigned int name_len)
{
    unsigned int off = 0;
    while (off + sizeof(struct alefs_direntry) <= ALEFS_BLOCK_SIZE) {
        struct alefs_direntry *de = (struct alefs_direntry *)(bh->b_data + off);
        if (de->name_len == 0)
            return -ENOENT;
        unsigned int sz = alefs_dentry_size(de->name_len);
        if (off + sz > ALEFS_BLOCK_SIZE)
            break;
        if (de->name_len == name_len &&
            memcmp(de->name, name, name_len) == 0)
            return off;
        off += sz;
    }
    return -ENOSPC;
}

/* Find the end-of-entries offset in a block (first zero name_len). */
static unsigned int alefs_dir_end_in_bh(struct buffer_head *bh)
{
    unsigned int off = 0;
    while (off + sizeof(struct alefs_direntry) <= ALEFS_BLOCK_SIZE) {
        struct alefs_direntry *de = (struct alefs_direntry *)(bh->b_data + off);
        if (de->name_len == 0)
            return off;
        unsigned int sz = alefs_dentry_size(de->name_len);
        if (off + sz > ALEFS_BLOCK_SIZE)
            return ALEFS_BLOCK_SIZE;
        off += sz;
    }
    return ALEFS_BLOCK_SIZE;
}

/* Add a directory entry: store (child_ino, name) in dir's data blocks. */
static int alefs_dir_add_entry(struct super_block *sb, uint64_t dir_ino,
                                uint64_t child_ino, const unsigned char *name,
                                unsigned int name_len)
{
    if (name_len == 0 || name_len > ALEFS_MAX_NAME)
        return -EINVAL;

    struct buffer_head *dir_bh;
    unsigned long dir_off;
    int ret = alefs_read_inode_block(sb, dir_ino, &dir_bh, &dir_off);
    if (ret) return ret;
    struct alefs_inode *dir_ei = (struct alefs_inode *)(dir_bh->b_data + dir_off);

    unsigned int esize = alefs_dentry_size(name_len);

    /* Scan existing data blocks */
    for (unsigned int e = 0; e < dir_ei->extent_count; e++) {
        uint64_t start = dir_ei->extents[e].start;
        uint64_t count = dir_ei->extents[e].count;

        for (uint64_t b = 0; b < count; b++) {
            struct buffer_head *bh = sb_bread(sb, start + b);
            if (!bh) { brelse(dir_bh); return -EIO; }

            /* Look for duplicate name */
            int found = alefs_dir_find_in_bh(bh, name, name_len);
            if (found >= 0) {
                struct alefs_direntry *de =
                    (struct alefs_direntry *)(bh->b_data + found);
                de->ino = child_ino;
                mark_buffer_dirty(bh);
                brelse(bh);
                brelse(dir_bh);
                return 0;
            }

            /* Check for free space at the end */
            unsigned int end = alefs_dir_end_in_bh(bh);
            if (end + esize <= ALEFS_BLOCK_SIZE) {
                struct alefs_direntry *de =
                    (struct alefs_direntry *)(bh->b_data + end);
                de->ino = child_ino;
                de->name_len = (uint8_t)name_len;
                memcpy(de->name, name, name_len);
                mark_buffer_dirty(bh);
                brelse(bh);
                brelse(dir_bh);
                return 0;
            }
            brelse(bh);
        }
    }

    /* No space found — allocate a new block */
    uint64_t new_block;
    ret = alefs_bitmap_alloc(sb, &new_block);
    if (ret) { brelse(dir_bh); return ret; }

    struct buffer_head *nbh = sb_bread(sb, new_block);
    if (!nbh) { brelse(dir_bh); return -EIO; }
    memset(nbh->b_data, 0, ALEFS_BLOCK_SIZE);

    struct alefs_direntry *de = (struct alefs_direntry *)nbh->b_data;
    de->ino = child_ino;
    de->name_len = (uint8_t)name_len;
    memcpy(de->name, name, name_len);
    mark_buffer_dirty(nbh);
    brelse(nbh);

    /* Update dir inode extent list */
    if (dir_ei->extent_count > 0) {
        struct alefs_extent *last = &dir_ei->extents[dir_ei->extent_count - 1];
        if (last->start + last->count == new_block) {
            last->count++;
        } else if (dir_ei->extent_count < ALEFS_NR_EXTENTS) {
            dir_ei->extents[dir_ei->extent_count].start = new_block;
            dir_ei->extents[dir_ei->extent_count].count = 1;
            dir_ei->extent_count++;
        } else {
            brelse(dir_bh);
            return -ENOSPC;
        }
    } else {
        dir_ei->extents[0].start = new_block;
        dir_ei->extents[0].count = 1;
        dir_ei->extent_count = 1;
    }
    mark_buffer_dirty(dir_bh);
    brelse(dir_bh);
    return 0;
}

/* Remove a directory entry by zeroing its name_len. */
static int alefs_dir_remove_entry(struct super_block *sb, uint64_t dir_ino,
                                   const unsigned char *name,
                                   unsigned int name_len)
{
    struct buffer_head *dir_bh;
    unsigned long dir_off;
    int ret = alefs_read_inode_block(sb, dir_ino, &dir_bh, &dir_off);
    if (ret) return ret;
    struct alefs_inode *dir_ei = (struct alefs_inode *)(dir_bh->b_data + dir_off);
    brelse(dir_bh);

    for (unsigned int e = 0; e < dir_ei->extent_count; e++) {
        uint64_t start = dir_ei->extents[e].start;
        uint64_t count = dir_ei->extents[e].count;

        for (uint64_t b = 0; b < count; b++) {
            struct buffer_head *bh = sb_bread(sb, start + b);
            if (!bh) continue;

            int found = alefs_dir_find_in_bh(bh, name, name_len);
            if (found >= 0) {
                struct alefs_direntry *de =
                    (struct alefs_direntry *)(bh->b_data + found);
                de->name_len = 0;  /* mark deleted */
                mark_buffer_dirty(bh);
                brelse(bh);
                return 0;
            }
            brelse(bh);
        }
    }
    return -ENOENT;
}

/* Lookup a name in the directory, return child inode number (0 if not found). */
static uint64_t alefs_dir_lookup(struct super_block *sb, uint64_t dir_ino,
                                  const unsigned char *name,
                                  unsigned int name_len)
{
    struct buffer_head *dir_bh;
    unsigned long dir_off;
    int ret = alefs_read_inode_block(sb, dir_ino, &dir_bh, &dir_off);
    if (ret) return 0;
    struct alefs_inode *dir_ei = (struct alefs_inode *)(dir_bh->b_data + dir_off);
    brelse(dir_bh);

    for (unsigned int e = 0; e < dir_ei->extent_count; e++) {
        uint64_t start = dir_ei->extents[e].start;
        uint64_t count = dir_ei->extents[e].count;

        for (uint64_t b = 0; b < count; b++) {
            struct buffer_head *bh = sb_bread(sb, start + b);
            if (!bh) continue;

            int found = alefs_dir_find_in_bh(bh, name, name_len);
            brelse(bh);
            if (found >= 0) {
                struct alefs_direntry *de =
                    (struct alefs_direntry *)(bh->b_data + found);
                return de->ino;
            }
        }
    }
    return 0;
}

static int __maybe_unused alefs_bitmap_set(struct super_block *sb, uint64_t block, bool value)
{
    struct alefs_sb_info *sbi = ALEFS_SB(sb);
    uint64_t bitmap_start = sbi->sb->bitmap_start;
    uint64_t byte_off = block / 8;
    uint64_t bit_off = block % 8;
    uint64_t buf_block = bitmap_start + byte_off / ALEFS_BLOCK_SIZE;
    unsigned long buf_off = byte_off % ALEFS_BLOCK_SIZE;

    struct buffer_head *bh = sb_bread(sb, buf_block);
    if (!bh)
        return -EIO;

    uint8_t *data = (uint8_t *)bh->b_data;
    if (value)
        data[buf_off] |= (1 << bit_off);
    else
        data[buf_off] &= ~(1 << bit_off);

    mark_buffer_dirty(bh);
    brelse(bh);
    return 0;
}

static int alefs_bitmap_alloc(struct super_block *sb, uint64_t *block)
{
    struct alefs_sb_info *sbi = ALEFS_SB(sb);
    uint64_t bitmap_bytes = ALEFS_BITMAP_BLOCKS * ALEFS_BLOCK_SIZE;

    for (uint64_t i = sbi->data_start; i < sbi->total_blocks; i++) {
        uint64_t byte_off = i / 8;
        uint64_t bit_off = i % 8;
        if (byte_off >= bitmap_bytes)
            break;
        uint64_t buf_block = sbi->sb->bitmap_start + byte_off / ALEFS_BLOCK_SIZE;
        unsigned long buf_off = byte_off % ALEFS_BLOCK_SIZE;

        struct buffer_head *bh = sb_bread(sb, buf_block);
        if (!bh)
            return -EIO;
        uint8_t *data = (uint8_t *)bh->b_data;

        if (!(data[buf_off] & (1 << bit_off))) {
            data[buf_off] |= (1 << bit_off);
            mark_buffer_dirty(bh);
            brelse(bh);
            *block = i;
            sbi->sb->free_blocks--;
            alefs_super_sync(sb);
            return 0;
        }
        brelse(bh);
    }
    return -ENOSPC;
}

static int alefs_inode_alloc(struct super_block *sb, uint64_t *ino)
{
    struct alefs_sb_info *sbi = ALEFS_SB(sb);

    for (uint64_t i = ALEFS_ROOT_INO; i <= sbi->inode_count; i++) {
        struct buffer_head *bh;
        unsigned long offset;
        int ret = alefs_read_inode_block(sb, i, &bh, &offset);
        if (ret)
            continue;

        struct alefs_inode *ei = (struct alefs_inode *)(bh->b_data + offset);
        if (ei->mode == 0) {
            memset(ei, 0, sizeof(*ei));
            mark_buffer_dirty(bh);
            brelse(bh);
            *ino = i;
            sbi->sb->free_inodes--;
            alefs_super_sync(sb);
            return 0;
        }
        brelse(bh);
    }
    return -ENOSPC;
}

static int alefs_inode_dirty(struct super_block *sb, uint64_t ino,
                              const struct alefs_inode *inode)
{
    struct buffer_head *bh;
    unsigned long offset;
    int ret = alefs_read_inode_block(sb, ino, &bh, &offset);
    if (ret)
        return ret;
    struct alefs_inode *ei = (struct alefs_inode *)(bh->b_data + offset);
    memcpy(ei, inode, sizeof(*ei));
    mark_buffer_dirty(bh);
    brelse(bh);
    return 0;
}

static int alefs_read_inode(struct inode *inode)
{
    struct super_block *sb = inode->i_sb;
    struct buffer_head *bh;
    unsigned long offset;
    struct alefs_inode *ei;
    uint64_t ino = inode->i_ino;
    int ret;

    ret = alefs_read_inode_block(sb, ino, &bh, &offset);
    if (ret)
        return ret;

    ei = (struct alefs_inode *)(bh->b_data + offset);

    inode->i_mode   = ei->mode;
    inode->i_uid.val = ei->uid;
    inode->i_gid.val = ei->gid;
    inode->i_size   = ei->size;
    inode->i_blocks = ei->blocks;
    inode_set_atime(inode, ei->atime, 0);
    inode_set_mtime(inode, ei->mtime, 0);
    inode_set_ctime(inode, ei->ctime, 0);
    set_nlink(inode, ei->links);

    brelse(bh);
    return 0;
}

static int alefs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
    struct super_block *sb = inode->i_sb;
    struct buffer_head *bh;
    unsigned long offset;
    struct alefs_inode *ei;
    uint64_t ino = inode->i_ino;
    int ret;

    ret = alefs_read_inode_block(sb, ino, &bh, &offset);
    if (ret)
        return ret;

    ei = (struct alefs_inode *)(bh->b_data + offset);

    ei->mode   = inode->i_mode;
    ei->uid    = from_kuid(&init_user_ns, inode->i_uid);
    ei->gid    = from_kgid(&init_user_ns, inode->i_gid);
    ei->size   = inode->i_size;
    ei->blocks = inode->i_blocks;
    ei->atime  = inode_get_atime(inode).tv_sec;
    ei->mtime  = inode_get_mtime(inode).tv_sec;
    ei->ctime  = inode_get_ctime(inode).tv_sec;
    ei->links  = inode->i_nlink;

    mark_buffer_dirty(bh);
    brelse(bh);
    return 0;
}

static int alefs_getattr(struct mnt_idmap *idmap,
                          const struct path *path, struct kstat *stat,
                          u32 request_mask, unsigned int flags)
{
    struct inode *inode = d_inode(path->dentry);
    generic_fillattr(&nop_mnt_idmap, request_mask, inode, stat);
    return 0;
}

static int alefs_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int alefs_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t alefs_read(struct file *file, char __user *buf,
                           size_t len, loff_t *ppos)
{
    struct inode *inode = file_inode(file);
    struct super_block *sb = inode->i_sb;
    struct alefs_sb_info *sbi = ALEFS_SB(sb);
    loff_t pos = *ppos;
    ssize_t ret = 0;

    if (pos < 0)
        return -EINVAL;
    if (pos >= inode->i_size)
        return 0;
    if (len > inode->i_size - pos)
        len = inode->i_size - pos;

    while (len > 0) {
        uint64_t block_off = pos / sbi->block_size;
        uint64_t byte_off = pos % sbi->block_size;
        struct buffer_head *bh;
        uint64_t dev_block;
        uint64_t extent_off = 0;
        unsigned int i;
        struct buffer_head *inode_bh;
        unsigned long io;
        struct alefs_inode *ei;

        if (alefs_read_inode_block(sb, inode->i_ino, &inode_bh, &io))
            return ret ? ret : -EIO;
        ei = (struct alefs_inode *)(inode_bh->b_data + io);

        dev_block = 0;
        for (i = 0; i < ei->extent_count; i++) {
            if (block_off < extent_off + ei->extents[i].count) {
                dev_block = ei->extents[i].start + (block_off - extent_off);
                break;
            }
            extent_off += ei->extents[i].count;
        }
        brelse(inode_bh);

        if (dev_block == 0)
            break;

        bh = sb_bread(sb, dev_block);
        if (!bh)
            break;

        size_t to_copy = sbi->block_size - byte_off;
        if (to_copy > len) to_copy = len;

        if (copy_to_user(buf, bh->b_data + byte_off, to_copy)) {
            brelse(bh);
            return ret ? ret : -EFAULT;
        }

        brelse(bh);
        buf += to_copy;
        pos += to_copy;
        ret += to_copy;
        len -= to_copy;
    }

    *ppos = pos;
    return ret;
}

static ssize_t alefs_write(struct file *file, const char __user *buf,
                            size_t len, loff_t *ppos)
{
    struct inode *inode = file_inode(file);
    struct super_block *sb = inode->i_sb;
    struct alefs_sb_info *sbi = ALEFS_SB(sb);
    loff_t pos = *ppos;
    ssize_t written = 0;

    if (pos < 0)
        return -EINVAL;
    if (len == 0)
        return 0;

    inode_lock(inode);

    while (len > 0) {
        uint64_t block_off = pos / sbi->block_size;
        uint64_t byte_off = pos % sbi->block_size;
        uint64_t dev_block = 0;
        int found = 0;
        struct buffer_head *inode_bh;
        unsigned long io;
        struct alefs_inode *ei;

        if (alefs_read_inode_block(sb, inode->i_ino, &inode_bh, &io)) {
            inode_unlock(inode);
            return written ? written : -EIO;
        }
        ei = (struct alefs_inode *)(inode_bh->b_data + io);

        uint64_t extent_off = 0;
        for (unsigned int i = 0; i < ei->extent_count; i++) {
            if (block_off >= extent_off &&
                block_off < extent_off + ei->extents[i].count) {
                dev_block = ei->extents[i].start + (block_off - extent_off);
                found = 1;
                break;
            }
            extent_off += ei->extents[i].count;
        }

        if (!found) {
            uint64_t new_block;
            int ret = alefs_bitmap_alloc(sb, &new_block);
            if (ret) {
                brelse(inode_bh);
                break;
            }

            if (ei->extent_count > 0) {
                struct alefs_extent *last = &ei->extents[ei->extent_count - 1];
                if (last->start + last->count == new_block) {
                    last->count++;
                    dev_block = new_block;
                    mark_buffer_dirty(inode_bh);
                    brelse(inode_bh);
                    goto write_data;
                }
            }

            if (ei->extent_count >= ALEFS_NR_EXTENTS) {
                brelse(inode_bh);
                break;
            }

            ei->extents[ei->extent_count].start = new_block;
            ei->extents[ei->extent_count].count = 1;
            ei->extent_count++;
            dev_block = new_block;

            mark_buffer_dirty(inode_bh);
        }
        brelse(inode_bh);

write_data:
        size_t to_write = sbi->block_size - byte_off;
        if (to_write > len) to_write = len;

        struct buffer_head *data_bh = sb_bread(sb, dev_block);
        if (!data_bh)
            break;

        if (copy_from_user(data_bh->b_data + byte_off, buf, to_write)) {
            brelse(data_bh);
            break;
        }
        mark_buffer_dirty(data_bh);
        brelse(data_bh);

        buf += to_write;
        pos += to_write;
        written += to_write;
        len -= to_write;

        if (pos > inode->i_size) {
            inode->i_size = pos;
            inode->i_blocks = pos / ALEFS_BLOCK_SIZE + 1;
        }
    }

    inode_unlock(inode);

    if (written > 0) {
        *ppos = pos;
        struct timespec64 now = current_time(inode);
        inode_set_mtime(inode, now.tv_sec, now.tv_nsec);
        mark_inode_dirty(inode);
    }
    return written ? written : -ENOSPC;
}

static const struct file_operations alefs_file_ops = {
    .open    = alefs_open,
    .release = alefs_release,
    .read    = alefs_read,
    .write   = alefs_write,
    .llseek  = generic_file_llseek,
};

static const struct inode_operations alefs_dir_inode_ops;
static const struct file_operations alefs_dir_ops;
static const struct inode_operations alefs_inode_ops;

static struct dentry *alefs_lookup(struct inode *dir, struct dentry *dentry,
                                    unsigned int flags)
{
    struct super_block *sb = dir->i_sb;
    const unsigned char *name = dentry->d_name.name;
    unsigned int name_len = dentry->d_name.len;

    uint64_t child_ino = alefs_dir_lookup(sb, dir->i_ino, name, name_len);
    if (!child_ino) {
        d_add(dentry, NULL);
        return NULL;
    }

    struct inode *child = iget_locked(sb, child_ino);
    if (!child)
        return ERR_PTR(-ENOMEM);

    if (child->i_state & I_NEW) {
        alefs_read_inode(child);
        if (S_ISDIR(child->i_mode)) {
            child->i_op = &alefs_dir_inode_ops;
            child->i_fop = &alefs_dir_ops;
        } else {
            child->i_op = &alefs_inode_ops;
            child->i_fop = &alefs_file_ops;
        }
        unlock_new_inode(child);
    }

    if (!child->i_ino) {
        iput(child);
        return ERR_PTR(-ENOENT);
    }

    return d_splice_alias(child, dentry);
}

static int alefs_iterate(struct file *file, struct dir_context *ctx)
{
    struct inode *dir = file_inode(file);
    struct super_block *sb = dir->i_sb;

    if (ctx->pos < 0)
        return 0;

    if (ctx->pos == 0) {
        if (!dir_emit_dots(file, ctx))
            return 0;
        ctx->pos = 1;
    }

    struct buffer_head *dir_bh;
    unsigned long dir_off;
    if (alefs_read_inode_block(sb, dir->i_ino, &dir_bh, &dir_off))
        return -EIO;
    struct alefs_inode *dir_ei = (struct alefs_inode *)(dir_bh->b_data + dir_off);
    unsigned int dir_extent_count = dir_ei->extent_count;
    struct alefs_extent dir_extents[ALEFS_NR_EXTENTS];
    memcpy(dir_extents, dir_ei->extents, sizeof(dir_extents));
    brelse(dir_bh);

    unsigned long long entry_idx = 1;  /* 0 = dots, 1+ = entries */

    for (unsigned int e = 0; e < dir_extent_count; e++) {
        uint64_t start = dir_extents[e].start;
        uint64_t count = dir_extents[e].count;

        for (uint64_t b = 0; b < count; b++) {
            struct buffer_head *bh = sb_bread(sb, start + b);
            if (!bh) continue;

            unsigned int off = 0;
            while (off + sizeof(struct alefs_direntry) <= ALEFS_BLOCK_SIZE) {
                struct alefs_direntry *de =
                    (struct alefs_direntry *)(bh->b_data + off);
                if (de->name_len == 0)
                    break;
                unsigned int sz = alefs_dentry_size(de->name_len);
                if (off + sz > ALEFS_BLOCK_SIZE)
                    break;

                if (entry_idx >= (unsigned long long)ctx->pos) {
                    umode_t dtype = DT_UNKNOWN;
                    struct inode *child = ilookup(sb, de->ino);
                    if (child) {
                        dtype = S_ISDIR(child->i_mode) ? DT_DIR : DT_REG;
                        iput(child);
                    }

                    if (!dir_emit(ctx, (const char *)de->name,
                                  de->name_len, de->ino, dtype)) {
                        brelse(bh);
                        return 0;
                    }
                    ctx->pos++;
                }
                entry_idx++;
                off += sz;
            }
            brelse(bh);
        }
    }
    return 0;
}

static int alefs_add_dentry(struct super_block *sb, struct inode *dir,
                             uint64_t child_ino, const char *name,
                             unsigned int name_len)
{
    return alefs_dir_add_entry(sb, dir->i_ino, child_ino,
                                (const unsigned char *)name, name_len);
}

static int alefs_remove_dentry(struct super_block *sb, struct inode *dir,
                                const char *name, unsigned int name_len)
{
    return alefs_dir_remove_entry(sb, dir->i_ino,
                                   (const unsigned char *)name, name_len);
}

static int alefs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
                        struct dentry *dentry, umode_t mode)
{
    struct super_block *sb = dir->i_sb;
    uint64_t ino;
    int ret;

    ret = alefs_inode_alloc(sb, &ino);
    if (ret)
        return ret;

    struct timespec64 now = current_time(dir);
    struct alefs_inode ei;
    memset(&ei, 0, sizeof(ei));
    ei.mode  = S_IFDIR | (mode & 07777);
    ei.uid   = from_kuid(&init_user_ns, current_fsuid());
    ei.gid   = from_kgid(&init_user_ns, current_fsgid());
    ei.links = 2;
    ei.atime = ei.mtime = ei.ctime = now.tv_sec;

    ret = alefs_inode_dirty(sb, ino, &ei);
    if (ret)
        return ret;

    ret = alefs_add_dentry(sb, dir, ino, dentry->d_name.name, dentry->d_name.len);
    if (ret)
        return ret;

    struct inode *child = iget_locked(sb, ino);
    if (!child)
        return -ENOMEM;
    alefs_read_inode(child);
    child->i_op = &alefs_dir_inode_ops;
    child->i_fop = &alefs_dir_ops;
    unlock_new_inode(child);

    d_instantiate(dentry, child);
    inode_inc_link_count(dir);
    mark_inode_dirty(dir);
    return 0;
}

static int alefs_create(struct mnt_idmap *idmap, struct inode *dir,
                         struct dentry *dentry, umode_t mode, bool excl)
{
    struct super_block *sb = dir->i_sb;
    uint64_t ino;
    int ret;

    ret = alefs_inode_alloc(sb, &ino);
    if (ret)
        return ret;

    struct timespec64 now = current_time(dir);
    struct alefs_inode ei;
    memset(&ei, 0, sizeof(ei));
    ei.mode  = S_IFREG | (mode & 07777);
    ei.uid   = from_kuid(&init_user_ns, current_fsuid());
    ei.gid   = from_kgid(&init_user_ns, current_fsgid());
    ei.links = 1;
    ei.atime = ei.mtime = ei.ctime = now.tv_sec;

    ret = alefs_inode_dirty(sb, ino, &ei);
    if (ret)
        return ret;

    ret = alefs_add_dentry(sb, dir, ino, dentry->d_name.name, dentry->d_name.len);
    if (ret)
        return ret;

    struct inode *child = iget_locked(sb, ino);
    if (!child)
        return -ENOMEM;
    alefs_read_inode(child);
    child->i_op = &alefs_inode_ops;
    child->i_fop = &alefs_file_ops;
    unlock_new_inode(child);

    d_instantiate(dentry, child);
    mark_inode_dirty(dir);
    return 0;
}

static int alefs_unlink(struct inode *dir, struct dentry *dentry)
{
    struct super_block *sb = dir->i_sb;
    struct inode *child = d_inode(dentry);
    if (!child)
        return -ENOENT;
    if (S_ISDIR(child->i_mode))
        return -EISDIR;

    int ret = alefs_remove_dentry(sb, dir, dentry->d_name.name, dentry->d_name.len);
    if (ret)
        return ret;

    drop_nlink(child);
    mark_inode_dirty(child);
    inode_inc_link_count(dir);
    mark_inode_dirty(dir);
    return 0;
}

static int alefs_rmdir(struct inode *dir, struct dentry *dentry)
{
    struct super_block *sb = dir->i_sb;
    struct inode *child = d_inode(dentry);
    if (!child)
        return -ENOENT;
    if (!S_ISDIR(child->i_mode))
        return -ENOTDIR;

    int ret = alefs_remove_dentry(sb, dir, dentry->d_name.name, dentry->d_name.len);
    if (ret)
        return ret;

    clear_nlink(child);
    mark_inode_dirty(child);
    inode_dec_link_count(dir);
    mark_inode_dirty(dir);
    return 0;
}

static int alefs_rename(struct mnt_idmap *idmap, struct inode *old_dir,
                         struct dentry *old_dentry, struct inode *new_dir,
                         struct dentry *new_dentry, unsigned int flags)
{
    struct super_block *sb = old_dir->i_sb;
    struct inode *child = d_inode(old_dentry);
    if (!child)
        return -ENOENT;

    int ret = alefs_remove_dentry(sb, old_dir, old_dentry->d_name.name,
                                   old_dentry->d_name.len);
    if (ret)
        return ret;

    ret = alefs_add_dentry(sb, new_dir, child->i_ino,
                            new_dentry->d_name.name, new_dentry->d_name.len);
    if (ret) {
        alefs_add_dentry(sb, old_dir, child->i_ino,
                          old_dentry->d_name.name, old_dentry->d_name.len);
        return ret;
    }

    if (old_dir != new_dir) {
        inode_dec_link_count(old_dir);
        mark_inode_dirty(old_dir);
        inode_inc_link_count(new_dir);
        mark_inode_dirty(new_dir);
    }
    return 0;
}

static const struct inode_operations alefs_dir_inode_ops = {
    .lookup  = alefs_lookup,
    .mkdir   = alefs_mkdir,
    .create  = alefs_create,
    .unlink  = alefs_unlink,
    .rmdir   = alefs_rmdir,
    .rename  = alefs_rename,
    .getattr = alefs_getattr,
};

static const struct file_operations alefs_dir_ops = {
    .open    = alefs_open,
    .release = alefs_release,
    .iterate_shared = alefs_iterate,
    .llseek  = generic_file_llseek,
};

static const struct inode_operations alefs_inode_ops = {
    .getattr = alefs_getattr,
};

static void alefs_put_super(struct super_block *sb)
{
    struct alefs_sb_info *sbi = ALEFS_SB(sb);
    if (sbi) {
        if (sbi->sb_bh)
            brelse(sbi->sb_bh);
        kfree(sbi);
    }
}

static int alefs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
    struct super_block *sb = dentry->d_sb;
    struct alefs_sb_info *sbi = ALEFS_SB(sb);

    buf->f_type    = ALEFS_MAGIC;
    buf->f_bsize   = sbi->block_size;
    buf->f_blocks  = sbi->total_blocks;
    buf->f_bfree   = sbi->sb->free_blocks;
    buf->f_bavail  = sbi->sb->free_blocks;
    buf->f_files   = sbi->inode_count;
    buf->f_ffree   = sbi->sb->free_inodes;
    buf->f_namelen = ALEFS_MAX_NAME;
    return 0;
}

static const struct super_operations alefs_super_ops = {
    .write_inode   = alefs_write_inode,
    .put_super     = alefs_put_super,
    .statfs        = alefs_statfs,
};

static int alefs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct alefs_sb_info *sbi;
    struct buffer_head *bh;
    struct inode *root_inode;
    struct alefs_superblock *disk_sb;
    int ret = 0;

    sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
    if (!sbi)
        return -ENOMEM;

    sb->s_fs_info = sbi;

    bh = sb_bread(sb, 0);
    if (!bh) {
        ret = -EIO;
        goto fail;
    }

    disk_sb = (struct alefs_superblock *)bh->b_data;

    if (disk_sb->magic != ALEFS_MAGIC) {
        if (!silent)
            pr_err("alefs: wrong magic 0x%llx\n",
                   (unsigned long long)disk_sb->magic);
        brelse(bh);
        ret = -EINVAL;
        goto fail;
    }

    brelse(bh);

    if (!sb_set_blocksize(sb, ALEFS_BLOCK_SIZE)) {
        pr_err("alefs: block size %u not supported by device\n", ALEFS_BLOCK_SIZE);
        ret = -EINVAL;
        goto fail;
    }

    bh = sb_bread(sb, 0);
    if (!bh) {
        ret = -EIO;
        goto fail;
    }

    sbi->sb = (struct alefs_superblock *)bh->b_data;
    sbi->sb_bh = bh;

    sbi->block_size        = ALEFS_BLOCK_SIZE;
    sbi->total_blocks      = sbi->sb->total_blocks;
    sbi->inode_count       = sbi->sb->inode_count;
    sbi->inode_table_start = sbi->sb->inode_table_start;
    sbi->data_start        = sbi->sb->data_start;

    sb->s_magic          = ALEFS_MAGIC;
    sb->s_op             = &alefs_super_ops;
    sb->s_maxbytes       = MAX_LFS_FILESIZE;

    root_inode = iget_locked(sb, ALEFS_ROOT_INO);
    if (!root_inode) {
        ret = -ENOMEM;
        goto fail;
    }

    alefs_read_inode(root_inode);
    root_inode->i_op  = &alefs_dir_inode_ops;
    root_inode->i_fop = &alefs_dir_ops;
    unlock_new_inode(root_inode);

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) {
        ret = -ENOMEM;
        goto fail;
    }

    return 0;

fail:
    if (sbi->sb_bh) brelse(sbi->sb_bh);
    kfree(sbi);
    return ret;
}

static struct dentry *alefs_mount(struct file_system_type *fs_type,
                                   int flags, const char *dev_name,
                                   void *data)
{
    return mount_bdev(fs_type, flags, dev_name, data, alefs_fill_super);
}

static struct file_system_type alefs_fs_type = {
    .owner   = THIS_MODULE,
    .name    = "alefs",
    .mount   = alefs_mount,
    .kill_sb = kill_block_super,
    .fs_flags = FS_REQUIRES_DEV,
};

static int __init alefs_init(void)
{
    int ret = register_filesystem(&alefs_fs_type);
    if (ret)
        pr_err("alefs: failed to register (%d)\n", ret);
    else
        pr_info("alefs: module loaded v%s\n", ALEFS_MOD_VER);
    return ret;
}

static void __exit alefs_exit(void)
{
    unregister_filesystem(&alefs_fs_type);
    pr_info("alefs: module unloaded\n");
}

module_init(alefs_init);
module_exit(alefs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AleFS Contributors");
MODULE_DESCRIPTION(ALEFS_MOD_DESC);
MODULE_VERSION(ALEFS_MOD_VER);
MODULE_ALIAS("alefs");
