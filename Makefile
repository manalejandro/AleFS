CC      ?= gcc
CFLAGS  ?= -O2
PREFIX  ?= /usr/local

CFLAGS  += -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration \
           -Werror=return-type -Wstrict-prototypes -Wvla \
           -std=gnu11 -D_GNU_SOURCE \
           -DALEFS_VERSION=\"$(VERSION)\"
LDFLAGS += -lpthread

ifneq ($(shell uname -s),Linux)
LDFLAGS += -lrt
endif

# Optional libs
HAS_ZSTD  := $(shell pkg-config --exists libzstd 2>/dev/null && echo 1 || echo 0)
HAS_LZ4   := $(shell pkg-config --exists liblz4  2>/dev/null && echo 1 || echo 0)
HAS_BLAKE3 := $(shell pkg-config --exists libblake3 2>/dev/null && echo 1 || echo 0)

ifeq ($(HAS_ZSTD),1)
CFLAGS  += $(shell pkg-config --cflags libzstd) -DHAS_ZSTD
LDFLAGS += $(shell pkg-config --libs libzstd)
endif

ifeq ($(HAS_LZ4),1)
CFLAGS  += $(shell pkg-config --cflags liblz4) -DHAS_LZ4
LDFLAGS += $(shell pkg-config --libs liblz4)
endif

ifeq ($(HAS_BLAKE3),1)
CFLAGS  += $(shell pkg-config --cflags libblake3) -DHAS_BLAKE3
LDFLAGS += $(shell pkg-config --libs libblake3)
endif

# Windows cross-compile (MinGW)
CC_WIN  ?= x86_64-w64-mingw32-gcc
CFLAGS_WIN ?= -O2
LDFLAGS_WIN ?= -lpthread -lshlwapi

# Sources
SRCDIR   = src
BUILDDIR = build
VERSION  := $(patsubst v%,%,$(shell git describe --tags 2>/dev/null || echo "1.0.0"))

# Kernel module source (excluded from userspace build)
KMOD_SRC := $(SRCDIR)/alefs_ko.c

