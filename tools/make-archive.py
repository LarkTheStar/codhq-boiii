#!/usr/bin/env python3
"""Build the published CodHQ BOIII archive so the same files always give the same ZIP.

Compress-Archive records each file's modification time, so a rebuild of the same
sources produced a different archive and therefore a different SHA-256, while the
installer pins exactly one archive. This writes the same entries with a fixed date,
fixed attributes and a fixed compression level, in a fixed order.

Usage: python tools/make-archive.py <site/boiii> <out.zip>
"""

import os
import sys
import zipfile

FIXED_DATE = (1980, 1, 1, 0, 0, 0)
FILE_ATTRIBUTE_ARCHIVE = 0o644


def entries(root):
    files = []
    executable = os.path.join(root, 'boiii.exe')
    if not os.path.isfile(executable):
        raise SystemExit(f'{executable} is missing; build the client first')
    files.append(executable)
    data = os.path.join(root, 'data')
    for directory, subdirectories, names in os.walk(data):
        subdirectories.sort()
        for name in sorted(names):
            files.append(os.path.join(directory, name))
    return files


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2

    root, destination = argv[1], argv[2]
    files = entries(root)
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in files:
            relative = os.path.relpath(path, root).replace(os.sep, '/')
            info = zipfile.ZipInfo(relative, date_time=FIXED_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = FILE_ATTRIBUTE_ARCHIVE << 16
            info.create_system = 0
            with open(path, 'rb') as handle:
                archive.writestr(info, handle.read(), compresslevel=9)
    print(f'{destination}: {len(files)} entries')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
