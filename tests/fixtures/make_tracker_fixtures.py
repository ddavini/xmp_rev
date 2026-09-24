#!/usr/bin/env python3
"""Generates the tracker-module fixtures decoder_test.cpp and tags_test.cpp use.

Each module is one 64-row pattern at speed 6 / tempo 125 with a single note
on row 0 of channel 1: middle C (MOD period 428 / XM and S3M C-4) of a
looped 32-sample sample holding exactly 4 sine cycles. Middle C plays a
sample at its base rate - 8287 Hz for a 4-channel "M.K." MOD, 8363 Hz for XM
and S3M - so the tone is base_rate * 4 / 32:
  tone.mod / tone.mdz : 8287 / 8 = ~1035.9 Hz
  tone.xm  / tone.xmz : 8363 / 8 = ~1045.4 Hz
  tone.s3m            : 8363 / 8 = ~1045.4 Hz
At a 48000 Hz render rate the song lasts 64 rows * 6 ticks * 960 samples per
tick (48000 * 5 / (2 * 125)) = 368640 frames.

The compressed variants mirror what's found in the wild. Classic
MODPlug-era .mdz/.xmz/.s3z files are ordinary PKZIP archives:
  tone.mdz       : zip, deflated, one entry "tone.mod"
  tone.xmz       : zip, deflated, a "readme.txt" entry *before* "tone.xm"
                   (the loader must pick the module, not the first entry)
  tone.s3z       : zip, stored (no compression), entry "TONE.S3M"
Some later tools wrote plain gzip under the same extensions instead:
  tone_gzip.mdz  : gzip of tone.mod

Run from the repo root: python3 tests/fixtures/make_tracker_fixtures.py
"""

import gzip
import io
import math
import os
import struct
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))

LOOP = [round(100 * math.sin(2 * math.pi * 4 * i / 32)) for i in range(32)]  # signed 8-bit


def name(s, n):
    return s.encode("ascii").ljust(n, b"\0")[:n]


def s8(values):
    return bytes(v & 0xFF for v in values)


