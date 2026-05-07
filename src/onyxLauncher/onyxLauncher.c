#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>
#include <dirent.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "system/keymap_sw.h"

#define SCREEN_W 640
#define SCREEN_H 480
#define ICON_SIZE 38
#define PANEL_X 0
#define PANEL_Y 24
#define PANEL_W 640
#define PANEL_H 392
#define ITEM_H 73
#define ITEM_GAP 0
#define LIST_TOP 81
#define VISIBLE_ROWS 4
#define MAX_PAGE_ITEMS 64
#define SYS_DIR "/mnt/SDCARD/.tmp_update"
#define MIYOO_APP_DIR "/mnt/SDCARD/miyoo/app"
#define ICON_DIR SYS_DIR "/res/onyx/icons"
#define ROMS_DIR "/mnt/SDCARD/Roms"
#define APPS_DIR "/mnt/SDCARD/App"
#define EMU_DIR "/mnt/SDCARD/Emu"

#define ACTION_NONE -1
#define ACTION_HOME_FAVORITES -10
#define ACTION_HOME_GAMES -11
#define ACTION_HOME_APPS -12
#define ACTION_HOME_SETTINGS -13
#define ACTION_OPEN_SYSTEM -20
#define ACTION_LAUNCH_ROM -21
#define ACTION_LAUNCH_APP -22

typedef enum {
    VIEW_HOME,
    VIEW_FAVORITES,
    VIEW_GAMES,
    VIEW_APPS,
    VIEW_SETTINGS,
    VIEW_SYSTEM_ROMS,
} ViewMode;

typedef struct {
    char title[96];
    char subtitle[128];
    char target[256];
    char aux[128];
    const char *icon;
    int action;
} PageItem;

static bool quit = false;
static ViewMode currentView = VIEW_HOME;
static PageItem pageItems[MAX_PAGE_ITEMS];
static int pageItemCount = 0;
static int selected = 0;
static int scrollOffset = 0;
static char selectedSystem[64] = "";
static char selectedSystemLabel[96] = "";
static char selectedSystemExts[128] = "";
static char pendingCommand[760] = "";

static void sigHandler(int sig)
{
    (void)sig;
    quit = true;
}

static SDL_Color rgb(Uint8 r, Uint8 g, Uint8 b)
{
    SDL_Color color = {r, g, b, 255};
    return color;
}

static void fill(SDL_Surface *screen, int x, int y, int w, int h, SDL_Color color)
{
    SDL_Rect rect = {x, y, w, h};
    SDL_FillRect(screen, &rect, SDL_MapRGB(screen->format, color.r, color.g, color.b));
}

static int rx(int x, int w)
{
    return SCREEN_W - x - w;
}

static int ry(int y, int h)
{
    return SCREEN_H - y - h;
}

static void fillRot(SDL_Surface *screen, int x, int y, int w, int h, SDL_Color color)
{
    fill(screen, rx(x, w), ry(y, h), w, h, color);
}

static void border(SDL_Surface *screen, int x, int y, int w, int h, SDL_Color color)
{
    fillRot(screen, x, y, w, 2, color);
    fillRot(screen, x, y + h - 2, w, 2, color);
    fillRot(screen, x, y, 2, h, color);
    fillRot(screen, x + w - 2, y, 2, h, color);
}

static int scaleUnit(int origin, int size, int value)
{
    return origin + value * size / 24;
}

static void glyphLine(SDL_Surface *screen, int x1, int y1, int x2, int y2, SDL_Color color)
{
    int dx = abs(x2 - x1);
    int sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1);
    int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        fillRot(screen, x1, y1, 2, 2, color);
        if (x1 == x2 && y1 == y2)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

static void glyphRect(SDL_Surface *screen, int x, int y, int w, int h, SDL_Color color)
{
    glyphLine(screen, x, y, x + w, y, color);
    glyphLine(screen, x + w, y, x + w, y + h, color);
    glyphLine(screen, x + w, y + h, x, y + h, color);
    glyphLine(screen, x, y + h, x, y, color);
}

