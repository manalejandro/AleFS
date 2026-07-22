#include "alefs.h"
#include <string.h>
#include <errno.h>

static inline struct alefs_btree_entry *
entry_at(struct alefs_btree_node *node, int idx)
{
    return (struct alefs_btree_entry *)(node->data + idx * sizeof(struct alefs_btree_entry));
}

int alefs_btree_init(struct alefs_dev *dev, uint64_t *root_block)
{
    int ret = alefs_bitmap_alloc(dev, root_block);
    if (ret) return ret;

    uint8_t buf[ALEFS_BLOCK_SIZE] = {0};
    struct alefs_btree_node *node = (struct alefs_btree_node *)buf;
    node->is_leaf = 1;
    node->num_keys = 0;

    return alefs_dev_write(dev, *root_block, buf);
}

int alefs_btree_insert(struct alefs_dev *dev, uint64_t root_block,
                       uint64_t key, uint64_t value)
{
    uint8_t buf[ALEFS_BLOCK_SIZE];
    int ret = alefs_dev_read(dev, root_block, buf);
    if (ret) return ret;

    struct alefs_btree_node *node = (struct alefs_btree_node *)buf;

    for (int i = 0; i < node->num_keys; i++) {
        if (entry_at(node, i)->key == key) {
            entry_at(node, i)->value = value;
            return alefs_dev_write(dev, root_block, buf);
        }
    }

    if (node->num_keys >= BTREE_ORDER)
        return -ENOSPC;

    int i;
    for (i = node->num_keys - 1; i >= 0 && entry_at(node, i)->key > key; i--)
        *entry_at(node, i + 1) = *entry_at(node, i);

    entry_at(node, i + 1)->key = key;
    entry_at(node, i + 1)->value = value;
    node->num_keys++;

    return alefs_dev_write(dev, root_block, buf);
}

int alefs_btree_lookup(struct alefs_dev *dev, uint64_t root_block,
                       uint64_t key, uint64_t *value)
{
    uint8_t buf[ALEFS_BLOCK_SIZE];
    int ret = alefs_dev_read(dev, root_block, buf);
    if (ret) return ret;

    struct alefs_btree_node *node = (struct alefs_btree_node *)buf;

    if (!node->is_leaf) {
        while (!node->is_leaf) {
            uint64_t child = *(uint64_t *)node->data;
            ret = alefs_dev_read(dev, child, buf);
            if (ret) return ret;
            node = (struct alefs_btree_node *)buf;
        }
    }

    for (int i = 0; i < node->num_keys; i++) {
        if (entry_at(node, i)->key == key) {
            *value = entry_at(node, i)->value;
            return 0;
        }
    }

    return -ENOENT;
}

int alefs_btree_delete(struct alefs_dev *dev, uint64_t root_block,
                       uint64_t key)
{
    uint8_t buf[ALEFS_BLOCK_SIZE];
    int ret = alefs_dev_read(dev, root_block, buf);
    if (ret) return ret;

    struct alefs_btree_node *node = (struct alefs_btree_node *)buf;

    int idx = -1;
    for (int i = 0; i < node->num_keys; i++) {
        if (entry_at(node, i)->key == key) {
            idx = i;
            break;
        }
    }

    if (idx < 0) return -ENOENT;

    memmove(entry_at(node, idx), entry_at(node, idx + 1),
            (node->num_keys - idx - 1) * sizeof(struct alefs_btree_entry));
    node->num_keys--;

    return alefs_dev_write(dev, root_block, buf);
}

int alefs_btree_iterate(struct alefs_dev *dev, uint64_t root_block,
                        uint64_t start, uint64_t end,
                        int (*cb)(uint64_t key, uint64_t value, void *arg),
                        void *arg)
{
    uint8_t buf[ALEFS_BLOCK_SIZE];
    int ret = alefs_dev_read(dev, root_block, buf);
    if (ret) return ret;

    struct alefs_btree_node *node = (struct alefs_btree_node *)buf;

    if (!node->is_leaf) {
        while (!node->is_leaf) {
            uint64_t child = *(uint64_t *)node->data;
            ret = alefs_dev_read(dev, child, buf);
            if (ret) return ret;
            node = (struct alefs_btree_node *)buf;
        }
    }

    for (int i = 0; i < node->num_keys; i++) {
        uint64_t k = entry_at(node, i)->key;
        uint64_t v = entry_at(node, i)->value;

        if ((start == 0 || k >= start) && (end == 0 || k <= end)) {
            ret = cb(k, v, arg);
            if (ret) return ret;
        }
    }

    return 0;
}
