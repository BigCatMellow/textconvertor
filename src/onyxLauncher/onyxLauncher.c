#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "system/keymap_sw.h"

#define SCREEN_W 640
#define SCREEN_H 480
#define ICON_SIZE 38
#define PANEL_X 34
#define PANEL_Y 24
#define PANEL_W 572
#define PANEL_H 392
#define ITEM_H 70
#define ITEM_GAP 0
#define LIST_TOP 81
#define SYS_DIR "/mnt/SDCARD/.tmp_update"
#define MIYOO_APP_DIR "/mnt/SDCARD/miyoo/app"
#define ICON_DIR SYS_DIR "/res/onyx/icons"

typedef struct {
    const char *icon;
    const char *iconFilled;
    const char *title;
    int state;
} LauncherItem;

static const LauncherItem ITEMS[] = {
    {"favorites-white.png", "favorites-white.png", "Favorites", 2},
    {"games-white.png", "games-white.png", "Games", 3},
    {"apps-white.png", "apps-white.png", "Apps", 5},
    {"settings-white.png", "settings-white.png", "Settings", 0},
};

static bool quit = false;

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
                 TTF_Font *fontTitle, int selected)
{
    SDL_Color bg = rgb(14, 17, 20);
    SDL_Color panel = rgb(22, 28, 33);
    SDL_Color panelDark = rgb(17, 20, 24);
    SDL_Color line = rgb(38, 43, 49);
    SDL_Color textMain = rgb(238, 242, 246);
    SDL_Color textDim = rgb(154, 160, 170);
    SDL_Color teal = rgb(28, 101, 219);
    SDL_Color blue = rgb(28, 101, 219);
    SDL_Color amber = rgb(221, 118, 0);

    fill(screen, 0, 0, SCREEN_W, SCREEN_H, bg);
    fillRot(screen, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, panelDark);
    border(screen, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, line);

    image(screen, "onyx-logo-header.png", 56, 40);
    text(screen, fontFooter, "10:30", 312, 40, textMain);
    text(screen, fontFooter, "83%", 520, 40, textMain);
    border(screen, 574, 42, 22, 12, textDim);

    for (int i = 0; i < (int)(sizeof(ITEMS) / sizeof(ITEMS[0])); i++) {
        int y = LIST_TOP + i * (ITEM_H + ITEM_GAP);
        bool active = i == selected;

        fillRot(screen, 52, y, 536, ITEM_H, active ? blue : panel);
        border(screen, 52, y, 536, ITEM_H, active ? teal : bg);
        icon(screen, active ? ITEMS[i].iconFilled : ITEMS[i].icon, 76, y + 16, ICON_SIZE);
        text(screen, fontTitle, ITEMS[i].title, 134, y + 17, textMain);
        icon(screen, active ? "square-rounded-arrow-right-filled.png" : "square-rounded-arrow-right.png",
             526, y + 16, ICON_SIZE);
    }

    fillRot(screen, 320, 374, 2, 26, line);
    icon(screen, "button-a.png", 66, 375, 30);
    text(screen, fontFooter, "Select", 102, 379, textDim);
    icon(screen, "button-b.png", 480, 375, 30);
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

    if (!screen || !fontFooter || !fontBrand || !fontTitle) {
        if (fontFooter)
            TTF_CloseFont(fontFooter);
        if (fontBrand)
            TTF_CloseFont(fontBrand);
        if (fontTitle)
            TTF_CloseFont(fontTitle);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    int selected = 0;
    draw(screen, fontFooter, fontBrand, fontTitle, selected);

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
            selected = (selected + 1) % (int)(sizeof(ITEMS) / sizeof(ITEMS[0]));
            draw(screen, fontFooter, fontBrand, fontTitle, selected);
        }
        else if (key == SW_BTN_UP) {
            selected--;
            if (selected < 0)
                selected = (int)(sizeof(ITEMS) / sizeof(ITEMS[0])) - 1;
            draw(screen, fontFooter, fontBrand, fontTitle, selected);
        }
        else if (key == SW_BTN_A) {
            handoffToRuntime(ITEMS[selected].state);
        }
        else if (key == SW_BTN_B || key == SW_BTN_MENU) {
            handoffToRuntime(0);
        }
    }

    TTF_CloseFont(fontFooter);
    TTF_CloseFont(fontBrand);
    TTF_CloseFont(fontTitle);
    TTF_Quit();
    SDL_Quit();

    return 0;
}
