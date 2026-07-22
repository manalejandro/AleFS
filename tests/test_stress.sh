#!/usr/bin/env bash
set -euo pipefail

ALEFS=${ALEFS:-./alefs}
TEST_IMG=$(mktemp /tmp/alefs-stress-XXXX.bin)
trap "rm -f $TEST_IMG" EXIT

echo "=== AleFS Stress/Edge Test Suite ==="

# 1. Format large image
echo "#1: Format 128 MiB image"
$ALEFS format "$TEST_IMG" 128
echo "  OK"

# 2. Create many files in one directory
echo "#2: Create 100 files"
for i in $(seq 1 100); do
    $ALEFS create "$TEST_IMG" "/f$i.txt" > /dev/null
done
echo "  OK"

# 3. List directory with many files
echo "#3: ls after 100 creates"
$ALEFS ls "$TEST_IMG" / > /dev/null
echo "  OK"

# 4. Delete all 100 files
echo "#4: Remove 100 files"
for i in $(seq 1 100); do
    $ALEFS rm "$TEST_IMG" "/f$i.txt" > /dev/null
done
echo "  OK"

# 5. Deeply nested directories (depth 50)
echo "#5: Deep nesting (depth 50)"
path=""
for i in $(seq 1 50); do
    path="$path/d$i"
done
# mkdir step by step
cur="/"
for i in $(seq 1 50); do
    cur="$cur/d$i"
    $ALEFS mkdir "$TEST_IMG" "$cur" > /dev/null
done
echo "  OK"

# 6. Create file at depth
echo "#6: File at depth 50"
$ALEFS create "$TEST_IMG" "$path/deep.txt"
echo "  OK"

# 7. Stat file at depth
echo "#7: Stat deep file"
$ALEFS stat "$TEST_IMG" "$path/deep.txt" > /dev/null
echo "  OK"

# 8. Clean up deep tree
echo "#8: Remove deep tree"
$ALEFS rm "$TEST_IMG" "$path/deep.txt"
for i in $(seq 50 -1 1); do
    p="/"
    for j in $(seq 1 $i); do
        p="$p/d$j"
    done
    $ALEFS rmdir "$TEST_IMG" "$p" > /dev/null
done
echo "  OK"

# 9. Long file name (max 255 chars)
echo "#9: Long file name"
long_name=$(python3 -c "print('A' * 255)")
$ALEFS create "$TEST_IMG" "/$long_name"
echo "  OK"

# 10. Special characters in names
echo "#10: Special chars"
$ALEFS mkdir "$TEST_IMG" "/dir with spaces"
$ALEFS create "$TEST_IMG" "/dir with spaces/file-dash_underscore.txt"
echo "  OK"

echo ""
echo "=== All stress/edge tests passed ==="
