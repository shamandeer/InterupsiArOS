#!/bin/sh
# demo.sh - demo bonus shell script execution
# shebang, komentar, dan baris kosong dilewati

mkdir demo
cd demo
mkdir src
mkdir docs
ls

# pipeline di dalam script
cat /demo2.sh | grep mkdir

# memanggil script kedua, berjalan di shell yang sama (cwd tetap /demo)
sh /demo2.sh

# command salah: error muncul, script tetap lanjut
perintahsalah
ls docs
