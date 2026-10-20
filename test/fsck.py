import sys

from ext2fmt import (BLOCK, BLOCKS_PER_GROUP, FS_BLOCKS, GROUPS, INODES_PER_GROUP, ROOT_BLOCK, SIGNATURE,
                     Image, align4)

img = Image(open(sys.argv[1], "rb").read())
results = []


def check(name, ok, detail=""):
    results.append(ok)
    print("%-4s %s%s" % ("OK" if ok else "FAIL", name, "" if ok else "  ->  " + detail))


def is_dir(node):
    return node["mode"] == 2 or node["mode"] & 0x4000 != 0


check("signature di block 0", img.block(0) == SIGNATURE)

live, dirs, tree, problems = {}, set(), [], []
owner = {}
stack = [(1, 1, "/")]
while stack:
    ino, parent, path = stack.pop()
    if ino in live:
        problems.append("inode %d dirujuk dua kali (%s)" % (ino, path))
        continue
    node = img.inode(ino)
    live[ino] = node
    data, meta = img.pointer_tree(node["i_block"])
    outside = [b for b in data + meta if b >= FS_BLOCKS]
    if outside:
        problems.append("%s: inode rusak, %d pointer di luar file system (mis. %d)" % (path, len(outside), outside[0]))
        tree.append("%s (INODE RUSAK)" % path)
        continue
    for b in data + meta:
        if b in owner:
            problems.append("block %d dipakai %s dan %s" % (b, owner[b], path))
        else:
            owner[b] = path
    if node["blocks"] != len(data) + len(meta):
        problems.append("%s i_blocks=%d, seharusnya %d" % (path, node["blocks"], len(data) + len(meta)))
    if not is_dir(node):
        if node["size"] > len(data) * BLOCK or (node["size"] + BLOCK - 1) // BLOCK != len(data):
            problems.append("%s i_size=%d tidak cocok dengan %d block" % (path, node["size"], len(data)))
        tree.append("%s (%d B)" % (path, node["size"]))
        continue
    dirs.add(ino)
    tree.append(path)
    for b in data:
        entries = img.entries(b)
        end = entries[-1]["off"] + entries[-1]["rec_len"] if entries else 0
        if end != BLOCK:
            problems.append("%s: rantai rec_len berakhir di %d, bukan %d" % (path, end, BLOCK))
        for k, e in enumerate(entries):
            if e["off"] % 4 or e["rec_len"] < align4(8 + len(e["name"])):
                problems.append("%s: entry %r rec_len/offset tidak valid" % (path, e["name"]))
            if b == data[0] and k == 0 and (e["name"] != "." or e["inode"] != ino):
                problems.append("%s: entry ke-0 bukan '.' ke dirinya sendiri" % path)
            elif b == data[0] and k == 1 and (e["name"] != ".." or e["inode"] != parent):
                problems.append("%s: entry ke-1 bukan '..' ke parent" % path)
            elif e["name"] not in (".", ".."):
                child = img.inode(e["inode"])
                if is_dir(child) != (e["type"] == 2):
                    problems.append("%s/%s: file_type entry tidak cocok dengan i_mode" % (path, e["name"]))
                stack.append((e["inode"], ino, path.rstrip("/") + "/" + e["name"]))

check("struktur direktori, inode, dan block konsisten", not problems, "\n       ".join([""] + problems))

used_inodes = img.used_inodes()
check("inode bitmap = inode yang bisa dicapai dari root", used_inodes == set(live),
      "terpakai tapi yatim: %s, dipakai tapi bit 0: %s" % (sorted(used_inodes - set(live)),
                                                            sorted(set(live) - used_inodes)))

reserved = set(range(ROOT_BLOCK))
expected_blocks = reserved | set(owner)
used_blocks = img.used_blocks()
check("block bitmap = metadata + block yang dirujuk inode", used_blocks == expected_blocks,
      "bocor: %s, dipakai tapi bit 0: %s" % (sorted(used_blocks - expected_blocks)[:12],
                                             sorted(expected_blocks - used_blocks)[:12]))

sb = img.superblock()
total_free_blocks = total_free_inodes = 0
count_problems = []
for g in range(GROUPS):
    d = img.bgd(g)
    free_b = BLOCKS_PER_GROUP - len(img.bits(d["block_bitmap"], BLOCKS_PER_GROUP))
    free_i = INODES_PER_GROUP - len(img.bits(d["inode_bitmap"], INODES_PER_GROUP))
    total_free_blocks += free_b
    total_free_inodes += free_i
    if (d["free_blocks"], d["free_inodes"]) != (free_b, free_i):
        count_problems.append("bgd[%d] free_blocks=%d (bitmap %d), free_inodes=%d (bitmap %d)"
                              % (g, d["free_blocks"], free_b, d["free_inodes"], free_i))
dirs_g0 = img.bgd(0)["dirs"]
if dirs_g0 != len(dirs):
    count_problems.append("bgd[0] dirs=%d, folder yang ada %d" % (dirs_g0, len(dirs)))
if (sb["free_blocks"], sb["free_inodes"]) != (total_free_blocks, total_free_inodes):
    count_problems.append("superblock free_blocks=%d (bitmap %d), free_inodes=%d (bitmap %d)"
                          % (sb["free_blocks"], total_free_blocks, sb["free_inodes"], total_free_inodes))
check("jumlah free di BGD & superblock cocok dengan bitmap", not count_problems,
      "\n       ".join([""] + count_problems))

print("\nisi file system:")
for line in sorted(tree):
    print("  " + line)
print("\n%d/%d cek lulus" % (sum(results), len(results)))
sys.exit(0 if all(results) else 1)
