#!/usr/bin/env python3
"""Generate the malformed TPK1 samples from the legal baseline.

TPK1 is a little-endian length-prefixed packet sequence:
  magic[4] = "TPK1", u32 packet_count,
  repeated: u32 packet_size, packet bytes.

Keeping the samples as packets avoids Ogg CRC/page-lacing noise: every test
case changes only the Theora payload that the decoder consumes.
"""

import os
import struct
import sys


def read_tpk(path):
    data = open(path, "rb").read()
    assert data[:4] == b"TPK1"
    n = struct.unpack_from("<I", data, 4)[0]
    off = 8
    packets = []
    for _ in range(n):
        size = struct.unpack_from("<I", data, off)[0]
        off += 4
        packets.append(bytearray(data[off:off + size]))
        off += size
    return packets


def write_tpk(path, packets):
    with open(path, "wb") as f:
        f.write(b"TPK1")
        f.write(struct.pack("<I", len(packets)))
        for packet in packets:
            f.write(struct.pack("<I", len(packet)))
            f.write(packet)


def get_bits(packet, pos, count):
    value = 0
    for _ in range(count):
        value = (value << 1) | (
            (packet[pos >> 3] >> (7 - (pos & 7))) & 1)
        pos += 1
    return value


def put_bits(packet, pos, count, value):
    for i in range(count):
        bit = (value >> (count - 1 - i)) & 1
        mask = 1 << (7 - ((pos + i) & 7))
        if bit:
            packet[(pos + i) >> 3] |= mask
        else:
            packet[(pos + i) >> 3] &= ~mask & 0xFF


def ilog(value):
    return value.bit_length()


def quant_layout(packet):
    """Return (nbms_bitpos, nbms, first_bmi_bitpos) for the setup header."""
    pos = 0

    def read(count):
        nonlocal pos
        value = get_bits(packet, pos, count)
        pos += count
        return value

    assert read(8) == 0x82
    assert bytes(read(8) for _ in range(6)) == b"theora"
    nbits = read(3)
    read(64 * nbits)
    nbits = read(4) + 1
    read(64 * nbits)
    nbits = read(4) + 1
    read(64 * nbits)
    nbms_bitpos = pos
    nbms = read(9) + 1
    read(64 * nbms * 8)
    first_bmi_bitpos = pos
    return nbms_bitpos, nbms, first_bmi_bitpos


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    sample_dir = os.path.join(here, "samples")
    base = read_tpk(os.path.join(sample_dir, "valid.tpk"))
    nbms_bitpos, nbms, bmi_bitpos = quant_layout(base[2])

    # A key frame cut in the middle of its DCT token data.
    write_tpk(os.path.join(sample_dir, "truncated_frame.tpk"),
              base[:3] + [base[3][:100]])

    # NBMS must not exceed 384. The field is 9 bits, so 511 is representable
    # but illegal.
    p = [bytearray(x) for x in base[:3]]
    put_bits(p[2], nbms_bitpos, 9, 511)
    write_tpk(os.path.join(sample_dir, "bad_quant_nbms.tpk"), p)

    # The first quant-range base-matrix index must be < NBMS. This sample has
    # NBMS=12 and four index bits, so 15 is illegal.
    p = [bytearray(x) for x in base[:3]]
    nbits = ilog(nbms - 1)
    illegal_bmi = (1 << nbits) - 1
    assert illegal_bmi >= nbms
    put_bits(p[2], bmi_bitpos, nbits, illegal_bmi)
    write_tpk(os.path.join(sample_dir, "bad_quant_bmi.tpk"), p)

    # A valid setup header truncated before the quant-range descriptors.
    write_tpk(os.path.join(sample_dir, "bad_quant_truncated.tpk"),
              base[:2] + [base[2][:200]])

    # Decode a key frame first, then cut an inter-frame packet inside the
    # mode/motion-vector region.
    write_tpk(os.path.join(sample_dir, "bad_mv_truncated.tpk"),
              base[:4] + [base[4][:16]])

    print(f"NBMS={nbms} nbms_bit={nbms_bitpos} first_bmi_bit={bmi_bitpos}")
    print("samples written to", sample_dir)


if __name__ == "__main__":
    main()
