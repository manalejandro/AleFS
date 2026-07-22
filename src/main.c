#include "alefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <time.h>
#include <stdint.h>

static void print_usage(void)
{
    printf("AleFS — Adaptive Linux Efficient Filesystem v" ALEFS_VERSION "\n");
    printf("Usage:\n");
    printf("  alefs format <img> <size_mb>\n");
    printf("  alefs ls <img> <path>\n");
    printf("  alefs mkdir <img> <path>\n");
    printf("  alefs rmdir <img> <path>\n");
    printf("  alefs cp-in <img> <src> <dst>\n");
    printf("  alefs cat <img> <path>\n");
    printf("  alefs stat <img> <path>\n");
    printf("  alefs mv <img> <src> <dst>\n");
    printf("  alefs rm <img> <path>\n");
    printf("  alefs create <img> <path>\n");
    printf("  alefs tree <img> <path>\n");
    printf("  alefs dump <img>\n");
    printf("  alefs mkfs <device> [size_mb]\n");
    printf("\nSymlink invocation:\n");
    printf("  mkfs.alefs <device> [size_mb]\n");
    printf("  mount.alefs <device> <mountpoint>\n");
}

static int cmd_format(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: alefs format <img> <size_mb>\n"); return 1; }
    const char *path = argv[2];
    uint64_t size_mb = strtoull(argv[3], NULL, 10);
    if (size_mb == 0) { fprintf(stderr, "invalid size\n"); return 1; }

    int ret = alefs_dev_create(path, size_mb);
    if (ret) { fprintf(stderr, "create failed: %s\n", strerror(-ret)); return 1; }

    struct alefs_dev dev;
    ret = alefs_dev_open(&dev, path, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    uint64_t total_blocks = (size_mb * 1024ULL * 1024ULL) / ALEFS_BLOCK_SIZE;
    ret = alefs_super_format(&dev, total_blocks);
    if (ret) { fprintf(stderr, "format failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    ret = alefs_journal_begin(&dev);
    if (ret) { fprintf(stderr, "journal begin failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    ret = alefs_journal_commit(&dev);
    if (ret) { fprintf(stderr, "journal commit failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    alefs_dev_close(&dev);
    printf("Formatted %s: %lu MiB, %lu blocks\n",
           path, (unsigned long)size_mb, (unsigned long)total_blocks);
    return 0;
}

static void print_mkfs_help(void)
{
    printf("Usage: mkfs.alefs [options] <device> [size_mb]\n");
    printf("Create an AleFS filesystem on a device or image file.\n");
    printf("\nOptions:\n");
    printf("  -V              print version\n");
    printf("  -h, --help      display this help\n");
    printf("\nArguments:\n");
    printf("  device          path to block device or image file\n");
    printf("  size_mb         size in MiB (default: 64 for regular files)\n");
    printf("\nExamples:\n");
    printf("  mkfs.alefs /tmp/test.img 64\n");
    printf("  mkfs.alefs /dev/sdb1\n");
    printf("  alefs mkfs /tmp/test.img 64\n");
}

static int cmd_mkfs(int argc, char **argv)
{
    int arg_offset = (strcmp(argv[0], "mkfs.alefs") == 0) ? 1 : 2;

    if (argc <= arg_offset) {
        print_mkfs_help();
        return 0;
    }

    if (strcmp(argv[arg_offset], "-V") == 0) {
        printf("mkfs.alefs " ALEFS_VERSION "\n");
        return 0;
    }
    if (strcmp(argv[arg_offset], "-h") == 0 ||
        strcmp(argv[arg_offset], "--help") == 0) {
        print_mkfs_help();
        return 0;
    }

    const char *device = argv[arg_offset];
    uint64_t size_mb = 0;

    if (argc > arg_offset + 1)
        size_mb = strtoull(argv[arg_offset + 1], NULL, 10);

    if (size_mb == 0) {
        struct stat st;
        if (stat(device, &st) == 0) {
            if (S_ISREG(st.st_mode))
                size_mb = st.st_size / (1024 * 1024);
            else if (S_ISBLK(st.st_mode)) {
                int fd = open(device, O_RDONLY);
                if (fd >= 0) {
                    off_t sz = lseek(fd, 0, SEEK_END);
                    if (sz > 0)
                        size_mb = (uint64_t)sz / (1024 * 1024);
                    close(fd);
                }
            }
        }
        if (size_mb == 0)
            size_mb = 64;
    }

    int ret = alefs_dev_create(device, size_mb);
    if (ret && ret != -EEXIST) {
        if (ret == -EACCES)
            fprintf(stderr, "mkfs.alefs: cannot open %s (try as root)\n", device);
        else
            fprintf(stderr, "mkfs.alefs: %s: %s\n", device, strerror(-ret));
        return 1;
    }

    struct alefs_dev dev;
    ret = alefs_dev_open(&dev, device, O_RDWR);
    if (ret) { fprintf(stderr, "mkfs.alefs: open failed: %s\n", strerror(-ret)); return 1; }

    uint64_t total_blocks = size_mb * 1024ULL * 1024ULL / ALEFS_BLOCK_SIZE;
    ret = alefs_super_format(&dev, total_blocks);
    if (ret) { fprintf(stderr, "mkfs.alefs: format failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    alefs_dev_close(&dev);
    printf("mkfs.alefs: formatted %s: %lu MiB (%lu blocks)\n",
           device, (unsigned long)size_mb, (unsigned long)total_blocks);
    return 0;
}

static int cmd_mount(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: alefs mount <img> <mountpoint> [options]\n");
        fprintf(stderr, "       mount.alefs <img> <mountpoint> [-o options]\n");
        fprintf(stderr, "\nRequires the alefs kernel module loaded:\n");
        fprintf(stderr, "  sudo insmod alefs.ko\n");
        fprintf(stderr, "  sudo mount -t alefs <img> <mountpoint>\n");
        fprintf(stderr, "  sudo mount.alefs <img> <mountpoint>\n");
        return 1;
    }

    const char *img = argv[2];
    const char *mnt = argv[3];
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            i++;
    }

    if (access(mnt, F_OK) != 0) {
        fprintf(stderr, "mount: %s does not exist (create it first)\n", mnt);
        return 1;
    }

    fprintf(stderr, "mount: use 'sudo mount -t alefs %s %s' instead\n", img, mnt);
    fprintf(stderr, "       (requires the alefs kernel module: insmod alefs.ko)\n");
    return 1;
}

/* mount.alefs helper: invoked by mount -t alefs or udev
 * Modes:
 *   mount.alefs --probe <device>   — check AleFS magic, return udev env
 *   mount.alefs <device> [mnt]      — mount AleFS filesystem
 */
static int run_mount_alefs(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: mount.alefs [--probe] <device> [mountpoint]\n");
        return 1;
    }

    int probe_mode = 0;
    int arg_idx = 1;

    if (strcmp(argv[1], "--probe") == 0) {
        probe_mode = 1;
        arg_idx = 2;
        if (argc < 3) return 1;
    }

    const char *device = argv[arg_idx];

    if (probe_mode) {
        uint64_t magic = 0;
        int fd = open(device, O_RDONLY);
        if (fd < 0) return 1;
        if (read(fd, &magic, sizeof(magic)) != sizeof(magic)) {
            close(fd);
            return 1;
        }
        close(fd);
        if (magic != ALEFS_MAGIC)
            return 1;
        printf("ID_FS_TYPE=alefs\n");
        return 0;
    }

    const char *mnt;

    if (argc > arg_idx + 1) {
        mnt = argv[arg_idx + 1];
    } else {
        static char auto_mnt[256];
        const char *base = strrchr(device, '/');
        if (!base) base = device; else base++;
        snprintf(auto_mnt, sizeof(auto_mnt), "/media/%s", base);
        if (mkdir(auto_mnt, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "mount.alefs: cannot create %s: %s\n",
                    auto_mnt, strerror(errno));
            return 1;
        }
        mnt = auto_mnt;
    }

    system("modprobe alefs 2>/dev/null");

    unsigned long flags = 0;
    int ret = mount(device, mnt, "alefs", flags, NULL);
    if (ret < 0) {
        fprintf(stderr, "mount.alefs: mount %s on %s failed: %s\n",
                device, mnt, strerror(errno));
        return 1;
    }
    printf("mount.alefs: %s mounted on %s\n", device, mnt);
    return 0;
}

static int cmd_mkdir(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: alefs mkdir <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct alefs_dev dev;
    int ret = alefs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = alefs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    char name[ALEFS_MAX_NAME + 1];
    uint64_t parent;
    ret = alefs_path_resolve(&dev, path, &parent, name);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    if (name[0] == '\0') { fprintf(stderr, "mkdir: cannot create root\n"); alefs_dev_close(&dev); return 1; }

    uint64_t ino;
    ret = alefs_dir_create(&dev, parent, name, S_IFDIR | 0755, &ino);
    if (ret) { fprintf(stderr, "mkdir failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    printf("Created directory: %s (ino=%lu)\n", path, (unsigned long)ino);
    alefs_dev_close(&dev);
    return 0;
}

static int cmd_rmdir(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: alefs rmdir <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct alefs_dev dev;
    int ret = alefs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = alefs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    if (strcmp(path, "/") == 0) {
        fprintf(stderr, "rmdir: cannot remove root\n");
        alefs_dev_close(&dev);
        return 1;
    }

    char name[ALEFS_MAX_NAME + 1];
    uint64_t parent;
    ret = alefs_path_resolve(&dev, path, &parent, name);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    uint64_t child_ino;
    ret = alefs_dir_lookup(&dev, parent, name, &child_ino);
    if (ret) { fprintf(stderr, "lookup failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    struct alefs_inode inode;
    ret = alefs_inode_read(&dev, child_ino, &inode);
    if (ret) { fprintf(stderr, "read inode failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    if (!S_ISDIR(inode.mode)) {
        fprintf(stderr, "rmdir: not a directory\n");
        alefs_dev_close(&dev);
        return 1;
    }

    ret = alefs_dir_remove_entry(&dev, parent, name);
    if (ret) { fprintf(stderr, "remove entry failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    ret = alefs_inode_free(&dev, child_ino);
    if (ret) { fprintf(stderr, "free inode failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    printf("Removed directory: %s\n", path);
    alefs_dev_close(&dev);
    return 0;
}

static int cmd_ls(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: alefs ls <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct alefs_dev dev;
    int ret = alefs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = alefs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    uint64_t dir_ino;
    ret = alefs_path_resolve_full(&dev, path, &dir_ino);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    ret = alefs_dir_list(&dev, dir_ino, false);
    if (ret) { fprintf(stderr, "list failed: %s\n", strerror(-ret)); }

    alefs_dev_close(&dev);
    return ret ? 1 : 0;
}

static int cmd_cp_in(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: alefs cp-in <img> <src> <dst>\n"); return 1; }
    const char *img = argv[2];
    const char *src_path = argv[3];
    const char *dst_path = argv[4];

    FILE *src = fopen(src_path, "rb");
    if (!src) { fprintf(stderr, "open src failed: %s\n", strerror(errno)); return 1; }

    fseek(src, 0, SEEK_END);
    long file_size = ftell(src);
    fseek(src, 0, SEEK_SET);

    uint8_t *data = malloc(file_size > 0 ? file_size : 1);
    if (!data) { fclose(src); fprintf(stderr, "malloc failed\n"); return 1; }

    size_t nread = fread(data, 1, file_size, src);
    fclose(src);

    struct alefs_dev dev;
    int ret = alefs_dev_open(&dev, img, O_RDWR);
    if (ret) { free(data); fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = alefs_super_load(&dev);
    if (ret) { free(data); alefs_dev_close(&dev); fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); return 1; }

    char name[ALEFS_MAX_NAME + 1];
    uint64_t parent;
    ret = alefs_path_resolve(&dev, dst_path, &parent, name);
    if (ret) { free(data); alefs_dev_close(&dev); fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); return 1; }

    uint64_t ino;
    ret = alefs_dir_create(&dev, parent, name, S_IFREG | 0644, &ino);
    if (ret) { free(data); alefs_dev_close(&dev); fprintf(stderr, "create file failed: %s\n", strerror(-ret)); return 1; }

    struct alefs_inode inode;
    ret = alefs_inode_read(&dev, ino, &inode);
    if (ret) { free(data); alefs_dev_close(&dev); fprintf(stderr, "read inode failed: %s\n", strerror(-ret)); return 1; }

    uint64_t written;
    ret = alefs_extent_append(&dev, &inode, data, nread, &written, ino);
    if (ret) { free(data); alefs_dev_close(&dev); fprintf(stderr, "write failed: %s\n", strerror(-ret)); return 1; }

    free(data);
    printf("Copied %zu bytes to %s (ino=%lu)\n", nread, dst_path, (unsigned long)ino);
    alefs_dev_close(&dev);
    return 0;
}

static int cmd_cat(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: alefs cat <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct alefs_dev dev;
    int ret = alefs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = alefs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    uint64_t ino;
    ret = alefs_path_resolve_full(&dev, path, &ino);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    struct alefs_inode inode;
    ret = alefs_inode_read(&dev, ino, &inode);
    if (ret) { fprintf(stderr, "read inode failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    if (!S_ISREG(inode.mode)) {
        fprintf(stderr, "cat: not a regular file\n");
        alefs_dev_close(&dev);
        return 1;
    }

    uint8_t *buf = malloc(inode.size > 0 ? inode.size : 1);
    if (!buf) { alefs_dev_close(&dev); return 1; }

    ret = alefs_extent_read(&dev, &inode, 0, buf, inode.size);
    if (ret < 0) { free(buf); alefs_dev_close(&dev); fprintf(stderr, "read failed: %s\n", strerror(-ret)); return 1; }

    fwrite(buf, 1, inode.size, stdout);
    free(buf);
    alefs_dev_close(&dev);
    return 0;
}

static int cmd_stat(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: alefs stat <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct alefs_dev dev;
    int ret = alefs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = alefs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    uint64_t ino;
    ret = alefs_path_resolve_full(&dev, path, &ino);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    struct alefs_inode inode;
    ret = alefs_inode_read(&dev, ino, &inode);
    if (ret) { fprintf(stderr, "read inode failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

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

    char atime_buf[64], mtime_buf[64], ctime_buf[64];
    time_t at = (time_t)inode.atime;
    time_t mt = (time_t)inode.mtime;
    time_t ct = (time_t)inode.ctime;
    struct tm *tm;
    tm = localtime(&at);
    strftime(atime_buf, sizeof(atime_buf), "%Y-%m-%d %H:%M:%S", tm);
    tm = localtime(&mt);
    strftime(mtime_buf, sizeof(mtime_buf), "%Y-%m-%d %H:%M:%S", tm);
    tm = localtime(&ct);
    strftime(ctime_buf, sizeof(ctime_buf), "%Y-%m-%d %H:%M:%S", tm);

    printf("  File: %s\n", path);
    printf("  Inode: %lu\n", (unsigned long)ino);
    printf("  Size: %lu\n", (unsigned long)inode.size);
    printf("  Blocks: %lu\n", (unsigned long)inode.blocks);
    printf("  Mode: %s (%04o)\n", mode_str, inode.mode & 07777);
    printf("  UID: %u  GID: %u\n", inode.uid, inode.gid);
    printf("  Links: %u\n", inode.links);
    printf("  Access: %s\n", atime_buf);
    printf("  Modify: %s\n", mtime_buf);
    printf("  Change: %s\n", ctime_buf);
    printf("  Extents: %u\n", inode.extent_count);
    for (uint32_t i = 0; i < inode.extent_count; i++)
        printf("    [%u] block=%lu count=%lu\n",
               i, (unsigned long)inode.extents[i].start,
               (unsigned long)inode.extents[i].count);

    alefs_dev_close(&dev);
    return 0;
}

static int cmd_mv(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: alefs mv <img> <src> <dst>\n"); return 1; }
    const char *img = argv[2];
    const char *src_path = argv[3];
    const char *dst_path = argv[4];

    struct alefs_dev dev;
    int ret = alefs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = alefs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    char src_name[ALEFS_MAX_NAME + 1];
    uint64_t src_parent;
    ret = alefs_path_resolve(&dev, src_path, &src_parent, src_name);
    if (ret) { fprintf(stderr, "src resolve failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    uint64_t child_ino;
    ret = alefs_dir_lookup(&dev, src_parent, src_name, &child_ino);
    if (ret) { fprintf(stderr, "src lookup failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    ret = alefs_dir_remove_entry(&dev, src_parent, src_name);
    if (ret) { fprintf(stderr, "remove entry failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    char dst_name[ALEFS_MAX_NAME + 1];
    uint64_t dst_parent;
    ret = alefs_path_resolve(&dev, dst_path, &dst_parent, dst_name);
    if (ret) { fprintf(stderr, "dst resolve failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    struct alefs_inode child_inode;
    ret = alefs_inode_read(&dev, child_ino, &child_inode);
    if (ret) { fprintf(stderr, "read child inode failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    uint8_t ft = S_ISDIR(child_inode.mode) ? ALEFS_FT_DIR : ALEFS_FT_FILE;
    ret = alefs_dir_add_entry(&dev, dst_parent, child_ino, dst_name, ft);
    if (ret) { fprintf(stderr, "add entry failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    printf("Moved: %s -> %s\n", src_path, dst_path);
    alefs_dev_close(&dev);
    return 0;
}

static int cmd_rm(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: alefs rm <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct alefs_dev dev;
    int ret = alefs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = alefs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    if (strcmp(path, "/") == 0) {
        fprintf(stderr, "rm: cannot remove root\n");
        alefs_dev_close(&dev);
        return 1;
    }

    char name[ALEFS_MAX_NAME + 1];
    uint64_t parent;
    ret = alefs_path_resolve(&dev, path, &parent, name);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    uint64_t child_ino;
    ret = alefs_dir_lookup(&dev, parent, name, &child_ino);
    if (ret) { fprintf(stderr, "lookup failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    struct alefs_inode inode;
    ret = alefs_inode_read(&dev, child_ino, &inode);
    if (ret) { fprintf(stderr, "read inode failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    if (S_ISDIR(inode.mode)) {
        fprintf(stderr, "rm: is a directory, use rmdir\n");
        alefs_dev_close(&dev);
        return 1;
    }

    ret = alefs_dir_remove_entry(&dev, parent, name);
    if (ret) { fprintf(stderr, "remove entry failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    for (uint32_t i = 0; i < inode.extent_count; i++)
        alefs_extent_free(&dev, &inode.extents[i]);

    ret = alefs_inode_free(&dev, child_ino);
    if (ret) { fprintf(stderr, "free inode failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    printf("Removed: %s\n", path);
    alefs_dev_close(&dev);
    return 0;
}

static int cmd_create(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: alefs create <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct alefs_dev dev;
    int ret = alefs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = alefs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    char name[ALEFS_MAX_NAME + 1];
    uint64_t parent;
    ret = alefs_path_resolve(&dev, path, &parent, name);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    uint64_t ino;
    ret = alefs_dir_create(&dev, parent, name, S_IFREG | 0644, &ino);
    if (ret) { fprintf(stderr, "create failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    printf("Created empty file: %s (ino=%lu)\n", path, (unsigned long)ino);
    alefs_dev_close(&dev);
    return 0;
}

struct tree_print_ctx {
    struct alefs_dev *dev;
    int depth;
};

static int tree_print_cb(uint64_t key, uint64_t value, void *arg)
{
    struct tree_print_ctx *ctx = (struct tree_print_ctx *)arg;
    struct alefs_inode inode;
    (void)key;

    if (alefs_inode_read(ctx->dev, value, &inode) != 0)
        return 0;

    for (int i = 0; i < ctx->depth; i++)
        printf("    ");

    printf("\u2514\u2500\u2500 ino=%lu %s size=%lu\n",
           (unsigned long)value,
           S_ISDIR(inode.mode) ? "(dir)" : "(file)",
           (unsigned long)inode.size);

    if (S_ISDIR(inode.mode)) {
        struct tree_print_ctx sub_ctx;
        sub_ctx.dev = ctx->dev;
        sub_ctx.depth = ctx->depth + 1;
        uint64_t sk = value << 32;
        uint64_t ek = ((value + 1) << 32) - 1;
        alefs_btree_iterate(ctx->dev, ctx->dev->sb.btree_root,
                             sk, ek, tree_print_cb, &sub_ctx);
    }

    return 0;
}

static int cmd_tree(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: alefs tree <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct alefs_dev dev;
    int ret = alefs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = alefs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    uint64_t dir_ino;
    ret = alefs_path_resolve_full(&dev, path, &dir_ino);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    printf("%s\n", path);

    struct tree_print_ctx ctx;
    ctx.dev = &dev;
    ctx.depth = 1;

    uint64_t sk = dir_ino << 32;
    uint64_t ek = ((dir_ino + 1) << 32) - 1;
    ret = alefs_btree_iterate(&dev, dev.sb.btree_root,
                               sk, ek, tree_print_cb, &ctx);

    alefs_dev_close(&dev);
    return ret ? 1 : 0;
}

static int cmd_dump(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: alefs dump <img>\n"); return 1; }
    const char *img = argv[2];

    struct alefs_dev dev;
    int ret = alefs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = alefs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); alefs_dev_close(&dev); return 1; }

    struct alefs_superblock *sb = &dev.sb;
    printf("AleFS Superblock Dump\n");
    printf("  Magic:        0x%016lX\n", (unsigned long)sb->magic);
    printf("  Version:      %u\n", sb->version);
    printf("  Block Size:   %u\n", sb->block_size);
    printf("  Total Blocks: %lu\n", (unsigned long)sb->total_blocks);
    printf("  Total Size:   %lu MiB\n",
           (unsigned long)(sb->total_blocks * sb->block_size / (1024*1024)));
    printf("  Inode Count:  %lu\n", (unsigned long)sb->inode_count);
    printf("  Free Inodes:  %lu\n", (unsigned long)sb->free_inodes);
    printf("  Free Blocks:  %lu\n", (unsigned long)sb->free_blocks);
    printf("  Root Inode:   %lu\n", (unsigned long)sb->root_inode);
    printf("  Journal:      block=%lu count=%lu\n",
           (unsigned long)sb->journal_start, (unsigned long)sb->journal_blocks);
    printf("  Inode Table:  block=%lu\n", (unsigned long)sb->inode_table_start);
    printf("  Bitmap:       block=%lu\n", (unsigned long)sb->bitmap_start);
    printf("  Data Start:   block=%lu\n", (unsigned long)sb->data_start);
    printf("  BTree Root:   block=%lu\n", (unsigned long)sb->btree_root);
    printf("  Features:     0x%016lX\n", (unsigned long)sb->features);

    alefs_dev_close(&dev);
    return 0;
}

static bool is_invoked_as(const char *argv0, const char *name)
{
    const char *base = strrchr(argv0, '/');
    if (!base) base = argv0; else base++;
    return strcmp(base, name) == 0;
}

int main(int argc, char **argv)
{
    if (is_invoked_as(argv[0], "mkfs.alefs")) {
        argv[0] = (char *)"mkfs.alefs";
        return cmd_mkfs(argc, argv);
    }

    if (is_invoked_as(argv[0], "mount.alefs")) {
        argv[0] = (char *)"mount.alefs";
        return run_mount_alefs(argc, argv);
    }

    if (argc < 2) { print_usage(); return 1; }

    const char *cmd = argv[1];

    if (strcmp(cmd, "format") == 0)    return cmd_format(argc, argv);
    if (strcmp(cmd, "ls") == 0)        return cmd_ls(argc, argv);
    if (strcmp(cmd, "mkdir") == 0)     return cmd_mkdir(argc, argv);
    if (strcmp(cmd, "rmdir") == 0)     return cmd_rmdir(argc, argv);
    if (strcmp(cmd, "cp-in") == 0)     return cmd_cp_in(argc, argv);
    if (strcmp(cmd, "cat") == 0)       return cmd_cat(argc, argv);
    if (strcmp(cmd, "stat") == 0)      return cmd_stat(argc, argv);
    if (strcmp(cmd, "mv") == 0)        return cmd_mv(argc, argv);
    if (strcmp(cmd, "rm") == 0)        return cmd_rm(argc, argv);
    if (strcmp(cmd, "create") == 0)    return cmd_create(argc, argv);
    if (strcmp(cmd, "tree") == 0)      return cmd_tree(argc, argv);
    if (strcmp(cmd, "dump") == 0)      return cmd_dump(argc, argv);
    if (strcmp(cmd, "mount") == 0)     return cmd_mount(argc, argv);
    if (strcmp(cmd, "mkfs") == 0)      return cmd_mkfs(argc, argv);

    fprintf(stderr, "Unknown command: %s\n", cmd);
    print_usage();
    return 1;
}
