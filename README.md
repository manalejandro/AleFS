# AleQFS — Adaptive Linux Efficient QUANTUM Filesystem

AleQFS is a **quantum-inspired** Linux filesystem that simulates superposition,
entanglement, and Grover search — all running on classical hardware with a native
kernel module (no FUSE).

## Quantum Features

- **Superposition** — Files can exist in multiple directories simultaneously
  via probability amplitude fields. Reading a file "collapses" its quantum state.
- **Entanglement** — Directories can be entangled so operations on one
  instantly mirror to the other (spooky action at a distance).
- **Grover Search** — O(1) hash-based quantum search index replacing
  traditional B-trees. Pattern matching across all entries in constant time.
- **Decoherence Detection** — Each block carries a quantum checksum.
  Tampering triggers decoherence, making corrupted data detectable.
- **Quantum Temperature** — The system runs near absolute zero (0.01 K)
  to maintain coherence.

## Quick Start

```bash
# Build
make

# Format a 64 MiB quantum image
./mkfs.aleqfs /tmp/test.qfs 64
# or: ./aleqfs format /tmp/test.qfs 64

# Check quantum status
./aleqfs qstatus /tmp/test.qfs

# Create directories and files (superposition-enabled)
./aleqfs mkdir /tmp/test.qfs /hello
./aleqfs create /tmp/test.qfs /hello/world.txt

# Entangle two directories
./aleqfs mkdir /tmp/test.qfs /a
./aleqfs mkdir /tmp/test.qfs /b
./aleqfs entangle /tmp/test.qfs /a /b

# Observe quantum state (collapses superposition)
./aleqfs observe /tmp/test.qfs /hello/world.txt

# Grover search
./aleqfs grover /tmp/test.qfs 1

# Mount (requires kernel module)
sudo modprobe aleqfs
sudo mount -t aleqfs /tmp/test.qfs /mnt
```

## Commands

| Command | Description |
|---------|-------------|
| `format <img> <size_mb>` | Create and format a new quantum image |
| `mkfs <device> [size_mb]` | Format a device or image |
| `ls <img> <path>` | List directory contents (quantum collapse with --collapse) |
| `mkdir <img> <path>` | Create a directory |
| `rmdir <img> <path>` | Remove a directory |
| `cp-in <img> <src> <dst>` | Copy a file into the quantum image |
| `cat <img> <path>` | Display file contents |
| `stat <img> <path>` | Show file metadata + quantum state |
| `mv <img> <src> <dst>` | Rename |
| `rm <img> <path>` | Remove a file |
| `create <img> <path>` | Create an empty file |
| `tree <img> <path>` | Show directory tree (quantum collapse with --collapse) |
| `dump <img>` | Display superblock info (quantum parameters) |
| `entangle <img> <path_a> <path_b>` | Entangle two directories |
| `observe <img> <path>` | Collapse quantum state and read |
| `decohere <img> <path>` | Force decoherence on a file |
| `grover <img> <pattern>` | Grover quantum search |
| `qstatus <img>` | Quantum system status |

## Source Layout

```
src/
├── aleqfs.h        — Core quantum structures and API
├── aleqfs_layout.h — Shared on-disk quantum layout (kernel + userspace)
├── main.c          — CLI entry point and quantum commands
├── super.c         — Quantum superblock operations
├── io.c            — Block device read/write
├── bitmap.c        — Free block/inode bitmap
├── inode.c         — Quantum inode allocation
├── extent.c        — Extent-based file I/O
├── dir.c           — Quantum directory operations (superposition)
├── path.c          — Path resolution
├── grover.c        — Grover quantum search index
├── entangle.c      — Quantum entanglement operations
├── checksum.c      — Decoherence detection via checksums
├── quantum.c       — Quantum state management
└── aleqfs_ko.c     — Linux kernel module (VFS driver)
```

## Build Options

```bash
make              # Release build
make CFLAGS="-g"  # Debug build
make check        # Run quantum test suite
make kmod         # Build kernel module
make deb          # Build .deb package
make install      # Install to /usr/local
```

## Kernel Module

```bash
sudo apt install dkms linux-headers-amd64
sudo make deb
sudo apt install ./pkg/aleqfs-2.0.0.deb
sudo modprobe aleqfs
sudo mount -t aleqfs /tmp/test.qfs /mnt
```

## Supporting file types

AleQFS supports **regular files** and **directories** plus **entangled directories**
(linked pairs). Symlinks, device nodes, FIFOs, and sockets are not supported.
