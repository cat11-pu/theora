#!/usr/bin/env python3
"""Generate 2000 malformed Theora streams.
Two containers per case:
  raw  -> length-prefixed packets for fuzz_raw
  ogg  -> real Ogg muxing for fuzz_ogg
Four mutation classes:
  trunc   : truncated at every interesting offset
  shift   : byte insertion/deletion/bit-shift (misalignment)
  quant   : illegal quant tables (bad qi, bad values, truncated table)
  size    : oversized / zero / inconsistent frame dimensions
Plus a fifth "soup" class of deterministic random mutations to reach 2000.
"""
import os, struct, subprocess, random, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(ROOT, "bin", "gen_legal")
OUT = os.path.join(ROOT, "corpus")
os.makedirs(os.path.join(OUT, "raw"), exist_ok=True)
os.makedirs(os.path.join(OUT, "ogg"), exist_ok=True)


def gen_seed_stream(seed, w=96, h=64, frames=6, pf=0):
    path = f"/tmp/seed_{seed}.ogv"
    subprocess.run([GEN, path, str(w), str(h), "25", "1", "32",
                    str(frames), "64", str(pf), str(seed)], check=True)
    with open(path, "rb") as f:
        return f.read()


# ---- minimal Ogg demux for extracting packets from the seed ---------------
def ogg_packets(blob):
    pages = []
    i = 0
    while i + 27 <= len(blob):
        if blob[i:i+4] != b"OggS":
            i += 1
            continue
        nseg = blob[i+26]
        hdr = i + 27
        segtab = blob[hdr:hdr+nseg]
        body_start = hdr + nseg
        body_len = sum(segtab)
        body = blob[body_start:body_start+body_len]
        bos = blob[i+5] & 2
        eos = blob[i+5] & 4
        gp = struct.unpack("<q", blob[i+6:i+14])[0]
        pages.append((bos, eos, gp, segtab, body))
        i = body_start + body_len
    # reconstruct packets
    packets, cur, first_bos = [], bytearray(), True
    for bos, eos, gp, segtab, body in pages:
        off = 0
        for j, l in enumerate(segtab):
            cur += body[off:off+l]
            off += l
            if l < 255:
                packets.append((bytes(cur), first_bos))
                cur = bytearray()
                first_bos = False
    return packets


# ---- minimal Ogg muxer for mutated packets --------------------------------
def ogg_mux(packets, serial=0x42424242):
    out = bytearray()
    seq = 0
    granule = 0
    data = [(p[0] if isinstance(p, tuple) else p) for p in packets]
    pi = 0
    off = 0
    first_page = True
    while pi < len(data):
        segtab, body = [], bytearray()
        bos = 2 if first_page else 0
        eos = 0
        while pi < len(data) and len(segtab) < 255:
            pkt = data[pi]
            remaining = len(pkt) - off
            if remaining == 0:
                pi += 1; off = 0; continue
            # lacing: 255 means continuation; final lacing value < 255 ends
            # the packet (zero-length packet gets a single 0).
            while remaining > 0 and len(segtab) < 255:
                take = min(255, remaining)
                segtab.append(take)
                body += pkt[off:off + take]
                off += take
                remaining -= take
                if take < 255:
                    pi += 1; off = 0
                    break
            if len(segtab) >= 255:
                break
        if pi >= len(data) and off == 0:
            eos = 4
        hdr = bytearray(b"OggS")
        hdr += bytes([0, bos | eos])
        hdr += struct.pack("<q", granule)
        hdr += struct.pack("<I", serial)
        hdr += struct.pack("<I", seq)
        hdr += struct.pack("<I", 0)  # crc placeholder
        hdr += bytes([len(segtab)])
        hdr += bytes(segtab)
        page = bytes(hdr) + bytes(body)
        crc = ogg_crc(page)
        page = page[:22] + struct.pack("<I", crc) + page[26:]
        out += page
        seq += 1
        first_page = False
        granule = seq
    return bytes(out)


CRC_POLY = 0x04c11db7
_CRC_TABLE = []
for _i in range(256):
    _r = _i << 24
    for _ in range(8):
        _r = ((_r << 1) ^ CRC_POLY) & 0xFFFFFFFF if _r & 0x80000000 else (_r << 1) & 0xFFFFFFFF
    _CRC_TABLE.append(_r)


def ogg_crc(page):
    data = page[:22] + b"\x00\x00\x00\x00" + page[26:]
    c = 0
    for b in data:
        c = ((c << 8) ^ _CRC_TABLE[((c >> 24) & 0xFF) ^ b]) & 0xFFFFFFFF
    return c


def raw_mux(packets):
    out = bytearray()
    for p in packets:
        d = p[0] if isinstance(p, tuple) else p
        out += struct.pack("<I", len(d)) + d
    return bytes(out)


def write_case(idx, cls, packets):
    base = f"case_{idx:04d}_{cls}"
    with open(os.path.join(OUT, "raw", base + ".raw"), "wb") as f:
        f.write(raw_mux(packets))
    with open(os.path.join(OUT, "ogg", base + ".ogv"), "wb") as f:
        f.write(ogg_mux(packets))


