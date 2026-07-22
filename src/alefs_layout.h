#ifndef ALEFS_LAYOUT_H
#define ALEFS_LAYOUT_H

#ifdef __KERNEL__
#include <linux/types.h>
#define ALEFS_ATTR __attribute__((packed))
#else
#include <stdint.h>
#define ALEFS_ATTR __attribute__((packed))
#endif

#define ALEFS_MAGIC           0x414C454653ULL
#define ALEFS_FS_VERSION      1
#define ALEFS_BLOCK_SIZE      4096
#define ALEFS_INODE_SIZE      128
#define ALEFS_INODES_PER_BLK  (ALEFS_BLOCK_SIZE / ALEFS_INODE_SIZE)
#define ALEFS_NR_EXTENTS      4
#define ALEFS_MAX_NAME        255
#define ALEFS_ROOT_INO        1
#define ALEFS_JOURNAL_BLOCKS  16
#define ALEFS_BITMAP_BLOCKS   4
#define ALEFS_INODE_BLOCKS    64
#define ALEFS_BTREE_ORDER     253

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
} ALEFS_ATTR;

struct alefs_extent {
    uint64_t start;
    uint64_t count;
} ALEFS_ATTR;

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
} ALEFS_ATTR;

struct alefs_btree_node {
    uint64_t parent;
    uint64_t next;
    uint64_t prev;
    uint16_t num_keys;
    uint16_t is_leaf;
    uint8_t  padding[12];
    uint8_t  data[];
} ALEFS_ATTR;

struct alefs_btree_entry {
    uint64_t key;
    uint64_t value;
};

/* Directory data block entries.
 * Stored in data blocks pointed to by the directory inode's extents.
 * Packed sequentially; no padding between entries.
 * An entry with name_len == 0 marks end-of-entries in a block.
 */
struct alefs_direntry {
    uint64_t ino;
    uint8_t  name_len;
    uint8_t  name[];
} ALEFS_ATTR;

#define ALEFS_DIR_ENTRY_MIN (sizeof(struct alefs_direntry))  /* 9 */
#define ALEFS_DIR_ENTRY_MAX (ALEFS_DIR_ENTRY_MIN + ALEFS_MAX_NAME)  /* 264 */

#endif
