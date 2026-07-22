#include "alefs.h"
#include <string.h>
#include <errno.h>

int alefs_path_resolve(struct alefs_dev *dev, const char *path,
                       uint64_t *parent_ino, char *leaf)
{
    char work[ALEFS_MAX_NAME * 8];
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
            int ret = alefs_dir_lookup(dev, current, prev_token, &next_ino);
            if (ret) return ret;
            struct alefs_inode inode;
            ret = alefs_inode_read(dev, next_ino, &inode);
            if (ret) return ret;
            if (!S_ISDIR(inode.mode))
                return -ENOTDIR;
            current = next_ino;
        }
    }

    *parent_ino = current;
    strncpy(leaf, prev_token ? prev_token : "", ALEFS_MAX_NAME);
    leaf[ALEFS_MAX_NAME] = '\0';
    return 0;
}

int alefs_path_resolve_full(struct alefs_dev *dev, const char *path,
                            uint64_t *ino)
{
    char name[ALEFS_MAX_NAME + 1];
    uint64_t parent;

    if (strcmp(path, "/") == 0) {
        *ino = dev->sb.root_inode;
        return 0;
    }

    int ret = alefs_path_resolve(dev, path, &parent, name);
    if (ret) return ret;

    return alefs_dir_lookup(dev, parent, name, ino);
}
