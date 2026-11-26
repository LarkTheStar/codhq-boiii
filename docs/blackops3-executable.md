# The game executable CodHQ BOIII loads

CodHQ BOIII is `boiii.exe`: it sits next to `BlackOps3.exe` and loads into it.
It does not patch the game executable on disk - every change (Arxan checks,
packet checksums, the offset tables) happens in memory. What it does need is one
exact build of the game, because the client carries over 4000 hard-coded offsets
and symbols for it.

## The build this fork supports

|                                    | Value                                      |
| ---------------------------------- | ------------------------------------------ |
| Black Ops 3 executable             | the build released **Sept 10, 2026**       |
| Size                               | 106,059,256 bytes                          |
| PE header checksum                 | `0x6531394`                                |
| SHA-1                              | `9D03F81086112113BFB1DD22B8538B9398AC3B9B` |
| Unranked dedicated server checksum | `0x14C28B4`                                |

The client reads that checksum from the PE optional header before it loads the
game and refuses anything else, so a Steam update that replaces the executable
stops the client until the matching build is back.

## Why there is a specific build at all

- **Feb 19, 2026** - Steam replaced the executable as part of an Arxan (DRM)
  update. It broke BOIII and T7x; the community workaround was to drop the older
  executable back in. Upstream's launcher still carries that fix as "Settings =>
  Patches", pointing at `archive.org/download/t7_full_game/BlackOps3.exe`.
- **Sept 10, 2026** - Steam shipped a new executable. Upstream **v3.0.0** moved
  the client to it, and **v3.1.0** removed support for the 2023 executable
  entirely, so the old archive.org link from the Feb workaround now points at a
  build this client rejects. Do not use it.
- This fork follows upstream v3.1.0, so the only build that works is the Sept
  10, 2026 one, which is what a current Steam install has.

## What the client does when the build does not match

1. **Before launch** it classifies `BlackOps3.exe`: `supported`, `incompatible`
   or `unreadable`.
   - `supported` - starts normally, nothing is downloaded.
   - `unreadable` - stops with "check that the game folder is complete, then
     verify the game files in Steam".
   - `incompatible` - offers the compatible executable in a prompt.
2. **If the player accepts**, the client downloads
   `https://archive.org/download/black-ops-3_20260926/BlackOps3.exe` (fallback
   `.../black-ops-3_20260927/BlackOps3.exe`), checks its SHA-1 and PE checksum,
   moves the current file to `BlackOps3.exe.boiii_backup`, installs the new one
   and reads it back to confirm it is the supported build. If anything fails,
   the backup is put back and kept.
3. **If the player declines**, nothing is written: the client stops and says the
   file was left as it was, with the two manual routes below.

The same installer is available at any time from **Settings => Patches**, which
also shows the supported checksum, the manual download link and the backup path.

## Manual recovery

1. **Put the supported build back through Steam**: right-click Black Ops III >
   Properties > Installed Files > Verify integrity of game files. Steam restores
   the executable it currently ships, which is the supported one.
2. **Restore your own copy**: rename `BlackOps3.exe.boiii_backup` back to
   `BlackOps3.exe`.
3. **Last resort**: download the Sept 10, 2026 executable from
   `https://archive.org/download/black-ops-3_20260926/BlackOps3.exe` and check
   that it is 106,059,256 bytes with PE checksum `0x6531394` (SHA-1
   `9D03F81086112113BFB1DD22B8538B9398AC3B9B`) before replacing anything.

The client never downloads anything when the executable already matches, and it
never writes to a game file without the player agreeing to it.