static void glyphCircle(SDL_Surface *screen, int cx, int cy, int r, SDL_Color color)
{
    int x = r;
    int y = 0;
    int err = 0;

    while (x >= y) {
        fillRot(screen, cx + x, cy + y, 2, 2, color);
        fillRot(screen, cx + y, cy + x, 2, 2, color);
        fillRot(screen, cx - y, cy + x, 2, 2, color);
        fillRot(screen, cx - x, cy + y, 2, 2, color);
        fillRot(screen, cx - x, cy - y, 2, 2, color);
        fillRot(screen, cx - y, cy - x, 2, 2, color);
        fillRot(screen, cx + y, cy - x, 2, 2, color);
        fillRot(screen, cx + x, cy - y, 2, 2, color);

        if (err <= 0) {
            y++;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}

static void glyphFillCircle(SDL_Surface *screen, int cx, int cy, int r, SDL_Color color)
{
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r)
                fillRot(screen, cx + x, cy + y, 2, 2, color);
        }
    }
}

static Uint32 getPixel(SDL_Surface *surface, int x, int y)
{
    int bpp = surface->format->BytesPerPixel;
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch (bpp) {
    case 1:
        return *p;
    case 2:
        return *(Uint16 *)p;
    case 3:
        if (SDL_BYTEORDER == SDL_BIG_ENDIAN)
            return p[0] << 16 | p[1] << 8 | p[2];
        return p[0] | p[1] << 8 | p[2] << 16;
    default:
        return *(Uint32 *)p;
    }
}

static void putPixel(SDL_Surface *surface, int x, int y, Uint32 pixel)
{
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * 4;
    *(Uint32 *)p = pixel;
}

static SDL_Surface *rotate180(SDL_Surface *surface)
{
    SDL_Surface *rotated = SDL_CreateRGBSurface(SDL_SWSURFACE, surface->w, surface->h,
                                                32, 0x00ff0000, 0x0000ff00,
                                                0x000000ff, 0xff000000);
    if (!rotated)
        return NULL;

    Uint32 transparent = SDL_MapRGBA(rotated->format, 0, 0, 0, 0);
    SDL_FillRect(rotated, NULL, transparent);
    SDL_SetColorKey(rotated, SDL_SRCCOLORKEY, transparent);

    SDL_LockSurface(surface);
    SDL_LockSurface(rotated);
    for (int py = 0; py < surface->h; py++) {
        for (int px = 0; px < surface->w; px++) {
            Uint8 r, g, b, a;
            SDL_GetRGBA(getPixel(surface, px, py), surface->format, &r, &g, &b, &a);
            putPixel(rotated, surface->w - 1 - px, surface->h - 1 - py,
                     SDL_MapRGBA(rotated->format, r, g, b, a));
        }
    }
    SDL_UnlockSurface(rotated);
    SDL_UnlockSurface(surface);

    return rotated;
}

static void blitRot(SDL_Surface *screen, SDL_Surface *surface, int x, int y)
{
    if (!surface)
        return;

    SDL_Surface *rotated = rotate180(surface);
    if (!rotated)
        return;

    SDL_Rect rect = {rx(x, surface->w), ry(y, surface->h), 0, 0};
    SDL_BlitSurface(rotated, NULL, screen, &rect);
    SDL_FreeSurface(rotated);
}

static void text(SDL_Surface *screen, TTF_Font *font, const char *value, int x, int y,
                 SDL_Color color)
{
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, value, color);
    if (!surface)
        return;

    blitRot(screen, surface, x, y);
    SDL_FreeSurface(surface);
}

static SDL_Surface *scaleIcon(SDL_Surface *src, int size)
{
    SDL_Surface *scaled = SDL_CreateRGBSurface(SDL_SWSURFACE, size, size, 32,
                                               0x00ff0000, 0x0000ff00,
                                               0x000000ff, 0xff000000);
    if (!scaled)
        return NULL;

    Uint32 transparent = SDL_MapRGBA(scaled->format, 0, 0, 0, 0);
    SDL_FillRect(scaled, NULL, transparent);
    SDL_SetColorKey(scaled, SDL_SRCCOLORKEY, transparent);

    SDL_LockSurface(src);
    SDL_LockSurface(scaled);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int sx = x * src->w / size;
            int sy = y * src->h / size;
            Uint8 r, g, b, a;
            SDL_GetRGBA(getPixel(src, sx, sy), src->format, &r, &g, &b, &a);
            putPixel(scaled, x, y, SDL_MapRGBA(scaled->format, r, g, b, a));
        }
    }
    SDL_UnlockSurface(scaled);
    SDL_UnlockSurface(src);

    return scaled;
}

