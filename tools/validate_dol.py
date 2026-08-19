#!/usr/bin/env python3
"""
Check a .dol the way Dolphin's loader does, and fail the build if it would
be rejected.

This exists because of a boot failure that took a long time to pin down:
"Failed to init core" is all Dolphin says, and the only way to tell a
malformed executable from an emulator problem is to check the file against
what the loader actually requires. Run it on every build so a bad DOL
never reaches a release again.

What it checks, mirroring Dolphin's DolReader:

  * the header fits and the file is big enough to be a DOL at all
  * every section with a nonzero size has all of its bytes present in the
    file, counting the 32-byte rounding the loader applies when it reads
    (this is what several early WiiKart releases got wrong: they promised
    up to 28 bytes that the file did not contain)
  * loaded sections and BSS land inside Wii memory and do not overlap
    each other
  * the entry point is inside a section that gets loaded
  * a write to HID4 appears in the text, since that is how Dolphin
    decides a DOL is Wii software rather than GameCube software

Usage: validate_dol.py wiikart.dol [--quiet]
"""

import struct
import sys

ALIGN = 32                      # Dolphin reads sections in 32-byte units
N_TEXT, N_DATA = 7, 11
HEADER_BYTES = 0x100

MEM1_START, MEM1_END = 0x80000000, 0x81800000
MEM2_START, MEM2_END = 0x90000000, 0x94000000

HID4_WRITES = (0x7C13FBA6, 0x7C1BFBA6)


def align_up(value, alignment=ALIGN):
    return (value + alignment - 1) // alignment * alignment


def in_wii_memory(start, size):
    end = start + size
    return ((MEM1_START <= start and end <= MEM1_END) or
            (MEM2_START <= start and end <= MEM2_END))


class Dol:
    def __init__(self, blob):
        self.blob = blob
        self.text_off = struct.unpack('>7I', blob[0x00:0x1C])
        self.data_off = struct.unpack('>11I', blob[0x1C:0x48])
        self.text_addr = struct.unpack('>7I', blob[0x48:0x64])
        self.data_addr = struct.unpack('>11I', blob[0x64:0x90])
        self.text_size = struct.unpack('>7I', blob[0x90:0xAC])
        self.data_size = struct.unpack('>11I', blob[0xAC:0xD8])
        self.bss_addr, self.bss_size, self.entry = struct.unpack(
            '>3I', blob[0xD8:0xE4])

    def sections(self):
        """(name, file offset, load address, advertised size) for each
        section that carries data."""
        for i in range(N_TEXT):
            if self.text_size[i]:
                yield ('text%d' % i, self.text_off[i], self.text_addr[i],
                       self.text_size[i])
        for i in range(N_DATA):
            if self.data_size[i]:
                yield ('data%d' % i, self.data_off[i], self.data_addr[i],
                       self.data_size[i])


def validate(path, quiet=False):
    with open(path, 'rb') as handle:
        blob = handle.read()

    problems = []
    notes = []

    if len(blob) < HEADER_BYTES:
        return ['%s is %d bytes, too small to hold a DOL header'
                % (path, len(blob))], notes

    dol = Dol(blob)
    loaded = []

    for name, offset, addr, size in dol.sections():
        needed = offset + align_up(size)
        if offset < HEADER_BYTES:
            problems.append('%s starts at 0x%x, inside the header'
                            % (name, offset))
        if needed > len(blob):
            problems.append(
                '%s promises 0x%x bytes at file offset 0x%x; the loader '
                'reads %d bytes (rounded up to %d) and the file only has '
                '%d — it is %d bytes short'
                % (name, size, offset, align_up(size), ALIGN, len(blob),
                   needed - len(blob)))
        if not in_wii_memory(addr, align_up(size)):
            problems.append('%s loads at 0x%08x..0x%08x, outside Wii memory'
                            % (name, addr, addr + align_up(size)))
        if size % ALIGN:
            notes.append('%s size 0x%x is not a multiple of %d (the loader '
                         'rounds up to 0x%x)'
                         % (name, size, ALIGN, align_up(size)))
        loaded.append((name, addr, align_up(size)))

    if not loaded:
        problems.append('no sections carry any data')

    loaded.append(('bss', dol.bss_addr, dol.bss_size))
    if dol.bss_size and not in_wii_memory(dol.bss_addr, dol.bss_size):
        problems.append('bss 0x%08x..0x%08x is outside Wii memory'
                        % (dol.bss_addr, dol.bss_addr + dol.bss_size))

    ordered = sorted((a, s, n) for n, a, s in loaded if s)
    for (a0, s0, n0), (a1, s1, n1) in zip(ordered, ordered[1:]):
        if a0 + s0 > a1:
            # BSS following the last data section is normal and adjacent;
            # a genuine overlap is not.
            problems.append('%s (0x%08x+0x%x) overlaps %s (0x%08x)'
                            % (n0, a0, s0, n1, a1))

    entry_ok = any(addr <= dol.entry < addr + align_up(size)
                   for _, _, addr, size in
                   [(n, o, a, s) for n, o, a, s in dol.sections()])
    if not entry_ok:
        problems.append('entry point 0x%08x is not inside any loaded section'
                        % dol.entry)

    wii = False
    for name, offset, _addr, size in dol.sections():
        if not name.startswith('text'):
            continue
        chunk = blob[offset:offset + align_up(size)]
        for pos in range(0, len(chunk) - 3, 4):
            if struct.unpack('>I', chunk[pos:pos + 4])[0] in HID4_WRITES:
                wii = True
                break
        if wii:
            break
    if not wii:
        problems.append('no write to HID4 in the text: Dolphin would treat '
                        'this as GameCube software, not Wii software')

    if not quiet:
        print('%s: %d bytes, entry 0x%08x, bss 0x%08x + %d bytes, %s'
              % (path, len(blob), dol.entry, dol.bss_addr, dol.bss_size,
                 'Wii' if wii else 'NOT Wii'))
        for name, offset, addr, size in dol.sections():
            print('  %-6s file 0x%06x  mem 0x%08x..0x%08x  size 0x%06x'
                  % (name, offset, addr, addr + align_up(size), size))
        for note in notes:
            print('  note: %s' % note)

    return problems, notes


def main(argv):
    quiet = '--quiet' in argv
    paths = [a for a in argv[1:] if not a.startswith('--')]
    if not paths:
        print(__doc__.strip())
        return 2

    failed = False
    for path in paths:
        problems, _notes = validate(path, quiet)
        for problem in problems:
            print('%s: ERROR: %s' % (path, problem))
            failed = True
        if not problems and not quiet:
            print('  looks loadable')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
