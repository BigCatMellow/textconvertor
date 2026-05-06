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
new = '''    # MainUI launch
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
if "useOnyxLauncher" not in runtime_text:
    if old not in runtime_text:
        raise SystemExit("Could not find MainUI launch block in runtime.sh")
    runtime_text = runtime_text.replace(old, new)
    runtime.write_text(runtime_text)
PY
