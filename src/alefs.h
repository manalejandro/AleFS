#ifndef ALEFS_H
#define ALEFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define ALEFS_MAGIC           0x414C454653  // "ALEFS"
#define ALEFS_FS_VERSION      1
#define ALEFS_BLOCK_SIZE      4096
#define ALEFS_BLOCK_SHIFT     12
#define ALEFS_INODE_SIZE      128
#define ALEFS_INODES_PER_BLK  (ALEFS_BLOCK_SIZE / ALEFS_INODE_SIZE)  // 32
#define ALEFS_NR_EXTENTS      4
#define ALEFS_MAX_NAME        255
#define ALEFS_ROOT_INO        1
#define ALEFS_JOURNAL_BLOCKS  16
#define ALEFS_BITMAP_BLOCKS   4
#define ALEFS_INODE_BLOCKS    64
#define ALEFS_INDIRECT_BLOCK  0

enum alefs_file_type {
    ALEFS_FT_UNKNOWN,
    ALEFS_FT_FILE,
    ALEFS_FT_DIR,
};

enum alefs_cmd {
    ALEFS_CMD_FORMAT,
    ALEFS_CMD_LS,
    ALEFS_CMD_MKDIR,
    ALEFS_CMD_RMDIR,
    ALEFS_CMD_CP_IN,
    ALEFS_CMD_CAT,
    ALEFS_CMD_STAT,
    ALEFS_CMD_MV,
    ALEFS_CMD_RM,
    ALEFS_CMD_CREATE,
    ALEFS_CMD_TREE,
    ALEFS_CMD_DUMP,
};

struct alefs_superblock {
    uint64_t magic;
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t inode_count;
    uint64_t free_blocks;
    uint64_t free_inodes;
    uint64_t root_inode;
    uint64_t journal_start;
    uint64_t journal_blocks;
    uint64_t btree_root;
    uint64_t inode_table_start;
    uint64_t bitmap_start;
    uint64_t data_start;
    uint64_t features;
    uint64_t checksum;
    uint8_t  padding[3976];
} __attribute__((packed));

_Static_assert(sizeof(struct alefs_superblock) == ALEFS_BLOCK_SIZE,
               "superblock must be exactly one block");

struct alefs_extent {
    uint64_t start;
    uint64_t count;
} __attribute__((packed));

struct alefs_inode {
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint16_t links;
    uint32_t flags;
    uint64_t size;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint64_t blocks;
    uint32_t extent_count;
    struct alefs_extent extents[ALEFS_NR_EXTENTS];
    uint8_t  padding[8];
} __attribute__((packed));

_Static_assert(sizeof(struct alefs_inode) == ALEFS_INODE_SIZE,
               "inode size must match ALEFS_INODE_SIZE");

struct alefs_dirent {
    uint64_t ino;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed));

struct alefs_btree_node {
    uint64_t parent;
    uint64_t next;
    uint64_t prev;
    uint16_t num_keys;
    uint16_t is_leaf;
    uint8_t  padding[12];
    uint8_t  data[];
} __attribute__((packed));

#define BTREE_NODE_HEADER_SIZE  offsetof(struct alefs_btree_node, data)
#define BTREE_ORDER             ((ALEFS_BLOCK_SIZE - BTREE_NODE_HEADER_SIZE) / 16)
#define BTREE_MIN_KEYS          (BTREE_ORDER / 2)

struct alefs_btree_entry {
    uint64_t key;
    uint64_t value;
};

struct alefs_dev {
    int      fd;
    uint64_t num_blocks;
    uint64_t block_size;
    char    *path;
    struct alefs_superblock sb;
    bool     dirty;
};

#define ALEFS_ASSERT(x) do { if (!(x)) { fprintf(stderr, "ASSERT: %s at %s:%d\n", #x, __FILE__, __LINE__); exit(1); } } while(0)