static void icon(SDL_Surface *screen, const char *file, int x, int y, int size)
{
    char path[256];
    snprintf(path, sizeof(path), ICON_DIR "/%s", file);

    SDL_Surface *loaded = IMG_Load(path);
    if (!loaded)
        return;

    SDL_Surface *scaled = scaleIcon(loaded, size);
    SDL_FreeSurface(loaded);

    if (!scaled)
        return;

    blitRot(screen, scaled, x, y);
    SDL_FreeSurface(scaled);
}

static void image(SDL_Surface *screen, const char *file, int x, int y)
{
    char path[256];
    snprintf(path, sizeof(path), ICON_DIR "/%s", file);

    SDL_Surface *loaded = IMG_Load(path);
    if (!loaded)
        return;

    blitRot(screen, loaded, x, y);
    SDL_FreeSurface(loaded);
}

static void drawGlyphIcon(SDL_Surface *screen, const char *name, int x, int y, int size,
                          SDL_Color color)
{
    int x0 = x;
    int y0 = y;
#define SX(v) scaleUnit(x0, size, (v))
#define SY(v) scaleUnit(y0, size, (v))

    if (strcmp(name, "star") == 0) {
        glyphLine(screen, SX(12), SY(3), SX(15), SY(9), color);
        glyphLine(screen, SX(15), SY(9), SX(22), SY(9), color);
        glyphLine(screen, SX(22), SY(9), SX(17), SY(14), color);
        glyphLine(screen, SX(17), SY(14), SX(19), SY(21), color);
        glyphLine(screen, SX(19), SY(21), SX(12), SY(17), color);
        glyphLine(screen, SX(12), SY(17), SX(5), SY(21), color);
        glyphLine(screen, SX(5), SY(21), SX(7), SY(14), color);
        glyphLine(screen, SX(7), SY(14), SX(2), SY(9), color);
        glyphLine(screen, SX(2), SY(9), SX(9), SY(9), color);
        glyphLine(screen, SX(9), SY(9), SX(12), SY(3), color);
    }
    else if (strcmp(name, "gamepad") == 0) {
        glyphRect(screen, SX(4), SY(7), SX(16) - SX(4), SY(10) - SY(7), color);
        glyphLine(screen, SX(7), SY(12), SX(11), SY(12), color);
        glyphLine(screen, SX(9), SY(10), SX(9), SY(14), color);
        glyphFillCircle(screen, SX(16), SY(12), 2, color);
        glyphFillCircle(screen, SX(19), SY(14), 2, color);
        glyphLine(screen, SX(4), SY(7), SX(2), SY(13), color);
        glyphLine(screen, SX(20), SY(7), SX(22), SY(13), color);
        glyphLine(screen, SX(2), SY(13), SX(4), SY(17), color);
        glyphLine(screen, SX(22), SY(13), SX(20), SY(17), color);
        glyphLine(screen, SX(4), SY(17), SX(9), SY(15), color);
        glyphLine(screen, SX(20), SY(17), SX(15), SY(15), color);
    }
    else if (strcmp(name, "cartridge") == 0) {
        glyphRect(screen, SX(5), SY(4), SX(19) - SX(5), SY(20) - SY(4), color);
        glyphLine(screen, SX(16), SY(4), SX(19), SY(7), color);
        glyphLine(screen, SX(8), SY(8), SX(16), SY(8), color);
        glyphLine(screen, SX(8), SY(12), SX(14), SY(12), color);
    }
    else if (strcmp(name, "arcade") == 0) {
        glyphRect(screen, SX(5), SY(3), SX(19) - SX(5), SY(21) - SY(3), color);
        glyphRect(screen, SX(7), SY(7), SX(17) - SX(7), SY(12) - SY(7), color);
        glyphLine(screen, SX(9), SY(16), SX(15), SY(16), color);
        glyphFillCircle(screen, SX(12), SY(19), 2, color);
    }
    else if (strcmp(name, "console") == 0) {
        glyphRect(screen, SX(3), SY(7), SX(21) - SX(3), SY(18) - SY(7), color);
        glyphLine(screen, SX(7), SY(12), SX(9), SY(12), color);
        glyphLine(screen, SX(16), SY(10), SX(16), SY(14), color);
        glyphLine(screen, SX(14), SY(12), SX(18), SY(12), color);
    }
    else if (strcmp(name, "handheld") == 0) {
        glyphRect(screen, SX(4), SY(3), SX(20) - SX(4), SY(21) - SY(3), color);
        glyphRect(screen, SX(7), SY(6), SX(17) - SX(7), SY(13) - SY(6), color);
        glyphFillCircle(screen, SX(9), SY(17), 2, color);
        glyphFillCircle(screen, SX(15), SY(17), 2, color);
    }
    else if (strcmp(name, "retroarch") == 0) {
        glyphCircle(screen, SX(12), SY(12), size * 9 / 24, color);
        glyphLine(screen, SX(12), SY(7), SX(12), SY(12), color);
        glyphLine(screen, SX(12), SY(12), SX(15), SY(14), color);
    }
    else if (strcmp(name, "port") == 0) {
        glyphLine(screen, SX(4), SY(6), SX(16), SY(6), color);
        glyphLine(screen, SX(16), SY(6), SX(20), SY(10), color);
        glyphLine(screen, SX(20), SY(10), SX(20), SY(18), color);
        glyphLine(screen, SX(20), SY(18), SX(4), SY(18), color);
        glyphLine(screen, SX(4), SY(18), SX(4), SY(6), color);
        glyphLine(screen, SX(16), SY(6), SX(16), SY(10), color);
        glyphLine(screen, SX(16), SY(10), SX(20), SY(10), color);
        glyphLine(screen, SX(8), SY(14), SX(14), SY(14), color);
    }
    else if (strcmp(name, "music") == 0) {
        glyphLine(screen, SX(9), SY(18), SX(9), SY(5), color);
        glyphLine(screen, SX(9), SY(5), SX(20), SY(3), color);
        glyphLine(screen, SX(20), SY(3), SX(20), SY(16), color);
        glyphCircle(screen, SX(6), SY(18), size * 3 / 24, color);
        glyphCircle(screen, SX(17), SY(16), size * 3 / 24, color);
    }
    else if (strcmp(name, "wifi") == 0) {
        glyphLine(screen, SX(2), SY(9), SX(7), SY(6), color);
        glyphLine(screen, SX(7), SY(6), SX(12), SY(5), color);
        glyphLine(screen, SX(12), SY(5), SX(17), SY(6), color);
        glyphLine(screen, SX(17), SY(6), SX(22), SY(9), color);
        glyphLine(screen, SX(5), SY(13), SX(9), SY(11), color);
        glyphLine(screen, SX(9), SY(11), SX(12), SY(10), color);
        glyphLine(screen, SX(12), SY(10), SX(15), SY(11), color);
        glyphLine(screen, SX(15), SY(11), SX(19), SY(13), color);
        glyphLine(screen, SX(8), SY(17), SX(12), SY(15), color);
        glyphLine(screen, SX(12), SY(15), SX(16), SY(17), color);
        glyphFillCircle(screen, SX(12), SY(20), 2, color);
    }
    else if (strcmp(name, "display") == 0) {
        glyphRect(screen, SX(3), SY(4), SX(21) - SX(3), SY(16) - SY(4), color);
        glyphLine(screen, SX(12), SY(16), SX(12), SY(20), color);
        glyphLine(screen, SX(8), SY(20), SX(16), SY(20), color);
    }
    else if (strcmp(name, "sound") == 0) {
        glyphLine(screen, SX(3), SY(10), SX(6), SY(10), color);
        glyphLine(screen, SX(6), SY(10), SX(11), SY(6), color);
        glyphLine(screen, SX(11), SY(6), SX(11), SY(18), color);
        glyphLine(screen, SX(11), SY(18), SX(6), SY(14), color);
        glyphLine(screen, SX(6), SY(14), SX(3), SY(14), color);
        glyphLine(screen, SX(16), SY(8), SX(18), SY(12), color);
        glyphLine(screen, SX(18), SY(12), SX(16), SY(16), color);
        glyphLine(screen, SX(19), SY(5), SX(22), SY(12), color);
        glyphLine(screen, SX(22), SY(12), SX(19), SY(19), color);
    }
    else if (strcmp(name, "info") == 0) {
        glyphCircle(screen, SX(12), SY(12), size * 9 / 24, color);
        glyphLine(screen, SX(12), SY(11), SX(12), SY(16), color);
        glyphFillCircle(screen, SX(12), SY(8), 2, color);
    }
    else {
        glyphLine(screen, SX(3), SY(7), SX(9), SY(7), color);
        glyphLine(screen, SX(9), SY(7), SX(11), SY(9), color);
        glyphLine(screen, SX(11), SY(9), SX(21), SY(9), color);
        glyphLine(screen, SX(21), SY(9), SX(21), SY(19), color);
        glyphLine(screen, SX(21), SY(19), SX(3), SY(19), color);
        glyphLine(screen, SX(3), SY(19), SX(3), SY(7), color);
    }

#undef SX
#undef SY
}

