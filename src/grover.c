#include "aleqfs.h"

#define GROVER_HASH_BITS 10

static uint64_t grover_hash(uint64_t key)
{
    return (key * 0x9E3779B97F4A7C15ULL) >> (64 - GROVER_HASH_BITS);
}

static int ensure_chain(struct aleqfs_dev *dev, uint64_t root_block,
                         uint64_t idx, uint64_t *block)
{
    uint8_t buf[ALEQFS_BLOCK_SIZE];
    struct aleqfs_grover_node *node = (struct aleqfs_grover_node *)buf;
    uint64_t cur = root_block;
    uint64_t i = 0;
    int ret;

    while (i < idx) {
        ret = aleqfs_dev_read(dev, cur, buf);
        if (ret < 0)
            return ret;
        if (node->next == 0) {
            uint64_t new_blk;
            uint8_t new_buf[ALEQFS_BLOCK_SIZE];
            struct aleqfs_grover_node *new_node = (struct aleqfs_grover_node *)new_buf;
            ret = aleqfs_bitmap_alloc(dev, &new_blk);
            if (ret < 0)
                return ret;
            memset(new_buf, 0, sizeof(new_buf));
            new_node->parent = cur;
            new_node->num_slots = ALEQFS_GROVER_SLOTS_PER_BLK;
            ret = aleqfs_dev_write(dev, new_blk, new_buf);
            if (ret < 0)
                return ret;
            node->next = new_blk;
            ret = aleqfs_dev_write(dev, cur, buf);
            if (ret < 0)
                return ret;
        }
        cur = node->next;
        i++;
    }
    *block = cur;
    return 0;
}

int aleqfs_grover_init(struct aleqfs_dev *dev, uint64_t *root_block)
{
    uint8_t buf[ALEQFS_BLOCK_SIZE];
    struct aleqfs_grover_node *node = (struct aleqfs_grover_node *)buf;
    int ret;

    ret = aleqfs_bitmap_alloc(dev, root_block);
    if (ret < 0)
        return ret;

    memset(buf, 0, sizeof(buf));
    node->num_slots = ALEQFS_GROVER_SLOTS_PER_BLK;

    ret = aleqfs_dev_write(dev, *root_block, buf);
    if (ret < 0)
        return ret;

    return 0;
}

int aleqfs_grover_insert(struct aleqfs_dev *dev, uint64_t root_block,
                          uint64_t key, uint64_t value,
                          int16_t amp_real, int16_t amp_imag)
{
    uint64_t hash = grover_hash(key);
    uint64_t start_block_idx = hash / ALEQFS_GROVER_SLOTS_PER_BLK;
    uint64_t start_off = hash % ALEQFS_GROVER_SLOTS_PER_BLK;
    uint8_t buf[ALEQFS_BLOCK_SIZE];
    struct aleqfs_grover_node *node = (struct aleqfs_grover_node *)buf;
    uint64_t blk;
    int ret;

    ret = ensure_chain(dev, root_block, start_block_idx, &blk);
    if (ret < 0)
        return ret;

    while (1) {
        ret = aleqfs_dev_read(dev, blk, buf);
        if (ret < 0)
            return ret;

        for (uint64_t i = start_off; i < node->num_slots; i++) {
            if (node->slots[i].key == key) {
                node->slots[i].value = value;
                node->slots[i].amplitude_real = amp_real;
                node->slots[i].amplitude_imag = amp_imag;
                node->slots[i].flags = 1;
                return aleqfs_dev_write(dev, blk, buf);
            }
            if (node->slots[i].key == 0) {
                node->slots[i].key = key;
                node->slots[i].value = value;
                node->slots[i].amplitude_real = amp_real;
                node->slots[i].amplitude_imag = amp_imag;
                node->slots[i].flags = 1;
                return aleqfs_dev_write(dev, blk, buf);
            }
        }

        if (node->next == 0) {
            uint64_t new_blk;
            uint8_t new_buf[ALEQFS_BLOCK_SIZE];
            struct aleqfs_grover_node *new_node = (struct aleqfs_grover_node *)new_buf;
            ret = aleqfs_bitmap_alloc(dev, &new_blk);
            if (ret < 0)
                return ret;
            memset(new_buf, 0, sizeof(new_buf));
            new_node->parent = blk;
            new_node->num_slots = ALEQFS_GROVER_SLOTS_PER_BLK;
            new_node->slots[0].key = key;
            new_node->slots[0].value = value;
            new_node->slots[0].amplitude_real = amp_real;
            new_node->slots[0].amplitude_imag = amp_imag;
            new_node->slots[0].flags = 1;
            ret = aleqfs_dev_write(dev, new_blk, new_buf);
            if (ret < 0)
                return ret;
            node->next = new_blk;
            return aleqfs_dev_write(dev, blk, buf);
        }

        blk = node->next;
        start_off = 0;
    }
}

