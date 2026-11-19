![Credit: 朧月](mascot.jpg)

# nOSleep

Sistem operasi x86 32-bit untuk Tugas Besar IF2130 Sistem Operasi 2026/2027.

## Daftar Isi

- [Cara Run](#cara-run)
- [Fitur yang Dibuat](#fitur-yang-dibuat)
- [Perintah Shell](#perintah-shell)
- [GUI dan Doom](#gui-dan-doom)
- [Struktur Kode](#struktur-kode)
- [Kredit](#kredit)
- [Maskot](#maskot)

## Cara Run

Kebutuhan (Ubuntu / WSL): `gcc`, `nasm`, `make`, `genisoimage`, `qemu-system-i386`, `qemu-img`.

```bash
make disk            # buat storage.bin kosong 4 MiB
make insert-shell    # masukkan shell ke disk
make insert-programs # masukkan clock dan gui ke disk
make run-nogdb       # build kernel, doom, ISO, lalu jalankan QEMU
```

`make run` sama seperti `make run-nogdb` tetapi QEMU menunggu debugger (`-s -S`) untuk dipakai dengan GDB di VS Code.

## Fitur yang Dibuat

Milestone 1-3: GDT, IDT, PIC, keyboard, framebuffer teks, driver disk ATA, file system EXT2, paging dengan
recursive mapping, swap, user mode, syscall, dan shell CLI.

Milestone 4:

- Multi virtual address space: setiap proses punya page directory sendiri (`paging_create_new_page_directory`,
  `paging_free_page_directory`, `paging_use_page_directory`). Saat proses dihapus, semua frame, page table, dan slot
  swap miliknya dikembalikan.
- Process control block, `process_create_user_process`, dan `process_destroy`. Shell adalah proses init (PID 1).
- Scheduler round-robin preemptive dengan timer PIT 1000 Hz (jatah 10 ms per proses) dan context switch di
  `context-switch.s`. Koreksi `intsetup.s` dan urutan `CPURegister` dari kit sudah diterapkan.
- Syscall `exec`, `ps`, `kill`, `exit`, `yield`, waktu CMOS, dan tick timer. Program yang selesai otomatis memanggil
  `exit` dari `crt0.s`.
- Program `clock`: membaca RTC CMOS dan menampilkan jam di baris status (baris paling bawah tidak ikut scroll).
- Fault di program user (page fault di luar batas, exception CPU) hanya menghentikan program tersebut, bukan seluruh OS.

Bonus:

- GUI 800x600 (Bochs VBE) dengan mouse PS/2 dan keyboard: ikon desktop, jendela yang bisa diseret dan ditutup,
  daftar file, taskbar dengan jam. Teks memakai font 8x16 bawaan VGA yang dibaca dari VRAM.
- Doom (shareware) bisa dimainkan, dari shell (`doom`) maupun dari ikon Doom di GUI.

## Perintah Shell

| Perintah | Keterangan |
|---|---|
| `ls`, `cd`, `mkdir`, `cat`, `cp [-r]`, `rm [-r]`, `mv`, `find`, `grep`, `echo` | operasi file, mendukung pipeline `\|` dan `>` |
| `exec <program> [&]` | jalankan program, `&` agar berjalan di background |
| `<program> [&]` | sama dengan `exec`, contoh `clock &`, `gui`, `doom` |
| `ps` | daftar proses beserta memori yang dipakai |
| `kill <pid>` | hentikan proses (PID 1 dilindungi) |
| `memtest [n]` | tes paging dan swap |
| `clear`, `help` | bersihkan layar, daftar perintah |

## GUI dan Doom

Jalankan `gui` dari shell.

- Mouse: klik dua kali ikon untuk membuka, seret judul jendela untuk memindah, tombol merah untuk menutup.
- Keyboard: panah atas/bawah atau Tab memilih ikon, Enter membuka, Esc menutup jendela atau keluar dari GUI.
- Ikon: Doom (shortcut ke game), Files (isi folder root), About, Shell (kembali ke shell teks).
- Semua teks yang tampil di OS (shell, kernel, GUI) berbahasa Inggris.

Kontrol Doom: panah untuk bergerak, Ctrl menembak, Spasi membuka pintu, Shift berlari, Alt strafe, Esc menu.
Keluar lewat menu Quit Game lalu tekan `y`.

Disk EXT2 hanya 4 MiB, jadi `doom` dan `doom1.wad` dimuat GRUB sebagai modul (lihat `src/menu.lst`) dan dibaca kernel
sebagai file boot read-only (`src/bootfile.c`). `exec` mencari program di folder sekarang lalu di file boot.

## Struktur Kode

| Lokasi | Isi |
|---|---|
| `src/process.c`, `src/scheduler.c`, `src/context-switch.s` | proses, scheduler, context switch |
| `src/paging.c`, `src/swap.c` | page directory per proses, demand paging, swap |
| `src/interrupt.c` | dispatch interrupt dan syscall (nomor di `src/header/process/syscall.h`) |
| `src/graphics.c`, `src/mouse.c`, `src/cmos.c`, `src/bootfile.c` | driver grafis BGA, mouse PS/2, RTC, modul GRUB |
| `src/user/libc` | libc kecil untuk program user (string, malloc, printf, file boot, wrapper syscall) |
| `src/user/clock.c`, `src/user/gui.c` | program jam dan GUI |
| `src/user/doom` | engine doomgeneric dan port nOSleep (`doomgeneric_nosleep.c`) |

## Kredit

- Engine Doom dari [doomgeneric](https://github.com/ozkl/doomgeneric) (GPL-2.0, lihat `src/user/doom/LICENSE`).
- `other/doom1.wad` adalah Doom shareware v1.9 dari id Software yang boleh disebarkan bebas.

## Maskot

Maskot kelompok ada di bagian atas README (`mascot.jpg`).
