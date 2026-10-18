#!/bin/sh
# run from repo root: ./tests/host/build.sh && ./tests/host/test_write
set -e
cd "$(dirname "$0")/../.."
clang -Wall -Wextra -Werror -std=c11 -Isrc \
    -Dwrite=ext2_write -Dread=ext2_read -Ddelete=ext2_delete \
    src/filesystem/ext2.c tests/host/fake_disk.c tests/host/test_write.c \
    -o tests/host/test_write
