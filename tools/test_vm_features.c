#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ULVM_MEM_SIZE 256
#include "../C-interpreter/unilang.h"
#include "../C-interpreter/uniaudio.h"

static uint16_t pixels[8 * 8];
static unsigned writes;
static int out_of_bounds;
static uint8_t saved_block[4];
static int saved_block_valid;
static int save_failure;
static int load_failure;

static int save_callback(uint8_t *data, uint32_t len)
{
    if (save_failure || len != sizeof(saved_block))
        return 0;
    memcpy(saved_block, data, len);
    saved_block_valid = 1;
    return 1;
}

static int load_callback(uint8_t *data, uint32_t len)
{
    if (load_failure || !saved_block_valid || len != sizeof(saved_block))
        return 0;
    memcpy(data, saved_block, len);
    return 1;
}

static void set_pixel(uint8_t x, uint8_t y, uint16_t color)
{
    if (x >= 8 || y >= 8)
    {
        out_of_bounds = 1;
        return;
    }
    pixels[y * 8 + x] = color;
    writes++;
}

static uint8_t read_byte(uint32_t addr) { (void)addr; return 0; }
static void output_char(char c) { (void)c; }

static void reset_pixels(void)
{
    memset(pixels, 0, sizeof(pixels));
    writes = 0;
    out_of_bounds = 0;
}

static void store_word(UnilangVM *vm, uint32_t addr, uint32_t value)
{
    ulvm_store_memory(vm, addr, 4, value);
}

static void test_clipped_rectangles(UnilangVM *vm)
{
    reset_pixels();
    ulvm_fillrect(vm, -2, -3, 4, 5, 7);
    assert(writes == 4);
    assert(pixels[0] == 7 && pixels[1] == 7);
    assert(pixels[8] == 7 && pixels[9] == 7);

    reset_pixels();
    ulvm_fillrect(vm, INT_MAX, INT_MAX, INT_MAX, INT_MAX, 9);
    assert(writes == 0);
    assert(!out_of_bounds);

    const int32_t edges[4][4] = {
        {-2, 2, 4, 3}, {7, 2, 3, 3}, {2, -2, 3, 4}, {2, 7, 3, 3}
    };
    const unsigned visible_writes[4] = {6, 3, 6, 3};
    for (unsigned edge = 0; edge < 4; edge++)
    {
        reset_pixels();
        ulvm_fillrect(vm, edges[edge][0], edges[edge][1], edges[edge][2],
                      edges[edge][3], 3);
        assert(writes == visible_writes[edge] && !out_of_bounds);
    }
}

static void test_clipped_lines(UnilangVM *vm)
{
    reset_pixels();
    ulvm_drawline(vm, INT_MIN, 3, INT_MAX, 3, 5);
    assert(writes == 8);
    for (unsigned x = 0; x < 8; x++)
        assert(pixels[3 * 8 + x] == 5);
    assert(!out_of_bounds);

    reset_pixels();
    ulvm_drawline(vm, 4, INT_MAX, 4, INT_MIN, 6);
    assert(writes == 8);
    for (unsigned y = 0; y < 8; y++)
        assert(pixels[y * 8 + 4] == 6);
    assert(!out_of_bounds);

    reset_pixels();
    ulvm_drawline(vm, INT_MIN, INT_MIN, -1, -1, 1);
    assert(writes == 0);
}

static void test_clipped_glyph(UnilangVM *vm)
{
    reset_pixels();
    ulvm_drawchar(vm, -2, -1, 1, 'A');
    assert(!out_of_bounds);
    for (unsigned i = 0; i < 64; i++)
        assert(pixels[i] <= 1);
    ulvm_drawchar(vm, 7, 7, 1, 'B');
    assert(!out_of_bounds);
}

static void test_clipped_string(UnilangVM *vm)
{
    reset_pixels();
    vm->mem[200] = 'A';
    vm->mem[201] = 'B';
    int32_t cursor_x, cursor_y;
    ulvm_drawstring_mem(vm, -2, 0, 1, 200, 2, 0, &cursor_x, &cursor_y);
    assert(!out_of_bounds);
    assert(cursor_x == 0 && cursor_y == 0);
}

static void set_blit_descriptor(UnilangVM *vm, int32_t x, int32_t y, int32_t sx, int32_t sy)
{
    store_word(vm, 0, (uint32_t)x);
    store_word(vm, 4, (uint32_t)y);
    store_word(vm, 8, 2);
    store_word(vm, 12, 2);
    store_word(vm, 16, 0x80);
    store_word(vm, 20, (uint32_t)sx);
    store_word(vm, 24, (uint32_t)sy);
    store_word(vm, 28, 2);
    store_word(vm, 32, 32);
    store_word(vm, 36, 0);
    store_word(vm, 40, 0);
}

