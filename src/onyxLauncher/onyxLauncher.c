#include <SDL/SDL.h>
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
#define SYS_DIR "/mnt/SDCARD/.tmp_update"
#define MIYOO_APP_DIR "/mnt/SDCARD/miyoo/app"

typedef struct {
    const char *icon;
    const char *title;
    const char *hint;
    int state;
} LauncherItem;

static const LauncherItem ITEMS[] = {
    {"[+]", "Games", "Browse systems and collections", 3},
    {"[~]", "Recent", "Resume something you played", 1},
    {"[*]", "Favorites", "Open your saved picks", 2},
    {"[#]", "Apps", "Tools, players, and utilities", 5},
    {"[=]", "Settings", "Open the stock Onion main menu", 0},
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

static void text(SDL_Surface *screen, TTF_Font *font, const char *value, int x, int y,
                 SDL_Color color)
{
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, value, color);
    if (!surface)
        return;

    SDL_Surface *rotated = SDL_CreateRGBSurface(SDL_SWSURFACE, surface->w, surface->h,
                                                surface->format->BitsPerPixel,
                                                surface->format->Rmask,
                                                surface->format->Gmask,
                                                surface->format->Bmask,
                                                surface->format->Amask);
    if (!rotated) {
        SDL_FreeSurface(surface);
        return;
    }

    Uint32 transparent = SDL_MapRGBA(rotated->format, 0, 0, 0, 0);
    SDL_FillRect(rotated, NULL, transparent);
    SDL_SetColorKey(rotated, SDL_SRCCOLORKEY, transparent);

    SDL_LockSurface(surface);
    SDL_LockSurface(rotated);
    for (int py = 0; py < surface->h; py++) {
        for (int px = 0; px < surface->w; px++) {
            Uint32 *src = (Uint32 *)((Uint8 *)surface->pixels + py * surface->pitch + px * 4);
            Uint32 *dst = (Uint32 *)((Uint8 *)rotated->pixels +
                                     (surface->h - 1 - py) * rotated->pitch +
                                     (surface->w - 1 - px) * 4);
            *dst = *src;
        }
    }
    SDL_UnlockSurface(rotated);
    SDL_UnlockSurface(surface);

    SDL_Rect rect = {rx(x, surface->w), ry(y, surface->h), 0, 0};
    SDL_BlitSurface(rotated, NULL, screen, &rect);
    SDL_FreeSurface(rotated);
    SDL_FreeSurface(surface);
}

static TTF_Font *openFont(int size)
{
    const char *paths[] = {
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

static void draw(SDL_Surface *screen, TTF_Font *font, TTF_Font *fontLarge, int selected)
{
    SDL_Color bg = rgb(14, 17, 20);
    SDL_Color panel = rgb(25, 29, 34);
    SDL_Color line = rgb(64, 73, 82);
    SDL_Color textMain = rgb(232, 235, 240);
    SDL_Color textDim = rgb(154, 160, 170);
    SDL_Color teal = rgb(70, 190, 178);
    SDL_Color blue = rgb(77, 124, 254);
    SDL_Color amber = rgb(224, 178, 74);

    fill(screen, 0, 0, SCREEN_W, SCREEN_H, bg);
    fillRot(screen, 0, 0, SCREEN_W, 58, rgb(17, 21, 25));
    fillRot(screen, 0, 58, SCREEN_W, 2, line);

    text(screen, fontLarge, "onyxOS", 28, 14, textMain);
    text(screen, font, "83%", 544, 19, textMain);
    border(screen, 586, 20, 30, 16, textDim);

    for (int i = 0; i < (int)(sizeof(ITEMS) / sizeof(ITEMS[0])); i++) {
        int y = 88 + i * 68;
        bool active = i == selected;

        fillRot(screen, 44, y, 552, 54, active ? blue : panel);
        border(screen, 44, y, 552, 54, active ? teal : rgb(31, 36, 42));
        text(screen, fontLarge, ITEMS[i].icon, 68, y + 11, active ? textMain : textDim);
        text(screen, fontLarge, ITEMS[i].title, 136, y + 9, textMain);
        text(screen, font, ITEMS[i].hint, 136, y + 33, active ? rgb(222, 230, 246) : textDim);
        text(screen, fontLarge, ">", 562, y + 10, active ? amber : textDim);
    }

    fillRot(screen, 0, 414, SCREEN_W, 2, line);
    text(screen, font, "A SELECT", 42, 436, teal);
    text(screen, font, "B STOCK MENU", 188, 436, amber);
    text(screen, font, "UP/DOWN MOVE", 420, 436, textDim);

    SDL_Flip(screen);
}

static void launchStock(int state)
{
    char command[80];
    snprintf(command, sizeof(command), SYS_DIR "/bin/setState %d", state);
    system(command);

    const char *device = getenv("DEVICE_ID");
    if (!device || strlen(device) == 0)
        device = "354";

    const char *mode = access(SYS_DIR "/config/.showExpert", F_OK) == 0 ? "expert" : "clean";

    char mainui[128];
    snprintf(mainui, sizeof(mainui), SYS_DIR "/bin/MainUI-%s-%s", device, mode);

    chdir(MIYOO_APP_DIR);
    setenv("PATH", MIYOO_APP_DIR ":" SYS_DIR "/bin:/bin:/usr/bin", 1);
    setenv("LD_LIBRARY_PATH", MIYOO_APP_DIR "/../lib:/config/lib:/lib", 1);
    setenv("LD_PRELOAD", MIYOO_APP_DIR "/../lib/libpadsp.so", 1);

    execl(mainui, "MainUI", NULL);
    exit(1);
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
    TTF_Font *font = openFont(18);
    TTF_Font *fontLarge = openFont(28);

    if (!screen || !font || !fontLarge) {
        if (font)
            TTF_CloseFont(font);
        if (fontLarge)
            TTF_CloseFont(fontLarge);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    int selected = 0;
    draw(screen, font, fontLarge, selected);

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
            draw(screen, font, fontLarge, selected);
        }
        else if (key == SW_BTN_UP) {
            selected--;
            if (selected < 0)
                selected = (int)(sizeof(ITEMS) / sizeof(ITEMS[0])) - 1;
            draw(screen, font, fontLarge, selected);
        }
        else if (key == SW_BTN_A) {
            launchStock(ITEMS[selected].state);
        }
        else if (key == SW_BTN_B || key == SW_BTN_MENU) {
            launchStock(0);
        }
    }

    TTF_CloseFont(font);
    TTF_CloseFont(fontLarge);
    TTF_Quit();
    SDL_Quit();

    return 0;
}
