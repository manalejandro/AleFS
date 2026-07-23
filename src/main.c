#include "aleqfs.h"
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

static void print_usage(const char *prog);
static void print_mkfs_help(void);
static int run_mount_aleqfs(int argc, char **argv);
static int cmd_format(int argc, char **argv);
static int cmd_mkfs(int argc, char **argv);
static int cmd_mount(int argc, char **argv);
static int cmd_ls(int argc, char **argv);
static int cmd_mkdir(int argc, char **argv);
static int cmd_rmdir(int argc, char **argv);
static int cmd_cp_in(int argc, char **argv);
static int cmd_cat(int argc, char **argv);
static int cmd_stat(int argc, char **argv);
static int cmd_mv(int argc, char **argv);
static int cmd_rm(int argc, char **argv);
static int cmd_create(int argc, char **argv);
static int cmd_tree(int argc, char **argv);
static int cmd_dump(int argc, char **argv);
static int cmd_entangle(int argc, char **argv);
static int cmd_observe(int argc, char **argv);
static int cmd_decohere(int argc, char **argv);
static int cmd_grover(int argc, char **argv);
static int cmd_qstatus(int argc, char **argv);

static bool is_invoked_as(const char *argv0, const char *name)
{
    const char *base = strrchr(argv0, '/');
    if (!base) base = argv0; else base++;
    return strcmp(base, name) == 0;
}

static void print_usage(const char *prog)
{
    printf("AleQFS — Aleatory Quantum Filesystem v%d\n", ALEQFS_FS_VERSION);
    printf("Usage:\n");
    printf("  %s format <img> <size_mb>\n", prog);
    printf("  %s mkfs <device> [size_mb]\n", prog);
    printf("  %s ls [--collapse] <img> <path>\n", prog);
    printf("  %s mkdir <img> <path>\n", prog);
    printf("  %s rmdir <img> <path>\n", prog);
    printf("  %s cp-in <img> <src> <dst>\n", prog);
    printf("  %s cat <img> <path>\n", prog);
    printf("  %s stat <img> <path>\n", prog);
    printf("  %s mv <img> <src> <dst>\n", prog);
    printf("  %s rm <img> <path>\n", prog);
    printf("  %s create <img> <path>\n", prog);
    printf("  %s tree [--collapse] <img> <path>\n", prog);
    printf("  %s dump <img>\n", prog);
    printf("  %s entangle <img> <path_a> <path_b>\n", prog);
    printf("  %s observe <img> <path>\n", prog);
    printf("  %s decohere <img> <path>\n", prog);
    printf("  %s grover <img> <pattern>\n", prog);
    printf("  %s mount <img> <mountpoint>\n", prog);
    printf("  %s qstatus <img>\n", prog);
    printf("\nSymlink invocation:\n");
    printf("  mkfs.aleqfs <device> [size_mb]\n");
    printf("  mount.aleqfs <device> <mountpoint>\n");
}

static void print_mkfs_help(void)
{
    printf("Usage: mkfs.aleqfs [options] <device> [size_mb]\n");
    printf("Create an AleQFS filesystem on a device or image file.\n");
    printf("\nOptions:\n");
    printf("  -V              print version\n");
    printf("  -h, --help      display this help\n");
    printf("\nArguments:\n");
    printf("  device          path to block device or image file\n");
    printf("  size_mb         size in MiB (default: 64 for regular files)\n");
    printf("\nExamples:\n");
    printf("  mkfs.aleqfs /tmp/test.img 64\n");
    printf("  mkfs.aleqfs /dev/sdb1\n");
    printf("  aleqfs mkfs /tmp/test.img 64\n");
}

static int run_mount_aleqfs(int argc, char **argv)
{
    int probe_mode = 0;
    int arg_idx = 1;

    if (argc > 1 && strcmp(argv[1], "--probe") == 0) {
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
        if (magic != ALEQFS_MAGIC)
            return 1;
        printf("ID_FS_TYPE=aleqfs\n");
        return 0;
    }

    return cmd_mount(argc, argv);
}

