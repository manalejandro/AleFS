#!/usr/bin/env bash
set -euo pipefail

ALEFS=${ALEFS:-./alefs}
TEST_IMG=$(mktemp /tmp/alefs-basic-XXXX.bin)
CONTENT=$(mktemp /tmp/alefs-content-XXXX.txt)
trap "rm -f $TEST_IMG $CONTENT" EXIT

echo "=== AleFS Basic Test Suite ==="

# 1. Format
echo "#1: Format 32 MiB image"
$ALEFS format "$TEST_IMG" 32
echo "  OK"

# 2. Dump superblock
echo "#2: Superblock dump"
$ALEFS dump "$TEST_IMG" > /dev/null
echo "  OK"

# 3. Create directory
echo "#3: mkdir /data"
$ALEFS mkdir "$TEST_IMG" /data
echo "  OK"

# 4. Create nested directories
echo "#4: mkdir -p /data/sub1/sub2"
$ALEFS mkdir "$TEST_IMG" /data/sub1
$ALEFS mkdir "$TEST_IMG" /data/sub1/sub2
echo "  OK"

# 5. List root
echo "#5: ls /"
$ALEFS ls "$TEST_IMG" /
echo "  OK"

# 6. List subdir
echo "#6: ls /data"
$ALEFS ls "$TEST_IMG" /data
echo "  OK"

# 7. Create empty file
echo "#7: create /data/sub1/empty.txt"
$ALEFS create "$TEST_IMG" /data/sub1/empty.txt
echo "  OK"

# 8. Copy file in
echo "#8: cp-in"
echo "Hello AleFS!" > "$CONTENT"
$ALEFS cp-in "$TEST_IMG" "$CONTENT" /data/hello.txt
echo "  OK"

# 9. Read file back
echo "#9: cat"
OUT=$($ALEFS cat "$TEST_IMG" /data/hello.txt)
if [ "$OUT" != "Hello AleFS!" ]; then
    echo "  FAIL: expected 'Hello AleFS!', got '$OUT'"
    exit 1
fi
echo "  OK"

# 10. Stat file
echo "#10: stat"
$ALEFS stat "$TEST_IMG" /data/hello.txt > /dev/null
echo "  OK"

# 11. Rename file
echo "#11: mv"
$ALEFS mv "$TEST_IMG" /data/hello.txt /data/greeting.txt
echo "  OK"

# 12. Verify renamed contents
echo "#12: cat renamed"
OUT=$($ALEFS cat "$TEST_IMG" /data/greeting.txt)
if [ "$OUT" != "Hello AleFS!" ]; then
    echo "  FAIL"
    exit 1
fi
echo "  OK"

# 13. Remove file
echo "#13: rm"
$ALEFS rm "$TEST_IMG" /data/greeting.txt
echo "  OK"

# 14. Remove directories bottom-up
echo "#14: rmdir chain"
$ALEFS rmdir "$TEST_IMG" /data/sub1/sub2
$ALEFS rmdir "$TEST_IMG" /data/sub1
$ALEFS rmdir "$TEST_IMG" /data
echo "  OK"

# 15. File with larger content (>1 block = 4096 bytes)
echo "#15: large file"
dd if=/dev/urandom bs=4096 count=4 of="$CONTENT" 2>/dev/null
$ALEFS cp-in "$TEST_IMG" "$CONTENT" /large.bin
$ALEFS stat "$TEST_IMG" /large.bin > /dev/null
echo "  OK"

# 16. Verify large file content
echo "#16: verify large file"
$ALEFS cat "$TEST_IMG" /large.bin > "$CONTENT.verify"
if ! diff "$CONTENT" "$CONTENT.verify"; then
    echo "  FAIL: content mismatch"
    exit 1
fi
echo "  OK"

echo ""
echo "=== All basic tests passed ==="
