#!/usr/bin/env python3
# Converts OoT Online "zzplayas" Link-rigged models (.zobj inside a ModLoader64 .pak) into a Ship of Harkinian
# mod archive (.o2r) the Companions mod can draw a girl with. Local tool; the models stay private.
#
#   zobj2o2r.py out.o2r  <resource base> <model.zobj>  [<resource base> <model.zobj> ...]
#
# Each model becomes, under its base path:
#   skel            Blob: the model's own skeleton (21 limbs, Link's hierarchy) and which limbs draw
#   dl_XXXXXX       DisplayList (F3DEX2) for every display list the limbs use, segment-6 references made OTR ones
#   vtx0, vtx8      Vertex: the whole file read as vertices (from byte 0 and from byte 8, for either alignment)
#   tex_XXXXXX      Texture: every texture and palette the lists load
#   eye_0..7        Texture: the eye textures (Link's layout, 64x32 CI8 at 0x0000 + 0x800 n)
#   mouth_0..3      Texture: the mouth textures (32x32 CI8 at 0x4000 + 0x400 n)
import struct
import sys
import zipfile


def crc64(s):
    poly = 0x42F0E1EBA9EA3693
    crc = 0xFFFFFFFFFFFFFFFF
    for ch in s.encode():
        crc ^= ch << 56
        for _ in range(8):
            crc = ((crc << 1) ^ poly) if (crc & (1 << 63)) else (crc << 1)
            crc &= 0xFFFFFFFFFFFFFFFF
    return crc


TYPE_DL, TYPE_TEX, TYPE_VTX, TYPE_BLOB = 0x4F444C54, 0x4F544558, 0x4F565458, 0x4F424C42


def header(rtype, version=0):
    h = bytearray(64)
    h[0] = 1  # big-endian
    h[1] = 0
    struct.pack_into('>IIQ', h, 4, rtype, version, 0)
    return bytes(h)


def find_skeleton(d):
    # A flex skeleton header: segment-6 pointer to the limb table, 21 limbs, 18 with display lists
    best = None
    i = 0
    while True:
        i = d.find(b'\x15\x00\x00\x00\x12', i + 1)
        if i < 0:
            break
        if i >= 4 and d[i - 4] == 6:
            best = i - 4
    if best is None:
        raise SystemExit('no Link skeleton found')
    return struct.unpack('>I', d[best:best + 4])[0] & 0xFFFFFF


BPP = {0: 0.5, 1: 1, 2: 2, 3: 4}  # bytes per texel by size


def tex_type(fmt, siz):
    if fmt == 0:  # RGBA
        return 1 if siz == 3 else 2
    if fmt == 2:  # CI
        return 3 if siz == 0 else 4
    if fmt == 3:  # IA
        return {0: 7, 1: 8, 2: 9}.get(siz, 8)
    if fmt == 4:  # I
        return 5 if siz == 0 else 6
    return 2


