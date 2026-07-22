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

struct alefs_inode_info {
    uint64_t ino;
};

static inline struct alefs_sb_info *ALEFS_SB(struct super_block *sb)
{
    return sb->s_fs_info;
}

static inline struct alefs_inode_info *ALEFS_I(struct inode *inode)
{
    return inode->i_private;
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

static const struct inode_operations alefs_inode_ops;

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

static const struct file_operations alefs_file_ops = {
    .open    = alefs_open,
    .release = alefs_release,
    .read    = alefs_read,
    .llseek  = generic_file_llseek,
};

static struct dentry *alefs_lookup(struct inode *dir, struct dentry *dentry,
                                    unsigned int flags)
{
    struct super_block *sb = dir->i_sb;
    struct buffer_head *bh;
    unsigned long offset;
    struct alefs_inode *ei;
    uint32_t hash;
    uint64_t key;
    struct buffer_head *btree_bh;
    struct alefs_btree_node *node;
    int i;

    if (alefs_read_inode_block(sb, dir->i_ino, &bh, &offset))
        return ERR_PTR(-ENOENT);
    ei = (struct alefs_inode *)(bh->b_data + offset);
    (void)ei;
    brelse(bh);

    {
        const unsigned char *name = dentry->d_name.name;
        unsigned int len = dentry->d_name.len;

        hash = (uint32_t)dir->i_ino;
        for (i = 0; i < len; i++)
            hash = hash * 31 + name[i];

        key = ((uint64_t)dir->i_ino << 32) | hash;
    }

    struct alefs_sb_info *sbi = ALEFS_SB(sb);
    btree_bh = sb_bread(sb, sbi->sb->btree_root);
    if (!btree_bh)
        return ERR_PTR(-EIO);

    node = (struct alefs_btree_node *)btree_bh->b_data;
    for (i = 0; i < node->num_keys; i++) {
        struct alefs_btree_entry *e = (struct alefs_btree_entry *)
            (node->data + i * sizeof(struct alefs_btree_entry));
        if (e->key == key) {
            uint64_t child_ino = e->value;
            struct inode *child = iget_locked(sb, child_ino);
            brelse(btree_bh);

            if (!child)
                return ERR_PTR(-ENOMEM);

            if (child->i_state & I_NEW) {
                alefs_read_inode(child);
                unlock_new_inode(child);
            }

            if (!child->i_ino) {
                iput(child);
                return ERR_PTR(-ENOENT);
            }

            return d_splice_alias(child, dentry);
        }
    }

    brelse(btree_bh);
    d_add(dentry, NULL);
    return NULL;
}

static int alefs_iterate(struct file *file, struct dir_context *ctx)
{
    struct inode *dir = file_inode(file);
    struct super_block *sb = dir->i_sb;
    struct alefs_sb_info *sbi = ALEFS_SB(sb);
    struct buffer_head *bh;
    struct alefs_btree_node *node;
    int i;

    if (ctx->pos < 0)
        return 0;

    if (ctx->pos == 0) {
        if (!dir_emit_dots(file, ctx))
            return 0;
        ctx->pos = 1;
    }

    bh = sb_bread(sb, sbi->sb->btree_root);
    if (!bh)
        return -EIO;

    node = (struct alefs_btree_node *)bh->b_data;
    uint64_t start_key = (uint64_t)dir->i_ino << 32;
    uint64_t end_key = ((uint64_t)(dir->i_ino + 1) << 32) - 1;
    int idx = 0;

    for (i = 0; i < node->num_keys; i++) {
        struct alefs_btree_entry *e = (struct alefs_btree_entry *)
            (node->data + i * sizeof(struct alefs_btree_entry));

        if (e->key >= start_key && e->key <= end_key) {
            if (idx < ctx->pos - 1) {
                idx++;
                continue;
            }

            struct inode *child = ilookup(sb, e->value);
            umode_t dtype = DT_UNKNOWN;
            if (child) {
                dtype = S_ISDIR(child->i_mode) ? DT_DIR : DT_REG;
                iput(child);
            }

            char name_buf[ALEFS_MAX_NAME + 1];
            int nlen = snprintf(name_buf, sizeof(name_buf), "ino_%llu",
                                (unsigned long long)e->value);

            if (!dir_emit(ctx, name_buf, nlen, e->value, dtype))
                break;

            ctx->pos++;
            idx++;
        }
    }

    brelse(bh);
    return 0;
}

static int alefs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
                        struct dentry *dentry, umode_t mode)
{
    return -EROFS;
}

static int alefs_create(struct mnt_idmap *idmap, struct inode *dir,
                         struct dentry *dentry, umode_t mode, bool excl)
{
    return -EROFS;
}

static int alefs_unlink(struct inode *dir, struct dentry *dentry)
{
    return -EROFS;
}

static int alefs_rmdir(struct inode *dir, struct dentry *dentry)
{
    return -EROFS;
}

static int alefs_rename(struct mnt_idmap *idmap, struct inode *old_dir,
                         struct dentry *old_dentry, struct inode *new_dir,
                         struct dentry *new_dentry, unsigned int flags)
{
    return -EROFS;
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

static struct inode *alefs_alloc_inode(struct super_block *sb)
{
    struct inode *inode = kzalloc(sizeof(struct inode), GFP_KERNEL);
    if (!inode)
        return NULL;
    struct alefs_inode_info *aii = kzalloc(sizeof(*aii), GFP_KERNEL);
    if (!aii) {
        kfree(inode);
        return NULL;
    }
    aii->ino = 0;
    inode->i_private = aii;
    return inode;
}

static void alefs_destroy_inode(struct inode *inode)
{
    kfree(inode->i_private);
    kfree(inode);
}

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
    .alloc_inode   = alefs_alloc_inode,
    .destroy_inode = alefs_destroy_inode,
    .write_inode   = alefs_write_inode,
    .put_super     = alefs_put_super,
    .statfs        = alefs_statfs,
};

static int alefs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct alefs_sb_info *sbi;
    struct buffer_head *bh;
    struct inode *root_inode;
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

    sbi->sb = (struct alefs_superblock *)bh->b_data;
    sbi->sb_bh = bh;

    if (sbi->sb->magic != ALEFS_MAGIC) {
        if (!silent)
            pr_err("alefs: wrong magic 0x%llx\n",
                   (unsigned long long)sbi->sb->magic);
        ret = -EINVAL;
        goto fail;
    }

    sbi->block_size        = sbi->sb->block_size;
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