// io.c
int  alefs_dev_open(struct alefs_dev *dev, const char *path, int flags);
int  alefs_dev_close(struct alefs_dev *dev);
int  alefs_dev_read(struct alefs_dev *dev, uint64_t block, void *buf);
int  alefs_dev_write(struct alefs_dev *dev, uint64_t block, const void *buf);
int  alefs_dev_sync(struct alefs_dev *dev);
int  alefs_dev_create(const char *path, uint64_t size_mb);

// super.c
int  alefs_super_format(struct alefs_dev *dev, uint64_t total_blocks);
int  alefs_super_load(struct alefs_dev *dev);
int  alefs_super_sync(struct alefs_dev *dev);

// bitmap.c
int  alefs_bitmap_init(struct alefs_dev *dev);
int  alefs_bitmap_alloc(struct alefs_dev *dev, uint64_t *block);
int  alefs_bitmap_free(struct alefs_dev *dev, uint64_t block);
int  alefs_bitmark_set(struct alefs_dev *dev, uint64_t block);
bool alefs_bitmap_get(struct alefs_dev *dev, uint64_t block);

// inode.c
int  alefs_inode_alloc(struct alefs_dev *dev, uint64_t *ino);
int  alefs_inode_free(struct alefs_dev *dev, uint64_t ino);
int  alefs_inode_read(struct alefs_dev *dev, uint64_t ino, struct alefs_inode *inode);
int  alefs_inode_write(struct alefs_dev *dev, uint64_t ino, const struct alefs_inode *inode);

// extent.c
int  alefs_extent_alloc(struct alefs_dev *dev, uint64_t count, struct alefs_extent *ext);
int  alefs_extent_free(struct alefs_dev *dev, const struct alefs_extent *ext);
int  alefs_extent_read(struct alefs_dev *dev, const struct alefs_inode *inode,
                       uint64_t offset, void *buf, uint64_t size);
int  alefs_extent_write(struct alefs_dev *dev, struct alefs_inode *inode,
                        uint64_t offset, const void *buf, uint64_t size);
int  alefs_extent_append(struct alefs_dev *dev, struct alefs_inode *inode,
                         const void *buf, uint64_t size, uint64_t *bytes_written,
                         uint64_t ino);

// dir.c
int  alefs_dir_lookup(struct alefs_dev *dev, uint64_t dir_ino,
                      const char *name, uint64_t *ino);
int  alefs_dir_add_entry(struct alefs_dev *dev, uint64_t dir_ino,
                         uint64_t child_ino, const char *name, uint8_t file_type);
int  alefs_dir_remove_entry(struct alefs_dev *dev, uint64_t dir_ino,
                            const char *name);
int  alefs_dir_list(struct alefs_dev *dev, uint64_t dir_ino, bool show_all);
int  alefs_dir_tree(struct alefs_dev *dev, uint64_t dir_ino, int depth, const char *path);
int  alefs_dir_create(struct alefs_dev *dev, uint64_t parent_ino,
                      const char *name, uint16_t mode, uint64_t *ino);

// path.c
int  alefs_path_resolve(struct alefs_dev *dev, const char *path,
                        uint64_t *parent_ino, char *leaf);
int  alefs_path_resolve_full(struct alefs_dev *dev, const char *path,
                             uint64_t *ino);

// btree.c
int  alefs_btree_init(struct alefs_dev *dev, uint64_t *root_block);
int  alefs_btree_insert(struct alefs_dev *dev, uint64_t root_block,
                        uint64_t key, uint64_t value);
int  alefs_btree_delete(struct alefs_dev *dev, uint64_t root_block,
                        uint64_t key);
int  alefs_btree_lookup(struct alefs_dev *dev, uint64_t root_block,
                        uint64_t key, uint64_t *value);
int  alefs_btree_iterate(struct alefs_dev *dev, uint64_t root_block,
                         uint64_t start, uint64_t end,
                         int (*cb)(uint64_t key, uint64_t value, void *arg),
                         void *arg);

// journal.c
int  alefs_journal_begin(struct alefs_dev *dev);
int  alefs_journal_commit(struct alefs_dev *dev);
int  alefs_journal_recover(struct alefs_dev *dev);

// checksum.c
uint64_t alefs_checksum(const void *data, size_t len);

#endif