static void addPageItem(const char *title, const char *subtitle, const char *iconFile,
                        int action, const char *target, const char *aux)
{
    if (pageItemCount >= MAX_PAGE_ITEMS)
        return;

    snprintf(pageItems[pageItemCount].title, sizeof(pageItems[pageItemCount].title),
             "%s", title);
    snprintf(pageItems[pageItemCount].subtitle, sizeof(pageItems[pageItemCount].subtitle),
             "%s", subtitle ? subtitle : "");
    snprintf(pageItems[pageItemCount].target, sizeof(pageItems[pageItemCount].target),
             "%s", target ? target : "");
    snprintf(pageItems[pageItemCount].aux, sizeof(pageItems[pageItemCount].aux),
             "%s", aux ? aux : "");
    pageItems[pageItemCount].icon = iconFile;
    pageItems[pageItemCount].action = action;
    pageItemCount++;
}

static void configValue(const char *path, const char *key, char *out, size_t outSize)
{
    FILE *file = fopen(path, "r");
    out[0] = '\0';
    if (!file)
        return;

    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char *found = strstr(line, needle);
        if (!found)
            continue;

        char *colon = strchr(found, ':');
        char *start = colon ? strchr(colon, '"') : NULL;
        char *end = start ? strchr(start + 1, '"') : NULL;
        if (start && end && end > start) {
            size_t length = (size_t)(end - start - 1);
            if (length >= outSize)
                length = outSize - 1;
            memcpy(out, start + 1, length);
            out[length] = '\0';
            break;
        }
    }

    fclose(file);
}

