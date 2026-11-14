#!/usr/bin/env python3
"""Extra edge-case corpus beyond the 2000 mutation cases:
- zero / maximal 24-bit dimension fields
- one-sided giants (width huge, height tiny and vice versa)
- random 64 KiB fuzz blobs (no packet structure)
- header-only / packet-only fragments
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_malformed import ogg_packets, ogg_mux, raw_mux, gen_seed_stream

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, "corpus")


def write(idx, cls, packets):
    base = f"edge_{idx:04d}_{cls}"
    with open(os.path.join(OUT, "raw", base + ".raw"), "wb") as f:
        f.write(raw_mux(packets))
    with open(os.path.join(OUT, "ogg", base + ".ogv"), "wb") as f:
        f.write(ogg_mux(packets))


def main():
    random.seed(2024)
    base = ogg_packets(gen_seed_stream(999))
    idx = 0
    idpkt = bytearray(base[0][0])
    # Theora id header layout (24-bit LE fields): frame_w at bytes 14-15 is
    # 16-bit<<4; the pic fields occupy bytes 17..26 region.  We mutate every
    # dimension-related field with extreme values.
    fields = [14, 17, 20, 23]
    extremes = [0, 1, 0xFFFF, 0xFFFFF0, 0xFFFFFF, 0x7FFFF0, 0x1000]
    for off in fields:
        for val in extremes:
            b = bytearray(idpkt)
            b[off] = val & 255
            b[off + 1] = (val >> 8) & 255
            b[off + 2] = (val >> 16) & 255
            write(idx, "dim", [(bytes(b), True)] + base[1:]); idx += 1

    # header only, varying truncation of the 3 header packets
    for keep in range(0, 4):
        write(idx, "hdronly", base[:keep]); idx += 1

    # pure random blobs wrapped as single packets
    for k in range(60):
        n = random.randrange(1, 65536)
        blob = bytes(random.randrange(256) for _ in range(n))
        write(idx, "blob", [(blob, True)]); idx += 1

    # valid headers followed by oversized random data packets
    for k in range(40):
        n = random.choice([1, 2, 255, 256, 4096, 65535, 200000])
        pkt = bytes(random.randrange(256) for _ in range(n))
        write(idx, "bigpkt", base[:3] + [(pkt, False)]); idx += 1

    print(f"generated {idx} edge cases")


if __name__ == "__main__":
    main()
