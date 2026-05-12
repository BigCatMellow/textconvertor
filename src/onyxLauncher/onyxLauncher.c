#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_mixer.h>
#include <SDL/SDL_ttf.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "system/keymap_hw.h"
#include "system/keymap_sw.h"

#define SCREEN_W 640
#define SCREEN_H 480
#define ICON_SIZE 38
#define PANEL_X 0
#define PANEL_Y 0
#define PANEL_W 640
#define PANEL_H 480
#define ITEM_H 88
#define ITEM_GAP 0
#define LIST_TOP 62
#define VISIBLE_ROWS 4
#define MAX_HW_INPUTS 8
#define MAX_PAGE_ITEMS 64
#define ONYX_VERSION "0.4"
#define SYS_DIR "/mnt/SDCARD/.tmp_update"
#define MIYOO_APP_DIR "/mnt/SDCARD/miyoo/app"
#define ICON_DIR SYS_DIR "/res/onyx/icons"
#define SOUND_DIR SYS_DIR "/res/onyx/sound"
#define FAVORITES_FILE SYS_DIR "/config/onyx_favorites.tsv"
#define ROMS_DIR "/mnt/SDCARD/Roms"
#define ONION_FAVORITES_FILE ROMS_DIR "/favourite.json"
#define ONION_RECENTS_FILE ROMS_DIR "/recentlist.json"
#define ONION_RECENTS_HIDDEN_FILE ROMS_DIR "/recentlist-hidden.json"
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
#define ACTION_DISABLE_ONYX -23
#define ACTION_CONFIRM_DISABLE_ONYX -24
#define ACTION_CANCEL_CONFIRM -25
#define ACTION_GAME_SWITCHER -26
#define ACTION_STOCK_MAIN -27
#define ACTION_LAUNCH_COMMAND -28
#define ACTION_HOME_RECENTS -29

typedef enum {
    VIEW_HOME,
    VIEW_FAVORITES,
    VIEW_GAMES,
    VIEW_APPS,
    VIEW_SETTINGS,
    VIEW_SYSTEM_ROMS,
    VIEW_RECENTS,
    VIEW_CONFIRM_DISABLE,
} ViewMode;

typedef enum {
    ROW_DRILL,
    ROW_VALUE,
    ROW_TOGGLE,
    ROW_STATIC,
} RowKind;

typedef struct {
    char title[96];
    char subtitle[128];
    char value[64];
    char target[256];
    char aux[128];
    const char *icon;
    int action;
    RowKind kind;
    bool enabled;
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
static bool audioReady = false;
static Mix_Chunk *sndNav = NULL;
static Mix_Chunk *sndIntro = NULL;

static int compareText(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }

    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int comparePageItems(const void *left, const void *right)
{
    const PageItem *a = (const PageItem *)left;
    const PageItem *b = (const PageItem *)right;
    return compareText(a->title, b->title);
}

static void sortPageItems(void)
{
    if (pageItemCount > 1)
        qsort(pageItems, pageItemCount, sizeof(PageItem), comparePageItems);
}

static int batteryPercent(void)
{
    FILE *fp = fopen("/tmp/percBat", "r");
    int percent = -1;

    if (fp) {
        if (fscanf(fp, "%d", &percent) != 1)
            percent = -1;
        fclose(fp);
    }

    if (percent == 500)
        return 100;
    if (percent < 0 || percent > 100)
        return -1;
    return percent;
}

static void clockLabel(char *out, size_t outSize)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm)
        strftime(out, outSize, "%H:%M", tm);
    else
        snprintf(out, outSize, "--:--");
}

static void sigHandler(int sig)
{
    (void)sig;
    quit = true;
}

static void initAudio(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return;
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    audioReady = true;
    sndNav = Mix_LoadWAV(SOUND_DIR "/drips2.wav");
    sndIntro = Mix_LoadWAV(SOUND_DIR "/intro2.wav");
}

static void playNav(void)
{
    if (audioReady && sndNav)
        Mix_PlayChannel(-1, sndNav, 0);
}

static void playIntro(void)
{
    if (audioReady && sndIntro)
        Mix_PlayChannel(-1, sndIntro, 0);
}