static void test_clipped_blit(UnilangVM *vm)
{
    reset_pixels();
    vm->mem[0x80] = 1;
    vm->mem[0x81] = 2;
    vm->mem[0x82] = 3;
    vm->mem[0x83] = 4;
    set_blit_descriptor(vm, -1, -1, 0, 0);
    ulvm_blit(vm, 0);
    assert(writes == 1);
    assert(pixels[0] == 4);
    assert(!out_of_bounds);

    const int32_t positions[4][2] = {{-1, 3}, {7, 3}, {3, -1}, {3, 7}};
    const uint16_t expected[4][2] = {{2, 4}, {1, 3}, {3, 4}, {1, 2}};
    for (unsigned edge = 0; edge < 4; edge++)
    {
        reset_pixels();
        set_blit_descriptor(vm, positions[edge][0], positions[edge][1], 0, 0);
        ulvm_blit(vm, 0);
        assert(writes == 2 && !out_of_bounds);
        if (edge < 2)
        {
            unsigned x = edge == 0 ? 0 : 7;
            unsigned y = 3;
            assert(pixels[y * 8 + x] == expected[edge][0]);
            assert(pixels[(y + 1) * 8 + x] == expected[edge][1]);
        }
        else
        {
            unsigned x = 3, y = edge == 2 ? 0 : 7;
            assert(pixels[y * 8 + x] == expected[edge][0]);
            assert(pixels[y * 8 + x + 1] == expected[edge][1]);
        }
    }

    reset_pixels();
    set_blit_descriptor(vm, 2, 2, -1, 0);
    ulvm_blit(vm, 0);
    assert(writes == 2 && !out_of_bounds);
    assert(pixels[2 * 8 + 3] == 1 && pixels[3 * 8 + 3] == 3);
}

static void test_save_load_opcodes(void)
{
    uint8_t program[] = {
        0x40, 0, 0, 0, 0, 0, /* setreg r0, 0 */
        0x40, 1, 0, 0, 0, 4, /* setreg r1, 4 */
        0x28, 0, 1, 2,       /* save r0, r1, r2 */
        0x03, 0, 0, 0, 0, 0, /* setbyte 0, 0 */
        0x29, 0, 1, 3,       /* load r0, r1, r3 */
        0xFF                  /* exit */
    };
    UnilangVM vm;
    ULVM_init(&vm, output_char, read_byte);
    vm.rom = program;
    vm.rom_size = sizeof(program);
    vm.mem[0] = 0x12;
    vm.mem[1] = 0x34;
    vm.mem[2] = 0x56;
    vm.mem[3] = 0x78;
    saved_block_valid = 0;
    ULVM_set_storage(&vm, save_callback, load_callback);
    while (vm.running)
        ULVM_run(&vm, 32);
    assert(!vm.faulted);
    assert(vm.reg[2] == 1 && vm.reg[3] == 1);
    assert(vm.mem[0] == 0x12 && vm.mem[1] == 0x34);
    assert(vm.mem[2] == 0x56 && vm.mem[3] == 0x78);

    uint8_t storage_opcode[] = {0x28, 0, 1, 2, 0xFF};
    ULVM_init(&vm, output_char, read_byte);
    vm.rom = storage_opcode;
    vm.rom_size = sizeof(storage_opcode);
    vm.reg[0] = ULVM_MEM_SIZE - 1;
    vm.reg[1] = 4;
    ULVM_set_storage(&vm, save_callback, load_callback);
    while (vm.running)
        ULVM_run(&vm, 8);
    assert(!vm.faulted && vm.reg[2] == 0);

    ULVM_init(&vm, output_char, read_byte);
    vm.rom = storage_opcode;
    vm.rom_size = sizeof(storage_opcode);
    vm.reg[0] = 0;
    vm.reg[1] = 4;
    save_failure = 1;
    ULVM_set_storage(&vm, save_callback, load_callback);
    while (vm.running)
        ULVM_run(&vm, 8);
    assert(!vm.faulted && vm.reg[2] == 0);
    save_failure = 0;

    uint8_t load_opcode[] = {0x29, 0, 1, 2, 0xFF};
    ULVM_init(&vm, output_char, read_byte);
    vm.rom = load_opcode;
    vm.rom_size = sizeof(load_opcode);
    vm.reg[0] = 0;
    vm.reg[1] = 4;
    memset(vm.mem, 0xAA, 4);
    load_failure = 1;
    ULVM_set_storage(&vm, save_callback, load_callback);
    while (vm.running)
        ULVM_run(&vm, 8);
    assert(!vm.faulted && vm.reg[2] == 0);
    for (unsigned i = 0; i < 4; i++) assert(vm.mem[i] == 0xAA);
    load_failure = 0;
}

