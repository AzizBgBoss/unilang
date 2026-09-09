#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define ULVM_MEM_SIZE 4096
#include "unilang.h"

#define SCALE 10

static uint8_t *rom = NULL;
static long romsize = 0;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static UnilangVM *g_vm = NULL;

static void my_outputchar(char c)
{
    putchar(c);
}

static uint8_t my_readbyte(uint32_t addr)
{
    if (addr < (uint32_t)romsize)
        return rom[addr];
    return 0;
}

static int gfx_width(void)
{
    uint8_t mode = ulvm_getflag(g_vm, 0);
    return (mode >= 4 && mode <= 6) ? 128 : 64;
}

static void my_setpixel(uint8_t x, uint8_t y, uint8_t color)
{
    if (!renderer)
        return;
    if (color)
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    else
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_Rect r = { x * SCALE, y * SCALE, SCALE, SCALE };
    SDL_RenderFillRect(renderer, &r);
}

static uint8_t my_getpixel(uint8_t x, uint8_t y)
{
    (void)x;
    (void)y;
    return 0;
}

static void my_refreshscreen(void)
{
    if (renderer)
        SDL_RenderPresent(renderer);
}

static uint8_t my_getkeyboard(void)
{
    SDL_PumpEvents();
    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    uint8_t state = 0;
    state |= keys[SDL_SCANCODE_UP] << 0;
    state |= keys[SDL_SCANCODE_DOWN] << 1;
    state |= keys[SDL_SCANCODE_LEFT] << 2;
    state |= keys[SDL_SCANCODE_RIGHT] << 3;
    state |= keys[SDL_SCANCODE_C] << 4;
    state |= keys[SDL_SCANCODE_V] << 5;
    state |= keys[SDL_SCANCODE_BACKSPACE] << 6;
    state |= keys[SDL_SCANCODE_RETURN] << 7;
    return state;
}

static uint32_t my_gettime(void)
{
    return SDL_GetTicks();
}

static void my_sleep(uint32_t ms)
{
    SDL_Delay(ms);
}

static int my_isflagsupported(uint8_t flag, uint8_t val)
{
    if (flag == 0)
    {
        if (val == 0 || val == 1)
            return 1;
        else
            return 0;
    }
    else if (flag == 1)
    {
        if (val == 0)
            return 1;
        else
            return 0;
    }
    else
        return 0;
}

static int pump_events(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e))
        if (e.type == SDL_QUIT)
            return 1;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: %s <bytecode file>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f)
    {
        printf("Could not open %s\n", argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    romsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    rom = malloc((size_t)romsize);
    fread(rom, 1, (size_t)romsize, f);
    fclose(f);

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("unilang", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               64 * SCALE, 64 * SCALE, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    UnilangVM vm;
    g_vm = &vm;
    ULVM_init(&vm, my_outputchar, my_readbyte);
    ULVM_set_graphics(&vm, my_setpixel, my_getpixel, my_refreshscreen);
    ULVM_set_keyboard(&vm, my_getkeyboard);
    ULVM_set_time(&vm, my_gettime, my_sleep);
    ULVM_set_flagsupport(&vm, my_isflagsupported);

    while (!ULVM_handlenextinstruction(&vm))
    {
        if (pump_events())
            break;
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    free(rom);
    return 0;
}