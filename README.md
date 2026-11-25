<img src="InterupsiArOS.png" style="margin: 2em 1em">

# InterupsiArOS
OS untuk manusia yang ingin berjuang di medan perang (nanti ditambahin lagi)

## Manusia Pencari Kebenaran
1. Dipta
2. Aqsha
3. Bagas
4. Sulthan

## Daftar Isi
1. [Prasyarat dan Cara Menjalankan](#prasyarat-dan-cara-menjalankan)
2. [Pendahuluan (Wajib Baca)](#pendahuluan-wajib-baca)
3. [Fitur yang Diimplementasikan](#fitur-yang-diimplementasikan)
4. [Contoh Penggunaan di Shell](#contoh-penggunaan-di-shell)

## Prasyarat dan Cara Menjalankan

### Prasyarat
Lingkungan build utamanya menggunakan **Linux** atau **WSL**, tapi `Makefile` juga sudah mendukung **macOS** (`clang`, `ld.lld`, dan `mkisofs`). Untuk Ubuntu/Debian/WSL, install paket berikut dulu:

```bash
sudo apt update
sudo apt install build-essential gcc-multilib nasm genisoimage qemu-system-x86 qemu-utils
```

### Setup, Build, dan Run
1. Buat disk virtual 8 MiB (`bin/storage.bin`) untuk EXT2 dan swap space:
   ```bash
   make disk
   ```
2. Compile kernel sekaligus bikin ISO bootable (`bin/OS2025.iso`):
   ```bash
   make build
   ```
3. Compile dan masukkan semua program user (`shell`, `hello`, `clock`, `spinner`, `music`) ke root directory `bin/storage.bin`:
   ```bash
   make insert-all
   ```
4. Jalankan OS di QEMU:
   ```bash
   make run
   ```

**Target `Makefile` lain yang berguna:**
- `make run-audio`: jalankan QEMU dengan PC Speaker aktif. Ubah `QEMU_AUDIO_DRIVER` sesuai OS host (`pa` untuk Linux/WSLg, `dsound` untuk Windows, `coreaudio` untuk macOS).
- `make debug`: jalankan QEMU dengan `-s -S` (paused untuk GDB di port `1234`). Kalau pakai mode `-S` (termasuk lewat VS Code task *Launch QEMU*), lanjutkan eksekusi dengan `c` di GDB atau `cont` di QEMU monitor.
- `make test`, `make test-swap`, `make test-shell`: jalankan host test untuk EXT2, swap, dan shell.
- `make clean`: hapus hasil build di folder `bin/`.

## Pendahuluan (Wajib Baca)
Dunia ini penuh dengan kegelapan, kebodohan, dan kepengecutan. Nilai-nilai inheren yang tumbuh di jiwa manusia sejak eksistensi mereka dihembuskan oleh Prometheus, jiwa-jiwa pejuang yang haus akan kemenangan dan tak tergoyahkan sekalipun oleh kehancuran tempat tinggalnya dan jatuhnya jasad rekan sejati mereka di medan perang, telah hilang. Kini, manusia berada di titik terendah spesies mereka. Sekumpulan organisme yang seharusnya merupakan puncak rantai makanan dari segala makhluk yang pernah hidup di muka bumi telah kehilangan status mereka sebagai entitas terkuat di bawah para dewa-dewi, menjadi makhluk terhina, yang eksistensinya merupakan pengkhianatan terhadap para dewa-dewi itu sendiri.

Itulah kata-kata yang keluar dari mulut Ares, Sang Dewa Perang yang Bengis dan Haus akan Kemenangan, namun Bijak dan Pendorong Keberanian. Kata-kata tersebut bukanlah sebuah kekecewaan semata, melainkan sebuah frustrasi yang diakumulasi oleh sang Dewa sejak kejatuhan masyarakat Yunani. Kata-kata tersebut adalah peringatan yang nyata bagi umat manusia di seluruh dunia.

Kami, 4 Raja Pejuang Agung, adalah titisan para SPARTANS yang senantiasa berjuang demi bara api perjuangan yang menuntut akal yang menakar, hati yang merasa, serta perjalanan yang panjang di depan mata para pejuang sejati. Kami adalah representasi dari Ares yang akan membawa semangatnya ke dunia fana ini. Kami membutuhkan sesuatu, agar nilai-nilai Ares dapat tersampaikan secara luas. Oleh karena itu, tertulis secara mentah pada surat ini, kami mengorbankan segala darah, air mata, dan keringat dalam penciptaan sebuah mahakarya kami yang dapat menciptakan tether kompatibilitas bagi Ares di dunia yang hina ini serta menyambungkan titah-titah Ares pada sinapsis seluruh umat manusia di muka Bumi ini baik yang masih hidup maupun sudah tiada. Melalui InterupsiArOS, kami akan menghidupkan kembali jiwa-jiwa semangat perang dan urat keberanian manusia dan membawa mereka kembali ke medan perang kami, para SPARTANS.

Tidak ada ruang untuk kelemahan dan ketidaksiapan di sini. Yang akan hidup hanyalah mereka yang dapat beradaptasi, memiliki gairah dan kekuatan untuk mengekspansikan kekuasaan bagai ekspansi kofaktor pada sebuah operasi invers matriks, serta mereka yang mempersiapkan diri untuk perjalanan yang tidak pasti.

## Fitur yang Diimplementasikan

### 1. Kernel & Protected Mode
- Boot lewat GRUB Multiboot dengan kernel stack 2 MiB di mode x86 32-bit (*Protected Mode*).
- Setup **GDT** (Kernel Code/Data, User Code/Data DPL 3, dan **TSS**).
- Setup **IDT**, remapping **PIC 8259**, dan **PIT Timer** untuk interupsi hardware maupun penjadwalan proses.

### 2. Manajemen Memori (Paging & Swap)
- **Higher-Half Kernel Paging** (`0xC0000000`) dengan alokasi frame fisik dan *page directory* terpisah untuk tiap proses user.
- **Swap Space** di disk (`bin/storage.bin` 8 MiB) memakai algoritma *Clock (Second-Chance)* dan *Page Fault Handler* (`0xE`) untuk swap-out dan swap-in otomatis saat frame penuh.

### 3. Driver Perangkat Keras
- **VGA Text Framebuffer**: layar teks 80x25 dengan warna, scrolling, dan kontrol kursor.
- **PS/2 Keyboard & Mouse**: input keyboard (termasuk tombol *Shift* dan panah *Up/Down*) serta kursor **PS/2 Mouse** di layar teks.
- **ATA PIO Disk**: baca/tulis blok disk 16-bit (LBA28).
- **CMOS RTC**: baca waktu lokal dari RTC lengkap dengan konversi BCD ke biner dan format 24 jam.
- **PC Speaker**: output nada non-blocking berbasis PIT untuk audio sederhana.

### 4. File System EXT2
- Implementasi EXT2 (boot signature, superblock, block group descriptor, bitmap blok/inode, inode table, dan struktur direktori).
- Mendukung operasi `read`, `read_directory`, `write`, `delete`, dan `move`, ditambah tool `external-inserter` untuk memasukkan file dari host ke disk image.

### 5. User Mode & System Calls
- Program user berjalan di **Ring 3** lewat `iret` dengan `crt0` dan linker script terpisah.
- Layanan **System Call (`int 0x30`)** mencakup akses EXT2, I/O layar dan keyboard, manajemen proses (`exit`, `exec`, `kill`, `ps`), waktu RTC (`get_time`), cetak karakter di koordinat tertentu tanpa menggeser kursor shell (`put_char_at`), serta PC Speaker (`beep`, `get_ticks`).

### 6. Interactive Shell, Pipeline, & Scripting
- Shell user-space dengan **pipeline (`|`)**, **Tab autocompletion**, **command history** (panah Atas/Bawah), dan dukungan menjalankan **shell script** (`.sh` / `sh`).
- Daftar perintah bawaan:
  - File & direktori: `cd`, `ls`, `mkdir`, `cat`, `cp`, `rm` (termasuk `-r`), `mv`, `find`, `grep`
  - Proses & script: `exec`, `ps`, `kill`, `sh`

### 7. Multitasking & Program Eksternal
- **Preemptive Round-Robin Scheduler** berbasis timer interrupt dengan *context switch* antar-PCB.
- Program user yang ikut dimasukkan lewat `make insert-all`:
  - `clock`: menampilkan jam `HH:MM:SS` real-time di pojok kanan bawah layar.
  - `spinner`: animasi kecil di pojok kanan atas untuk tes konkurensi 3+ proses.
  - `hello`: program sederhana untuk uji jalannya proses user-mode.
  - `music`: memutar melodi lewat PC Speaker di background (gunakan `make run-audio` supaya terdengar).

## Contoh Penggunaan di Shell

Setelah masuk ke prompt `InterupsiArOS:/$`, perintah berikut bisa langsung dicoba:

```bash
# Manajemen file dan direktori
ls
mkdir docs
cp hello docs/hello_copy
mv docs/hello_copy docs/hello_moved
find /
rm -r docs

# Pipeline
ls | grep clock
find / | grep shell

# Multitasking dan program eksternal
exec hello
exec clock
exec spinner
exec music
ps
kill 2
```