static void closeAudio(void)
{
    if (sndNav) { Mix_FreeChunk(sndNav); sndNav = NULL; }
    if (sndIntro) { Mix_FreeChunk(sndIntro); sndIntro = NULL; }
    if (audioReady) { Mix_CloseAudio(); audioReady = false; }
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

static void truncateText(const char *value, char *out, size_t outSize, size_t maxChars)
{
    size_t length = strlen(value);
    if (outSize == 0)
        return;

    if (length <= maxChars || maxChars + 1 >= outSize) {
        snprintf(out, outSize, "%s", value);
        return;
    }

    if (maxChars < 4) {
        snprintf(out, outSize, "%.*s", (int)maxChars, value);
        return;
    }

    snprintf(out, outSize, "%.*s...", (int)(maxChars - 3), value);
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

static void addPageItemEx(const char *title, const char *subtitle, const char *iconFile,
                          int action, const char *target, const char *aux,
                          RowKind kind, const char *value, bool enabled)
{
    if (pageItemCount >= MAX_PAGE_ITEMS)
        return;

    snprintf(pageItems[pageItemCount].title, sizeof(pageItems[pageItemCount].title),
             "%s", title);
    snprintf(pageItems[pageItemCount].subtitle, sizeof(pageItems[pageItemCount].subtitle),
             "%s", subtitle ? subtitle : "");
    snprintf(pageItems[pageItemCount].value, sizeof(pageItems[pageItemCount].value),
             "%s", value ? value : "");
    snprintf(pageItems[pageItemCount].target, sizeof(pageItems[pageItemCount].target),
             "%s", target ? target : "");
    snprintf(pageItems[pageItemCount].aux, sizeof(pageItems[pageItemCount].aux),
             "%s", aux ? aux : "");
    pageItems[pageItemCount].icon = iconFile;
    pageItems[pageItemCount].action = action;
    pageItems[pageItemCount].kind = kind;
    pageItems[pageItemCount].enabled = enabled;
    pageItemCount++;
}

static void addPageItem(const char *title, const char *subtitle, const char *iconFile,
                        int action, const char *target, const char *aux)
{
    addPageItemEx(title, subtitle, iconFile, action, target, aux, ROW_DRILL, NULL, false);
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

static void jsonLineValue(const char *line, const char *key, char *out, size_t outSize)
{
    out[0] = '\0';
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *found = strstr(line, needle);
    if (!found)
        return;

    const char *colon = strchr(found, ':');
    const char *start = colon ? strchr(colon, '"') : NULL;
    if (!start)
        return;

    const char *p = start + 1;
    size_t pos = 0;
    while (*p && pos + 1 < outSize) {
        if (*p == '"' && p[-1] != '\\')
            break;
        out[pos++] = *p++;
    }
    out[pos] = '\0';
}

static void normalizeSdPath(const char *path, char *out, size_t outSize)
{
    snprintf(out, outSize, "%s", path);

    char *emuPrefix = strstr(out, "/mnt/SDCARD/Emu/");
    char *romsPart = strstr(out, "/../../Roms/");
    if (emuPrefix && romsPart) {
        char normalized[256];
        snprintf(normalized, sizeof(normalized), "/mnt/SDCARD/Roms/%s", romsPart + 13);
        snprintf(out, outSize, "%s", normalized);
    }
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

static void trimLine(char *line)
{
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[length - 1] = '\0';
        length--;
    }
}

static bool isFavoritePath(const char *romPath)
{
    FILE *file = fopen(FAVORITES_FILE, "r");
    if (!file)
        return false;

    char line[640];
    while (fgets(line, sizeof(line), file)) {
        trimLine(line);
        char *path = strrchr(line, '\t');
        if (path && strcmp(path + 1, romPath) == 0) {
            fclose(file);
            return true;
        }
    }

    fclose(file);
    return false;
}

static void addFavoritesFromFile(void)
{
    FILE *file = fopen(FAVORITES_FILE, "r");
    if (!file)
        return;

    char line[640];
    while (fgets(line, sizeof(line), file)) {
        trimLine(line);
        char *system = strtok(line, "\t");
        char *label = strtok(NULL, "\t");
        char *title = strtok(NULL, "\t");
        char *path = strtok(NULL, "\t");
        if (system && label && title && path)
            addPageItem(title, label, "outline-file.png", ACTION_LAUNCH_ROM, path, system);
    }

    fclose(file);
}

static void addOnionJsonList(const char *path, int limit)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return;

    char line[768];
    while (pageItemCount < limit && fgets(line, sizeof(line), file)) {
        char label[96];
        char romPath[256];
        char launchPath[256];
        char normalizedPath[256];
        char system[64] = "";

        jsonLineValue(line, "label", label, sizeof(label));
        jsonLineValue(line, "rompath", romPath, sizeof(romPath));
        jsonLineValue(line, "launch", launchPath, sizeof(launchPath));
        normalizeSdPath(romPath, normalizedPath, sizeof(normalizedPath));

        char *emu = strstr(launchPath, "/Emu/");
        if (emu) {
            char *start = emu + 5;
            char *end = strchr(start, '/');
            if (end) {
                size_t length = (size_t)(end - start);
                if (length >= sizeof(system))
                    length = sizeof(system) - 1;
                memcpy(system, start, length);
                system[length] = '\0';
            }
        }

        if (label[0] && normalizedPath[0] && system[0]) {
            char systemLabel[96];
            char configPath[256];
            snprintf(configPath, sizeof(configPath), EMU_DIR "/%s/config.json", system);
            configValue(configPath, "label", systemLabel, sizeof(systemLabel));
            addPageItem(label, systemLabel[0] ? systemLabel : system,
                        "outline-file.png", ACTION_LAUNCH_ROM, normalizedPath, system);
        }
    }

    fclose(file);
}

static void jsonEscape(const char *value, char *out, size_t outSize)
{
    size_t pos = 0;
    for (const char *p = value; *p && pos + 3 < outSize; p++) {
        if (*p == '"' || *p == '\\') {
            out[pos++] = '\\';
            out[pos++] = *p;
        }
        else {
            out[pos++] = *p;
        }
    }
    out[pos] = '\0';
}

static void syncOnionFavorite(const PageItem *item, bool adding)
{
    const char *romPath = item->target;
    const char *system = item->aux;

    char emuRomPath[256];
    snprintf(emuRomPath, sizeof(emuRomPath),
             EMU_DIR "/%s/../../Roms/%s/%s", system, system,
             strrchr(romPath, '/') ? strrchr(romPath, '/') + 1 : romPath);

    char launchPath[256];
    snprintf(launchPath, sizeof(launchPath), EMU_DIR "/%s/launch.sh", system);

    FILE *in = fopen(ONION_FAVORITES_FILE, "r");
    FILE *out = fopen(ROMS_DIR "/favourite.tmp", "w");
    if (!out) {
        if (in) fclose(in);
        return;
    }

    if (in) {
        char line[768];
        while (fgets(line, sizeof(line), in)) {
            char lineRomPath[256];
            jsonLineValue(line, "rompath", lineRomPath, sizeof(lineRomPath));
            char normalized[256];
            normalizeSdPath(lineRomPath, normalized, sizeof(normalized));
            if (strcmp(normalized, romPath) == 0)
                continue;
            fputs(line, out);
        }
        fclose(in);
    }

    if (adding) {
        char escLabel[128], escLaunch[320], escRomPath[320];
        jsonEscape(item->title, escLabel, sizeof(escLabel));
        jsonEscape(launchPath, escLaunch, sizeof(escLaunch));
        jsonEscape(emuRomPath, escRomPath, sizeof(escRomPath));
        fprintf(out, "{\"label\":\"%s\",\"launch\":\"%s\",\"type\":5,\"rompath\":\"%s\"}\n",
                escLabel, escLaunch, escRomPath);
    }

    fclose(out);
    remove(ONION_FAVORITES_FILE);
    rename(ROMS_DIR "/favourite.tmp", ONION_FAVORITES_FILE);
}

static void toggleFavorite(const PageItem *item)
{
    if (!item || item->action != ACTION_LAUNCH_ROM)
        return;

    mkdir(SYS_DIR "/config", 0755);

    FILE *in = fopen(FAVORITES_FILE, "r");
    FILE *out = fopen(SYS_DIR "/config/onyx_favorites.tmp", "w");
    bool removed = false;

    if (!out)
        return;

    if (in) {
        char line[640];
        while (fgets(line, sizeof(line), in)) {
            char original[640];
            snprintf(original, sizeof(original), "%s", line);
            trimLine(line);
            char *path = strrchr(line, '\t');
            if (path && strcmp(path + 1, item->target) == 0) {
                removed = true;
                continue;
            }
            fputs(original, out);
        }
        fclose(in);
    }

    if (!removed)
        fprintf(out, "%s\t%s\t%s\t%s\n", item->aux, item->subtitle, item->title, item->target);

    fclose(out);
    remove(FAVORITES_FILE);
    rename(SYS_DIR "/config/onyx_favorites.tmp", FAVORITES_FILE);

    syncOnionFavorite(item, !removed);
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

static void doubleQuote(const char *value, char *out, size_t outSize)
{
    size_t pos = 0;
    if (outSize == 0)
        return;

    out[pos++] = '"';
    for (const char *p = value; *p && pos + 3 < outSize; p++) {
        if (*p == '"' || *p == '\\' || *p == '$' || *p == '`')
            out[pos++] = '\\';
        out[pos++] = *p;
    }
    if (pos + 1 < outSize)
        out[pos++] = '"';
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
        if (romCount == 0)
            continue;
        snprintf(subtitle, sizeof(subtitle), "%d %s", romCount, romCount == 1 ? "game" : "games");
        const char *glyph = "outline-gamepad.png";
        addPageItem(label[0] ? label : entry->d_name, subtitle, glyph,
                    ACTION_OPEN_SYSTEM, entry->d_name, extList);
    }

    closedir(dir);
    sortPageItems();
}

static void addAppsFromSd(void)
{
    DIR *dir = opendir(APPS_DIR);
    if (!dir)
        return;

    static const char *hiddenApps[] = {
        "OnyxLauncher", "PackageManager", "AdvanceMENU",
        "Search", "miyoo354_calibrate", NULL,
    };

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        bool hidden = false;
        for (int i = 0; hiddenApps[i]; i++) {
            if (strcmp(entry->d_name, hiddenApps[i]) == 0) {
                hidden = true;
                break;
            }
        }
        if (hidden)
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
        const char *glyph = "outline-grid.png";
        addPageItem(label[0] ? label : entry->d_name,
                    description[0] ? description : "App",
                    glyph, ACTION_LAUNCH_APP, launchPath, NULL);
    }

    closedir(dir);
    sortPageItems();
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
        const char *iconFile;
        snprintf(romPath, sizeof(romPath), "%s/%s", romDir, entry->d_name);
        displayNameFromFile(entry->d_name, displayName, sizeof(displayName));
        iconFile = "outline-file.png";
        addPageItem(displayName, selectedSystemLabel, iconFile, ACTION_LAUNCH_ROM, romPath, systemName);
    }

    closedir(dir);
    sortPageItems();
}

