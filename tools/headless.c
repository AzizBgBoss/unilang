/*
 * Headless, deterministic unilang host for regression and speed testing.
 *
 *   cc -O2 -std=c11 -I C-interpreter tools/headless.c -o tools/headless
 *   tools/headless prog.ulc [-frames N] [-instr N] [-keys SEED] [-hashes] [-quiet] [-count] [-dump FRAME out.ppm]
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
#include <limits.h>
#ifndef ULVM_MEM_SIZE
#define ULVM_MEM_SIZE 8192
#endif
#include "unilang.h"
#include "../C-interpreter/uniaudio.h"
#include "headless_net.h"

static uint8_t *rom;
static long romsize;
static uint16_t fb[256 * 128];
static uint16_t gw, gh;
static uint8_t gbits;
static uint16_t pal[256];
static const char *dump_path;
static uint32_t dump_frame;
static uint32_t frames, max_frames = 300, time_calls;
static uint32_t key_seed = 1;
static int print_hashes, quiet;
static const char *wav_path;
static int16_t *wav_samples;
static size_t wav_count, wav_capacity;
static uint32_t wav_fraction;
static int wav_error;
static Uniaudio audio_engine;
static uint8_t save_data[ULVM_MEM_SIZE];
static uint32_t save_size;
static int save_valid;
static uint64_t hash_all = 1469598103934665603ull;
static UnilangVM vm;
static ULVMNet headless_net;
static HeadlessNetContext headless_net_context;
static int count_instr;

static uint16_t normalize_pixel_color(uint16_t c)
{
    return (gbits & 0x80) ? (c & (gbits == 0x82 ? 3 : 0xFF)) : gbits == 1 ? (c ? 1 : 0) : gbits == 4 ? (c & 3) : gbits == 8 ? (c & 0xFF) : (c & 0x7FFF);
}

static void advance_audio_ms(uint32_t ms)
{
    if (!wav_path || wav_error)
        return;
    uint64_t total = (uint64_t)UNIAUDIO_SAMPLE_RATE * ms + wav_fraction;
    uint64_t remaining = total / 1000u;
    size_t max_samples = (UINT32_MAX - 36u) / 2u;
    wav_fraction = (uint32_t)(total % 1000u);
    int16_t buffer[512];
    while (remaining)
    {
        uint32_t chunk = remaining > 512u ? 512u : (uint32_t)remaining;
        if (wav_count > SIZE_MAX - chunk || wav_count + chunk > max_samples)
        {
            wav_error = 1;
            return;
        }
        size_t needed = wav_count + chunk;
        if (needed > wav_capacity)
        {
            size_t capacity = wav_capacity ? wav_capacity : 4096;
            while (capacity < needed && capacity <= max_samples / 2)
                capacity *= 2;
            if (capacity < needed || capacity > SIZE_MAX / sizeof(*wav_samples))
            {
                wav_error = 1;
                return;
            }
            int16_t *samples = (int16_t *)realloc(wav_samples, capacity * sizeof(*samples));
            if (!samples)
            {
                wav_error = 1;
                return;
            }
            wav_samples = samples;
            wav_capacity = capacity;
        }
        uniaudio_render(&audio_engine, buffer, chunk);
        memcpy(wav_samples + wav_count, buffer, chunk * sizeof(*buffer));
        wav_count = needed;
        remaining -= chunk;
    }
}

static void out_char(char c) { if (!quiet) putchar(c); hash_all = (hash_all ^ (uint8_t)c) * 1099511628211ull; }
static uint8_t read_byte(uint32_t a) { return a < (uint32_t)romsize ? rom[a] : 0; }
static void set_pixel(uint8_t x, uint8_t y, uint16_t c)
{
    if (!gbits || x >= gw || y >= gh) return;
    fb[y * 256 + x] = normalize_pixel_color(c);
}
static void fill_rect_fast(uint8_t x, uint8_t y, uint16_t w, uint16_t h, uint16_t c)
{
    if (!gbits || x >= gw || y >= gh) return;
    if (w > gw - x) w = (uint16_t)(gw - x);
    if (h > gh - y) h = (uint16_t)(gh - y);
    c = normalize_pixel_color(c);
    for (uint16_t row = 0; row < h; row++)
        for (uint16_t col = 0; col < w; col++)
            fb[(size_t)(y + row) * 256 + x + col] = c;
}
static void blit_row_fast(uint8_t x, uint8_t y, uint16_t count,
                          const uint16_t *colors, const uint8_t *opaque)
{
    if (!gbits || x >= gw || y >= gh) return;
    if (count > gw - x) count = (uint16_t)(gw - x);
    size_t start = (size_t)y * 256 + x;
    for (uint16_t col = 0; col < count; col++)
        if (opaque[col])
            fb[start + col] = normalize_pixel_color(colors[col]);
}
static uint16_t get_pixel(uint8_t x, uint8_t y) { return (!gbits || x >= gw || y >= gh) ? 0 : fb[y * 256 + x]; }
static void clear_screen(void) { memset(fb, 0, sizeof fb); }
static void set_palette(uint8_t i, uint16_t c) { pal[i] = c & 0x7FFF; }
static void enable_gfx(uint16_t w, uint16_t h, uint8_t b)
{
    gw = w; gh = h; gbits = b;
    for (int i = 0; i < 256; i++)
    {
        if (b == 0x82) { uint16_t v = (uint16_t)((i & 3) * 31 / 3); pal[i] = (uint16_t)((v << 10) | (v << 5) | v); }
        else pal[i] = (uint16_t)(((((i >> 5) & 7) * 31 / 7) << 10) | ((((i >> 2) & 7) * 31 / 7) << 5) | ((i & 3) * 31 / 3));
    }
    clear_screen();
}
/* resolve a framebuffer value to RGB555, mirroring the SDL host */
static uint16_t to_rgb555(uint16_t c)
{
    if (gbits & 0x80) return pal[c & 0xFF];
    if (gbits == 1) return c ? 0x7FFF : 0;
    if (gbits == 4) { uint16_t v = (uint16_t)((c & 3) * 31 / 3); return (uint16_t)((v << 10) | (v << 5) | v); }
    if (gbits == 16) return c & 0x7FFF;
    return (uint16_t)(((((c >> 5) & 7) * 31 / 7) << 10) | ((((c >> 2) & 7) * 31 / 7) << 5) | ((c & 3) * 31 / 3));
}
static void dump_ppm(void)
{
    FILE *f = fopen(dump_path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", (unsigned)gw, (unsigned)gh);
    for (uint32_t y = 0; y < gh; y++)
        for (uint32_t x = 0; x < gw; x++)
        {
            uint16_t c = to_rgb555(fb[y * 256 + x]);
            fputc(((c >> 10) & 31) * 255 / 31, f); fputc(((c >> 5) & 31) * 255 / 31, f); fputc((c & 31) * 255 / 31, f);
        }
    fclose(f);
}
static void disable_gfx(void) { gw = gh = 0; gbits = 0; }
static void refresh(void)
{
    headless_net_context.now_ms += 16u;
    uint64_t h = 1469598103934665603ull;
    if (dump_path && frames == dump_frame) dump_ppm();
    for (uint32_t y = 0; y < gh; y++)
        for (uint32_t x = 0; x < gw; x++)
            h = (h ^ ((gbits & 0x80) ? pal[fb[y * 256 + x] & 0xFF] : fb[y * 256 + x])) * 1099511628211ull;
    hash_all = (hash_all ^ h) * 1099511628211ull;
    if (print_hashes) printf("frame %u %016llx\n", (unsigned)frames, (unsigned long long)h);
    frames++;
    if (frames >= max_frames) vm.running = 0;  /* stop exactly here */
    advance_audio_ms(16);
}
static uint8_t get_keys(void)
{
    /* changes every 6 frames; right is held most of the time, jumps often */
    uint32_t x = (frames / 6 + 1) * 2654435761u ^ key_seed * 40503u;
    x ^= x >> 13; x *= 0x5bd1e995u; x ^= x >> 15;
    uint8_t k = (uint8_t)(x & 0x77);
    if ((x >> 8) % 4 != 0) k |= 8;           /* right */
    if (k & 8) k &= (uint8_t)~4;             /* not left+right */
    if ((key_seed & 1) && (frames / 6) % 25 == 1) k |= 128;  /* odd seeds press Start now and then */
    return k;
}
static uint32_t get_time(void) { time_calls++; headless_net_context.now_ms++; return frames * 16 + time_calls; }
static void do_sleep(uint32_t ms) { headless_net_context.now_ms += ms; advance_audio_ms(ms); }
static int flag_ok(uint8_t f, uint8_t v)
{
    return f == 0 ? (v >= 1 && v <= 12) : f == 1 ? v == 1 : f == 2 ? (v == 1 || v == 2) : 0;
}

static int save_block(uint8_t *data, uint32_t len)
{
    if (len > ULVM_MEM_SIZE)
        return 0;
    memcpy(save_data, data, len);
    save_size = len;
    save_valid = 1;
    return 1;
}

static int load_block(uint8_t *data, uint32_t len)
{
    if (!save_valid || len > save_size)
        return 0;
    memcpy(data, save_data, len);
    return 1;
}

static void headless_beep(uint32_t frequency, uint32_t duration_ms)
{
    uniaudio_beep(&audio_engine, frequency, duration_ms);
}

static uint32_t headless_audio_command(uint8_t command, uint32_t a, uint32_t b)
{
    return uniaudio_command(&audio_engine, command, a, b);
}

static int headless_audio_load(uint8_t kind, uint8_t index, const uint8_t *data, uint32_t len)
{
    return uniaudio_load(&audio_engine, kind, index, data, len);
}

static void write_u32le(FILE *f, uint32_t value)
{
    fputc((int)(value & 0xFFu), f);
    fputc((int)((value >> 8) & 0xFFu), f);
    fputc((int)((value >> 16) & 0xFFu), f);
    fputc((int)((value >> 24) & 0xFFu), f);
}

static int write_wav(void)
{
    if (!wav_path)
        return 1;
    if (wav_error || wav_count > (UINT32_MAX - 36u) / 2u)
    {
        fprintf(stderr, "WAV output exceeded the supported size or could not allocate audio data\n");
        return 0;
    }
    FILE *f = fopen(wav_path, "wb");
    if (!f)
    {
        perror(wav_path);
        return 0;
    }
    uint32_t data_bytes = (uint32_t)(wav_count * 2u);
    int ok = fwrite("RIFF", 1, 4, f) == 4;
    write_u32le(f, data_bytes + 36u);
    ok = ok && fwrite("WAVEfmt ", 1, 8, f) == 8;
    write_u32le(f, 16u);
    ok = ok && fwrite("\x01\x00\x01\x00", 1, 4, f) == 4;
    write_u32le(f, UNIAUDIO_SAMPLE_RATE);
    write_u32le(f, UNIAUDIO_SAMPLE_RATE * 2u);
    ok = ok && fwrite("\x02\x00\x10\x00", 1, 4, f) == 4;
    ok = ok && fwrite("data", 1, 4, f) == 4;
    write_u32le(f, data_bytes);
    for (size_t i = 0; ok && i < wav_count; i++)
    {
        uint16_t sample = (uint16_t)wav_samples[i];
        fputc((int)(sample & 0xFFu), f);
        fputc((int)(sample >> 8), f);
        if (ferror(f))
            ok = 0;
    }
    if (ferror(f))
        ok = 0;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok)
        fprintf(stderr, "Failed writing WAV output: %s\n", wav_path);
    return ok;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    uint64_t max_instr = 2000000000ull;
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-frames") && i + 1 < argc) max_frames = (uint32_t)strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-instr") && i + 1 < argc) max_instr = strtoull(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-keys") && i + 1 < argc) key_seed = (uint32_t)strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-dump") && i + 2 < argc) { dump_frame = (uint32_t)strtoul(argv[++i], 0, 0); dump_path = argv[++i]; }
        else if (!strcmp(argv[i], "-hashes")) print_hashes = 1;
        else if (!strcmp(argv[i], "-quiet")) quiet = 1;
        else if (!strcmp(argv[i], "-count")) count_instr = 1;
        else if (!strcmp(argv[i], "-wav") && i + 1 < argc) wav_path = argv[++i];
        else path = argv[i];
    }
    if (!path) { fprintf(stderr, "usage: %s prog.ulc [-frames N] [-instr N] [-keys SEED] [-hashes] [-quiet] [-wav out.wav]\n", argv[0]); return 2; }
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    fseek(f, 0, SEEK_END); romsize = ftell(f); fseek(f, 0, SEEK_SET);
    rom = malloc((size_t)romsize);
    if (fread(rom, 1, (size_t)romsize, f) != (size_t)romsize) return 2;
    fclose(f);

    ULVM_init(&vm, out_char, read_byte);
    ULVM_net_init(&headless_net);
    memset(&headless_net_context, 0, sizeof(headless_net_context));
    headless_net_context.owner = &headless_net;
    headless_net_context.broadcast_available = 1;
    ULVM_net_set_ops(&headless_net, &HEADLESS_NET_OPS, &headless_net_context);
    ULVM_set_net(&vm, &headless_net);
    uniaudio_init(&audio_engine);
    ULVM_set_rom(&vm, rom, (uint32_t)romsize);
    ULVM_set_graphics(&vm, set_pixel, get_pixel, refresh, enable_gfx, disable_gfx, clear_screen);
