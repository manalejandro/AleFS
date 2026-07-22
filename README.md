# AleFS — Adaptive Linux Efficient Filesystem

AleFS is a Linux filesystem with a userspace image tool and a native kernel
module (no FUSE). It uses a B+ tree for metadata, extent-based file storage,
and includes a journal for crash recovery.

## Features

- **B+ Tree Metadata** — O(log n) lookups for directories and files
- **Extent-based Storage** — Contiguous block runs for file data
- **Bitmap Allocation** — Efficient free space tracking
- **Journaling** — Crash recovery support
- **Native Kernel Module** — Direct VFS integration, no FUSE
- **Userspace Tool** — Format, inspect, and manipulate images without mounting

## Quick Start

```bash
# Build
make

# Format a 64 MiB image
./mkfs.alefs /tmp/test.img 64
# or: ./alefs mkfs /tmp/test.img 64

# Create directories and files
./alefs mkdir /tmp/test.img /hello
./alefs create /tmp/test.img /hello/world.txt
./alefs ls /tmp/test.img /hello

# Copy a file in
echo "hello from AleFS" > /tmp/src.txt
./alefs cp-in /tmp/test.img /tmp/src.txt /hello/world.txt

# Read it back
./alefs cat /tmp/test.img /hello/world.txt

# Mount (requires kernel module)
sudo modprobe alefs
sudo mount -t alefs /tmp/test.img /mnt
```

## Commands

| Command | Description |
|---------|-------------|
| `format <img> <size_mb>` | Create and format a new image |
| `mkfs <device> [size_mb]` | Format a device or image |
| `ls <img> <path>` | List directory contents |
| `mkdir <img> <path>` | Create a directory |
| `rmdir <img> <path>` | Remove a directory |
| `cp-in <img> <src> <dst>` | Copy a file into the image |
| `cat <img> <path>` | Display file contents |
| `stat <img> <path>` | Show file metadata |
| `mv <img> <src> <dst>` | Rename a file or directory |
| `rm <img> <path>` | Remove a file |
| `create <img> <path>` | Create an empty file |
| `tree <img> <path>` | Show directory tree |
| `dump <img>` | Display superblock info |

The `mkfs.alefs` symlink can be used directly:
```
mkfs.alefs [options] <device> [size_mb]
```

## Build Options

```bash
make              # Release build
make CFLAGS="-g"  # Debug build
make check        # Run test suite
make kmod         # Build kernel module (requires linux-headers)
make deb          # Build .deb package with DKMS support
make install      # Install to /usr/local
```

## Kernel Module

The kernel module requires DKMS and linux-headers on the target system:

```bash
sudo apt install dkms linux-headers-amd64
sudo make deb
sudo apt install ./pkg/alefs-1.0.0.deb
sudo modprobe alefs
sudo mount -t alefs /tmp/test.img /mnt
```

## Source Layout

```
src/
├── alefs.h        — Core structures and API
├── main.c         — CLI entry point and commands
├── io.c           — Block device read/write
├── super.c        — Superblock operations
├── bitmap.c       — Free block/inode bitmap
├── inode.c        — Inode allocation
├── extent.c       — Extent-based file I/O
├── btree.c        — B+ tree metadata
├── dir.c          — Directory operations
├── path.c         — Path resolution
├── journal.c      — Crash recovery journal
├── checksum.c     — Integrity checksums
├── alefs_layout.h — Shared on-disk layout (kernel + userspace)
├── alefs_ko.c     — Linux kernel module (VFS driver)
tests/
├── test_basic.sh  — Core operation tests
└── test_stress.sh — Stress and edge case tests
dkms/
├── dkms.conf      — DKMS build configuration
└── Makefile       — Kernel module Makefile
scripts/
├── postinst       — Debian post-install script
├── postrm         — Debian post-remove script
└── preinst        — Debian pre-install script
```

## License

MIT — see [LICENSE](LICENSE)
