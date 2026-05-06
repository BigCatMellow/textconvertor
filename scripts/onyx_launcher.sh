#!/bin/sh

sysdir=/mnt/SDCARD/.tmp_update

case "$1" in
    off|disable)
        rm -f "$sysdir/config/.useOnyxLauncher"
        ;;
    *)
        touch "$sysdir/config/.useOnyxLauncher"
        ;;
esac

sync
