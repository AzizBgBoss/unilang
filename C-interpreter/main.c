#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ULVM_MEM_SIZE 8192
#include "unilang.h"

#define SCALE 10

static int debug_enabled = 0;

#define DEBUG_PRINT(...) \
    do { if (debug_enabled) printf(__VA_ARGS__); } while (0)
#define DEBUG_ERROR(...) \
    do { if (debug_enabled) fprintf(stderr, __VA_ARGS__); } while (0)

#define FRAMEBUFFER_WIDTH 128
#define FRAMEBUFFER_HEIGHT 64

static uint8_t *rom = NULL;
static long romsize = 0;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *screen_texture = NULL;
static uint8_t framebuffer[FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT];
static uint32_t display_buffer[FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT];
static uint16_t graphics_width = 0;
static uint16_t graphics_height = 0;
static uint8_t graphics_colorsize = 0;
static uint32_t presented_frames = 0;
static uint32_t last_frame_time = 0;
static uint8_t previous_keyboard_state = 0;

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

static void my_setpixel(uint8_t x, uint8_t y, uint8_t color)
{
    if (!graphics_colorsize || x >= graphics_width || y >= graphics_height)
        return;

    if (graphics_colorsize == 1)
        color = color ? 1 : 0;
    else if (graphics_colorsize == 4)
        color &= 0x0F;

    framebuffer[(size_t)y * FRAMEBUFFER_WIDTH + x] = color;
}

static uint8_t my_getpixel(uint8_t x, uint8_t y)
{
    if (!graphics_colorsize || x >= graphics_width || y >= graphics_height)
        return 0;
    return framebuffer[(size_t)y * FRAMEBUFFER_WIDTH + x];
}

static void my_disablegraphics(void);

static uint32_t my_color_to_argb(uint8_t color)
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    if (graphics_colorsize == 1)
    {
        red = green = blue = color ? 255 : 0;
    }
    else if (graphics_colorsize == 4)
    {
        red = green = blue = (uint8_t)((color & 0x0F) * 17);
    }
    else
    {
        red = (uint8_t)(((color >> 5) & 0x07) * 255 / 7);
        green = (uint8_t)(((color >> 2) & 0x07) * 255 / 7);
        blue = (uint8_t)((color & 0x03) * 255 / 3);
    }

    return 0xFF000000u | ((uint32_t)red << 16) |
           ((uint32_t)green << 8) | blue;
}

static void my_refreshscreen(void)
{
    if (!renderer || !screen_texture)
        return;

    for (uint16_t y = 0; y < graphics_height; ++y)
    {
        for (uint16_t x = 0; x < graphics_width; ++x)
        {
            size_t index = (size_t)y * FRAMEBUFFER_WIDTH + x;
            display_buffer[index] = my_color_to_argb(framebuffer[index]);
        }
    }

    SDL_UpdateTexture(screen_texture, NULL, display_buffer,
                      FRAMEBUFFER_WIDTH * (int)sizeof(display_buffer[0]));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, screen_texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    uint32_t now = SDL_GetTicks();
    if (debug_enabled)
    {
        DEBUG_PRINT("[debug] frame %u presented at %u ms (+%u ms)\n",
                    (unsigned)++presented_frames, (unsigned)now,
                    (unsigned)(last_frame_time ? now - last_frame_time : 0));
        last_frame_time = now;
    }
}

static void my_clearscreen(void)
{
    memset(framebuffer, 0, sizeof(framebuffer));
}

