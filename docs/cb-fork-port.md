# CB's fork patches, and where each one stands in this fork

`CBServers/boiii` is CB's copy of this project (last pushed 2026-08-16). It is not a
GitHub fork of `Ezz-lol/boiii-free`, but the shared history is still visible: of its
1368 commits, **60 are CB's own** (authored by `bradstv`/`Brad` after their
2023-05-31 initial commit); the rest are commits that are also in upstream's 3300.

Every one of those 60 is listed below with what happened to it: already in this tree
(with the file and symbol that proves it), ported here, or not portable and why.

## Ported here

| CB commit | Change | Where it lives now |
| --- | --- | --- |
| `a8009ee` | `+set` on the command line was ignored for most dvars | `src/client/component/command_line.cpp`: the `set`/`seta` arguments are re-applied after the config is read. CB's other half of that commit stubs an engine address (`0x14133B830` client / `0x140183C80` server) to widen the whitelist; those offsets belong to the pre-Sept-2026 executable, so only the address-free half is ported. |
| `0d3a950` | Manage the Black Ops 3 binary from the updater "since Steam changed it" | `src/client/client_binary.cpp`: the compatible executable is checked, offered, verified by SHA-1 and PE checksum, installed behind a prompt, and the player's file is kept as `BlackOps3.exe.boiii_backup`. |

## Already in this tree

| CB commit(s) | Change | Proof in this tree |
| --- | --- | --- |
| `a8da1f4` | Random crash when showing LUI message dialogs (cached `HksObject*` dangles after the api stack grows) | `src/client/game/ui_scripting/stack_guard.hpp` keeps an offset from `m_apistack.bottom` and restores by index; `script_value.cpp` moves `ref_` with the value. |
| `9f81f9f` | Pause-menu UI options disappearing on map rotations | `src/client/component/lua/ui_scripting.cpp`: `cl_first_snapshot_stub` checks `getinfo::is_host()`. |
| `e722c16`, `f77379d`, `43bc774`, `721be84`, `4f3aa54` | FastDL: GB sizes, large map files, force FastDL when kicked for a missing map | `src/client/component/fastdl.cpp` (download context, `Download size: %.2f GB` popup) and `component/workshop/*` (FastDL start paths). |
| `140fa2d` | Server browser fixed for the t7x changes | `src/client/component/server_list.cpp` plus the Sept-2026 protocol values in `game/structs.hpp`. |
| `60c9118`, `fb36663` | New protocol / subprotocol value | `src/client/game/structs.hpp` from the v3.1.0 base. |
| `9656aea` | t7x patches (chat, status, network, arxan, dvars) | Present in the v3.1.0 base; this fork adds its own Arxan handling on top (`component/arxan.cpp`). |
| `2192724`, `a1802d0`, `d14b500`, `4354a37` | Master server list and a master-server dvar | `src/client/component/server_list.cpp` with `boiii_players/user/master_servers.txt`. |
| `78c01fc`, `a12a3dc`, `33d0ffe` | yaw/pitch speed dvars | `src/client/component/client_patches.cpp`. |
| `48d2335`, `03ba0d2`, `eba2ed2`, `8795082` | Bot names from `bots.txt`, no built-in names, bot fixes | `src/client/component/bots.cpp` (`boiii/bots.txt`, default list fallback). |
| `46f249a`, `e957e58` | Branding off by default, version in the console prefix | `src/client/component/branding.cpp` (`-nobranding`, `"CodHQ BOIII " VERSION`). |
| `add6afd` | Workshop mods/maps no longer missing | `src/client/component/steam_proxy.cpp`, `component/workshop/*`. |
| `a31bf5d`, `4c38c48` | Usermap pub id / missing usermap on custom maps | `src/client/component/workshop/content_hash.cpp` (`usermap_content`, `loaded_usermap_content`). |
| `7337afe` | Name from the command line | `-name` is the catalogue's name argument and `src/client/component/name.cpp` reads it. |
| `5dcdd85` | Password prompt when joining a protected server | `data/ui_scripts/server_password/__init__.lua` and `component/network_password.cpp`. |
| `12e8d64`, `57fca70` | Cache files / cache creation outside the updater | `src/client/updater/file_updater.cpp`. |
| `2b8ce44`, `9ae9712`, `64f616a`, `2b3eeb1` | CB's updater endpoints and "allow binary to update" | This fork uses its own Pages site and `boiii.json` (`updater/`, `client_binary.cpp`). CB's endpoints are deliberately not used. |

## Not portable as-is

| CB commit(s) | Change | Why |
| --- | --- | --- |
| `5c19cdc`, `a254801`, `381b365`, `f3885b0`, `c22565f`, `cf0302a`, `61979ef`, `ac70a85`, `a8da1f4` (friends half mainly `cf0302a`) | Discord presence/invites, CB friends in game, friend-of-friend join, UDP hole punching, "open to friends" | These run over `src/client/component/ipc.cpp`, a named pipe to **CB Launcher**, and CB's own friend graph. This project's launcher is the PowerShell setup utility, so the IPC side has nothing to talk to. This fork already has its own friends, invites, party and NAT code (`component/friends.cpp`, `party.cpp`, `nat.cpp`); porting CB's variant would replace working code with a launcher-bound one. |
| `486de5e` | Shared GUID with other CB clients | Ties the player identity to CB's launcher install; this fork keeps upstream's identity handling. |
| `82de7db`, `7ab0363`, `5057ef9`, `78558a6` | Skip Steam checks, ext.dll block/allow, CB build workflow | Build and anti-tamper choices for CB's launcher and their `ext.dll`; this fork has its own build workflow and no `ext.dll`. |
| `9210609`, `2b3eeb1`, `43bc774` (build half), `4bdce97`, `02842e7`, `cb0b86e`, `3ef2789`, `dc70099` | Build artifact versions, README/workflow updates | Repo housekeeping for CB's CI, not code. |

## Checked and deliberately different

- **Proxy DLLs** (`8b223e0`): CB pre-loads the system copy of any DLL in the game folder
  that shares a name with a system DLL, to keep proxies like t7patch or CleanOps out of
  the process. This fork instead detects a proxying DLL, a WinDivert driver, a running
  GoodbyeDPI or a non-Windows Winsock entry and reports it (`component/proxy_guard.cpp`)
  without patching around it. Same threat, opposite policy: say so rather than
  silently override a file the player put there. Say the word if you want CB's blocking
  behaviour instead; the code is self-contained and has no game addresses.
- **Bot balance in `_teams.gsc`** (`83088e9`): the file is compiled bytecode in this
  tree; CB's change would have to be made in the script source and recompiled, so it
  cannot be merged as a patch.
- **Build-specific addresses**: CB's binary patches carry offsets for the executable
  they were built against (before Sept 10, 2026). This fork is built for the Sept 10,
  2026 executable with over 4000 offsets of its own, so copying an address patch over
  would point at the wrong code. Any CB patch that only works by writing to an address
  is therefore not carried over; the behaviour it implements is ported instead where it
  can be expressed without an address.