def convert(zf, base, path):
    d = open(path, 'rb').read()
    lt = find_skeleton(d)
    limbs = []
    for k in range(21):
        lp = struct.unpack('>I', d[lt + 4 * k:lt + 4 * k + 4])[0] & 0xFFFFFF
        x, y, z, ch, sib, dl = struct.unpack('>hhhBBI', d[lp:lp + 12])
        limbs.append((x, y, z, ch, sib, dl & 0xFFFFFF if (dl >> 24) == 6 else 0))

    dls = {}      # address -> converted bytes
    texs = {}     # address -> [fmt, siz, width, bytes]
    pending = [l[5] for l in limbs if l[5]]
    while pending:
        a = pending.pop()
        if a in dls:
            continue
        out = bytearray()
        p = a
        cur_tex = None
        while True:
            w0, w1 = struct.unpack('>II', d[p:p + 8])
            op = w0 >> 24
            seg = w1 >> 24
            if op == 0x01 and seg == 6:
                off = w1 & 0xFFFFFF
                vbase = 'vtx8' if (off % 16) == 8 else 'vtx0'
                rel = off - (8 if vbase == 'vtx8' else 0)
                out += struct.pack('>II', (0x32 << 24) | (w0 & 0xFFFFFF), rel)
                h = crc64(base + '/' + vbase)
                out += struct.pack('>II', h >> 32, h & 0xFFFFFFFF)
            elif op == 0xFD and seg == 6:
                off = w1 & 0xFFFFFF
                fmt, siz, width = (w0 >> 21) & 7, (w0 >> 19) & 3, (w0 & 0xFFF) + 1
                t = texs.setdefault(off, [fmt, siz, width, 0])
                cur_tex = (off, siz, width)
                out += struct.pack('>II', (0x20 << 24) | (w0 & 0xFFFFFF), 0)
                h = crc64(base + '/tex_%06X' % off)
                out += struct.pack('>II', h >> 32, h & 0xFFFFFFFF)
            elif op == 0xDE and seg == 6:
                off = w1 & 0xFFFFFF
                pending.append(off)
                out += struct.pack('>II', (0x31 << 24) | (w0 & 0xFFFFFF), 0)
                h = crc64(base + '/dl_%06X' % off)
                out += struct.pack('>II', h >> 32, h & 0xFFFFFFFF)
                if (w0 >> 16) & 0xFF == 1:
                    out += struct.pack('>II', 0xDF000000, 0)  # (a branch ends the list)
                    break
            elif op in (0xDA, 0xDC, 0xDB, 0x03) and seg == 6:
                raise SystemExit('unsupported segment-6 command %08X %08X at %X in %s' % (w0, w1, p, path))
            else:
                if cur_tex is not None:
                    off, tsiz, width = cur_tex
                    need = 0
                    if op == 0xF3:  # load block
                        lrs = (w1 >> 12) & 0xFFF
                        need = int((lrs + 1) * BPP[tsiz] + 0.999)
                    elif op == 0xF0:  # load tlut
                        need = (((w1 >> 14) & 0x3FF) + 1) * 2
                    elif op == 0xF4:  # load tile
                        lrt = w1 & 0xFFF
                        need = int(width * ((lrt >> 2) + 1) * BPP[tsiz] + 0.999)
                    if need:
                        texs[off][3] = max(texs[off][3], need)
                out += struct.pack('>II', w0, w1)
                if op == 0xDF:
                    break
            p += 8
        dls[a] = out

    for a, body in dls.items():
        data = header(TYPE_DL) + bytes([4]) + bytes(7) + bytes(body)  # ucode f3dex2, padded to 8
        zf.writestr(base + '/dl_%06X' % a, data)

    for start, name in ((0, 'vtx0'), (8, 'vtx8')):
        raw = d[start:]
        n = len(raw) // 16
        zf.writestr(base + '/' + name, header(TYPE_VTX) + struct.pack('>I', n) + raw[:n * 16])

    def texture(name, fmt, siz, width, data):
        bpp = BPP[siz]
        height = max(1, int(len(data) / max(1, width * bpp)))
        zf.writestr(base + '/' + name,
                    header(TYPE_TEX) + struct.pack('>IIII', tex_type(fmt, siz), width, height, len(data)) + data)

    for off, (fmt, siz, width, need) in texs.items():
        if need <= 0:
            need = 2048
        data = d[off:off + need]
        texture('tex_%06X' % off, fmt, siz, width, data)

    for n in range(8):
        texture('eye_%d' % n, 2, 1, 64, d[0x800 * n:0x800 * n + 0x800])
    for n in range(4):
        texture('mouth_%d' % n, 2, 1, 32, d[0x4000 + 0x400 * n:0x4000 + 0x400 * n + 0x400])

    # The skeleton: "CMDL", limb count, display-list count, then per limb x, y, z, child, sibling, display list
    # address (0: none)
    blob = bytearray(b'CMDL') + bytes([21, sum(1 for l in limbs if l[5])]) + bytes(2)
    for x, y, z, ch, sib, dl in limbs:
        blob += struct.pack('>hhhBBI', x, y, z, ch, sib, dl)
    zf.writestr(base + '/skel', header(TYPE_BLOB) + struct.pack('>I', len(blob)) + bytes(blob))
    print(base, path.split('/')[-1], len(dls), 'lists', len(texs), 'textures')


if __name__ == '__main__':
    out = sys.argv[1]
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as zf:
        args = sys.argv[2:]
        for i in range(0, len(args), 2):
            convert(zf, args[i], args[i + 1])