static int cmd_format(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: aleqfs format <img> <size_mb>\n"); return 1; }
    const char *path = argv[2];
    uint64_t size_mb = strtoull(argv[3], NULL, 10);
    if (size_mb == 0) { fprintf(stderr, "invalid size\n"); return 1; }

    int ret = aleqfs_dev_create(path, size_mb);
    if (ret) { fprintf(stderr, "create failed: %s\n", strerror(-ret)); return 1; }

    struct aleqfs_dev dev;
    ret = aleqfs_dev_open(&dev, path, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    uint64_t total_blocks = (size_mb * 1024ULL * 1024ULL) / ALEQFS_BLOCK_SIZE;
    ret = aleqfs_super_format(&dev, total_blocks);
    if (ret) { fprintf(stderr, "format failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    aleqfs_dev_close(&dev);
    printf("Formatted %s: %lu MiB, %lu blocks\n",
           path, (unsigned long)size_mb, (unsigned long)total_blocks);
    return 0;
}

static int cmd_mkfs(int argc, char **argv)
{
    int arg_offset = (is_invoked_as(argv[0], "mkfs.aleqfs")) ? 1 : 2;

    if (argc <= arg_offset) {
        print_mkfs_help();
        return 0;
    }

    if (strcmp(argv[arg_offset], "-V") == 0) {
        printf("mkfs.aleqfs v%d\n", ALEQFS_FS_VERSION);
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

    int ret = aleqfs_dev_create(device, size_mb);
    if (ret && ret != -EEXIST) {
        if (ret == -EACCES)
            fprintf(stderr, "mkfs.aleqfs: cannot open %s (try as root)\n", device);
        else
            fprintf(stderr, "mkfs.aleqfs: %s: %s\n", device, strerror(-ret));
        return 1;
    }

    struct aleqfs_dev dev;
    ret = aleqfs_dev_open(&dev, device, O_RDWR);
    if (ret) { fprintf(stderr, "mkfs.aleqfs: open failed: %s\n", strerror(-ret)); return 1; }

    uint64_t total_blocks = size_mb * 1024ULL * 1024ULL / ALEQFS_BLOCK_SIZE;
    ret = aleqfs_super_format(&dev, total_blocks);
    if (ret) { fprintf(stderr, "mkfs.aleqfs: format failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    aleqfs_dev_close(&dev);
    printf("mkfs.aleqfs: formatted %s: %lu MiB (%lu blocks)\n",
           device, (unsigned long)size_mb, (unsigned long)total_blocks);
    return 0;
}

static int cmd_mount(int argc, char **argv)
{
    const char *img = NULL, *mnt = NULL, *fstype = "aleqfs";
    unsigned long mountflags = 0;

    /* Called as mount.aleqfs: argv = [prog, dev, mnt, -t, fstype, -o, opts] */
    /* Called as aleqfs mount: argv = [prog, mount, dev, mnt, -o, opts]     */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
            fstype = argv[++i];
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            /* parse options like ro */
            const char *opts = argv[++i];
            if (strstr(opts, "ro")) mountflags |= MS_RDONLY;
            if (strstr(opts, "noexec")) mountflags |= MS_NOEXEC;
            if (strstr(opts, "nosuid")) mountflags |= MS_NOSUID;
        } else if (argv[i][0] == '/') {
            if (!img) img = argv[i];
            else if (!mnt) mnt = argv[i];
        }
    }

    if (!img || !mnt) {
        fprintf(stderr, "usage: mount.aleqfs <img> <mountpoint> [-o options]\n");
        fprintf(stderr, "       aleqfs mount <img> <mountpoint>\n");
        return 1;
    }

    if (access(mnt, F_OK) != 0) {
        fprintf(stderr, "mount: %s does not exist (create it first)\n", mnt);
        return 1;
    }

    int ret = mount(img, mnt, fstype, mountflags, NULL);
    if (ret < 0) {
        fprintf(stderr, "mount: %s\n", strerror(errno));
        return 1;
    }

    return 0;
}

static int cmd_ls(int argc, char **argv)
{
    int idx = 2;
    bool collapse = false;

    if (argc > idx && strcmp(argv[idx], "--collapse") == 0) {
        collapse = true;
        idx++;
    }

    if (argc < idx + 2) { fprintf(stderr, "usage: aleqfs ls [--collapse] <img> <path>\n"); return 1; }
    const char *img = argv[idx];
    const char *path = argv[idx + 1];

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    uint64_t dir_ino;
    ret = aleqfs_path_resolve_full(&dev, path, &dir_ino);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    ret = aleqfs_dir_list(&dev, dir_ino, true, collapse);
    if (ret) { fprintf(stderr, "list failed: %s\n", strerror(-ret)); }

    aleqfs_dev_close(&dev);
    return ret ? 1 : 0;
}

static int cmd_mkdir(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: aleqfs mkdir <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    char name[ALEQFS_MAX_NAME + 1];
    uint64_t parent;
    ret = aleqfs_path_resolve(&dev, path, &parent, name);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    if (name[0] == '\0') { fprintf(stderr, "mkdir: cannot create root\n"); aleqfs_dev_close(&dev); return 1; }

    uint64_t ino;
    ret = aleqfs_dir_create(&dev, parent, name, S_IFDIR | 0755, &ino);
    if (ret) { fprintf(stderr, "mkdir failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    printf("Created directory: %s (ino=%lu)\n", path, (unsigned long)ino);
    aleqfs_dev_close(&dev);
    return 0;
}

static int cmd_rmdir(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: aleqfs rmdir <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    if (strcmp(path, "/") == 0) {
        fprintf(stderr, "rmdir: cannot remove root\n");
        aleqfs_dev_close(&dev);
        return 1;
    }

    char name[ALEQFS_MAX_NAME + 1];
    uint64_t parent;
    ret = aleqfs_path_resolve(&dev, path, &parent, name);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    uint64_t child_ino;
    ret = aleqfs_dir_lookup(&dev, parent, name, &child_ino);
    if (ret) { fprintf(stderr, "lookup failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    struct aleqfs_inode inode;
    ret = aleqfs_inode_read(&dev, child_ino, &inode);
    if (ret) { fprintf(stderr, "read inode failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    if (!S_ISDIR(inode.mode)) {
        fprintf(stderr, "rmdir: not a directory\n");
        aleqfs_dev_close(&dev);
        return 1;
    }

    ret = aleqfs_dir_remove_entry(&dev, parent, name);
    if (ret) { fprintf(stderr, "remove entry failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    ret = aleqfs_inode_free(&dev, child_ino);
    if (ret) { fprintf(stderr, "free inode failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    printf("Removed directory: %s\n", path);
    aleqfs_dev_close(&dev);
    return 0;
}

static int cmd_cp_in(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: aleqfs cp-in <img> <src> <dst>\n"); return 1; }
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

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { free(data); fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { free(data); aleqfs_dev_close(&dev); fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); return 1; }

    char name[ALEQFS_MAX_NAME + 1];
    uint64_t parent;
    ret = aleqfs_path_resolve(&dev, dst_path, &parent, name);
    if (ret) { free(data); aleqfs_dev_close(&dev); fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); return 1; }

    uint64_t ino;
    ret = aleqfs_dir_create(&dev, parent, name, S_IFREG | 0644, &ino);
    if (ret) { free(data); aleqfs_dev_close(&dev); fprintf(stderr, "create file failed: %s\n", strerror(-ret)); return 1; }

    struct aleqfs_inode inode;
    ret = aleqfs_inode_read(&dev, ino, &inode);
    if (ret) { free(data); aleqfs_dev_close(&dev); fprintf(stderr, "read inode failed: %s\n", strerror(-ret)); return 1; }

    uint64_t written;
    ret = aleqfs_extent_append(&dev, &inode, data, nread, &written, ino);
    if (ret) { free(data); aleqfs_dev_close(&dev); fprintf(stderr, "write failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_inode_write(&dev, ino, &inode);
    if (ret) { free(data); aleqfs_dev_close(&dev); fprintf(stderr, "inode write failed: %s\n", strerror(-ret)); return 1; }

    free(data);
    printf("Copied %zu bytes to %s (ino=%lu)\n", nread, dst_path, (unsigned long)ino);
    aleqfs_dev_close(&dev);
    return 0;
}

static int cmd_cat(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: aleqfs cat <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    uint64_t ino;
    ret = aleqfs_path_resolve_full(&dev, path, &ino);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    struct aleqfs_inode inode;
    ret = aleqfs_inode_read(&dev, ino, &inode);
    if (ret) { fprintf(stderr, "read inode failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    if (!S_ISREG(inode.mode)) {
        fprintf(stderr, "cat: not a regular file\n");
        aleqfs_dev_close(&dev);
        return 1;
    }

    uint8_t *buf = malloc(inode.size > 0 ? inode.size : 1);
    if (!buf) { aleqfs_dev_close(&dev); return 1; }

    ret = aleqfs_extent_read(&dev, &inode, 0, buf, inode.size);
    if (ret < 0) { free(buf); aleqfs_dev_close(&dev); fprintf(stderr, "read failed: %s\n", strerror(-ret)); return 1; }

    fwrite(buf, 1, inode.size, stdout);
    free(buf);
    aleqfs_dev_close(&dev);
    return 0;
}

static int cmd_stat(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: aleqfs stat <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    uint64_t ino;
    ret = aleqfs_path_resolve_full(&dev, path, &ino);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    struct aleqfs_inode inode;
    ret = aleqfs_inode_read(&dev, ino, &inode);
    if (ret) { fprintf(stderr, "read inode failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

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

    double prob = aleqfs_amplitude_probability(inode.amplitude.real, inode.amplitude.imag);

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
    printf("  Quantum Amplitude: (%d, %d)\n", inode.amplitude.real, inode.amplitude.imag);
    printf("  Quantum Probability: %.6f\n", prob);
    printf("  Entanglement Partner: %lu\n", (unsigned long)inode.entanglement_partner);
    printf("  Observe Count: %u\n", inode.observe_count);

    aleqfs_dev_close(&dev);
    return 0;
}

static int cmd_mv(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: aleqfs mv <img> <src> <dst>\n"); return 1; }
    const char *img = argv[2];
    const char *src_path = argv[3];
    const char *dst_path = argv[4];

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    char src_name[ALEQFS_MAX_NAME + 1];
    uint64_t src_parent;
    ret = aleqfs_path_resolve(&dev, src_path, &src_parent, src_name);
    if (ret) { fprintf(stderr, "src resolve failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    uint64_t child_ino;
    ret = aleqfs_dir_lookup(&dev, src_parent, src_name, &child_ino);
    if (ret) { fprintf(stderr, "src lookup failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    ret = aleqfs_dir_remove_entry(&dev, src_parent, src_name);
    if (ret) { fprintf(stderr, "remove entry failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    char dst_name[ALEQFS_MAX_NAME + 1];
    uint64_t dst_parent;
    ret = aleqfs_path_resolve(&dev, dst_path, &dst_parent, dst_name);
    if (ret) { fprintf(stderr, "dst resolve failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    struct aleqfs_inode child_inode;
    ret = aleqfs_inode_read(&dev, child_ino, &child_inode);
    if (ret) { fprintf(stderr, "read child inode failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    uint8_t ft = S_ISDIR(child_inode.mode) ? ALEQFS_FT_DIR : ALEQFS_FT_FILE;
    ret = aleqfs_dir_add_entry(&dev, dst_parent, child_ino, dst_name, ft);
    if (ret) { fprintf(stderr, "add entry failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    printf("Moved: %s -> %s\n", src_path, dst_path);
    aleqfs_dev_close(&dev);
    return 0;
}

static int cmd_rm(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: aleqfs rm <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    if (strcmp(path, "/") == 0) {
        fprintf(stderr, "rm: cannot remove root\n");
        aleqfs_dev_close(&dev);
        return 1;
    }

    char name[ALEQFS_MAX_NAME + 1];
    uint64_t parent;
    ret = aleqfs_path_resolve(&dev, path, &parent, name);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    uint64_t child_ino;
    ret = aleqfs_dir_lookup(&dev, parent, name, &child_ino);
    if (ret) { fprintf(stderr, "lookup failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    struct aleqfs_inode inode;
    ret = aleqfs_inode_read(&dev, child_ino, &inode);
    if (ret) { fprintf(stderr, "read inode failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    if (S_ISDIR(inode.mode)) {
        fprintf(stderr, "rm: is a directory, use rmdir\n");
        aleqfs_dev_close(&dev);
        return 1;
    }

    ret = aleqfs_dir_remove_entry(&dev, parent, name);
    if (ret) { fprintf(stderr, "remove entry failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    for (uint32_t i = 0; i < inode.extent_count; i++)
        aleqfs_extent_free(&dev, &inode.extents[i]);

    ret = aleqfs_inode_free(&dev, child_ino);
    if (ret) { fprintf(stderr, "free inode failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    printf("Removed: %s\n", path);
    aleqfs_dev_close(&dev);
    return 0;
}

static int cmd_create(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: aleqfs create <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    char name[ALEQFS_MAX_NAME + 1];
    uint64_t parent;
    ret = aleqfs_path_resolve(&dev, path, &parent, name);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    uint64_t ino;
    ret = aleqfs_dir_create(&dev, parent, name, S_IFREG | 0644, &ino);
    if (ret) { fprintf(stderr, "create failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    printf("Created empty file: %s (ino=%lu)\n", path, (unsigned long)ino);
    aleqfs_dev_close(&dev);
    return 0;
}

static int cmd_tree(int argc, char **argv)
{
    int idx = 2;
    bool collapse = false;

    if (argc > idx && strcmp(argv[idx], "--collapse") == 0) {
        collapse = true;
        idx++;
    }

    if (argc < idx + 2) { fprintf(stderr, "usage: aleqfs tree [--collapse] <img> <path>\n"); return 1; }
    const char *img = argv[idx];
    const char *path = argv[idx + 1];

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    uint64_t dir_ino;
    ret = aleqfs_path_resolve_full(&dev, path, &dir_ino);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    printf("%s\n", path);
    ret = aleqfs_dir_tree(&dev, dir_ino, 0, path, collapse);
    if (ret) { fprintf(stderr, "tree failed: %s\n", strerror(-ret)); }

    aleqfs_dev_close(&dev);
    return ret ? 1 : 0;
}

static int cmd_dump(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: aleqfs dump <img>\n"); return 1; }
    const char *img = argv[2];

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    struct aleqfs_superblock *sb = &dev.sb;
    printf("AleQFS Superblock Dump\n");
    printf("  Magic:             0x%016lX\n", (unsigned long)sb->magic);
    printf("  Version:           %u\n", sb->version);
    printf("  Block Size:        %u\n", sb->block_size);
    printf("  Total Blocks:      %lu\n", (unsigned long)sb->total_blocks);
    printf("  Total Size:        %lu MiB\n",
           (unsigned long)(sb->total_blocks * sb->block_size / (1024*1024)));
    printf("  Inode Count:       %lu\n", (unsigned long)sb->inode_count);
    printf("  Free Inodes:       %lu\n", (unsigned long)sb->free_inodes);
    printf("  Free Blocks:       %lu\n", (unsigned long)sb->free_blocks);
    printf("  Root Inode:        %lu\n", (unsigned long)sb->root_inode);
    printf("  Journal:           block=%lu count=%lu\n",
           (unsigned long)sb->journal_start, (unsigned long)sb->journal_blocks);
    printf("  Grover Root:       block=%lu\n", (unsigned long)sb->grover_root);
    printf("  Inode Table:       block=%lu\n", (unsigned long)sb->inode_table_start);
    printf("  Bitmap:            block=%lu\n", (unsigned long)sb->bitmap_start);
    printf("  Data Start:        block=%lu\n", (unsigned long)sb->data_start);
    printf("  Features:          0x%016lX\n", (unsigned long)sb->features);
    printf("  Quantum Temp:      %.2f\n", sb->quantum_temperature);
    printf("  Decoherence Rate:  %.6f\n", sb->decoherence_rate);
    printf("  Checksum:          0x%016lX\n", (unsigned long)sb->checksum);

    aleqfs_dev_close(&dev);
    return 0;
}

static int cmd_entangle(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: aleqfs entangle <img> <path_a> <path_b>\n"); return 1; }
    const char *img = argv[2];
    const char *path_a = argv[3];
    const char *path_b = argv[4];

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    uint64_t ino_a, ino_b;
    ret = aleqfs_path_resolve_full(&dev, path_a, &ino_a);
    if (ret) { fprintf(stderr, "resolve %s failed: %s\n", path_a, strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    ret = aleqfs_path_resolve_full(&dev, path_b, &ino_b);
    if (ret) { fprintf(stderr, "resolve %s failed: %s\n", path_b, strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    ret = aleqfs_entangle(&dev, ino_a, ino_b, ALEQFS_OP_MIRROR | ALEQFS_OP_SYNC);
    if (ret) { fprintf(stderr, "entangle failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    printf("Entanglement established: %lu <-> %lu\n",
           (unsigned long)ino_a, (unsigned long)ino_b);
    aleqfs_dev_close(&dev);
    return 0;
}

static int cmd_observe(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: aleqfs observe <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    uint64_t ino;
    ret = aleqfs_path_resolve_full(&dev, path, &ino);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    struct aleqfs_inode inode;
    ret = aleqfs_inode_read(&dev, ino, &inode);
    if (ret) { fprintf(stderr, "read inode failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    ret = aleqfs_quantum_collapse(&inode.amplitude.real, &inode.amplitude.imag);
    if (ret < 0) { fprintf(stderr, "quantum collapse failed\n"); aleqfs_dev_close(&dev); return 1; }

    inode.observe_count++;
    ret = aleqfs_inode_write(&dev, ino, &inode);
    if (ret) { fprintf(stderr, "write inode failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    printf("Quantum state collapsed \u2014 file observed.\n");

    if (S_ISREG(inode.mode) && inode.size > 0) {
        uint8_t *buf = malloc(inode.size > 0 ? inode.size : 1);
        if (buf) {
            ret = aleqfs_extent_read(&dev, &inode, 0, buf, inode.size);
            if (ret >= 0)
                fwrite(buf, 1, inode.size, stdout);
            free(buf);
        }
    }

    aleqfs_dev_close(&dev);
    return 0;
}

static int cmd_decohere(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: aleqfs decohere <img> <path>\n"); return 1; }
    const char *img = argv[2];
    const char *path = argv[3];

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    uint64_t ino;
    ret = aleqfs_path_resolve_full(&dev, path, &ino);
    if (ret) { fprintf(stderr, "path resolve failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    struct aleqfs_inode inode;
    ret = aleqfs_inode_read(&dev, ino, &inode);
    if (ret) { fprintf(stderr, "read inode failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    inode.decoherence_stamp = ~inode.decoherence_stamp;

    ret = aleqfs_inode_write(&dev, ino, &inode);
    if (ret) { fprintf(stderr, "write inode failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    printf("Decoherence forced on inode %lu \u2014 checksum will fail on next read.\n",
           (unsigned long)ino);
    aleqfs_dev_close(&dev);
    return 0;
}

static int cmd_grover(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: aleqfs grover <img> <pattern>\n"); return 1; }
    const char *img = argv[2];
    uint64_t pattern = strtoull(argv[3], NULL, 0);

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    uint64_t results[ALEQFS_GROVER_SLOTS];
    int found = aleqfs_grover_search(&dev, dev.sb.grover_root, pattern, results, ALEQFS_GROVER_SLOTS);
    if (found < 0) { fprintf(stderr, "grover search failed\n"); aleqfs_dev_close(&dev); return 1; }

    printf("Grover search for pattern 0x%lx found %d result(s):\n",
           (unsigned long)pattern, found);
    for (int i = 0; i < found; i++) {
        struct aleqfs_inode inode;
        if (aleqfs_inode_read(&dev, results[i], &inode) == 0) {
            printf("  ino=%lu size=%lu\n",
                   (unsigned long)results[i], (unsigned long)inode.size);
        }
    }

    aleqfs_dev_close(&dev);
    return 0;
}

static int cmd_qstatus(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: aleqfs qstatus <img>\n"); return 1; }
    const char *img = argv[2];

    struct aleqfs_dev dev;
    int ret = aleqfs_dev_open(&dev, img, O_RDWR);
    if (ret) { fprintf(stderr, "open failed: %s\n", strerror(-ret)); return 1; }

    ret = aleqfs_super_load(&dev);
    if (ret) { fprintf(stderr, "load superblock failed: %s\n", strerror(-ret)); aleqfs_dev_close(&dev); return 1; }

    uint64_t entanglement_count = 0;
    uint8_t buf[ALEQFS_BLOCK_SIZE];
    uint64_t start = dev.sb.entanglement_start;

    for (uint64_t b = 0; b < dev.sb.entanglement_blocks; b++) {
        ret = aleqfs_dev_read(&dev, start + b, buf);
        if (ret < 0) break;
        struct aleqfs_entanglement_record *recs = (struct aleqfs_entanglement_record *)buf;
        uint64_t recs_per_blk = ALEQFS_BLOCK_SIZE / sizeof(struct aleqfs_entanglement_record);
        for (uint64_t i = 0; i < recs_per_blk; i++) {
            if (recs[i].ino_a != 0 || recs[i].ino_b != 0)
                entanglement_count++;
        }
    }

    printf("AleQFS Quantum System Status\n");
    printf("  Quantum Temperature:  %.2f\n", dev.sb.quantum_temperature);
    printf("  Decoherence Rate:     %.6f\n", dev.sb.decoherence_rate);
    printf("  Entanglement Count:   %lu\n", (unsigned long)entanglement_count);
    printf("  Grover Root Block:    %lu\n", (unsigned long)dev.sb.grover_root);
    printf("  Total Inodes:         %lu\n", (unsigned long)dev.sb.inode_count);
    printf("  Free Inodes:          %lu\n", (unsigned long)dev.sb.free_inodes);
    printf("  Free Blocks:          %lu\n", (unsigned long)dev.sb.free_blocks);

    aleqfs_dev_close(&dev);
    return 0;
}

int main(int argc, char **argv)
{
    if (is_invoked_as(argv[0], "mkfs.aleqfs")) {
        argv[0] = (char *)"mkfs.aleqfs";
        return cmd_mkfs(argc, argv);
    }

    if (is_invoked_as(argv[0], "mount.aleqfs")) {
        argv[0] = (char *)"mount.aleqfs";
        return run_mount_aleqfs(argc, argv);
    }

    if (argc < 2) { print_usage(argv[0]); return 1; }

    const char *cmd = argv[1];

    if (strcmp(cmd, "format") == 0)    return cmd_format(argc, argv);
    if (strcmp(cmd, "mkfs") == 0)      return cmd_mkfs(argc, argv);
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
    if (strcmp(cmd, "entangle") == 0)  return cmd_entangle(argc, argv);
    if (strcmp(cmd, "observe") == 0)   return cmd_observe(argc, argv);
    if (strcmp(cmd, "decohere") == 0)  return cmd_decohere(argc, argv);
    if (strcmp(cmd, "grover") == 0)    return cmd_grover(argc, argv);
    if (strcmp(cmd, "mount") == 0)     return cmd_mount(argc, argv);
    if (strcmp(cmd, "qstatus") == 0)   return cmd_qstatus(argc, argv);

    fprintf(stderr, "Unknown command: %s\n", cmd);
    print_usage(argv[0]);
    return 1;
}
