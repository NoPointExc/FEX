# MapleStory Classic World on ARM Android: FEX patch notes

This is an **unofficial fork** of [FEX-Emu/FEX](https://github.com/FEX-Emu/FEX).
It was made to run **MapleStory Classic World** (Steam app 216150) on one
ARM Android handheld. It is **not tested with other games or devices**, and it
is not part of upstream FEX. Use it at your own risk.

The code changes were written with an AI coding agent. FEX does not accept
AI-written code (see upstream `CONTRIBUTING.md`), so **do not send these
commits to FEX-Emu as a pull request**.

## Tested setup

| Item | Value |
| --- | --- |
| Device | Retroid Pocket 6 (Snapdragon 8 Gen 2, Android 13, not rooted) |
| Launcher | DroidDeck 0.5.0 (Steam Big Picture in proot) |
| Proton | Valve "Proton Experimental (ARM64)": `experimental-11.0-20261014-arm64`, then `experimental-11.0-20261027b-arm64` |
| FEX base | Valve's FEX builds `FEX-2610-25-g5285020` (this branch) and `FEX-2610-101-g1c4f04b` (branch `maplestory-classic-1c4f04b`) |
| Games | MapleStory Classic World (216150), MapleStory M (3969080) |

## What the patch changes

Two small commits on top of `5285020de`, both in the ARM64EC (Wine on
Windows-on-ARM style) frontend:

1. **`Windows: Track executable images mapped without loader notification`**
   (`Source/Windows/Common/InvalidationTracker.{h,cpp}`)
   - Wine only sends `NotifyMapViewOfSection` for images mapped by its loader.
     An image mapped directly with `NtMapViewOfSection` (Themida maps a private
     `ntdll` copy this way) is never added to `XIntervals`, so FEX raises a
     NoExec fault when code jumps into it.
   - Fix: when `QueryExecutableRange` misses, query the real page protection
     and register committed, executable `MEM_IMAGE` regions.
   - Needed for: MapleStory M. Kept here because the tested build has it.
2. **`Windows: Avoid process heap allocations when converting section paths`**
   (`Source/Windows/Common/Module.h`)
   - `GetSectionFilePath` runs from `HandleImageMap` while
     `ThreadCreationMutex` is held, and `RtlUnicodeStringToAnsiString(..., TRUE)`
     allocates from the process heap. Another thread holding the heap lock can
     need `ThreadCreationMutex` (heap growth → `NtAllocateVirtualMemory`
     notification), so the game deadlocks at startup.
   - Fix: convert into a stack buffer instead.
   - Needed for: MapleStory Classic World.

The same two commits as one file: [`patches/maplestory-classic-arm64ec.patch`](patches/maplestory-classic-arm64ec.patch).

## Guide for coding agents

Follow these steps to build and install the patch for a new Proton version.

### 1. Find the FEX version that Proton ships

Proton's FEX DLL is
`<Proton>/files/lib/wine/aarch64-windows/libarm64ecfex.dll`. Read its version:

```sh
strings libarm64ecfex.dll | grep -m1 -E 'FEX-[0-9]{4}'   # e.g. FEX-2610-25-g5285020
```

The `g<hash>` part is the upstream FEX commit. Build on that commit, so the DLL
matches the rest of Proton.

### 2. Apply the patch

```sh
git clone https://github.com/FEX-Emu/FEX.git && cd FEX
git checkout <hash-from-step-1>
git submodule update --init --recursive
git am /path/to/patches/maplestory-classic-arm64ec.patch
```

If `git am` fails, the code moved upstream. Re-apply the two changes by hand
using the descriptions above. As of upstream `75df0f815` (2026-11), both
commits still cherry-pick cleanly.

### 3. Build `libarm64ecfex.dll`

You need [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) with ARM64EC
support (tested with clang 23.1.4), CMake and Ninja. Put llvm-mingw's `bin` on
`PATH`, then:

```sh
cmake -S . -B build_arm64ec -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=Data/CMake/toolchain_mingw.cmake \
  -DMINGW_TRIPLE=arm64ec-w64-mingw32 \
  -DCMAKE_BUILD_TYPE=Release \
  -DTUNE_CPU=none -DTUNE_ARCH=generic \
  -DENABLE_LTO=False -DENABLE_ASSERTIONS=False \
  -DBUILD_TESTING=False -DENABLE_JEMALLOC_GLIBC_ALLOC=False
ninja -C build_arm64ec arm64ecfex
# output: build_arm64ec/Bin/libarm64ecfex.dll
```

Always pass `-DTUNE_CPU=none -DTUNE_ARCH=generic`: the default tunes for the build machine,
and on macOS the configure step fails (it reads `/proc/cpuinfo`).

### 4. Install it on the device

The launch script in [`maplestory-kit/`](maplestory-kit/) does this by itself at every game
start (see "Launch script" below): put the new DLL at
`/root/.local/share/mscw-inputfix/fex/<Valve FEX version>/libarm64ecfex.dll`, where the version
is the string from step 1 of **Valve's** DLL (for example `FEX-2610-101-g1c4f04b`). By hand:

```sh
D="<Proton Experimental (ARM64)>/files/lib/wine/aarch64-windows"
cp "$D/libarm64ecfex.dll" "$D/libarm64ecfex.dll.valve-orig"   # keep Valve's copy
cp libarm64ecfex.dll "$D/libarm64ecfex.dll"
```

In DroidDeck, `<Proton Experimental (ARM64)>` is
`/root/.local/share/Steam/steamapps/common/Proton Experimental (ARM64)` in the
guest. DroidDeck's `~/.local/share/droiddeck-esync/dist/valve-experimental-*/files/lib/wine/aarch64-windows`
is only a symlink to that folder.

To undo, copy `libarm64ecfex.dll.valve-orig` back.

### 5. Known problem: Steam deletes the patched DLL

When a Proton update is pending, Steam verifies the Proton files, marks the
patched DLL as "Files Corrupt", and **deletes** it. The game then hangs on the
Steam spinner. Starting the game also pauses the Proton update, so it may never
finish.

Signs, in `~/.local/share/Steam/logs/content_log.txt`:

```
AppID 4427310 state changed : Update Required,Fully Installed,Update Queued,Files Corrupt,...
```

Fix: let the Proton update finish (do not start a game meanwhile), then repeat
steps 1–4 for the new Proton version. The launch script puts a fixed DLL back at the next
start even if Steam deleted it. If it has no build for the new Valve version, it uses the
newest build it has and writes a warning to `/root/.local/share/mscw-inputfix/patch.log`.

## Other fixes this game needs (not FEX)

These are outside FEX, but the game does not work without them. Their sources are in
[`maplestory-kit/`](maplestory-kit/).

- **SD card is noexec (Themida "An internal exception occurred").** Since the game update of
  2026-11-04, Themida makes `MapleStory.exe`'s `.rdata` `PAGE_EXECUTE_READWRITE`. Wine maps
  that section from the file, and on a noexec mount `mprotect(PROT_EXEC)` fails with `EACCES`,
  so the page stays read-only while Wine records it as writable. The next write crashes
  (`EXCEPTION_ACCESS_VIOLATION` at `0x1445F65F5`, write to `0x143359C80`). `noexec-shim.so`
  (LD_PRELOAD) catches that `EACCES`, copies the pages to anonymous memory with `mremap`, and
  retries. Build: `clang --target=aarch64-linux-gnu -O2 -fPIC -shared -nostdlib -fuse-ld=lld
  -o noexec-shim.so noexec-shim.c` (works with llvm-mingw's clang). To check, read
  `/proc/<MapleStory.exe pid>/maps`: the `143359000` line must not name `MapleStory.exe`.

- **Steam overlay flag.** When the Nexon launcher closes, Proton sets the
  named event `__wine_steamclient_GameOverlayActivated`, which makes Wine drop
  all keyboard, mouse and gamepad input. A small helper that waits for
  `MapleStory.exe` and resets that event while the game runs fixes it (used as
  the Steam launch option `mscw-inputfix.sh %command%`).
- **Controller.** In game: Esc → Game Option → Controller → turn on
  "Enable Controller Support" (registry
  `HKCU\Software\Wizet\MapleStoryClassic\cEnable` = 1).
- **Launch script.** `mscw-inputfix.sh` is the Steam launch option
  (`/root/.local/bin/mscw-inputfix.sh %command%`). At each start it re-installs the FEX fix,
  loads `noexec-shim.so`, starts `mscw-inputfix.exe`, and reads optional debug variables from
  `/root/.local/share/mscw-inputfix/debug.env` (for example `PROTON_LOG=1`). Files on the device:

  ```
  /root/.local/bin/mscw-inputfix.sh
  /root/.local/share/mscw-inputfix/{mscw-inputfix.exe,noexec-shim.so,patch.log}
  /root/.local/share/mscw-inputfix/fex/<Valve FEX version>/libarm64ecfex.dll
  ```
- **Resolution.** Set the game to 1280x720 windowed, the same size as the Wine
  screen, or the mouse cannot reach the right and bottom edges.