static void my_enablegraphics(uint16_t wx, uint16_t wy, uint8_t colorsize)
{
    if ((wx != 64 && wx != 128) || wy != 64 ||
        (colorsize != 1 && colorsize != 4 && colorsize != 8))
    {
        DEBUG_ERROR("[debug] unsupported graphics mode: %ux%u, %u bits per pixel\n",
                    wx, wy, colorsize);
        return;
    }

    my_disablegraphics();
    DEBUG_PRINT("[debug] enabling graphics: %ux%u, scale=%d\n", wx, wy, SCALE);
    window = SDL_CreateWindow("unilang", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               wx * SCALE, wy * SCALE, 0);
    if (!window)
    {
        DEBUG_ERROR("[debug] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return;
    }
    graphics_width = wx;
    graphics_height = wy;
    graphics_colorsize = colorsize;
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
    {
        DEBUG_ERROR("[debug] accelerated renderer unavailable: %s; trying software renderer\n",
                    SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer)
    {
        DEBUG_ERROR("[debug] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        my_disablegraphics();
        return;
    }
    screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, wx, wy);
    if (!screen_texture)
    {
        DEBUG_ERROR("[debug] SDL_CreateTexture failed: %s\n", SDL_GetError());
        my_disablegraphics();
        return;
    }
    DEBUG_PRINT("[debug] graphics window created\n");
    my_clearscreen();
    my_refreshscreen();
}

static void my_disablegraphics(void)
{
    if (renderer)
    {
        if (screen_texture)
        {
            SDL_DestroyTexture(screen_texture);
            screen_texture = NULL;
        }
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
    if (window)
    {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    graphics_width = 0;
    graphics_height = 0;
    graphics_colorsize = 0;
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
    if (state != previous_keyboard_state)
    {
        DEBUG_PRINT("[debug] keyboard state: 0x%02x -> 0x%02x\n",
                    (unsigned)previous_keyboard_state, (unsigned)state);
        previous_keyboard_state = state;
    }
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
    int supported = 0;
    if (flag == 0)
    {
        supported = val >= 1 && val <= 6;
    }
    else if (flag == 1)
    {
        supported = val == 1;
    }

    DEBUG_PRINT("[debug] isflagsupported(%u, %u) -> %s\n",
                (unsigned)flag, (unsigned)val, supported ? "yes" : "no");
    return supported;
}

static int pump_events(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        if (e.type == SDL_QUIT)
        {
            DEBUG_PRINT("[debug] SDL quit event received\n");
            return 1;
        }
    }
    return 0;
}

static void print_hot_instructions(const UnilangVM *vm)
{
    uint32_t best_pc[5] = {0};
    uint32_t best_hits[5] = {0};
    for (uint32_t pc = 0; pc < (uint32_t)romsize; ++pc)
    {
        uint32_t hits = vm->instruction_hits[pc];
        for (size_t rank = 0; rank < 5; ++rank)
        {
            if (hits > best_hits[rank])
            {
                for (size_t move = 4; move > rank; --move)
                {
                    best_hits[move] = best_hits[move - 1];
                    best_pc[move] = best_pc[move - 1];
                }
                best_hits[rank] = hits;
                best_pc[rank] = pc;
                break;
            }
        }
    }

    for (size_t rank = 0; rank < 5 && best_hits[rank]; ++rank)
    {
        printf("[debug] hot PC %u: %u instructions; bytes:",
               (unsigned)best_pc[rank], (unsigned)best_hits[rank]);
        for (uint32_t offset = 0; offset < 8 && best_pc[rank] + offset < (uint32_t)romsize; ++offset)
            printf(" %02x", (unsigned)rom[best_pc[rank] + offset]);
        putchar('\n');
    }
    memset(vm->instruction_hits, 0, (size_t)romsize * sizeof(*vm->instruction_hits));
}

int main(int argc, char **argv)
{
    const char *bytecode_path = NULL;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-debug") == 0)
            debug_enabled = 1;
        else if (!bytecode_path)
            bytecode_path = argv[i];
        else
        {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s <bytecode file> [-debug]\n", argv[0]);
            return 1;
        }
    }

    if (!bytecode_path)
    {
        fprintf(stderr, "Usage: %s <bytecode file> [-debug]\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(bytecode_path, "rb");
    if (!f)
    {
        fprintf(stderr, "Could not open %s\n", bytecode_path);
        return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "Could not seek in %s\n", bytecode_path);
        fclose(f);
        return 1;
    }
    romsize = ftell(f);
    if (romsize <= 0 || fseek(f, 0, SEEK_SET) != 0)
    {
        fprintf(stderr, "Invalid bytecode file: %s\n", bytecode_path);
        fclose(f);
        return 1;
    }
    rom = malloc((size_t)romsize);
    if (!rom)
    {
        fprintf(stderr, "Could not allocate %ld bytes for bytecode\n", romsize);
        fclose(f);
        return 1;
    }
    if (fread(rom, 1, (size_t)romsize, f) != (size_t)romsize)
    {
        fprintf(stderr, "Could not read bytecode file: %s\n", bytecode_path);
        free(rom);
        fclose(f);
        return 1;
    }
    fclose(f);
    DEBUG_PRINT("[debug] loaded bytecode %s (%ld bytes)\n", bytecode_path, romsize);

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    DEBUG_PRINT("[debug] SDL video subsystem initialized\n");
    DEBUG_PRINT("[debug] VM RAM configured: %u bytes\n", (unsigned)ULVM_MEM_SIZE);
    UnilangVM vm;
    ULVM_init(&vm, my_outputchar, my_readbyte);
    if (debug_enabled)
    {
        vm.instruction_hits = calloc((size_t)romsize, sizeof(*vm.instruction_hits));
        if (!vm.instruction_hits)
        {
            fprintf(stderr, "Could not allocate VM instruction profiling data\n");
            my_disablegraphics();
            SDL_Quit();
            free(rom);
            return 1;
        }
    }
    ULVM_set_rom(&vm, rom, (uint32_t)romsize);  // fast instruction fetch, no callback per byte
    ULVM_set_graphics(&vm, my_setpixel, my_getpixel, my_refreshscreen, my_enablegraphics, my_disablegraphics, my_clearscreen);
    ULVM_set_keyboard(&vm, my_getkeyboard);
    ULVM_set_time(&vm, my_gettime, my_sleep);
    ULVM_set_flagsupport(&vm, my_isflagsupported);

    // Run in batches and handle window events between them: far cheaper than
    // polling SDL after every single instruction.
    DEBUG_PRINT("[debug] starting VM run\n");
    uint32_t last_status_time = SDL_GetTicks();
    uint64_t completed_batches = 0;
    while (!ULVM_run(&vm, 256))
    {
        completed_batches++;
        uint32_t now = SDL_GetTicks();
        if (debug_enabled && now - last_status_time >= 1000)
        {
            printf("[debug] VM active: pc=%u, batches=%llu\n",
                   (unsigned)vm.pc, (unsigned long long)completed_batches);
            print_hot_instructions(&vm);
            last_status_time = now;
        }
        if (pump_events())
            break;
    }
    if (vm.faulted)
    {
        if (vm.fault_opcode)
            DEBUG_ERROR("[debug] VM stopped on invalid opcode 0x%02x at bytecode offset %u\n",
                        (unsigned)vm.fault_opcode, (unsigned)vm.fault_pc);
        else
            DEBUG_ERROR("[debug] VM program counter left bytecode at offset %u\n",
                        (unsigned)vm.fault_pc);
    }
    else
    {
        DEBUG_PRINT("[debug] VM run finished at bytecode offset %u\n", (unsigned)vm.pc);
    }

    my_disablegraphics();
    SDL_Quit();
    free(vm.instruction_hits);
    free(rom);
    return 0;
}