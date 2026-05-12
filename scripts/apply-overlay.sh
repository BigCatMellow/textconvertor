#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
onion_root="${1:-Onion}"

if [[ ! -d "$onion_root/src" ]]; then
    echo "Onion source directory not found: $onion_root" >&2
    exit 1
fi

mkdir -p "$onion_root/src/onyxLauncher"
cp "$repo_root/src/onyxLauncher/Makefile" "$onion_root/src/onyxLauncher/Makefile"
cp "$repo_root/src/onyxLauncher/onyxLauncher.c" "$onion_root/src/onyxLauncher/onyxLauncher.c"

mkdir -p "$onion_root/static/build/.tmp_update/script"
cp "$repo_root/scripts/onyx_launcher.sh" "$onion_root/static/build/.tmp_update/script/onyx_launcher.sh"
chmod +x "$onion_root/static/build/.tmp_update/script/onyx_launcher.sh"

mkdir -p "$onion_root/static/build/.tmp_update/res/onyx/icons"
if compgen -G "$repo_root/icons/*.png.b64" > /dev/null; then
    for icon in "$repo_root"/icons/*.png.b64; do
        base="$(basename "$icon" .b64)"
        base64 -d "$icon" > "$onion_root/static/build/.tmp_update/res/onyx/icons/$base"
    done
fi

mkdir -p "$onion_root/static/build/.tmp_update/res/onyx/sound"
if compgen -G "$repo_root/res/onyx/sound/*.wav" > /dev/null; then
    cp "$repo_root"/res/onyx/sound/*.wav "$onion_root/static/build/.tmp_update/res/onyx/sound/"
fi

mkdir -p "$onion_root/static/build/miyoo/app"
if compgen -G "$repo_root/fonts/*.b64" > /dev/null; then
    for font in "$repo_root"/fonts/*.b64; do
        base="$(basename "$font" .b64)"
        base64 -d "$font" > "$onion_root/static/build/miyoo/app/$base"
    done
fi

if [[ -f "$repo_root/splash/bootScreen.png.b64" ]]; then
    base64 -d "$repo_root/splash/bootScreen.png.b64" > "$onion_root/static/build/.tmp_update/res/bootScreen.png"
    mkdir -p "$onion_root/src/bootScreen/res"
    cp "$onion_root/static/build/.tmp_update/res/bootScreen.png" "$onion_root/src/bootScreen/res/bootScreen.png"
fi

python3 - "$onion_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
makefile = root / "Makefile"
runtime = root / "static/build/.tmp_update/runtime.sh"

make_text = makefile.read_text()
needle = "\t@cd $(SRC_DIR)/setState && BUILD_DIR=$(BIN_DIR) make\n"
insert = needle + "\t@cd $(SRC_DIR)/onyxLauncher && BUILD_DIR=$(BIN_DIR) make\n"
if "$(SRC_DIR)/onyxLauncher" not in make_text:
    if needle not in make_text:
        raise SystemExit("Could not find setState build line in Makefile")
    make_text = make_text.replace(needle, insert)
    makefile.write_text(make_text)

runtime_text = runtime.read_text()
old = '''    # MainUI launch
    cd $miyoodir/app
    PATH="$miyoodir/app:$PATH" \\
        LD_LIBRARY_PATH="$miyoodir/lib:/config/lib:/lib" \\
        LD_PRELOAD="$miyoodir/lib/libpadsp.so" \\
        ./MainUI 2>&1 > /dev/null
'''
old_onyx = '''    # MainUI launch
    cd $miyoodir/app
    if [ -f "$sysdir/config/.useOnyxLauncher" ] && [ -x "$sysdir/bin/onyxLauncher" ]; then
        PATH="$miyoodir/app:$PATH" \\
            LD_LIBRARY_PATH="$miyoodir/lib:/config/lib:/lib" \\
            LD_PRELOAD="$miyoodir/lib/libpadsp.so" \\
            "$sysdir/bin/onyxLauncher" 2>&1 > /dev/null
    else
        PATH="$miyoodir/app:$PATH" \\
            LD_LIBRARY_PATH="$miyoodir/lib:/config/lib:/lib" \\
            LD_PRELOAD="$miyoodir/lib/libpadsp.so" \\
            ./MainUI 2>&1 > /dev/null
    fi
'''
old_onyx_no_else = '''    # MainUI launch
    cd $miyoodir/app
    if [ -f "$sysdir/config/.useOnyxLauncher" ] && [ -x "$sysdir/bin/onyxLauncher" ]; then
        PATH="$miyoodir/app:$PATH" \\
            LD_LIBRARY_PATH="$miyoodir/lib:/config/lib:/lib" \\
            LD_PRELOAD="$miyoodir/lib/libpadsp.so" \\
            "$sysdir/bin/onyxLauncher" 2>&1 > /dev/null
    fi

    PATH="$miyoodir/app:$PATH" \\
        LD_LIBRARY_PATH="$miyoodir/lib:/config/lib:/lib" \\
        LD_PRELOAD="$miyoodir/lib/libpadsp.so" \\
        ./MainUI 2>&1 > /dev/null
'''
new = '''    # MainUI launch
    cd $miyoodir/app
    # ONYX launcher hook
    if [ -f "$sysdir/config/.useOnyxLauncher" ] && [ -x "$sysdir/bin/onyxLauncher" ]; then
        PATH="$miyoodir/app:$PATH" \\
            LD_LIBRARY_PATH="$miyoodir/lib:/config/lib:/lib" \\
            LD_PRELOAD="$miyoodir/lib/libpadsp.so" \\
            "$sysdir/bin/onyxLauncher" 2>&1 > /dev/null
    else
        PATH="$miyoodir/app:$PATH" \\
            LD_LIBRARY_PATH="$miyoodir/lib:/config/lib:/lib" \\
            LD_PRELOAD="$miyoodir/lib/libpadsp.so" \\
            ./MainUI 2>&1 > /dev/null
    fi
'''
if new not in runtime_text:
    if old_onyx in runtime_text:
        runtime_text = runtime_text.replace(old_onyx, new)
    elif old_onyx_no_else in runtime_text:
        runtime_text = runtime_text.replace(old_onyx_no_else, new)
    elif old in runtime_text:
        runtime_text = runtime_text.replace(old, new)
    else:
        raise SystemExit("Could not find MainUI launch block in runtime.sh")
    runtime.write_text(runtime_text)
PY
