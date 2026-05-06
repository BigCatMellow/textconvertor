# Onyx Launcher Builder

Scratch GitHub Actions builder for a prototype Onion OS custom launcher.

The workflow clones `OnionUI/Onion` at `v4.3.1-1`, overlays `src/onyxLauncher`, patches the Onion build/runtime hooks, builds `onyxLauncher` inside the Miyoo Mini toolchain Docker image, and uploads an installable artifact.

## Build

1. Open the **Actions** tab.
2. Select **Build Onyx Launcher**.
3. Click **Run workflow**.
4. Download the `onyx-launcher-package` artifact after the run finishes.

## Install On Device

Copy the artifact contents onto the SD card so the files land at:

- `/mnt/SDCARD/.tmp_update/bin/onyxLauncher`
- `/mnt/SDCARD/.tmp_update/script/onyx_launcher.sh`
- `/mnt/SDCARD/.tmp_update/res/onyx/icons/*`
- `/mnt/SDCARD/.tmp_update/res/bootScreen.png`
- `/mnt/SDCARD/miyoo/app/SairaSemiCondensed-Medium.ttf`

The artifact also includes `preview/onyx-launcher-type-lab.html` with local `Icons` and `Fonts` folders so the current UI can be previewed offline.

Enable it on the device with:

```sh
/mnt/SDCARD/.tmp_update/script/onyx_launcher.sh
```

Disable it with:

```sh
/mnt/SDCARD/.tmp_update/script/onyx_launcher.sh off
```
