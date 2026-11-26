#!/usr/bin/env python3
"""Make a linked boiii.exe independent of when and where it was linked.

/Brepro already replaces the PE timestamp with a hash of the output, but the linker
still writes a fresh CodeView GUID (and a timestamp that hashes it) on every run, so
two builds of the same sources differ in exactly those fields. This zeroes them:

  * the COFF FileHeader TimeDateStamp,
  * the TimeDateStamp of every debug directory entry,
  * the RSDS GUID of each CodeView record (the PDB name and age stay),
  * the payload of the linker's REPRO debug entry.

boiii.exe also embeds tlsdll.dll as a resource, so the same fields are cleared in
every PE image found inside the file, not just the outer one.

Nothing the game or the client reads changes: the timestamp is not used to load an
image, and the CodeView GUID is only the PDB identity. The published executable is
the only thing that depends on those fields, and the installer pins it by SHA-256.

Usage: python tools/canonicalize-pe.py <file.exe>
"""

import struct
import sys

IMAGE_DIRECTORY_ENTRY_DEBUG = 6
IMAGE_DEBUG_TYPE_CODEVIEW = 2
IMAGE_DEBUG_TYPE_REPRO = 16


def read_u16(data, offset):
    return struct.unpack_from('<H', data, offset)[0]


def read_u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def pe_offset(data, base):
    """Offset of the PE header when a valid image starts at base, else None."""
    if data[base:base + 2] != b'MZ' or base + 0x40 > len(data):
        return None
    offset = read_u32(data, base + 0x3C)
    if offset < 0x40 or base + offset + 24 > len(data):
        return None
    if data[base + offset:base + offset + 4] != b'PE\0\0':
        return None
    return offset


def section_table(data, base, pe):
    file_header = base + pe + 4
    optional_size = read_u16(data, file_header + 16)
    section_count = read_u16(data, file_header + 2)
    optional_header = file_header + 20
    sections = []
    for index in range(section_count):
        header = optional_header + optional_size + index * 40
        virtual_size = read_u32(data, header + 8)
        virtual_address = read_u32(data, header + 12)
        raw_size = read_u32(data, header + 16)
        raw_pointer = read_u32(data, header + 20)
        sections.append((virtual_address, virtual_size, raw_pointer, raw_size))
    return file_header, optional_header, sections


def rva_to_offset(sections, rva):
    for virtual_address, virtual_size, raw_pointer, raw_size in sections:
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            return raw_pointer + (rva - virtual_address)
    return None


def canonicalize_image(data, base):
    pe = pe_offset(data, base)
    if pe is None:
        return None
    file_header, optional_header, sections = section_table(data, base, pe)
    struct.pack_into('<I', data, file_header + 4, 0)  # FileHeader TimeDateStamp

    debug_entry = optional_header + 112 + IMAGE_DIRECTORY_ENTRY_DEBUG * 8
    debug_rva = read_u32(data, debug_entry)
    debug_size = read_u32(data, debug_entry + 4)
    if debug_rva and debug_size:
        debug_offset = rva_to_offset(sections, debug_rva)
        if debug_offset is not None:
            for index in range(debug_size // 28):
                entry = base + debug_offset + index * 28
                debug_type = read_u32(data, entry + 12)
                payload_size = read_u32(data, entry + 16)
                payload_rva = read_u32(data, entry + 20)
                struct.pack_into('<I', data, entry + 4, 0)  # entry TimeDateStamp
                payload = rva_to_offset(sections, payload_rva) if payload_rva else None
                if payload is None:
                    continue
                payload += base
                if debug_type == IMAGE_DEBUG_TYPE_CODEVIEW and data[payload:payload + 4] == b'RSDS':
                    data[payload + 4:payload + 20] = b'\0' * 16
                elif debug_type == IMAGE_DEBUG_TYPE_REPRO and payload_size:
                    data[payload:payload + payload_size] = b'\0' * payload_size
    return pe


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2

    path = argv[1]
    with open(path, 'rb') as handle:
        data = bytearray(handle.read())

    if canonicalize_image(data, 0) is None:
        raise SystemExit(f'{path} is not a PE image')

    # boiii.exe carries tlsdll.dll inside its resources; it is a PE image of its own and
    # carries its own link time, so clear that one too.
    embedded = 0
    position = 2
    while True:
        found = data.find(b'MZ', position)
        if found < 0:
            break
        position = found + 2
        if canonicalize_image(data, found) is not None:
            embedded += 1

    with open(path, 'wb') as handle:
        handle.write(data)
    print(f'{path}: timestamp and debug identity cleared in the executable and {embedded} embedded image(s)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
