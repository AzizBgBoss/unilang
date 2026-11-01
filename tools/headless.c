/*
 * Headless, deterministic unilang host for regression and speed testing.
 *
 *   cc -O2 -std=c11 -I C-interpreter tools/headless.c -o tools/headless
 *   tools/headless prog.ulc [-frames N] [-instr N] [-keys SEED] [-hashes] [-quiet] [-count]
 *
 * Time, keyboard input and the random seed only depend on how many frames
 * the program has presented and how many times it asked for the time, so
 * two builds of the same program that behave identically produce identical
 * text output and per-frame framebuffer hashes, however many VM
 * instructions each one executes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef ULVM_MEM_SIZE
#define ULVM_MEM_SIZE 8192
#endif
#include "unilang.h"

static uint8_t *rom;
static long romsize;
static uint16_t fb[256 * 128];
static uint16_t gw, gh;
static uint8_t gbits;
static uint32_t frames, max_frames = 300, time_calls;
static uint32_t key_seed = 1;
static int print_hashes, quiet;
static uint64_t hash_all = 1469598103934665603ull;
static UnilangVM vm;
static int count_instr;

static void out_char(char c) { if (!quiet) putchar(c); hash_all = (hash_all ^ (uint8_t)c) * 1099511628211ull; }
static uint8_t read_byte(uint32_t a) { return a < (uint32_t)romsize ? rom[a] : 0; }
static void set_pixel(uint8_t x, uint8_t y, uint16_t c)
{
    if (!gbits || x >= gw || y >= gh) return;
    fb[y * 256 + x] = gbits == 1 ? (c ? 1 : 0) : gbits == 4 ? (c & 3) : gbits == 8 ? (c & 0xFF) : (c & 0x7FFF);
}
static uint16_t get_pixel(uint8_t x, uint8_t y) { return (!gbits || x >= gw || y >= gh) ? 0 : fb[y * 256 + x]; }
static void clear_screen(void) { memset(fb, 0, sizeof fb); }
static void enable_gfx(uint16_t w, uint16_t h, uint8_t b) { gw = w; gh = h; gbits = b; clear_screen(); }
static void disable_gfx(void) { gw = gh = 0; gbits = 0; }
static void refresh(void)
{
    uint64_t h = 1469598103934665603ull;
    for (uint32_t y = 0; y < gh; y++)
        for (uint32_t x = 0; x < gw; x++)
            h = (h ^ fb[y * 256 + x]) * 1099511628211ull;
    hash_all = (hash_all ^ h) * 1099511628211ull;
    if (print_hashes) printf("frame %u %016llx\n", (unsigned)frames, (unsigned long long)h);
    frames++;
    if (frames >= max_frames) vm.running = 0;  /* stop exactly here */
}
static uint8_t get_keys(void)
{
    /* changes every 6 frames; right is held most of the time, jumps often */
    uint32_t x = (frames / 6 + 1) * 2654435761u ^ key_seed * 40503u;
    x ^= x >> 13; x *= 0x5bd1e995u; x ^= x >> 15;
    uint8_t k = (uint8_t)(x & 0x77);
    if ((x >> 8) % 4 != 0) k |= 8;           /* right */
    if (k & 8) k &= (uint8_t)~4;             /* not left+right */
    if ((key_seed & 1) && (frames / 6) % 25 == 1) k |= 128;  /* odd seeds press Enter now and then */
    return k;
}
static uint32_t get_time(void) { time_calls++; return frames * 16 + time_calls; }
static void do_sleep(uint32_t ms) { (void)ms; }
static int flag_ok(uint8_t f, uint8_t v) { return f == 0 ? (v >= 1 && v <= 8) : f == 1 ? v == 1 : 0; }

int main(int argc, char **argv)
{
    const char *path = NULL;
    uint64_t max_instr = 2000000000ull;
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-frames") && i + 1 < argc) max_frames = (uint32_t)strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-instr") && i + 1 < argc) max_instr = strtoull(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-keys") && i + 1 < argc) key_seed = (uint32_t)strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-hashes")) print_hashes = 1;
        else if (!strcmp(argv[i], "-quiet")) quiet = 1;
        else if (!strcmp(argv[i], "-count")) count_instr = 1;
        else path = argv[i];
    }
    if (!path) { fprintf(stderr, "usage: %s prog.ulc [-frames N] [-instr N] [-keys SEED] [-hashes] [-quiet]\n", argv[0]); return 2; }
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    fseek(f, 0, SEEK_END); romsize = ftell(f); fseek(f, 0, SEEK_SET);
    rom = malloc((size_t)romsize);
    if (fread(rom, 1, (size_t)romsize, f) != (size_t)romsize) return 2;
    fclose(f);

    ULVM_init(&vm, out_char, read_byte);
    ULVM_set_rom(&vm, rom, (uint32_t)romsize);
    ULVM_set_graphics(&vm, set_pixel, get_pixel, refresh, enable_gfx, disable_gfx, clear_screen);
    ULVM_set_keyboard(&vm, get_keys);
    ULVM_set_time(&vm, get_time, do_sleep);
    ULVM_set_flagsupport(&vm, flag_ok);

    if (count_instr) vm.instruction_hits = calloc((size_t)romsize + 1, sizeof(uint32_t));
    uint64_t executed = 0;
    clock_t t0 = clock();
    while (vm.running && frames < max_frames && executed < max_instr)
    {
        uint32_t chunk = 4096;
        ULVM_run(&vm, chunk);
        executed += chunk;   /* upper bound; exact count below */
    }
    if (count_instr)
    {
        executed = 0;
        for (long i = 0; i < romsize; i++) executed += vm.instruction_hits[i];
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    fprintf(stderr, "instr=%llu frames=%u faulted=%d fault_pc=%u op=0x%02x secs=%.3f digest=%016llx\n",
            (unsigned long long)executed, (unsigned)frames, vm.faulted, (unsigned)vm.fault_pc, vm.fault_opcode, secs,
            (unsigned long long)hash_all);
    printf("\nDIGEST %016llx frames=%u faulted=%d\n", (unsigned long long)hash_all, (unsigned)frames, vm.faulted);
    return 0;
}
