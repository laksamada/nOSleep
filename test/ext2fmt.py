import struct

BLOCK = 512
DISK_BLOCKS = 4194304 // BLOCK
INODE_SIZE = 70
INODES_PER_TABLE = BLOCK // INODE_SIZE
INODE_TABLE_BLOCKS = 16
INODES_PER_GROUP = INODES_PER_TABLE * INODE_TABLE_BLOCKS
GROUPS = (BLOCK // 32) // 2
BLOCKS_PER_GROUP = DISK_BLOCKS // (BLOCK // 32) // 2
FS_BLOCKS = BLOCKS_PER_GROUP * GROUPS
POINTERS = BLOCK // 4
S_IFREG, S_IFDIR = 0x8000, 0x4000
FT_REG, FT_DIR = 1, 2
ROOT_BLOCK = 3 + 2 + INODE_TABLE_BLOCKS

SIGNATURE = bytearray(BLOCK)
SIGNATURE[:80] = (b"Course          Designed by     Lab Sister ITB  Made with <3    "
                  b"-----------2025\n")
SIGNATURE[BLOCK - 2:] = b"Ok"


def pattern(size):
    return bytes((i * 31 + 7) % 251 for i in range(size))


def align4(n):
    return (n + 3) & ~3


class Image:
    def __init__(self, data=None):
        self.data = bytearray(data) if data else bytearray(DISK_BLOCKS * BLOCK)

    def block(self, n):
        return self.data[n * BLOCK:(n + 1) * BLOCK]

    def put(self, n, payload):
        payload = bytes(payload).ljust(BLOCK, b"\0")[:BLOCK]
        self.data[n * BLOCK:(n + 1) * BLOCK] = payload

    def superblock(self):
        f = struct.unpack_from("<10IH", self.data, BLOCK)
        return {"inodes": f[0], "blocks": f[1], "free_blocks": f[3], "free_inodes": f[4]}

    def bgd(self, g):
        f = struct.unpack_from("<3I3H", self.data, 2 * BLOCK + g * 32)
        return {"block_bitmap": f[0], "inode_bitmap": f[1], "inode_table": f[2],
                "free_blocks": f[3], "free_inodes": f[4], "dirs": f[5]}

    def bits(self, block_no, limit):
        b = self.block(block_no)
        return {i for i in range(limit) if b[i // 8] >> (i % 8) & 1}

    def used_blocks(self):
        return {g * BLOCKS_PER_GROUP + i for g in range(GROUPS)
                for i in self.bits(self.bgd(g)["block_bitmap"], BLOCKS_PER_GROUP)}

    def used_inodes(self):
        return {g * INODES_PER_GROUP + i + 1 for g in range(GROUPS)
                for i in self.bits(self.bgd(g)["inode_bitmap"], INODES_PER_GROUP)}

    def inode_pos(self, n):
        g, local = (n - 1) // INODES_PER_GROUP, (n - 1) % INODES_PER_GROUP
        blk = self.bgd(g)["inode_table"] + local // INODES_PER_TABLE
        return blk * BLOCK + (local % INODES_PER_TABLE) * INODE_SIZE

    def inode(self, n):
        mode, size, blocks = struct.unpack_from("<HII", self.data, self.inode_pos(n))
        return {"mode": mode, "size": size, "blocks": blocks,
                "i_block": list(struct.unpack_from("<15I", self.data, self.inode_pos(n) + 10))}

    def entries(self, block_no):
        b, off, out = self.block(block_no), 0, []
        while off < BLOCK:
            ino, rec_len, name_len, ftype = struct.unpack_from("<IHBB", b, off)
            if ino == 0 or rec_len == 0:
                break
            out.append({"off": off, "inode": ino, "rec_len": rec_len, "type": ftype,
                        "name": bytes(b[off + 8:off + 8 + name_len]).decode()})
            off += rec_len
        return out

    def pointer_tree(self, i_block):
        data, meta = [], []

        def walk(ptr, depth):
            if ptr == 0:
                return
            if ptr >= DISK_BLOCKS:
                data.append(ptr)
                return
            if depth == 0:
                data.append(ptr)
                return
            meta.append(ptr)
            for child in struct.unpack_from("<%dI" % POINTERS, self.data, ptr * BLOCK):
                walk(child, depth - 1)

        for p in i_block[:12]:
            walk(p, 0)
        for depth, p in zip((1, 2, 3), i_block[12:15]):
            walk(p, depth)
        return data, meta