#ifndef ULVM_DISABLE_GRAPHICS_FAST
    ULVM_set_graphics_fast(&vm, fill_rect_fast, blit_row_fast);
#endif
    ULVM_set_keyboard(&vm, get_keys);
    ULVM_set_time(&vm, get_time, do_sleep);
    ULVM_set_flagsupport(&vm, flag_ok);
    ULVM_set_palette(&vm, set_palette);
    ULVM_set_storage(&vm, save_block, load_block);
    ULVM_set_audio(&vm, headless_beep, headless_audio_command, headless_audio_load);

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
    fprintf(stderr, "instr=%llu frames=%u faulted=%d fault_pc=%u op=0x%02x secs=%.3f digest=%016llx exited=%d exit_code=%u\n",
            (unsigned long long)executed, (unsigned)frames, vm.faulted, (unsigned)vm.fault_pc, vm.fault_opcode, secs,
            (unsigned long long)hash_all, !vm.running && !vm.faulted, (unsigned)vm.exit_code);
    printf("\nDIGEST %016llx frames=%u faulted=%d\n", (unsigned long long)hash_all, (unsigned)frames, vm.faulted);
    int result = write_wav() ? 0 : 2;
    ULVM_net_shutdown(&headless_net);
    free(wav_samples);
    free(vm.instruction_hits);
    free(rom);
    return result;
}