# All .c files except the kernel module
SRCS := $(filter-out $(KMOD_SRC),$(wildcard $(SRCDIR)/*.c))
OBJS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

TARGET = alefs
TARGET_WIN = alefs.exe

# Kernel module
KMOD_DIR  = $(BUILDDIR)/kmod
KMOD_OBJ  = $(KMOD_DIR)/alefs.ko

.PHONY: all clean install uninstall check test testv \
        win32 clean-win32 dist kmod

all: $(BUILDDIR) $(TARGET)
	@ln -sf $(TARGET) mkfs.alefs 2>/dev/null || true
	@ln -sf $(TARGET) mount.alefs 2>/dev/null || true

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)
	@echo "  LD  $@"
	@echo "  Features: ZSTD=$(HAS_ZSTD) LZ4=$(HAS_LZ4) BLAKE3=$(HAS_BLAKE3)"

# Kernel module (requires kernel headers)
KVER    ?= $(shell uname -r)
KDIR    ?= /lib/modules/$(KVER)/build

kmod: $(KMOD_OBJ)

$(KMOD_DIR):
	mkdir -p $(KMOD_DIR)

$(KMOD_OBJ): $(KMOD_DIR) $(KMOD_SRC) $(SRCDIR)/alefs_layout.h
	cp $(KMOD_SRC) $(KMOD_DIR)/alefs_ko.c
	cp $(SRCDIR)/alefs_layout.h $(KMOD_DIR)/
	echo "obj-m := alefs_ko.o" > $(KMOD_DIR)/Makefile
	$(MAKE) -C $(KDIR) M=$(PWD)/$(KMOD_DIR) modules

# Windows cross-compile
win32: CC = $(CC_WIN)
win32: CFLAGS = $(CFLAGS_WIN) -Wall -Wextra -std=gnu11 -DALEFS_VERSION=\"$(VERSION)\"
win32: LDFLAGS = $(LDFLAGS_WIN)
win32: clean-win32 $(TARGET_WIN)

$(BUILDDIR)/win/%.o: $(SRCDIR)/%.c | $(BUILDDIR)/win
	$(CC_WIN) $(CFLAGS_WIN) -Wall -Wextra -std=gnu11 -DALEFS_VERSION=\"$(VERSION)\" -MMD -MP -c $< -o $@

OBJS_WIN := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/win/%.o,$(SRCS))

$(BUILDDIR)/win:
	mkdir -p $(BUILDDIR)/win

$(TARGET_WIN): $(OBJS_WIN)
	$(CC_WIN) $(CFLAGS_WIN) $(OBJS_WIN) -o $@ $(LDFLAGS_WIN)
	@echo "  WIN LD $@"

# Test
TEST_IMG = /tmp/alefs-test-$$(id -u).bin

check: $(TARGET)
	@echo "=== AleFS Test Suite ==="
	@-rm -f $(TEST_IMG)
	@echo "1. Format..."
	./$(TARGET) format $(TEST_IMG) 64
	@echo "2. Directory operations..."
	./$(TARGET) mkdir $(TEST_IMG) /subdir
	./$(TARGET) ls $(TEST_IMG) /
	@echo "3. File operations (skip duplicate)..."
	@echo "hello world" > /tmp/alefs_content.txt
	./$(TARGET) cp-in $(TEST_IMG) /tmp/alefs_content.txt /subdir/hello.txt
	./$(TARGET) cat $(TEST_IMG) /subdir/hello.txt
	./$(TARGET) stat $(TEST_IMG) /subdir/hello.txt
	./$(TARGET) ls $(TEST_IMG) /subdir
	@echo "4. Rename..."
	./$(TARGET) mv $(TEST_IMG) /subdir/hello.txt /subdir/world.txt
	./$(TARGET) ls $(TEST_IMG) /subdir
	@echo "5. Delete..."
	./$(TARGET) rm $(TEST_IMG) /subdir/world.txt
	./$(TARGET) ls $(TEST_IMG) /subdir
	@echo "6. Subdir..."
	./$(TARGET) rmdir $(TEST_IMG) /subdir
	./$(TARGET) ls $(TEST_IMG) /
	@echo "7. Deep tree..."
	./$(TARGET) mkdir $(TEST_IMG) /a
	./$(TARGET) mkdir $(TEST_IMG) /a/b
	./$(TARGET) mkdir $(TEST_IMG) /a/b/c
	./$(TARGET) create $(TEST_IMG) /a/b/c/file.txt
	./$(TARGET) tree $(TEST_IMG) /
	./$(TARGET) rm $(TEST_IMG) /a/b/c/file.txt
	./$(TARGET) rmdir $(TEST_IMG) /a/b/c
	./$(TARGET) rmdir $(TEST_IMG) /a/b
	./$(TARGET) rmdir $(TEST_IMG) /a
	@echo "8. Superblock dump..."
	./$(TARGET) dump $(TEST_IMG)
	@-rm -f $(TEST_IMG) /tmp/alefs_content.txt
	@echo "=== All tests passed ==="

test: check

testv: $(TARGET)
	valgrind --tool=memcheck --leak-check=full ./$(TARGET) format /tmp/alefs-vtest.bin 32 2>&1 | head -20
	@rm -f /tmp/alefs-vtest.bin

# Install
install: $(TARGET)
	install -d $(DESTDIR)/sbin $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/
	ln -sf $(TARGET) $(DESTDIR)$(PREFIX)/bin/mkfs.alefs 2>/dev/null || true
	ln -sf $(PREFIX)/bin/$(TARGET) $(DESTDIR)/sbin/mount.alefs 2>/dev/null || true
	@echo "AleFS installed to $(DESTDIR)$(PREFIX)/bin/$(TARGET)"
	@echo "  mkfs.alefs -> alefs (symlink)"
	@echo "  mount.alefs -> alefs (symlink)"
	@if [ -f $(KMOD_OBJ) ]; then \
		install -d $(DESTDIR)$(PREFIX)/lib/modules/$(KVER)/extra; \
		install -m 644 $(KMOD_OBJ) $(DESTDIR)$(PREFIX)/lib/modules/$(KVER)/extra/; \
		echo "  alefs.ko installed to kernel modules"; \
	fi

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(DESTDIR)$(PREFIX)/bin/mkfs.alefs
	rm -f $(DESTDIR)/sbin/mount.alefs

format:
	@if command -v clang-format >/dev/null 2>&1; then \
		clang-format -i $(SRCDIR)/*.c $(SRCDIR)/*.h; \
	else \
		echo "clang-format not installed, skipping"; \
	fi

clean:
	rm -rf $(BUILDDIR) $(TARGET)
	rm -f mkfs.alefs mount.alefs
	rm -f /tmp/alefs-test-*.bin
	$(MAKE) -C $(KDIR) M=$(PWD)/build/kmod clean 2>/dev/null || true

clean-win32:
	rm -rf $(BUILDDIR)/win $(TARGET_WIN)

deb: $(TARGET)
	@echo "=== Building .deb package ==="
	rm -rf pkg/deb
	mkdir -p pkg/deb/DEBIAN \
	         pkg/deb/usr/bin \
	         pkg/deb/usr/sbin \
	         pkg/deb/usr/src \
	         pkg/deb/usr/share/doc/alefs/dkms \
	         pkg/deb/usr/share/man/man1 \
	         pkg/deb/usr/share/man/man8 \
	         pkg/deb/lib/udev/rules.d
	# Binary and symlinks
	cp $(TARGET) pkg/deb/usr/bin/alefs
	ln -sf alefs pkg/deb/usr/bin/mkfs.alefs
	ln -sf /usr/bin/alefs pkg/deb/usr/sbin/mount.alefs
	# Documentation
	cp README.md LICENSE pkg/deb/usr/share/doc/alefs/
	echo "AleFS $(VERSION) changelog" | gzip -9nf > pkg/deb/usr/share/doc/alefs/changelog.gz
	chmod 644 pkg/deb/usr/share/doc/alefs/*.md
	chmod 644 pkg/deb/usr/share/doc/alefs/LICENSE 2>/dev/null || true
	# udev rule for auto-mount
	cp scripts/90-alefs.rules pkg/deb/lib/udev/rules.d/
	# DKMS sources (kernel module builds on target system)
	cp src/alefs_ko.c pkg/deb/usr/share/doc/alefs/dkms/
	cp src/alefs_layout.h pkg/deb/usr/share/doc/alefs/dkms/
	cp dkms/dkms.conf pkg/deb/usr/share/doc/alefs/dkms/
	cp dkms/Makefile pkg/deb/usr/share/doc/alefs/dkms/
	# Man pages
	printf '.TH ALEFS 1 "July 2026" "alefs %s" "User Commands"\n.SH NAME\nalefs \\- Adaptive Linux Efficient Filesystem\n.SH SYNOPSIS\n.B alefs\n.I command\n.RI [ options ]\n.SH DESCRIPTION\nAleFS filesystem image tool.\n.SH COMMANDS\n.TP\n.B format \\fIimg\\fR \\fIsize_mb\\fR\nFormat image.\n.TP\n.B ls \\fIimg\\fR \\fIpath\\fR\nList directory.\n.TP\n.B mkdir \\fIimg\\fR \\fIpath\\fR\nCreate directory.\n.TP\n.B rmdir \\fIimg\\fR \\fIpath\\fR\nRemove directory.\n.TP\n.B cp-in \\fIimg\\fR \\fIsrc\\fR \\fIdst\\fR\nCopy file into image.\n.TP\n.B cat \\fIimg\\fR \\fIpath\\fR\nShow file contents.\n.TP\n.B stat \\fIimg\\fR \\fIpath\\fR\nShow file metadata.\n.TP\n.B mv \\fIimg\\fR \\fIsrc\\fR \\fIdst\\fR\nRename.\n.TP\n.B rm \\fIimg\\fR \\fIpath\\fR\nRemove file.\n.TP\n.B create \\fIimg\\fR \\fIpath\\fR\nCreate empty file.\n.TP\n.B tree \\fIimg\\fR \\fIpath\\fR\nDirectory tree.\n.TP\n.B dump \\fIimg\\fR\nSuperblock dump.\n.SH LICENSE\nMIT\n' | gzip -9nf > pkg/deb/usr/share/man/man1/alefs.1.gz
	printf '.TH MKFS.ALEFS 8 "July 2026" "mkfs.alefs %s" "System Administration"\n.SH NAME\nmkfs.alefs \\- build an AleFS filesystem\n.SH SYNOPSIS\n.B mkfs.alefs\n.RI [ options ]\n.I device\n.RI [ size_mb ]\n.SH DESCRIPTION\nmkfs.alefs creates an AleFS filesystem.\n.SH OPTIONS\n.B -V\nPrint version.\n.TP\n.B -h, --help\nThis help.\n.SH LICENSE\nMIT\n' | gzip -9nf > pkg/deb/usr/share/man/man8/mkfs.alefs.8.gz
	printf '.TH MOUNT.ALEFS 8 "July 2026" "mount.alefs %s" "System Administration"\n.SH NAME\nmount.alefs \\- mount an AleFS filesystem\n.SH SYNOPSIS\n.B mount -t alefs\n.I device\n.I mountpoint\n.SH DESCRIPTION\nmount.alefs is called by mount(8) to mount AleFS.\nRequires the alefs kernel module built via DKMS.\n.SH LICENSE\nMIT\n' | gzip -9nf > pkg/deb/usr/share/man/man8/mount.alefs.8.gz
	# DEBIAN control files
	printf 'Package: alefs\nVersion: %s\nSection: utils\nPriority: optional\nArchitecture: amd64\nMaintainer: AleFS Contributors <alefs@example.com>\nDepends: libc6 (>= 2.17)\nRecommends: dkms, linux-headers-generic\nDescription: Adaptive Linux Efficient Filesystem\n AleFS is a Linux filesystem with native kernel module (DKMS).\n Includes mkfs.alefs and mount.alefs helpers.\n .\n The kernel module is built for the current kernel via DKMS\n on package install. Run "modprobe alefs" after installation.\nHomepage: https://github.com/anomalyco/opencode\n' "$(VERSION)" > pkg/deb/DEBIAN/control
	printf 'Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/\nUpstream-Name: alefs\n\nFiles: *\nCopyright: 2026 AleFS Contributors\nLicense: MIT\n' > pkg/deb/DEBIAN/copyright
	cat LICENSE >> pkg/deb/DEBIAN/copyright
	# postinst: build kernel module via DKMS or directly
	sed 's/@VERSION@/$(VERSION)/g' scripts/postinst > pkg/deb/DEBIAN/postinst
	chmod 755 pkg/deb/DEBIAN/postinst
	# postrm: clean up DKMS
	sed 's/@VERSION@/$(VERSION)/g' scripts/postrm > pkg/deb/DEBIAN/postrm
	chmod 755 pkg/deb/DEBIAN/postrm
	# preinst: remove old DKMS module
	sed 's/@VERSION@/$(VERSION)/g' scripts/preinst > pkg/deb/DEBIAN/preinst
	chmod 755 pkg/deb/DEBIAN/preinst
	# Build .deb
	fakeroot dpkg-deb --build pkg/deb pkg/alefs-$(VERSION).deb
	@echo "Package: pkg/alefs-$(VERSION).deb"

dist: clean
	tar czf alefs-$(VERSION).tar.gz src/ Makefile README.md tests/ LICENSE pkg/

-include $(DEPS)