static bool isDirectoryPath(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool hasExtension(const char *file, const char *extList)
{
    const char *dot = strrchr(file, '.');
    if (!dot || !extList[0])
        return false;

    char ext[32];
    snprintf(ext, sizeof(ext), "%s", dot + 1);
    for (char *p = ext; *p; p++) {
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p - 'A' + 'a');
    }

    char list[160];
    snprintf(list, sizeof(list), "|%s|", extList);

    char needle[40];
    snprintf(needle, sizeof(needle), "|%s|", ext);
    return strstr(list, needle) != NULL;
}

static int countRomsForSystem(const char *systemName, const char *extList)
{
    int count = 0;
    char romDir[256];
    snprintf(romDir, sizeof(romDir), ROMS_DIR "/%s", systemName);

    DIR *dir = opendir(romDir);
    if (!dir)
        return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.' && hasExtension(entry->d_name, extList))
            count++;
    }

    closedir(dir);
    return count;
}

static void displayNameFromFile(const char *fileName, char *out, size_t outSize)
{
    snprintf(out, outSize, "%s", fileName);

    char *dot = strrchr(out, '.');
    if (dot)
        *dot = '\0';
}

static void shellQuote(const char *value, char *out, size_t outSize)
{
    size_t pos = 0;
    if (outSize == 0)
        return;

    out[pos++] = '\'';
    for (const char *p = value; *p && pos + 5 < outSize; p++) {
        if (*p == '\'') {
            memcpy(out + pos, "'\\''", 4);
            pos += 4;
        }
        else {
            out[pos++] = *p;
        }
    }
    if (pos + 1 < outSize)
        out[pos++] = '\'';
    out[pos] = '\0';
}

static void addSystemsFromSd(void)
{
    DIR *dir = opendir(ROMS_DIR);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        char path[256];
        char configPath[256];
        char label[96];
        char extList[128];
        char subtitle[64];
        int romCount;
        snprintf(path, sizeof(path), ROMS_DIR "/%s", entry->d_name);
        snprintf(configPath, sizeof(configPath), EMU_DIR "/%s/config.json", entry->d_name);
        if (!isDirectoryPath(path) || access(configPath, R_OK) != 0)
            continue;

        configValue(configPath, "label", label, sizeof(label));
        configValue(configPath, "extlist", extList, sizeof(extList));
        romCount = countRomsForSystem(entry->d_name, extList);
        snprintf(subtitle, sizeof(subtitle), "%d %s", romCount, romCount == 1 ? "game" : "games");
        const char *glyph = "html-games.png";
        addPageItem(label[0] ? label : entry->d_name, subtitle, glyph,
                    ACTION_OPEN_SYSTEM, entry->d_name, extList);
    }

    closedir(dir);
}

