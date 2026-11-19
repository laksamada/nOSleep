![Credit: 朧月](mascot.jpg)

# nOSleep

Sistem operasi x86 32-bit untuk Tugas Besar IF2130 Sistem Operasi 2026/2027.

## Daftar Isi

- [Cara Run](#cara-run)
- [Fitur yang Dibuat](#fitur-yang-dibuat)
- [Perintah Shell](#perintah-shell)
- [Struktur Kode](#struktur-kode)
- [Maskot](#maskot)

## Cara Run

Kebutuhan (Ubuntu / WSL): `gcc`, `nasm`, `make`, `genisoimage`, `qemu-system-i386`, `qemu-img`.

```bash
make disk            # buat storage.bin kosong 4 MiB
make insert-shell    # masukkan shell ke disk
make insert-programs # masukkan program clock ke disk
make run-nogdb       # build kernel dan ISO, lalu jalankan QEMU
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
- Syscall `exit` (11), `exec` (12), `kill` (13), `ps` (14), waktu CMOS (15), tulis di posisi layar (16), `yield` (17),
  tick timer (18), dan cek proses hidup (19). Program yang selesai otomatis memanggil `exit` dari `crt0.s`.
- Program `clock`: membaca RTC CMOS dan menampilkan jam di baris status (baris paling bawah tidak ikut scroll).
- Fault di program user (page fault di luar batas, exception CPU) hanya menghentikan program tersebut, bukan seluruh OS.
- Semua teks yang tampil di OS berbahasa Inggris.

## Perintah Shell

| Perintah | Keterangan |
|---|---|
| `ls`, `cd`, `mkdir`, `cat`, `cp [-r]`, `rm [-r]`, `mv`, `find`, `grep`, `echo` | operasi file, mendukung pipeline `\|` dan `>` |
| `exec <program> [&]` | jalankan program, `&` agar berjalan di background |
| `<program> [&]` | sama dengan `exec`, contoh `clock &` |
| `ps` | daftar proses beserta memori yang dipakai |
| `kill <pid>` | hentikan proses (PID 1 dilindungi) |
| `memtest [n]` | tes paging dan swap |
| `clear`, `help` | bersihkan layar, daftar perintah |

## Struktur Kode

| Lokasi | Isi |
|---|---|
| `src/process.c`, `src/scheduler.c`, `src/context-switch.s` | proses, scheduler, context switch |
| `src/paging.c`, `src/swap.c` | page directory per proses, demand paging, swap |
| `src/interrupt.c` | dispatch interrupt dan syscall (nomor di `src/header/process/syscall.h`) |
| `src/cmos.c` | driver RTC CMOS |
| `src/user-shell.c`, `src/user/clock.c` | shell dan program jam |

## Maskot

Maskot kelompok ada di bagian atas README (`mascot.jpg`).
