#!/bin/bash
set -e

ALEFS=./aleqfs
TEST_IMG=/tmp/aleqfs-stress-$$.bin
echo "=== AleQFS Stress Test ==="

cleanup() {
    rm -f "$TEST_IMG"
}
trap cleanup EXIT

$ALEFS format "$TEST_IMG" 64

echo "Creating 100 files..."
for i in $(seq 1 100); do
    $ALEFS create "$TEST_IMG" /file-$i.txt
done

echo "Listing root..."
$ALEFS ls "$TEST_IMG" /

echo "Stress test complete."