static void loadPage(ViewMode view)
{
    currentView = view;
    pageItemCount = 0;
    selected = 0;
    scrollOffset = 0;

    if (view == VIEW_HOME) {
        addPageItem("Favorites", "Pinned games and apps", "outline-star.png",
                    ACTION_HOME_FAVORITES, NULL, NULL);
        addPageItem("Games", "Browse by system", "outline-gamepad.png",
                    ACTION_HOME_GAMES, NULL, NULL);
        addPageItem("Apps", "Tools and utilities", "outline-grid.png",
                    ACTION_HOME_APPS, NULL, NULL);
        addPageItem("Settings", "System configuration", "outline-settings.png",
                    ACTION_HOME_SETTINGS, NULL, NULL);
        return;
    }

    if (view == VIEW_FAVORITES) {
        addFavoritesFromFile();
        if (pageItemCount == 0)
            addOnionJsonList(ONION_FAVORITES_FILE, MAX_PAGE_ITEMS);
        if (pageItemCount == 0)
            addOnionJsonList(ONION_RECENTS_FILE, MAX_PAGE_ITEMS);
        if (pageItemCount == 0)
            addPageItemEx("No favorites yet", "Press Y on a game to pin it here",
                          "outline-star.png", ACTION_NONE, NULL, NULL, ROW_STATIC, NULL, false);
        return;
    }

    if (view == VIEW_GAMES) {
        addSystemsFromSd();
        if (pageItemCount == 0)
            addPageItemEx("No systems found", "Check /Roms and /Emu",
                          "outline-gamepad.png", ACTION_NONE, NULL, NULL, ROW_STATIC, NULL, false);
        return;
    }

    if (view == VIEW_SYSTEM_ROMS) {
        addRomsForSystem(selectedSystem, selectedSystemExts);
        if (pageItemCount == 0)
            addPageItemEx("No games found", selectedSystemLabel, "outline-file.png",
                          ACTION_NONE, NULL, NULL, ROW_STATIC, NULL, false);
        return;
    }

    if (view == VIEW_APPS) {
        addAppsFromSd();
        if (pageItemCount == 0)
            addPageItemEx("No apps found", "Check /App", "outline-grid.png",
                          ACTION_NONE, NULL, NULL, ROW_STATIC, NULL, false);
        return;
    }

    if (view == VIEW_RECENTS) {
        addOnionJsonList(ONION_RECENTS_FILE, MAX_PAGE_ITEMS);
        addOnionJsonList(ONION_RECENTS_HIDDEN_FILE, MAX_PAGE_ITEMS);
        if (pageItemCount == 0)
            addPageItemEx("No recent games", "Play something to see it here",
                          "outline-clock.png", ACTION_NONE, NULL, NULL, ROW_STATIC, NULL, false);
        return;
    }

    if (view == VIEW_CONFIRM_DISABLE) {
        addPageItem("Keep ONYX Enabled", "Return to Settings", "outline-settings.png", ACTION_CANCEL_CONFIRM, NULL, NULL);
        addPageItem("Disable ONYX", "Boot stock Onion next time", "outline-settings.png", ACTION_CONFIRM_DISABLE_ONYX, NULL, NULL);
        return;
    }

    addPageItem("Tweaks", "System behavior and tools", "outline-settings.png",
                ACTION_LAUNCH_COMMAND, "cd " SYS_DIR "; ./bin/tweaks", NULL);
    addPageItem("UI Theme", "Browse and apply themes", "outline-settings.png",
                ACTION_LAUNCH_COMMAND, "cd " SYS_DIR "; ./bin/themeSwitcher", NULL);
    addPageItem("Package Manager", "Add Onion apps and systems", "outline-grid.png",
                ACTION_LAUNCH_COMMAND, SYS_DIR "/bin/packageManager", NULL);
    addPageItem("Activity Tracker", "Play time and history", "outline-clock.png",
                ACTION_LAUNCH_COMMAND, "cd " SYS_DIR "; ./bin/playActivityUI", NULL);
    addPageItem("Recents", "Recent games and quick resume", "outline-clock.png", ACTION_HOME_RECENTS, NULL, NULL);
    addPageItem("Stock Onion", "Open the original main screen once", "outline-settings.png",
                ACTION_STOCK_MAIN, NULL, NULL);
    addPageItemEx("ONYX launcher", "Toggle back to stock Onion", "outline-settings.png",
                  ACTION_DISABLE_ONYX, NULL, NULL, ROW_TOGGLE, NULL, true);
    addPageItemEx("Version", "Local development build", "outline-settings.png",
                  ACTION_NONE, NULL, NULL, ROW_VALUE, ONYX_VERSION, false);
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

static const char *backLabel(void)
{
    if (currentView == VIEW_HOME)
        return "Back";
    if (currentView == VIEW_CONFIRM_DISABLE)
        return "Cancel";
    return "Back";
}

static bool isEmptyState(void)
{
    return pageItemCount == 1 && pageItems[0].action == ACTION_NONE &&
           strncmp(pageItems[0].title, "No ", 3) == 0;
}

static void drawToggle(SDL_Surface *screen, int x, int y, bool enabled,
                       SDL_Color activeColor, SDL_Color offColor, SDL_Color knobColor)
{
    SDL_Color body = enabled ? activeColor : offColor;
    fillRot(screen, x, y, 56, 28, body);
    border(screen, x, y, 56, 28, body);
    fillRot(screen, x + (enabled ? 30 : 6), y + 5, 18, 18, knobColor);
}

static void drawRowAccessory(SDL_Surface *screen, const PageItem *item, TTF_Font *fontFooter,
                             int y, int iconY, bool active, SDL_Color textMain,
                             SDL_Color textDim)
{
    if (item->kind == ROW_VALUE) {
        char value[48];
        truncateText(item->value, value, sizeof(value), 16);
        text(screen, fontFooter, value, 492, y + 31, active ? textMain : textDim);
        return;
    }

    if (item->kind == ROW_TOGGLE) {
        drawToggle(screen, 532, y + 30, item->enabled, rgb(221, 118, 0),
                   rgb(42, 47, 54), textMain);
        return;
    }

    if (item->kind == ROW_DRILL)
        icon(screen, active ? "outline-chevron-filled.png" : "outline-chevron.png",
             574, iconY, ICON_SIZE);
}

static void draw(SDL_Surface *screen, TTF_Font *fontFooter, TTF_Font *fontBrand,
                 TTF_Font *fontTitle, TTF_Font *fontRowTitle, TTF_Font *fontSubtitle)
{
    (void)fontBrand;

    SDL_Color bg = rgb(14, 17, 20);
    SDL_Color panel = rgb(22, 28, 33);
    SDL_Color panelDark = rgb(17, 20, 24);
    SDL_Color line = rgb(38, 43, 49);
    SDL_Color textMain = rgb(238, 242, 246);
    SDL_Color textDim = rgb(156, 163, 173);
    SDL_Color teal = rgb(28, 101, 219);
    SDL_Color blue = rgb(28, 101, 219);

    fill(screen, 0, 0, SCREEN_W, SCREEN_H, bg);
    fillRot(screen, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, panelDark);
    border(screen, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, line);
    fillRot(screen, 0, 60, 640, 2, line);
    fillRot(screen, 0, 416, 640, 64, panelDark);
    fillRot(screen, 0, 416, 640, 2, line);

    image(screen, "header-logo.png", 20, 17);
    char timeLabel[8];
    clockLabel(timeLabel, sizeof(timeLabel));
    text(screen, fontFooter, timeLabel, 312, 15, textMain);
    int battery = batteryPercent();
    char batteryLabel[8];
    if (battery >= 0)
        snprintf(batteryLabel, sizeof(batteryLabel), "%d%%", battery);
    else
        snprintf(batteryLabel, sizeof(batteryLabel), "--%%");
    text(screen, fontFooter, batteryLabel, 520, 15, textMain);

    if (isEmptyState()) {
        int emptyIconSize = 76;
        icon(screen, pageItems[0].icon, (SCREEN_W - emptyIconSize) / 2, 140, emptyIconSize);
        int tw, th;
        if (TTF_SizeUTF8(fontTitle, pageItems[0].title, &tw, &th) == 0)
            text(screen, fontTitle, pageItems[0].title, (SCREEN_W - tw) / 2, 234, textMain);
        if (TTF_SizeUTF8(fontSubtitle, pageItems[0].subtitle, &tw, &th) == 0)
            text(screen, fontSubtitle, pageItems[0].subtitle, (SCREEN_W - tw) / 2, 270, textDim);
    }
    else {
        for (int row = 0; row < VISIBLE_ROWS; row++) {
            int itemIndex = scrollOffset + row;
            if (itemIndex >= pageItemCount)
                break;

            int y = LIST_TOP + row * (ITEM_H + ITEM_GAP);
            bool active = itemIndex == selected;

            fillRot(screen, 0, y, 640, ITEM_H, active ? blue : panel);
            border(screen, 0, y, 640, ITEM_H, active ? teal : bg);
            int iconY = y + (ITEM_H - ICON_SIZE) / 2;
            icon(screen, pageItems[itemIndex].icon, 28, iconY, ICON_SIZE);
            if (pageItems[itemIndex].subtitle[0]) {
                char title[80];
                char subtitle[96];
                truncateText(pageItems[itemIndex].title, title, sizeof(title), 32);
                truncateText(pageItems[itemIndex].subtitle, subtitle, sizeof(subtitle), 42);
                text(screen, fontRowTitle, title, 86, y + 18, textMain);
                text(screen, fontSubtitle, subtitle, 86, y + 52,
                     active ? rgb(202, 209, 220) : textDim);
            }
            else {
                char title[80];
                truncateText(pageItems[itemIndex].title, title, sizeof(title), 24);
                text(screen, fontTitle, title, 86, y + 27, textMain);
            }
            drawRowAccessory(screen, &pageItems[itemIndex], fontFooter, y, iconY,
                             active, textMain, textDim);
        }
    }

    if (pageItemCount > VISIBLE_ROWS) {
        int trackX = 632;
        int trackY = LIST_TOP + 4;
        int trackH = VISIBLE_ROWS * (ITEM_H + ITEM_GAP) - 8;
        int thumbH = trackH * VISIBLE_ROWS / pageItemCount;
        if (thumbH < 12) thumbH = 12;
        int thumbY = trackY + (trackH - thumbH) * scrollOffset / (pageItemCount - VISIBLE_ROWS);
        fillRot(screen, trackX, trackY, 4, trackH, rgb(30, 34, 40));
        fillRot(screen, trackX, thumbY, 4, thumbH, rgb(100, 108, 118));
    }

    fillRot(screen, 320, 432, 2, 26, line);
    icon(screen, "button-a.png", 66, 432, 30);
    text(screen, fontFooter, "Select", 102, 431, textDim);
    if (currentView == VIEW_SYSTEM_ROMS || currentView == VIEW_FAVORITES ||
        currentView == VIEW_RECENTS)
        text(screen, fontSubtitle, "Y Favorite", 265, 434, textDim);
    icon(screen, "button-b.png", 480, 432, 30);
    text(screen, fontFooter, backLabel(), 516, 431, textDim);

    SDL_Flip(screen);
}

static void handoffToRuntime(int state)
{
    char command[80];
    snprintf(command, sizeof(command), SYS_DIR "/bin/setState %d", state);
    system(command);
    quit = true;
}

static void runDirectCommand(const char *command)
{
    snprintf(pendingCommand, sizeof(pendingCommand), "%s", command);
    quit = true;
}

static void queueRuntimeCommand(const char *command)
{
    FILE *file = fopen("/tmp/cmd_to_run.sh", "w");
    if (!file) {
        runDirectCommand(command);
        return;
    }

    fprintf(file, "%s\n", command);
    fclose(file);
    chmod("/tmp/cmd_to_run.sh", 0755);
    quit = true;
}

static void launchRom(const char *systemName, const char *romPath)
{
    char launchPath[256];
    char quotedLaunch[320];
    char quotedRom[320];
    char command[760];

    snprintf(launchPath, sizeof(launchPath), EMU_DIR "/%s/launch.sh", systemName);
    doubleQuote(launchPath, quotedLaunch, sizeof(quotedLaunch));
    doubleQuote(romPath, quotedRom, sizeof(quotedRom));
    snprintf(command, sizeof(command),
             "LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so %s %s",
             quotedLaunch, quotedRom);
    queueRuntimeCommand(command);
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

    doubleQuote(appDir, quotedDir, sizeof(quotedDir));
    doubleQuote(launchPath, quotedLaunch, sizeof(quotedLaunch));
    snprintf(command, sizeof(command), "cd %s; %s", quotedDir, quotedLaunch);
    queueRuntimeCommand(command);
}

static void launchCommand(const char *command)
{
    if (command && command[0])
        queueRuntimeCommand(command);
}

static void launchGameSwitcher(void)
{
    FILE *file = fopen(SYS_DIR "/.runGameSwitcher", "w");
    if (file) {
        fclose(file);
    }
    else {
        queueRuntimeCommand("cd " SYS_DIR "; LD_PRELOAD=/mnt/SDCARD/miyoo/lib/libpadsp.so gameSwitcher");
        return;
    }
    quit = true;
}

static void launchStockMain(void)
{
    runDirectCommand("cd " MIYOO_APP_DIR " && "
                     "PATH=" MIYOO_APP_DIR ":$PATH "
                     "LD_LIBRARY_PATH=/mnt/SDCARD/miyoo/lib:/config/lib:/lib "
                     "LD_PRELOAD=/mnt/SDCARD/miyoo/lib/libpadsp.so "
                     "./MainUI 2>&1 > /dev/null");
}

static void disableOnyx(void)
{
    remove(SYS_DIR "/config/.useOnyxLauncher");
    handoffToRuntime(0);
}

static void activateSelection(void)
{
    if (pageItemCount <= 0)
        return;

    int action = pageItems[selected].action;
    if (action == ACTION_HOME_RECENTS)
        loadPage(VIEW_RECENTS);
    else if (action == ACTION_HOME_FAVORITES)
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
    else if (action == ACTION_LAUNCH_COMMAND)
        launchCommand(pageItems[selected].target);
    else if (action == ACTION_GAME_SWITCHER)
        launchGameSwitcher();
    else if (action == ACTION_STOCK_MAIN)
        launchStockMain();
    else if (action == ACTION_DISABLE_ONYX)
        loadPage(VIEW_CONFIRM_DISABLE);
    else if (action == ACTION_CONFIRM_DISABLE_ONYX)
        disableOnyx();
    else if (action == ACTION_CANCEL_CONFIRM)
        loadPage(VIEW_SETTINGS);
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

    initAudio();

    SDL_Surface *screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 32, SDL_SWSURFACE);
    TTF_Font *fontFooter = openFont(23);
    TTF_Font *fontBrand = openFont(17);
    TTF_Font *fontTitle = openFont(29);
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
    playIntro();

    int hwInputs[MAX_HW_INPUTS];
    struct pollfd hwPolls[MAX_HW_INPUTS];
    int hwPollCount = 0;
    for (int i = 0; i < MAX_HW_INPUTS; i++) {
        char path[32];
        int fd;
        hwInputs[i] = -1;
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            hwInputs[hwPollCount] = fd;
            hwPolls[hwPollCount].fd = fd;
            hwPolls[hwPollCount].events = POLLIN;
            hwPolls[hwPollCount].revents = 0;
            hwPollCount++;
        }
    }

    while (!quit) {
        SDL_Event event;
        bool sawEvent = false;

        while (SDL_PollEvent(&event)) {
            sawEvent = true;
            if (event.type == SDL_QUIT) {
                quit = true;
                break;
            }

            if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP)
                continue;

            SDLKey key = event.key.keysym.sym;
            if (key == SW_BTN_MENU) {
                loadPage(VIEW_RECENTS);
                draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);
                break;
            }

            if (event.type != SDL_KEYUP)
                continue;

            if (key == SW_BTN_DOWN) {
                moveSelection(1);
                playNav();
                draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);
            }
            else if (key == SW_BTN_UP) {
                moveSelection(-1);
                playNav();
                draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);
            }
            else if (key == SW_BTN_A) {
                activateSelection();
                if (!quit)
                    draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);
            }
            else if (key == SW_BTN_Y) {
                if ((currentView == VIEW_SYSTEM_ROMS || currentView == VIEW_FAVORITES ||
                     currentView == VIEW_RECENTS) &&
                    pageItemCount > 0 && pageItems[selected].action == ACTION_LAUNCH_ROM) {
                    toggleFavorite(&pageItems[selected]);
                    if (currentView == VIEW_SYSTEM_ROMS)
                        loadPage(VIEW_SYSTEM_ROMS);
                    else if (currentView == VIEW_RECENTS)
                        loadPage(VIEW_RECENTS);
                    else
                        loadPage(VIEW_FAVORITES);
                    draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);
                }
            }
            else if (key == SW_BTN_B) {
                if (currentView == VIEW_SYSTEM_ROMS) {
                    loadPage(VIEW_GAMES);
                    draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);
                }
                else if (currentView == VIEW_CONFIRM_DISABLE) {
                    loadPage(VIEW_SETTINGS);
                    draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);
                }
                else if (currentView != VIEW_HOME) {
                    loadPage(VIEW_HOME);
                    draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);
                }
            }
        }

        if (quit)
            break;

        if (hwPollCount > 0 && poll(hwPolls, hwPollCount, sawEvent ? 0 : 40) > 0) {
            bool launchedSwitcher = false;
            for (int i = 0; i < hwPollCount && !launchedSwitcher; i++) {
                struct input_event hwEvent;
                if (!(hwPolls[i].revents & POLLIN))
                    continue;

                while (read(hwInputs[i], &hwEvent, sizeof(hwEvent)) == sizeof(hwEvent)) {
                    if (hwEvent.type == EV_KEY && hwEvent.code == HW_BTN_MENU &&
                        hwEvent.value != 2) {
                        loadPage(VIEW_RECENTS);
                        draw(screen, fontFooter, fontBrand, fontTitle, fontRowTitle, fontSubtitle);
                        launchedSwitcher = true;
                        break;
                    }
                }
            }
        }
        else if (!sawEvent) {
            SDL_Delay(16);
        }
    }

    for (int i = 0; i < hwPollCount; i++) {
        if (hwInputs[i] >= 0)
            close(hwInputs[i]);
    }

    closeAudio();
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
