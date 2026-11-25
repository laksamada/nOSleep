<p align="center">
  <img src="docs/banner.png" alt="nOSleep, the OS that never sleeps" width="100%">
</p>

<p align="center">
  a 32-bit x86 OS we built from scratch for IF2130 Operating Systems.<br>
  it boots and multitasks and even plays Bad Apple!!. sleep was never part of the plan.
</p>

<img src="docs/h-meet.png" alt="meet Owlnighter" width="100%">

<img src="docs/mascot.png" alt="Owlnighter, the nOSleep owl" width="160" align="right">

Owlnighter is our mascot. He has been pulling all-nighters since Milestone 2 so the panda eyes are earned. The coffee never leaves his wing and his pupils are literally terminal cursors waiting for your next command.

Also there is no light mode. Owlnighter is sensitive to light and we respect that.

<br clear="right">

<img src="docs/h-features.png" alt="what it can do" width="100%">

<p align="center">
  <img src="docs/screenshot-desktop.png" alt="nOSleep desktop with the Terminal window" width="49%">
  <img src="docs/screenshot-badapple.png" alt="Bad Apple!! running on nOSleep" width="49%">
</p>

- **boots straight into a retro desktop.** 800x600 in full color with real mouse support. Windows drag around and the shell lives inside a Terminal window just like cmd.
- **actual multitasking.** a round-robin scheduler runs on a 1000 Hz timer. Start `clock` and it keeps ticking in the background while you type. `ps` and `kill` do what you expect.
- **its own EXT2 file system.** all the usual commands like `ls` `cd` `mkdir` `cat` `cp` `rm` `mv` `find` `grep` with pipes and `>` redirects.
- **paging that respects boundaries.** every process gets its own page directory. Pages load on demand and swap to disk when RAM gets tight.
- **Bad Apple!! at 20 FPS.** 4381 frames squeezed into 712 KB and read from 12 files on disk. Hit Esc or the red X to dip.

<img src="docs/h-run.png" alt="run it" width="100%">

You need Ubuntu or WSL with `gcc` `nasm` `make` `genisoimage` `qemu-system-i386` and `qemu-img`. macOS works too through Homebrew LLVM.

```bash
make disk
make insert-shell
make insert-programs
make run-nogdb
```

Click the Terminal window and type `help`. `badapple` is right there no cap.

<img src="docs/h-squad.png" alt="who did what" width="100%">

| Name | NIM | What |
|---|---|---|
| Reinhard Mikhael Tandra | 13525076 | framebuffer, disk driver, ext2 read, external inserter, PCB & process lifecycle, GUI syscalls, user libc |
| Arga Cyrano Simanjuntak | 13525094 | build system, ext2 init, GDT/TSS, shell, clock & hello apps, Bad Apple |
| Satya Radhityan Yahya | 13525098 | keyboard, ext2 write, context switch, paging, scheduler, syscall hardening, graphics & mouse driver |
| Mochamad Fachri Alfaridzi | 13525128 | GDT, interrupt, ext2 delete & fixes, user mode + syscall, swap, process syscalls, CMOS RTC, desktop GUI, README |

<img src="docs/h-credits.png" alt="credits" width="100%">

- base kit from [OS Template 2026](https://github.com/Nathan-Marpaung/os-template-2026) by the Lab Sister ITB assistants
- [Bad Apple!!](https://youtu.be/FtutLA63Cp8) by Alstroemeria Records and Anira for the course bonus
- Owlnighter and the banner and the logo were drawn pixel by pixel for nOSleep
