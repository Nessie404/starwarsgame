#!/usr/bin/env python3
"""
Make a .dol agree with itself, so a loader will open it.

devkitPPC's elf2dol leaves two related problems behind, and both come down
to the same thing: a loader handles sections in 32-byte units, and elf2dol
does not.

  1. Section sizes are the ELF's exact byte counts, so they are usually not
     multiples of 32. A loader that reads in 32-byte units then wants more
     bytes than the header describes.
  2. The file ends at the last section's exact size, so those extra bytes
     are not there to read.

Either one makes Dolphin's DolReader::Initialize return false and refuse
the whole executable, which reaches the user as the entirely unhelpful
"Failed to init core" — reported before the emulated CPU runs a single
instruction, so nothing the program itself does can report on it.

This rounds every nonzero section's advertised size up to 32 bytes and then
extends the file to match. Both are safe:

  * The bytes a rounded-up section now covers are the padding elf2dol
    already placed between sections, and the memory it now covers is the
    gap before the next section's load address — which is 32-byte aligned,
    so the rounded section reaches it exactly and never past it.
  * For the last section the rounding can reach up to 31 bytes into the
    start of BSS. That is harmless: crt0 zeroes BSS before main runs.

WiiKart's history with this: v0.1 was 12 bytes short and v1.0 through
v1.2.1 were 28 bytes short. Every build from v1.5.0 on had an unaligned
text section — and v1.4.0, the last build known to boot on the reporter's
Dolphin, is the last one where every section size happened to be a
multiple of 32 already.

Idempotent: a file that is already consistent is left untouched.

Usage: pad_dol.py wiikart.dol
"""

import struct
import sys

ALIGN = 32
N_TEXT, N_DATA = 7, 11
HEADER_BYTES = 0x100

OFF_TEXT_OFFSET, OFF_DATA_OFFSET = 0x00, 0x1C
OFF_TEXT_SIZE, OFF_DATA_SIZE = 0x90, 0xAC


def align_up(value, alignment=ALIGN):
    return (value + alignment - 1) // alignment * alignment


def sections(blob):
    """(label, offset-field position, size-field position) per section."""
    for i in range(N_TEXT):
        yield ('text%d' % i, OFF_TEXT_OFFSET + 4 * i, OFF_TEXT_SIZE + 4 * i)
    for i in range(N_DATA):
        yield ('data%d' % i, OFF_DATA_OFFSET + 4 * i, OFF_DATA_SIZE + 4 * i)


def fix(path):
    with open(path, 'rb') as handle:
        blob = bytearray(handle.read())

    if len(blob) < HEADER_BYTES:
        sys.stderr.write('%s: %d bytes, too small to be a DOL\n'
                         % (path, len(blob)))
        return 1

    rounded = []
    need = HEADER_BYTES

    for label, off_pos, size_pos in sections(blob):
        offset = struct.unpack_from('>I', blob, off_pos)[0]
        size = struct.unpack_from('>I', blob, size_pos)[0]
        if not size:
            continue
        if size % ALIGN:
            struct.pack_into('>I', blob, size_pos, align_up(size))
            rounded.append('%s 0x%x->0x%x' % (label, size, align_up(size)))
        need = max(need, offset + align_up(size))

    appended = 0
    if len(blob) < need:
        appended = need - len(blob)
        blob.extend(b'\0' * appended)

    if not rounded and not appended:
        print('%s: %d bytes, every section already 32-byte clean'
              % (path, len(blob)))
        return 0

    with open(path, 'wb') as handle:
        handle.write(bytes(blob))

    if rounded:
        print('%s: rounded section sizes up to %d bytes: %s'
              % (path, ALIGN, ', '.join(rounded)))
    if appended:
        print('%s: appended %d zero bytes so the file reaches %d'
              % (path, appended, need))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip())
        return 2
    return max(fix(p) for p in argv[1:])


if __name__ == '__main__':
    sys.exit(main(sys.argv))