def main():
    random.seed(1337)
    seeds = {pf: ogg_packets(gen_seed_stream(100 + pf, pf=pf)) for pf in range(3)}
    base = seeds[0]
    idx = 0

    # class 1: truncation — cut every packet and the stream at many offsets
    for cut in range(0, min(260, sum(len(p[0]) for p in base))):
        total = 0
        pk = []
        for p in base:
            if total + len(p[0]) <= cut:
                pk.append(p); total += len(p[0])
            else:
                keep = max(0, cut - total)
                if keep:
                    pk.append((p[0][:keep], p[1]))
                break
        write_case(idx, "trunc", pk); idx += 1

    # class 2: misalignment — insert/delete/shift bytes at varied offsets
    blob = b"".join(p[0] for p in base)
    for off in range(0, min(400, len(blob)), 2):
        for mode in range(4):
            b = bytearray(blob)
            if mode == 0:       # insert garbage byte
                b.insert(off, random.randrange(256))
            elif mode == 1 and off < len(b):  # delete byte
                del b[off]
            elif mode == 2 and off < len(b):  # overwrite
                b[off] ^= 0xFF
            else:               # bit shift right by 1 from offset
                if off < len(b):
                    b[off] = (b[off] >> 1) | (b[off] << 7 & 0xFF)
            # re-split using original packet boundaries
            pk, pos = [], 0
            for p in base:
                end = min(pos + len(p[0]), len(b))
                if pos >= len(b):
                    break
                pk.append((bytes(b[pos:end]), p[1])); pos = end
            write_case(idx, "shift", pk); idx += 1

    # class 3: illegal quant tables
    # packet 2 is the setup header containing Huffman + quant tables.
    setup = bytearray(base[2][0])
    # 3a: flip quant table count / reserved fields near header start
    for tweak in range(120):
        b = bytearray(setup)
        rnd = random.Random(9000 + tweak)
        # Theora id header is packet 0; setup starts with 0x80 'theora'
        pos = rnd.randrange(9, max(10, len(b) - 1))
        b[pos] = rnd.choice([0x00, 0xFF, 0x7F, 0x80, 0x01])
        pk = base[:2] + [(bytes(b), False)] + base[3:]
        write_case(idx, "quant", pk); idx += 1
    # 3b: truncate setup header progressively
    for cut in range(9, len(setup), max(1, len(setup) // 120)):
        pk = base[:2] + [(bytes(setup[:cut]), False)] + base[3:]
        write_case(idx, "quant", pk); idx += 1

    # class 4: oversized / illegal dimensions (id header is packet 0).
    # Fields are read MSB-first by oc_pack_read after the 7-byte marker:
    #   bytes 10-11 FMBW (16-bit BE, decoder does val<<4 -> up to 1,048,560)
    #   bytes 12-13 FMBH (16-bit BE)
    #   bytes 14-16 PICW (24-bit BE), 17-19 PICH (24-bit BE), 20/21 PICX/PICY
    # The mutator writes raw octets; the big16/big24 values place the most
    # significant octet first so the resulting field matches the intent.
    idpkt = bytearray(base[0][0])
    field16 = [10, 12]
    field24 = [14, 17, 20, 23]
    big16 = [0x0000, 0x0001, 0x0FFF, 0x3FFF, 0x4000, 0x7FFF, 0xFFFF]
    big24 = [0x000000, 0x000001, 0x00FFFF, 0x3FFFE0, 0x7FFFF0, 0xFFFFFF]
    for off in field16:
        for val in big16:
            b = bytearray(idpkt)
            b[off] = (val >> 8) & 0xFF
            b[off + 1] = val & 0xFF
            pk = [(bytes(b), True)] + base[1:]
            write_case(idx, "size", pk); idx += 1
    for off in field24:
        for val in big24:
            b = bytearray(idpkt)
            b[off] = (val >> 16) & 0xFF
            b[off + 1] = (val >> 8) & 0xFF
            b[off + 2] = val & 0xFF
            pk = [(bytes(b), True)] + base[1:]
            write_case(idx, "size", pk); idx += 1

    # class 5: random mutation soup until we hit 2000
    while idx < 2000:
        src = seeds[idx % 3]
        rnd = random.Random(50000 + idx)
        pk = [(bytearray(p[0]), p[1]) for p in src]
        nmut = rnd.randrange(1, 6)
        for _ in range(nmut):
            k = rnd.randrange(len(pk))
            d = pk[k][0]
            if not d:
                continue
            pos = rnd.randrange(len(d))
            kind = rnd.randrange(4)
            if kind == 0:
                d[pos] = rnd.randrange(256)
            elif kind == 1:
                del d[pos]
            elif kind == 2:
                d.insert(pos, rnd.randrange(256))
            else:
                d[pos:pos+1] = os.urandom(1)
        # occasionally drop or duplicate a packet
        if rnd.random() < 0.15 and len(pk) > 3:
            del pk[rnd.randrange(len(pk))]
        if rnd.random() < 0.1:
            k = rnd.randrange(len(pk))
            pk.insert(k, (bytearray(pk[k][0]), False))
        pk = [(bytes(d), bos) for d, bos in pk]
        write_case(idx, "soup", pk); idx += 1

    print(f"generated {idx} cases")


if __name__ == "__main__":
    main()
