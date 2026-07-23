#!/bin/bash
set -e

ALEFS=./aleqfs
TEST_IMG=/tmp/aleqfs-test-$$.bin
echo "=== AleQFS Basic Test Suite ==="

cleanup() {
    rm -f "$TEST_IMG"
}
trap cleanup EXIT

# Format
echo "--- format ---"
$ALEFS format "$TEST_IMG" 64

# Dump superblock
echo "--- dump ---"
$ALEFS dump "$TEST_IMG"

# Quantum status
echo "--- qstatus ---"
$ALEFS qstatus "$TEST_IMG"

# Mkdir
echo "--- mkdir ---"
$ALEFS mkdir "$TEST_IMG" /hello

# Create file
echo "--- create ---"
$ALEFS create "$TEST_IMG" /hello/world.txt

# Write content to file (via cp-in)
echo "--- cp-in ---"
echo "hello quantum world" > /tmp/aleqfs-content-$$.txt
$ALEFS cp-in "$TEST_IMG" /tmp/aleqfs-content-$$.txt /hello/world.txt
rm -f /tmp/aleqfs-content-$$.txt

# Cat
echo "--- cat ---"
$ALEFS cat "$TEST_IMG" /hello/world.txt

# Stat
echo "--- stat ---"
$ALEFS stat "$TEST_IMG" /hello/world.txt

# Entangle directories
echo "--- entangle ---"
$ALEFS mkdir "$TEST_IMG" /a
$ALEFS mkdir "$TEST_IMG" /b
$ALEFS entangle "$TEST_IMG" /a /b

# Observe (collapse quantum state)
echo "--- observe ---"
$ALEFS observe "$TEST_IMG" /hello/world.txt

# Grover search
echo "--- grover ---"
$ALEFS grover "$TEST_IMG" 1

# Ls
echo "--- ls ---"
$ALEFS ls "$TEST_IMG" /

# Tree
echo "--- tree ---"
$ALEFS tree "$TEST_IMG" /

# Mv
echo "--- mv ---"
$ALEFS mv "$TEST_IMG" /hello/world.txt /hello/quantum.txt

# Rm
echo "--- rm ---"
$ALEFS rm "$TEST_IMG" /hello/quantum.txt

# Rmdir
echo "--- rmdir ---"
$ALEFS rmdir "$TEST_IMG" /a
$ALEFS rmdir "$TEST_IMG" /b
$ALEFS rmdir "$TEST_IMG" /hello

echo "=== All AleQFS tests passed ==="
