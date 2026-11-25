"""Encode the Bad Apple video into badapple.0, badapple.1, ... for nOSleep.

usage: python3 encode.py <video> [ffmpeg]

Every frame is a 1-bit image stored as run lengths (black first, then
alternating). The run stream is compressed with a small LZ77 and split into
64 KiB files because one EXT2 file in nOSleep holds at most 70 KiB.
"""
import struct
import subprocess
import sys

WIDTH, HEIGHT, FPS = 120, 90, 20
CHUNK_SIZE = 65536
MIN_MATCH = 4
MAX_OFFSET = 65535


def read_frames(video, ffmpeg):
    raw = subprocess.run(
        [ffmpeg, '-v', 'quiet', '-i', video, '-vf', f'fps={FPS},scale={WIDTH}:{HEIGHT}:flags=area',
         '-f', 'rawvideo', '-pix_fmt', 'gray', '-'],
        check=True, capture_output=True).stdout
    size = WIDTH * HEIGHT
    return [raw[i:i + size] for i in range(0, len(raw) - size + 1, size)]


def put_run(out, length):
    if length < 128:
        out.append(length)
    else:
        out += bytes((0x80 | (length >> 8), length & 0xFF))


def encode_runs(frame):
    out, color, length = bytearray(), 0, 0
    for pixel in frame:
        if (pixel >= 128) == color:
            length += 1
        else:
            put_run(out, length)
            color, length = not color, 1
    put_run(out, length)
    return out


def put_length(out, length):
    while length >= 255:
        out.append(255)
        length -= 255
    out.append(length)


def compress(data):
    out, recent = bytearray(), {}
    literal_start = i = 0
    while i < len(data):
        key = bytes(data[i:i + MIN_MATCH])
        best_length = best_offset = 0
        for j in reversed(recent.get(key, [])[-16:]):
            if i - j > MAX_OFFSET:
                break
            length = 0
            while i + length < len(data) and data[j + length] == data[i + length]:
                length += 1
            if length > best_length:
                best_length, best_offset = length, i - j
        recent.setdefault(key, []).append(i)

        if best_length < MIN_MATCH:
            i += 1
            continue
        literals = data[literal_start:i]
        match = best_length - MIN_MATCH
        out.append((min(len(literals), 15) << 4) | min(match, 15))
        if len(literals) >= 15:
            put_length(out, len(literals) - 15)
        out += literals
        out += struct.pack('<H', best_offset)
        if match >= 15:
            put_length(out, match - 15)
        for k in range(i + 1, i + best_length):
            recent.setdefault(bytes(data[k:k + MIN_MATCH]), []).append(k)
        i += best_length
        literal_start = i

    literals = data[literal_start:]
    if literals:
        out.append(min(len(literals), 15) << 4)
        if len(literals) >= 15:
            put_length(out, len(literals) - 15)
        out += literals
    return out


def main():
    video = sys.argv[1]
    ffmpeg = sys.argv[2] if len(sys.argv) > 2 else 'ffmpeg'
    frames = read_frames(video, ffmpeg)
    runs = bytearray()
    for frame in frames:
        runs += encode_runs(frame)
    packed = compress(runs)

    header = struct.pack('<4sHHHHIII', b'BAPL', WIDTH, HEIGHT, FPS, 0, len(frames), len(packed), len(runs))
    data = header + packed
    for index in range(0, len(data), CHUNK_SIZE):
        with open(f'badapple.{index // CHUNK_SIZE}', 'wb') as file:
            file.write(data[index:index + CHUNK_SIZE])
    print(f'{len(frames)} frames, {len(runs)} bytes of runs, {len(data)} bytes in '
          f'{(len(data) + CHUNK_SIZE - 1) // CHUNK_SIZE} files')


if __name__ == '__main__':
    main()
