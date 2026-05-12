# ONYX v2 Layout Contract

All coordinates below are mockup-space coordinates for a 640x480 display. The C
renderer should convert them once at the drawing boundary if the framebuffer needs
rotation. Internal layout math should not mix rotated and unrotated values.

## Colors

- screen background: `#0e1114`
- status/menu surface: `#111418`
- row background: `#161c21`
- row border: `#0e1114`
- divider line: `#262b31`
- main text: `#eef2f6`
- muted text: `#9ca3ad`
- active fill/border: `#1c65db`
- accent/toggle on: `#dd7600`

## Screen Chrome

- full screen surface: `0,0,640,480`
- inner status/menu surface: `0,0,640,480`
- border: 1 to 2 px `#262b31`

## Header

- logo: `x=20, y=17`, visual size about `64x21`
- clock: centered near `x=320, y=15`
- battery percent: `x=520, y=15`
- battery outline: `x=574, y=18`, size about `22x12`

## List

- list top: `62`
- row height: `88`
- row gap: `0`
- row count visible on home pages: `4`
- row y positions: `62`, `150`, `238`, `326`
- row width: `640`
- row border: `2`
- row horizontal padding: `24`
- icon: `x=28`, centered vertically, size `38`
- title x: `86`
- title y, no subtitle: `rowY + 27`
- title y, with subtitle: `rowY + 18`
- subtitle y: `rowY + 52`
- chevron: `x=574`, centered vertically, size `38`

## Footer

- bottom action band: `x=0`, `y=416`, size `640x64`
- footer y: `432`
- footer height: `30`
- left action icon: `x=66`, `y=432`, size `30`
- left text: `x=102`, `y=431`
- center divider: `x=320`, `y=432`, size `2x26`
- favorite hint: `x=265`, `y=434` (visible on Favorites, ROM list, and Recents views)
- right action icon: `x=480`, `y=432`, size `30`
- right text: `x=516`, `y=431`

## Asset Names

The C renderer uses the package PNG names directly:

- home/page icons: `favorites-white.png`, `games-white.png`, `apps-white.png`, `settings-white.png`
- chevron: `square-rounded-arrow-right.png`
- footer buttons: `button-a.png`, `button-b.png`
- header logo: `onyx-logo-header.png`

## Views

- **Home**: Favorites, Games, Apps, Settings (4 static rows, no scrolling)
- **Recents**: populated from `recentlist.json` and `recentlist-hidden.json`; scrollable
- **Favorites**: populated from `onyx_favorites.tsv`, falls back to Onion `favourite.json`/`recentlist.json`
- **Games**: system list from `/Roms` + `/Emu` config; scrollable, sorted alphabetically
- **System ROMs**: ROM list for a selected system; scrollable, sorted alphabetically
- **Apps**: app list from `/App`; scrollable, sorted alphabetically
- **Settings**: Tweaks, UI Theme, Package Manager, Activity Tracker, Recents, Stock Onion, ONYX toggle, Version
- **Confirm Disable**: two-row confirmation before disabling ONYX

## MENU Behavior

The physical MENU button opens the ONYX Recents view (VIEW_RECENTS) from any page.
ONYX reads both SDL key events (SW_BTN_MENU) and raw hardware input (HW_BTN_MENU)
to catch MENU presses. The Recents view is also reachable from the Settings page.

The stock `gameSwitcher` binary is kept as a fallback but is no longer the default
path. If `keymon` intercepts MENU before ONYX sees it, the Settings > Recents row
provides an alternative entry point.
