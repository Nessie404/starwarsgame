#!/usr/bin/env python3
"""
Pad a .dol out to the length a loader actually reads, and nothing more.

devkitPPC's elf2dol ends the file at the last section's real size. Dolphin
reads each section in 32-byte units, so when that size is not a multiple of
32 the loader wants up to 31 bytes the file does not contain, and
DolReader::Initialize refuses the whole executable — which the user sees as
the entirely unhelpful "Failed to init core", before a single instruction
runs.

Whether it bites is pure luck: it depends on the last section's size modulo
32, so it comes and goes as unrelated code is added. WiiKart shipped it in
v0.1 (12 bytes short) and v1.0 through v1.2.1 (28 bytes short), and it
turned up again in a 2026 diagnostic build (4 bytes short). This runs in
the Makefile after every build so it cannot ship again.

Idempotent: a file that is already long enough is left untouched.

Usage: pad_dol.py wiikart.dol
"""

import os
import struct
import sys

ALIGN = 32
N_TEXT, N_DATA = 7, 11
HEADER_BYTES = 0x100


def align_up(value, alignment=ALIGN):
    return (value + alignment - 1) // alignment * alignment


def bytes_the_loader_reads(blob):
    """The smallest file length that satisfies every section's promise."""
    text_off = struct.unpack('>7I', blob[0x00:0x1C])
    data_off = struct.unpack('>11I', blob[0x1C:0x48])
    text_size = struct.unpack('>7I', blob[0x90:0xAC])
    data_size = struct.unpack('>11I', blob[0xAC:0xD8])

    need = HEADER_BYTES
    for off, size in list(zip(text_off, text_size))[:N_TEXT] + \
                     list(zip(data_off, data_size))[:N_DATA]:
        if size:
            need = max(need, off + align_up(size))
    return need


def pad(path):
    with open(path, 'rb') as handle:
        blob = handle.read()

    if len(blob) < HEADER_BYTES:
        sys.stderr.write('%s: %d bytes, too small to be a DOL\n'
                         % (path, len(blob)))
        return 1

    need = bytes_the_loader_reads(blob)
    have = len(blob)

    if have >= need:
        print('%s: %d bytes, loader reads %d — no padding needed'
              % (path, have, need))
        return 0

    with open(path, 'ab') as handle:
        handle.write(b'\0' * (need - have))

    print('%s: %d bytes but the loader reads %d — appended %d zero bytes'
          % (path, have, need, need - have))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip())
        return 2
    return max(pad(p) for p in argv[1:])


if __name__ == '__main__':
    sys.exit(main(sys.argv))