static void addAppsFromSd(void)
{
    DIR *dir = opendir(APPS_DIR);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        char appDir[256];
        char configPath[256];
        char launchPath[256];
        char label[96];
        char description[128];
        snprintf(appDir, sizeof(appDir), APPS_DIR "/%s", entry->d_name);
        snprintf(configPath, sizeof(configPath), "%s/config.json", appDir);
        snprintf(launchPath, sizeof(launchPath), "%s/launch.sh", appDir);
        if (!isDirectoryPath(appDir) || access(launchPath, R_OK) != 0)
            continue;

        configValue(configPath, "label", label, sizeof(label));
        configValue(configPath, "description", description, sizeof(description));
        const char *glyph = "html-apps.png";
        addPageItem(label[0] ? label : entry->d_name,
                    description[0] ? description : "App",
                    glyph, ACTION_LAUNCH_APP, launchPath, NULL);
    }

    closedir(dir);
}

static void addRomsForSystem(const char *systemName, const char *extList)
{
    char romDir[256];
    snprintf(romDir, sizeof(romDir), ROMS_DIR "/%s", systemName);

    DIR *dir = opendir(romDir);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' || !hasExtension(entry->d_name, extList))
            continue;

        char romPath[256];
        char displayName[96];
        snprintf(romPath, sizeof(romPath), "%s/%s", romDir, entry->d_name);
        displayNameFromFile(entry->d_name, displayName, sizeof(displayName));
        addPageItem(displayName, selectedSystemLabel, "html-games.png", ACTION_LAUNCH_ROM,
                    romPath, systemName);
    }

    closedir(dir);
}

static void loadPage(ViewMode view)
{
    currentView = view;
    pageItemCount = 0;
    selected = 0;
    scrollOffset = 0;

    if (view == VIEW_HOME) {
        addPageItem("Favorites", NULL, "html-favorites.png", ACTION_HOME_FAVORITES, NULL, NULL);
        addPageItem("Games", NULL, "html-games.png", ACTION_HOME_GAMES, NULL, NULL);
        addPageItem("Apps", NULL, "html-apps.png", ACTION_HOME_APPS, NULL, NULL);
        addPageItem("Settings", NULL, "html-settings.png", ACTION_HOME_SETTINGS, NULL, NULL);
        return;
    }

    if (view == VIEW_FAVORITES) {
        addPageItem("Open Onion Favorites", "Stock favorites list", "html-favorites.png", 2, NULL, NULL);
        addPageItem("Pinned Games", "ONYX favorites coming next", "html-favorites.png", ACTION_NONE, NULL, NULL);
        addPageItem("Collections", "Custom groups", "html-favorites.png", ACTION_NONE, NULL, NULL);
        return;
    }

    if (view == VIEW_GAMES) {
        addPageItem("Open Onion Games", "Stock system browser", "html-games.png", 3, NULL, NULL);
        addSystemsFromSd();
        if (pageItemCount == 1)
            addPageItem("No systems found", "Check /Roms and /Emu", "html-games.png", ACTION_NONE, NULL, NULL);
        return;
    }

    if (view == VIEW_SYSTEM_ROMS) {
        addRomsForSystem(selectedSystem, selectedSystemExts);
        if (pageItemCount == 0)
            addPageItem("No games found", selectedSystemLabel, "html-games.png", ACTION_NONE, NULL, NULL);
        return;
    }

    if (view == VIEW_APPS) {
        addPageItem("Open Onion Apps", "Stock app list", "html-apps.png", 5, NULL, NULL);
        addAppsFromSd();
        if (pageItemCount == 1)
            addPageItem("No apps found", "Check /App", "html-apps.png", ACTION_NONE, NULL, NULL);
        return;
    }

    addPageItem("Open Onion Settings", "Stock settings menu", "html-settings.png", 0, NULL, NULL);
    addPageItem("Interface", "Theme and launcher behavior", "html-settings.png", ACTION_NONE, NULL, NULL);
    addPageItem("System", "Runtime and device tools", "html-settings.png", ACTION_NONE, NULL, NULL);
    addPageItem("About ONYX", "Custom launcher preview", "html-settings.png", ACTION_NONE, NULL, NULL);
}

