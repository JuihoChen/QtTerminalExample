#!/bin/bash
# Makes a Windows desktop shortcut that starts QtTerminalExample through WSLg,
# with the app's own icon. Shortcuts WSLg makes from .desktop launchers get a
# penguin badge on their icon; this one doesn't.
#
# Run it inside WSL after building:  ./wsl-desktop-shortcut.sh [path/to/QtTerminalExample]
set -e

SRC="$(cd "$(dirname "$0")" && pwd)"
BIN="$(realpath "${1:-$SRC/build/QtTerminalExample}")"

if [ -z "$WSL_DISTRO_NAME" ]; then
    echo "Run this inside WSL." >&2
    exit 1
fi
if [ ! -x "$BIN" ]; then
    echo "$BIN not found; build first, or pass the program's path." >&2
    exit 1
fi

# Windows can't reliably read shortcut icons from \\wsl.localhost, so keep a
# copy of the icon in %LOCALAPPDATA%\QtTerminal
LOCALAPPDATA_WIN="$(cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r')"
ICON_DIR="$(wslpath "$LOCALAPPDATA_WIN")/QtTerminal"
mkdir -p "$ICON_DIR"
cp "$SRC/icons/qtterminal.ico" "$ICON_DIR/"
ICON_WIN="$LOCALAPPDATA_WIN\\QtTerminal\\qtterminal.ico"

# wslg.exe starts a Linux GUI app without opening a console window
powershell.exe -NoProfile -Command "
\$wslg = (Get-Command wslg.exe -ErrorAction Stop).Source
\$lnk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Qt Terminal SSH Manager.lnk'
\$s = (New-Object -ComObject WScript.Shell).CreateShortcut(\$lnk)
\$s.TargetPath = \$wslg
\$s.Arguments = '-d $WSL_DISTRO_NAME --cd ~ -- $BIN'
\$s.IconLocation = '$ICON_WIN,0'
\$s.Description = 'Terminal with SSH Connection Management'
\$s.Save()
Write-Output \"Created \$lnk\"
"
