#!/usr/bin/env python3
"""Run byte truncations and randomized frame mutations through decode_tpk.

The caller supplies the sanitizer-built decode_tpk binary. A non-zero exit
status from the child, a sanitizer diagnostic, or a timeout is counted as a
crash. Results are written as a TSV file.
"""

import os
import random
import struct
import subprocess
import sys
import tempfile


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


def main():
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: run_mutations.py decode_tpk valid.tpk out.tsv")
    decoder, valid_path, out_path = sys.argv[1:]
    base = read_tpk(valid_path)
    rng = random.Random(4085)
    cases = []
    for frame in range(3, len(base)):
        for n in range(len(base[frame]) + 1):
            p = [bytearray(x) for x in base]
            p[frame] = p[frame][:n]
            cases.append((f"trunc_p{frame}_{n}", p))
    for i in range(2000):
        frame = 3 + rng.randrange(len(base) - 3)
        p = [bytearray(x) for x in base]
        for _ in range(1 + rng.randrange(16)):
            pos = rng.randrange(len(p[frame]))
            p[frame][pos] = rng.randrange(256)
        cases.append((f"rand_{i}", p))
    crashes = 0
    rejected = 0
    accepted = 0
    with tempfile.TemporaryDirectory() as td, open(out_path, "w") as out:
        out.write("case\tstatus\trc\ttimeout\tsanitizer\n")
        for name, packets in cases:
            path = os.path.join(td, "case.tpk")
            write_tpk(path, packets)
            try:
                proc = subprocess.run(
                    [decoder, path], stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, timeout=5,
                    env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1"))
                err = proc.stderr.decode("utf-8", "replace")
                san = "AddressSanitizer" in err or "runtime error" in err
                timed_out = False
                rc = proc.returncode
            except subprocess.TimeoutExpired:
                rc = 124
                san = False
                timed_out = True
            if timed_out or san or rc not in (0, 1):
                crashes += 1
                status = "CRASH"
            elif rc == 1:
                rejected += 1
                status = "rejected"
            else:
                accepted += 1
                status = "accepted"
            out.write(f"{name}\t{status}\t{rc}\t{int(timed_out)}\t{int(san)}\n")
    print(f"cases={len(cases)} rejected={rejected} accepted={accepted} "
          f"crashes={crashes}")
    return 1 if crashes else 0


if __name__ == "__main__":
    sys.exit(main())