static void moveSelection(int delta)
{
    if (pageItemCount <= 0)
        return;

    selected += delta;
    if (selected < 0)
        selected = pageItemCount - 1;
    else if (selected >= pageItemCount)
        selected = 0;

    if (selected < scrollOffset)
        scrollOffset = selected;
    else if (selected >= scrollOffset + VISIBLE_ROWS)
        scrollOffset = selected - VISIBLE_ROWS + 1;
}

static TTF_Font *openFont(int size)
{
    const char *paths[] = {
        MIYOO_APP_DIR "/SairaSemiCondensed-Medium.ttf",
        MIYOO_APP_DIR "/Exo-2-Bold-Italic.ttf",
        MIYOO_APP_DIR "/Helvetica-Neue-2.ttf",
        SYS_DIR "/res/Arkhip_font.ttf",
        NULL,
    };

    for (int i = 0; paths[i]; i++) {
        TTF_Font *font = TTF_OpenFont(paths[i], size);
        if (font)
            return font;
    }

    return NULL;
}

static void draw(SDL_Surface *screen, TTF_Font *fontFooter, TTF_Font *fontBrand,
                 TTF_Font *fontTitle, TTF_Font *fontRowTitle, TTF_Font *fontSubtitle)
{
    (void)fontBrand;

    SDL_Color bg = rgb(8, 11, 18);
    SDL_Color panel = rgb(16, 20, 27);
    SDL_Color panelDark = rgb(16, 20, 27);
    SDL_Color line = rgb(38, 43, 49);
    SDL_Color textMain = rgb(225, 229, 236);
    SDL_Color textDim = rgb(137, 141, 147);
    SDL_Color teal = rgb(29, 50, 86);
    SDL_Color blue = rgb(29, 50, 86);

    fill(screen, 0, 0, SCREEN_W, SCREEN_H, bg);
    fillRot(screen, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, panelDark);
    border(screen, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, line);

    image(screen, "onyx-logo-header.png", 56, 40);
    text(screen, fontFooter, "10:30", 312, 40, textMain);
    text(screen, fontFooter, "83%", 520, 40, textMain);
    border(screen, 574, 42, 22, 12, textDim);

    for (int row = 0; row < VISIBLE_ROWS; row++) {
        int itemIndex = scrollOffset + row;
        if (itemIndex >= pageItemCount)
            break;

        int y = LIST_TOP + row * (ITEM_H + ITEM_GAP);
        bool active = itemIndex == selected;

        fillRot(screen, 0, y, 640, ITEM_H, active ? blue : panel);
        border(screen, 0, y, 640, ITEM_H, active ? teal : bg);
        icon(screen, pageItems[itemIndex].icon, 28, y + 16, ICON_SIZE);
        if (pageItems[itemIndex].subtitle[0]) {
            text(screen, fontRowTitle, pageItems[itemIndex].title, 86, y + 8, textMain);
            text(screen, fontSubtitle, pageItems[itemIndex].subtitle, 86, y + 43,
                 active ? rgb(202, 209, 220) : textDim);
        }
        else {
            text(screen, fontTitle, pageItems[itemIndex].title, 86, y + 17, textMain);
        }
        icon(screen, "html-chev.png", 574, y + 16, ICON_SIZE);
    }

    fillRot(screen, 320, 374, 2, 26, line);
    icon(screen, "html-btn-a.png", 66, 375, 30);
    text(screen, fontFooter, "Select", 102, 379, textDim);
    icon(screen, "html-btn-b.png", 480, 375, 30);
    text(screen, fontFooter, "Back", 516, 379, textDim);

    SDL_Flip(screen);
}

static void handoffToRuntime(int state)
{
    char command[80];
    snprintf(command, sizeof(command), SYS_DIR "/bin/setState %d", state);
    system(command);
    quit = true;
}

static void runShellCommand(const char *command)
{
    snprintf(pendingCommand, sizeof(pendingCommand), "%s", command);
    quit = true;
}

static void launchRom(const char *systemName, const char *romPath)
{
    char launchPath[256];
    char quotedLaunch[320];
    char quotedRom[320];
    char command[760];

    snprintf(launchPath, sizeof(launchPath), EMU_DIR "/%s/launch.sh", systemName);
    shellQuote(launchPath, quotedLaunch, sizeof(quotedLaunch));
    shellQuote(romPath, quotedRom, sizeof(quotedRom));
    snprintf(command, sizeof(command), "%s %s", quotedLaunch, quotedRom);
    runShellCommand(command);
}