def make_mod():
    out = bytearray(name("MOD Tone Fixture", 20))
    for i in range(31):
        if i == 0:
            # name, length (words), finetune, volume, loop start/length (words)
            out += name("sine", 22) + struct.pack(">HBBHH", len(LOOP) // 2, 0, 64, 0, len(LOOP) // 2)
        else:
            out += name("", 22) + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
    out += bytes([1, 127]) + bytes(128)  # song length, restart, order table (pattern 0)
    out += b"M.K."
    for row in range(64):
        for ch in range(4):
            if row == 0 and ch == 0:
                period, ins = 428, 1
                out += bytes([(ins & 0xF0) | (period >> 8), period & 0xFF, (ins & 0x0F) << 4, 0])
            else:
                out += bytes(4)
    out += s8(LOOP)
    return bytes(out)


def make_xm():
    num_channels = 2
    out = bytearray(b"Extended Module: " + name("XM Tone Fixture", 20) + b"\x1a" + name("xmad fixture", 20))
    out += struct.pack("<H", 0x0104)
    # header size (from offset 60), song length, restart, channels, patterns,
    # instruments, flags (1 = linear frequency table), speed, tempo
    out += struct.pack("<IHHHHHHHH", 276, 1, 0, num_channels, 1, 1, 1, 6, 125)
    out += bytes(256)  # order table (pattern 0)
    # Pattern, packed: 0x80 alone = empty note; 0x83 = note + instrument follow.
    packed = bytearray([0x83, 49, 1]) + bytes([0x80]) * (64 * num_channels - 1)
    out += struct.pack("<IBHH", 9, 0, 64, len(packed)) + packed
    # Instrument: 263-byte header, one sample, every key mapped to it, no envelopes.
    inst = bytearray(263)
    struct.pack_into("<I", inst, 0, 263)
    inst[4:26] = name("sine", 22)
    struct.pack_into("<H", inst, 27, 1)  # number of samples
    struct.pack_into("<I", inst, 29, 40)  # sample header size
    out += inst
    # Sample header: length, loop start, loop length, volume, finetune,
    # type (1 = forward loop, 8-bit), panning, relative note, reserved, name.
    out += struct.pack("<IIIBbBBbB", len(LOOP), 0, len(LOOP), 64, 0, 1, 128, 0, 0) + name("sine", 22)
    prev = 0
    for v in LOOP:  # XM stores sample data as deltas
        out.append((v - prev) & 0xFF)
        prev = v
    return bytes(out)


def make_s3m():
    # Layout (all offsets paragraph-aligned where S3M needs them):
    #   0x000 header (96) + orders (2) + instrument/pattern parapointers (4)
    #   0x070 instrument (80)
    #   0x0C0 pattern
    #   0x100 sample data
    ins_off, pat_off, smp_off = 0x70, 0xC0, 0x100
    out = bytearray(name("S3M Tone Fixture", 28) + b"\x1a" + bytes([16]) + bytes(2))
    # orders, instruments, patterns, flags, created-with, format (2 = unsigned samples)
    out += struct.pack("<HHHHHH", 2, 1, 1, 0, 0x1320, 2)
    out += b"SCRM"
    out += bytes([64, 6, 125, 0xB0, 0, 0]) + bytes(8) + struct.pack("<H", 0)
    out += bytes([0, 8] + [255] * 30)  # channel 1 left, channel 2 right, rest unused
    out += bytes([0, 255])  # order list: pattern 0, end marker
    out += struct.pack("<HH", ins_off >> 4, pat_off >> 4)
    out = out.ljust(ins_off, b"\0")
    inst = bytearray(80)
    inst[0] = 1  # sample instrument
    inst[1:13] = name("sine.raw", 12)
    inst[13] = smp_off >> 20
    struct.pack_into("<H", inst, 14, (smp_off >> 4) & 0xFFFF)
    struct.pack_into("<IIIBBBBI", inst, 16, len(LOOP), 0, len(LOOP), 64, 0, 0, 1, 8363)
    inst[48:76] = name("sine", 28)
    inst[76:80] = b"SCRS"
    out += inst
    out = out.ljust(pat_off, b"\0")
    rows = bytearray([0x20, 0x40, 1, 0]) + bytes(63)  # row 0: ch 0 note C-4 ins 1; 63 empty rows
    out += struct.pack("<H", len(rows) + 2) + rows
    out = out.ljust(smp_off, b"\0")
    out += bytes((v + 128) & 0xFF for v in LOOP)  # unsigned 8-bit
    return bytes(out)


def make_zip(entries, method):
    """entries: [(name, bytes)]. Fixed timestamps keep the output byte-identical across runs."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for entry_name, data in entries:
            info = zipfile.ZipInfo(entry_name, date_time=(1997, 1, 1, 0, 0, 0))
            info.compress_type = method
            z.writestr(info, data)
    return buf.getvalue()


def write(filename, data):
    with open(os.path.join(HERE, filename), "wb") as f:
        f.write(data)


def main():
    mod, xm, s3m = make_mod(), make_xm(), make_s3m()
    write("tone.mod", mod)
    write("tone.xm", xm)
    write("tone.s3m", s3m)
    write("tone.mdz", make_zip([("tone.mod", mod)], zipfile.ZIP_DEFLATED))
    readme = b"Tone fixture for X.MaD Player Revival's decoder tests.\r\n"
    write("tone.xmz", make_zip([("readme.txt", readme), ("tone.xm", xm)], zipfile.ZIP_DEFLATED))
    write("tone.s3z", make_zip([("TONE.S3M", s3m)], zipfile.ZIP_STORED))
    # mtime=0 keeps the gzip output byte-identical across runs.
    write("tone_gzip.mdz", gzip.compress(mod, mtime=0))


if __name__ == "__main__":
    main()
