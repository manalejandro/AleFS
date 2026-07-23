#include "aleqfs.h"
#include <string.h>
#include <errno.h>

int aleqfs_path_resolve(struct aleqfs_dev *dev, const char *path,
                        uint64_t *parent_ino, char *leaf)
{
    char work[ALEQFS_MAX_NAME * 8];
    strncpy(work, path, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    uint64_t current = dev->sb.root_inode;

    char *saveptr;
    char *token = strtok_r(work, "/", &saveptr);
    char *prev_token = NULL;

    if (!token) {
        *parent_ino = current;
        leaf[0] = '\0';
        return 0;
    }

    while (token) {
        prev_token = token;
        token = strtok_r(NULL, "/", &saveptr);
        if (token) {
            uint64_t next_ino;
            int ret = aleqfs_dir_lookup(dev, current, prev_token, &next_ino);
            if (ret) return ret;
            current = next_ino;
        }
    }

    *parent_ino = current;
    strncpy(leaf, prev_token ? prev_token : "", ALEQFS_MAX_NAME);
    leaf[ALEQFS_MAX_NAME] = '\0';
    return 0;
}

int aleqfs_path_resolve_full(struct aleqfs_dev *dev, const char *path,
                             uint64_t *ino)
{
    if (strcmp(path, "/") == 0) {
        *ino = dev->sb.root_inode;
        return 0;
    }

    char name[ALEQFS_MAX_NAME + 1];
    uint64_t parent;

    int ret = aleqfs_path_resolve(dev, path, &parent, name);
    if (ret) return ret;

    return aleqfs_dir_lookup(dev, parent, name, ino);
}