static void launchApp(const char *launchPath)
{
    char appDir[256];
    char quotedLaunch[320];
    char quotedDir[320];
    char command[760];
    char *slash;

    snprintf(appDir, sizeof(appDir), "%s", launchPath);
    slash = strrchr(appDir, '/');
    if (slash)
        *slash = '\0';

    shellQuote(appDir, quotedDir, sizeof(quotedDir));
    shellQuote(launchPath, quotedLaunch, sizeof(quotedLaunch));
    snprintf(command, sizeof(command), "cd %s && %s", quotedDir, quotedLaunch);
    runShellCommand(command);
}

static void activateSelection(void)
{
    if (pageItemCount <= 0)
        return;

    int action = pageItems[selected].action;
    if (action == ACTION_HOME_FAVORITES)
        loadPage(VIEW_FAVORITES);
    else if (action == ACTION_HOME_GAMES)
        loadPage(VIEW_GAMES);
    else if (action == ACTION_HOME_APPS)
        loadPage(VIEW_APPS);
    else if (action == ACTION_HOME_SETTINGS)
        loadPage(VIEW_SETTINGS);
    else if (action == ACTION_OPEN_SYSTEM) {
        snprintf(selectedSystem, sizeof(selectedSystem), "%s", pageItems[selected].target);
        snprintf(selectedSystemLabel, sizeof(selectedSystemLabel), "%s", pageItems[selected].title);
        snprintf(selectedSystemExts, sizeof(selectedSystemExts), "%s", pageItems[selected].aux);
        loadPage(VIEW_SYSTEM_ROMS);
    }
    else if (action == ACTION_LAUNCH_ROM)
        launchRom(pageItems[selected].aux, pageItems[selected].target);
    else if (action == ACTION_LAUNCH_APP)
        launchApp(pageItems[selected].target);
    else if (action >= 0)
        handoffToRuntime(action);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return 1;
    if (TTF_Init() != 0) {
        SDL_Quit();
        return 1;
    }

    SDL_Surface *screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 32, SDL_SWSURFACE);
    TTF_Font *fontFooter = openFont(20);
    TTF_Font *fontBrand = openFont(17);
    TTF_Font *fontTitle = openFont(30);
    TTF_Font *fontRowTitle = openFont(26);
    TTF_Font *fontSubtitle = openFont(16);

    if (!screen || !fontFooter || !fontBrand || !fontTitle || !fontRowTitle || !fontSubtitle) {
        if (fontFooter)
            TTF_CloseFont(fontFooter);
        if (fontBrand)
            TTF_CloseFont(fontBrand);
        if (fontTitle)
            TTF_CloseFont(fontTitle);
        if (fontRowTitle)
            TTF_CloseFont(fontRowTitle);
        if (fontSubtitle)
            TTF_CloseFont(fontSubtitle);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    loadPage(VIEW_HOME);
    draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);

    while (!quit) {
        SDL_Event event;
        if (!SDL_WaitEvent(&event))
            continue;

        if (event.type == SDL_QUIT)
            break;

        if (event.type != SDL_KEYUP)
            continue;

        SDLKey key = event.key.keysym.sym;
        if (key == SW_BTN_DOWN) {
            moveSelection(1);
            draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);
        }
        else if (key == SW_BTN_UP) {
            moveSelection(-1);
            draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);
        }
        else if (key == SW_BTN_A) {
            activateSelection();
            if (!quit)
                draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);
        }
        else if (key == SW_BTN_B || key == SW_BTN_MENU) {
            if (currentView == VIEW_HOME)
                handoffToRuntime(0);
            else if (currentView == VIEW_SYSTEM_ROMS) {
                loadPage(VIEW_GAMES);
                draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);
            }
            else {
                loadPage(VIEW_HOME);
                draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);
            }
        }
    }

    TTF_CloseFont(fontFooter);
    TTF_CloseFont(fontBrand);
    TTF_CloseFont(fontTitle);
    TTF_CloseFont(fontRowTitle);
    TTF_CloseFont(fontSubtitle);
    TTF_Quit();
    SDL_Quit();

    if (pendingCommand[0])
        system(pendingCommand);

    return 0;
}
