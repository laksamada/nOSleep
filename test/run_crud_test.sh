#!/bin/bash
set -e
TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$TEST_DIR/.." && pwd)"
WORK=/tmp/tescrud
OUT="$TEST_DIR/hasil"

rm -rf "$WORK" "$OUT" && mkdir -p "$WORK" "$OUT"
(cd "$REPO" && tar --exclude=./.git --exclude=./test/hasil -cf - .) | tar -xf - -C "$WORK"
cd "$WORK"
rm -f bin/*.o bin/*.iso bin/storage.bin
cp "$TEST_DIR/kernel_test_crud.c" src/kernel.c
[ "$STACK" = "2mb" ] && sed -i 's/^KERNEL_STACK_SIZE equ 4096/KERNEL_STACK_SIZE equ 2097152/' src/kernel-entrypoint.s
grep "^KERNEL_STACK_SIZE" src/kernel-entrypoint.s

make build 2>&1 | grep -E "error|warning" || true
[ -f bin/OS2025.iso ] || { echo "BUILD GAGAL"; exit 1; }
echo "build OK (-Wall -Wextra -Werror)"
truncate -s 4M bin/storage.bin

boot() {
    rm -f /tmp/qmon.sock /tmp/screen.ppm
    (cd "$WORK/bin" && qemu-system-i386 -display none -monitor unix:/tmp/qmon.sock,server,nowait \
        -drive file=storage.bin,format=raw,if=ide,index=0,media=disk -cdrom OS2025.iso 2>/dev/null) &
    sleep 10
    python3 - <<'EOF'
import socket, time
s = socket.socket(socket.AF_UNIX)
s.connect("/tmp/qmon.sock")
time.sleep(0.3)
for cmd in ("screendump /tmp/screen.ppm\n", "quit\n"):
    s.send(cmd.encode())
    time.sleep(0.8)
EOF
    wait 2>/dev/null || true
    python3 - "$1" <<'EOF'
import struct, sys, zlib
d = open("/tmp/screen.ppm", "rb").read()
p = d.split(maxsplit=4)
w, h, px = int(p[1]), int(p[2]), p[4]
raw = b"".join(b"\0" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
ch = lambda t, b: struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xffffffff)
open(sys.argv[1], "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                              + ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b""))
EOF
}

for n in 1 2; do
    boot "$OUT/boot$n.png"
    cp "$WORK/bin/storage.bin" "$OUT/storage-boot$n.bin"
    echo "=== fsck setelah boot $n"
    python3 "$TEST_DIR/fsck.py" "$OUT/storage-boot$n.bin" || true
done
echo "screenshot: $OUT/boot1.png dan $OUT/boot2.png"
