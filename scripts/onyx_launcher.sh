#!/bin/sh

sysdir=/mnt/SDCARD/.tmp_update
runtime="$sysdir/runtime.sh"
backup="$sysdir/runtime.sh.onion.bak"
flag="$sysdir/config/.useOnyxLauncher"

patch_runtime() {
    if [ ! -f "$runtime" ]; then
        echo "ONYX: runtime.sh not found at $runtime" >&2
        return 1
    fi

    if awk '
        /ONYX launcher hook/ { in_hook = 1; next }
        in_hook && /^[[:space:]]*else[[:space:]]*$/ { found = 1; exit }
        in_hook && /^[[:space:]]*fi[[:space:]]*$/ { exit }
        END { if (found) exit 0; exit 1 }
    ' "$runtime"; then
        return 0
    fi

    if [ ! -f "$backup" ]; then
        cp "$runtime" "$backup"
    fi

    tmp="$runtime.onyx.tmp"
    awk '
        BEGIN { in_mainui = 0; inserted = 0; saw_mainui = 0 }
        /# MainUI launch/ && inserted == 0 {
            print "    # MainUI launch"
            print "    cd $miyoodir/app"
            print "    # ONYX launcher hook"
            print "    if [ -f \"$sysdir/config/.useOnyxLauncher\" ] && [ -x \"$sysdir/bin/onyxLauncher\" ]; then"
            print "        PATH=\"$miyoodir/app:$PATH\" \\"
            print "            LD_LIBRARY_PATH=\"$miyoodir/lib:/config/lib:/lib\" \\"
            print "            LD_PRELOAD=\"$miyoodir/lib/libpadsp.so\" \\"
            print "            \"$sysdir/bin/onyxLauncher\" 2>&1 > /dev/null"
            print "    else"
            print "        PATH=\"$miyoodir/app:$PATH\" \\"
            print "            LD_LIBRARY_PATH=\"$miyoodir/lib:/config/lib:/lib\" \\"
            print "            LD_PRELOAD=\"$miyoodir/lib/libpadsp.so\" \\"
            print "            ./MainUI 2>&1 > /dev/null"
            print "    fi"
            in_mainui = 1
            saw_mainui = 0
            inserted = 1
            next
        }
        in_mainui == 1 {
            if ($0 ~ /[.][/]MainUI 2>&1 > [/]dev[/]null/) {
                saw_mainui = 1
                next
            }
            if (saw_mainui == 1) {
                if ($0 ~ /^[[:space:]]*fi[[:space:]]*$/) {
                    in_mainui = 0
                    next
                }
                in_mainui = 0
            }
            if (in_mainui == 0)
                print
            next
        }
        { print }
        END { if (!inserted) exit 2 }
    ' "$runtime" > "$tmp" && mv "$tmp" "$runtime"
    chmod +x "$runtime"
}

restore_runtime() {
    rm -f "$flag"
    if [ -f "$backup" ]; then
        cp "$backup" "$runtime"
        chmod +x "$runtime"
    fi
}

cmd="${1:-on}"

case "$cmd" in
    off|disable)
        rm -f "$flag"
        ;;
    restore|uninstall)
        restore_runtime
        ;;
    *)
        patch_runtime || exit 1
        mkdir -p "$sysdir/config"
        touch "$flag"
        ;;
esac

sync