int aleqfs_grover_lookup(struct aleqfs_dev *dev, uint64_t root_block,
                          uint64_t key, uint64_t *value)
{
    uint64_t hash = grover_hash(key);
    uint64_t start_block_idx = hash / ALEQFS_GROVER_SLOTS_PER_BLK;
    uint64_t start_off = hash % ALEQFS_GROVER_SLOTS_PER_BLK;
    uint8_t buf[ALEQFS_BLOCK_SIZE];
    struct aleqfs_grover_node *node = (struct aleqfs_grover_node *)buf;
    uint64_t blk = root_block;
    uint64_t idx = 0;
    int ret;

    while (idx < start_block_idx) {
        ret = aleqfs_dev_read(dev, blk, buf);
        if (ret < 0)
            return ret;
        if (node->next == 0)
            return -2;
        blk = node->next;
        idx++;
    }

    while (1) {
        ret = aleqfs_dev_read(dev, blk, buf);
        if (ret < 0)
            return ret;

        for (uint64_t i = start_off; i < node->num_slots; i++) {
            if (node->slots[i].key == key) {
                *value = node->slots[i].value;
                return 0;
            }
            if (node->slots[i].key == 0)
                return -2;
        }

        if (node->next == 0)
            return -2;

        blk = node->next;
        start_off = 0;
    }
}

int aleqfs_grover_delete(struct aleqfs_dev *dev, uint64_t root_block, uint64_t key)
{
    uint64_t hash = grover_hash(key);
    uint64_t start_block_idx = hash / ALEQFS_GROVER_SLOTS_PER_BLK;
    uint64_t start_off = hash % ALEQFS_GROVER_SLOTS_PER_BLK;
    uint8_t buf[ALEQFS_BLOCK_SIZE];
    struct aleqfs_grover_node *node = (struct aleqfs_grover_node *)buf;
    uint64_t blk = root_block;
    uint64_t idx = 0;
    int ret;

    while (idx < start_block_idx) {
        ret = aleqfs_dev_read(dev, blk, buf);
        if (ret < 0)
            return ret;
        if (node->next == 0)
            return -2;
        blk = node->next;
        idx++;
    }

    while (1) {
        ret = aleqfs_dev_read(dev, blk, buf);
        if (ret < 0)
            return ret;

        for (uint64_t i = start_off; i < node->num_slots; i++) {
            if (node->slots[i].key == key) {
                memset(&node->slots[i], 0, sizeof(node->slots[i]));
                return aleqfs_dev_write(dev, blk, buf);
            }
            if (node->slots[i].key == 0)
                return -2;
        }

        if (node->next == 0)
            return -2;

        blk = node->next;
        start_off = 0;
    }
}

int aleqfs_grover_search(struct aleqfs_dev *dev, uint64_t root_block,
                          uint64_t pattern, uint64_t *results, int max_results)
{
    uint8_t buf[ALEQFS_BLOCK_SIZE];
    struct aleqfs_grover_node *node = (struct aleqfs_grover_node *)buf;
    uint64_t blk = root_block;
    int found = 0;
    int ret;

    while (blk != 0) {
        ret = aleqfs_dev_read(dev, blk, buf);
        if (ret < 0)
            return ret;

        for (uint64_t i = 0; i < node->num_slots; i++) {
            if (node->slots[i].key != 0 &&
                (node->slots[i].key & pattern) == pattern) {
                if (found < max_results)
                    results[found] = node->slots[i].value;
                found++;
            }
        }

        blk = node->next;
    }

    return found;
}
