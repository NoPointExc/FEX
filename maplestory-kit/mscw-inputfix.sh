#!/bin/bash
# Steam launch option for MapleStory (216150) in DroidDeck:  /root/.local/bin/mscw-inputfix.sh %command%
#
# Before the game starts, this puts back the local fixes that updates can undo, then runs the
# game unchanged:
#   1. FEX fix: Proton updates replace libarm64ecfex.dll (and Steam deletes a changed copy while
#      an update is pending). Fixed builds are kept per Valve FEX version in $KIT/fex/<version>/.
#   2. noexec-shim.so: LD_PRELOAD library for the noexec SD card (see noexec-shim.c).
#   3. mscw-inputfix.exe: clears the stuck Steam overlay flag once MapleStory.exe runs.
# What it did is written to $KIT/patch.log.
KIT=/root/.local/share/mscw-inputfix
HELPER=$KIT/mscw-inputfix.exe
SHIM=$KIT/noexec-shim.so
LOG=$KIT/last.log
PATCHLOG=$KIT/patch.log
PROTON_WINE="/root/.local/share/Steam/steamapps/common/Proton Experimental (ARM64)/files/lib/wine/aarch64-windows"

md5() { md5sum < "$1" | cut -d' ' -f1; }
fex_version() { grep -a -o -m1 'FEX-[0-9]\{4\}-[0-9]*-g[0-9a-f]*' "$1" 2>/dev/null | head -1; }

install_fex_fix() {
  local dll="$PROTON_WINE/libarm64ecfex.dll" cur="" build ver pick
  if [ ! -d "$PROTON_WINE" ]; then
    echo "Proton Experimental (ARM64) not found; FEX fix skipped"
    return
  fi
  [ -f "$dll" ] && cur=$(md5 "$dll")
  for build in "$KIT"/fex/*/libarm64ecfex.dll; do
    if [ -f "$build" ] && [ "$(md5 "$build")" = "$cur" ]; then
      echo "FEX fix already installed ($(basename "$(dirname "$build")"))"
      return
    fi
  done

  # Any other file here is Valve's own build (a Proton update). Keep it as the backup.
  if [ -f "$dll" ]; then
    cp -f "$dll" "$dll.valve-orig"
  fi
  ver=$(fex_version "$dll.valve-orig")
  pick="$KIT/fex/$ver/libarm64ecfex.dll"
  if [ -z "$ver" ] || [ ! -f "$pick" ]; then
    pick=$(ls -1d "$KIT"/fex/*/ 2>/dev/null | sort -V | tail -1)libarm64ecfex.dll
    if [ ! -f "$pick" ]; then
      echo "WARNING: no FEX fix builds in $KIT/fex; using Valve's FEX"
      return
    fi
    echo "WARNING: no FEX fix built for Valve's ${ver:-unknown version}; using the newest one," \
      "$(basename "$(dirname "$pick")"). Build one for $ver (see MAPLESTORY-CLASSIC.md in the FEX fork)."
  fi
  chmod u+w "$dll" 2>/dev/null
  if cp -f "$pick" "$dll" && chmod 555 "$dll"; then
    echo "installed FEX fix $(basename "$(dirname "$pick")") (Valve's FEX is ${ver:-unknown})"
  else
    echo "ERROR: could not install the FEX fix into $PROTON_WINE"
  fi
}

{
  echo "== $(date '+%F %T')"
  install_fex_fix
  [ -f "$SHIM" ] || echo "WARNING: $SHIM missing; the game may crash with a Themida error"
  [ -f "$HELPER" ] || echo "WARNING: $HELPER missing; input may not work"
} >> "$PATCHLOG" 2>&1
tail -n 200 "$PATCHLOG" > "$PATCHLOG.tmp" 2>/dev/null && mv -f "$PATCHLOG.tmp" "$PATCHLOG"

(
  for _ in $(seq 600); do
    P=$(pgrep -f '^MapleStory.exe' | head -1)
    [ -n "$P" ] && break
    sleep 1
  done
  [ -z "$P" ] && exit 0
  while IFS= read -r -d '' kv; do
    case "$kv" in WINESERVERSOCKET=*|WINEPRELOADRESERVE=*|WINELOADERNOEXEC=*) ;; *) export "$kv" ;; esac
  done < "/proc/$P/environ"
  export WINEDEBUG=-all
  exec wine "$HELPER"
) > "$LOG" 2>&1 &

# Copy SD-card (noexec) image pages to memory when a program makes them executable; see noexec-shim.c.
[ -f "$SHIM" ] && export LD_PRELOAD="$SHIM${LD_PRELOAD:+:$LD_PRELOAD}"
# Optional debug settings for the game (e.g. PROTON_LOG=1); remove the file to turn them off.
[ -f "$KIT/debug.env" ] && set -a && . "$KIT/debug.env" && set +a
exec "$@"
