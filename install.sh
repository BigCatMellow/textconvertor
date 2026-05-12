#!/bin/sh
#
# ONYX Launcher install script
# Run from the package directory on the Miyoo Mini SD card,
# or pass the SD card mount point as the first argument.
#
# Usage:
#   ./install.sh              # assumes SD card at /mnt/SDCARD
#   ./install.sh /media/sd    # custom mount point
#   ./install.sh F:           # Windows drive letter (via MSYS/Git Bash)

set -e

sd="${1:-/mnt/SDCARD}"
sysdir="$sd/.tmp_update"
pkgdir="$(cd "$(dirname "$0")" && pwd)"

if [ ! -d "$sysdir" ]; then
    echo "ERROR: $sysdir not found. Is the SD card mounted at $sd?" >&2
    exit 1
fi

echo "Installing ONYX Launcher to $sd ..."

mkdir -p "$sysdir/bin"
mkdir -p "$sysdir/script"
mkdir -p "$sysdir/res/onyx/icons"
mkdir -p "$sysdir/res/onyx/sound"
mkdir -p "$sysdir/config"
mkdir -p "$sd/Saves/CurrentProfile/theme/skin/extra"
mkdir -p "$sd/miyoo/app"
mkdir -p "$sd/App/OnyxLauncher"

cp "$pkgdir/bin/onyxLauncher" "$sysdir/bin/onyxLauncher"
chmod +x "$sysdir/bin/onyxLauncher"

cp "$pkgdir/script/onyx_launcher.sh" "$sysdir/script/onyx_launcher.sh"
chmod +x "$sysdir/script/onyx_launcher.sh"

cp "$pkgdir/res/onyx/icons/"*.png "$sysdir/res/onyx/icons/"
cp "$pkgdir/res/onyx/sound/"*.wav "$sysdir/res/onyx/sound/" 2>/dev/null || true
cp "$pkgdir/res/bootScreen.png" "$sysdir/res/bootScreen.png"
cp "$pkgdir/res/bootScreen.png" "$sd/Saves/CurrentProfile/theme/skin/extra/bootScreen.png"

cp "$pkgdir/miyoo/app/SairaSemiCondensed-Medium.ttf" "$sd/miyoo/app/SairaSemiCondensed-Medium.ttf"

cp "$pkgdir/App/OnyxLauncher/config.json" "$sd/App/OnyxLauncher/config.json"
cp "$pkgdir/App/OnyxLauncher/launch.sh" "$sd/App/OnyxLauncher/launch.sh"
chmod +x "$sd/App/OnyxLauncher/launch.sh"

echo "Patching runtime.sh and enabling ONYX ..."
"$sysdir/script/onyx_launcher.sh"

echo "Done. ONYX will launch on next boot."
echo "To disable: $sysdir/script/onyx_launcher.sh off"