static void test_audio_engine(void)
{
    Uniaudio audio;
    uniaudio_init(&audio);
    uint8_t sfx[UNIAUDIO_SFX_BYTES] = {0, 16, 0, 0};
    for (uint32_t i = 0; i < UNIAUDIO_SFX_NOTES; i++)
    {
        sfx[4 + i * 2] = 0xF9;
        sfx[5 + i * 2] = 0x0E;
    }
    assert(uniaudio_load(&audio, 0, 0, sfx, sizeof(sfx)));
    uint8_t music[UNIAUDIO_MUSIC_BYTES] = {0, 65, 66, 67};
    assert(uniaudio_load(&audio, 1, 0, music, sizeof(music)));
    assert(uniaudio_command(&audio, ULVM_AUDIO_SFX, 0, UINT32_MAX) == 0);
    assert(uniaudio_command(&audio, ULVM_AUDIO_STATUS, 0, 0) == 0);
    assert(uniaudio_command(&audio, ULVM_AUDIO_SFX, 0, 1) == 0);
    assert(uniaudio_command(&audio, ULVM_AUDIO_STATUS, 1, 0) == 0);
    assert(uniaudio_command(&audio, ULVM_AUDIO_SFX, 0, UINT32_MAX) == 0);
    assert(uniaudio_command(&audio, ULVM_AUDIO_STATUS, 2, 0) == 0);
    int16_t samples[2048];
    uniaudio_render(&audio, samples, 2048);
    int nonzero = 0, crossings = 0;
    for (unsigned i = 0; i < 2048; i++)
    {
        nonzero |= samples[i] != 0;
        if (i && samples[i - 1] < 0 && samples[i] >= 0)
            crossings++;
    }
    assert(nonzero && crossings >= 35 && crossings <= 55);
    assert(uniaudio_command(&audio, ULVM_AUDIO_MUSIC, 0, 0) == 0);
    assert(uniaudio_command(&audio, ULVM_AUDIO_STATUS, UINT32_MAX, 0) == 0);
    uniaudio_command(&audio, ULVM_AUDIO_PAUSE, 0, 0);
    uniaudio_render(&audio, samples, 32);
    for (unsigned i = 0; i < 32; i++) assert(samples[i] == 0);
    uniaudio_command(&audio, ULVM_AUDIO_RESUME, 0, 0);
    uniaudio_command(&audio, ULVM_AUDIO_STOP, 0, 0);
    assert(uniaudio_command(&audio, ULVM_AUDIO_STATUS, UINT32_MAX, 0) == UINT32_MAX);
    int16_t beep_samples[UNIAUDIO_SAMPLE_RATE / 10u];
    uniaudio_beep(&audio, 440, 100);
    assert(audio.beep_samples_total == UNIAUDIO_SAMPLE_RATE / 10u);
    uniaudio_render(&audio, beep_samples, UNIAUDIO_SAMPLE_RATE / 10u - 1u);
    assert(audio.beep_active);
    uniaudio_render(&audio, beep_samples, 1);
    assert(audio.beep_active == 0);
}

static void test_music_markers(void)
{
    Uniaudio audio;
    uniaudio_init(&audio);
    uint8_t sfx[UNIAUDIO_SFX_BYTES] = {0, 1, 0, 0};
    assert(uniaudio_load(&audio, 0, 0, sfx, sizeof(sfx)));
    uint8_t loop_start[UNIAUDIO_MUSIC_BYTES] = {0x80, 65, 66, 67};
    uint8_t loop_back[UNIAUDIO_MUSIC_BYTES] = {65, 0xC1, 66, 67};
    uint8_t stop[UNIAUDIO_MUSIC_BYTES] = {0, 65, 0xC2, 67};
    assert(uniaudio_load(&audio, 1, 0, loop_start, sizeof(loop_start)));
    assert(uniaudio_load(&audio, 1, 1, loop_back, sizeof(loop_back)));
    assert(uniaudio_load(&audio, 1, 2, stop, sizeof(stop)));
    assert(uniaudio_command(&audio, ULVM_AUDIO_MUSIC, 0, 0) == 0);
    int16_t samples[6000];
    uniaudio_render(&audio, samples, 6000);
    assert(audio.music_playing && audio.music_pattern == 0);
    assert(uniaudio_command(&audio, ULVM_AUDIO_MUSIC, 2, 0) == 0);
    uniaudio_render(&audio, samples, 6000);
    assert(!audio.music_playing);
}

int main(void)
{
    UnilangVM vm;
    ULVM_init(&vm, output_char, read_byte);
    vm.gfx_w = 8;
    vm.gfx_h = 8;
    vm.setpixel = set_pixel;
    test_clipped_rectangles(&vm);
    test_clipped_lines(&vm);
    test_clipped_glyph(&vm);
    test_clipped_string(&vm);
    test_clipped_blit(&vm);
    test_save_load_opcodes();
    test_audio_engine();
    test_music_markers();
    puts("VM feature tests passed");
    return 0;
}
