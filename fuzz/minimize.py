#!/usr/bin/env python3
"""Delta-debugging minimizer.
Given a raw corpus case that triggers a specific decoder rejection, shrink it
to the smallest byte sequence that still triggers the same rejection.
Usage: minimize.py <input.raw> <want_error_substr> <out.raw>
"""
import os, subprocess, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
DIAG = os.path.join(ROOT, "bin", "fuzz_diag")


def packets_of(raw):
    pk, p, end = [], 0, len(raw)
    while p + 4 <= end:
        n = int.from_bytes(raw[p:p + 4], "little")
        p += 4
        if n > end - p:
            break
        pk.append(raw[p:p + n])
        p += n
    return pk


def to_raw(pk):
    out = bytearray()
    for d in pk:
        out += len(d).to_bytes(4, "little") + d
    return bytes(out)


def triggers(raw, want):
    path = "/tmp/min_try.raw"
    with open(path, "wb") as f:
        f.write(raw)
    r = subprocess.run([DIAG, "raw", path], capture_output=True, text=True, timeout=10)
    return want in r.stdout


def main():
    src, want, out = sys.argv[1], sys.argv[2], sys.argv[3]
    raw = open(src, "rb").read()
    assert triggers(raw, want), "seed does not trigger target"
    pk = packets_of(raw)

    # phase 1: drop whole packets while retaining the trigger
    changed = True
    while changed:
        changed = False
        for i in range(len(pk)):
            trial = pk[:i] + pk[i + 1:]
            if trial and triggers(to_raw(trial), want):
                pk = trial
                changed = True
                break

    # phase 2: byte-level shrinking inside each packet (tail truncation first,
    # then individual byte removal)
    for pi in list(range(len(pk))):
        data = bytearray(pk[pi])
        # tail truncation
        lo, hi = 0, len(data)
        while lo < hi:
            mid = (lo + hi + 1) // 2
            trial = pk[:pi] + [bytes(data[:mid])] + pk[pi + 1:]
            if triggers(to_raw(trial), want):
                lo = mid
            else:
                hi = mid - 1
        data = data[:lo] if lo else data
        # individual byte removal
        i = 0
        while i < len(data):
            trial_data = bytes(data[:i] + data[i + 1:])
            trial = pk[:pi] + [trial_data] + pk[pi + 1:]
            if triggers(to_raw(trial), want):
                data = bytearray(trial_data)
            else:
                i += 1
        pk[pi] = bytes(data)

    final = to_raw(pk)
    with open(out, "wb") as f:
        f.write(final)
    print(f"minimized {len(raw)} -> {len(final)} bytes across {len(pk)} packets")
    print("packet sizes:", [len(p) for p in pk])
    r = subprocess.run([DIAG, "raw", out], capture_output=True, text=True)
    print("verification:", r.stdout.strip())


if __name__ == "__main__":
    main()
