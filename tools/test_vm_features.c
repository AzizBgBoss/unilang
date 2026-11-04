#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ULVM_MEM_SIZE 256
#include "../C-interpreter/unilang.h"
#include "../C-interpreter/uniaudio.h"
#include "headless_net.h"

static uint16_t pixels[8 * 8];
static unsigned writes;
static int out_of_bounds;
static uint8_t saved_block[4];
static int saved_block_valid;
static int save_failure;
static int load_failure;
static char output_buffer[512];
static size_t output_length;

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
static void output_char(char c)
{
    if (output_length < sizeof(output_buffer))
        output_buffer[output_length++] = c;
}

static void emit_setreg(uint8_t *program, size_t *pos, uint8_t reg, uint32_t value)
{
    program[(*pos)++] = 0x40;
    program[(*pos)++] = reg;
    program[(*pos)++] = (uint8_t)((value >> 24) & 0xFFu);
    program[(*pos)++] = (uint8_t)((value >> 16) & 0xFFu);
    program[(*pos)++] = (uint8_t)((value >> 8) & 0xFFu);
    program[(*pos)++] = (uint8_t)(value & 0xFFu);
}

static void test_formatted_output(void)
{
    static const uint8_t program[] = {
        0x3B, 0, 0, 0, 0, 5, 'd', 5, 255, 0,
        0x3B, 0, 0, 0, 4, 8, 'f', 0, 2, 0,
        0xFF
    };
    static const uint8_t invalid_program[] = {
        0x3B, 0, 0, 0, 0, 5, 'q', 0, 255, 0,
        0xFF
    };
    UnilangVM vm;
    ULVM_init(&vm, output_char, read_byte);
    ulvm_store_memory(&vm, 0, 4, (uint32_t)-42);
    ulvm_store_memory64(&vm, 4, UINT64_C(0x400921FB54442D18));
    output_length = 0;
    ULVM_set_rom(&vm, program, sizeof(program));
    ULVM_run(&vm, 100);
    assert(!vm.faulted);
    assert(output_length == strlen("  -423.14"));
    assert(memcmp(output_buffer, "  -423.14", output_length) == 0);

    ULVM_init(&vm, output_char, read_byte);
    ULVM_set_rom(&vm, invalid_program, sizeof(invalid_program));
    ULVM_run(&vm, 10);
    assert(vm.faulted && vm.fault_opcode == 0x3B);
}

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

static void test_indirect_control_flow(void)
{
    uint8_t call_program[] = {
        0x40, 0, 0, 0, 0, 9, /* setreg r0, subroutine address */
        0x38, 0,             /* callreg r0 */
        0xFF,                /* exit */
        0x40, 1, 0, 0, 0, 42,
        0x11                 /* return */
    };
    UnilangVM vm;
    ULVM_init(&vm, output_char, read_byte);
    ULVM_set_rom(&vm, call_program, sizeof(call_program));
    ULVM_run(&vm, 100);
    assert(!vm.faulted && !vm.running && vm.reg[1] == 42 && vm.callsp == 0);

    uint8_t jump_program[] = {
        0x40, 0, 0, 0, 0, 14,
        0x37, 0,
        0x40, 1, 0, 0, 0, 99,
        0xFF
    };
    ULVM_init(&vm, output_char, read_byte);
    ULVM_set_rom(&vm, jump_program, sizeof(jump_program));
    ULVM_run(&vm, 100);
    assert(!vm.faulted && !vm.running && vm.reg[1] == 0);

    uint8_t invalid_target[] = {
        0x40, 0, 0, 0, 0, 0xFF,
        0x37, 0,
        0xFF
    };
    ULVM_init(&vm, output_char, read_byte);
    ULVM_set_rom(&vm, invalid_target, sizeof(invalid_target));
    ULVM_run(&vm, 100);
    assert(vm.faulted && vm.fault_opcode == 0x37);

    uint8_t overflow_program[] = {0x10, 0, 0, 0, 0}; /* call self */
    ULVM_init(&vm, output_char, read_byte);
    ULVM_set_rom(&vm, overflow_program, sizeof(overflow_program));
    ULVM_run(&vm, 100);
    assert(vm.faulted && vm.fault_opcode == 0x10 &&
           vm.callsp == ULVM_CALLSTACK_SIZE);

    uint8_t return_underflow[] = {0x11};
    ULVM_init(&vm, output_char, read_byte);
    ULVM_set_rom(&vm, return_underflow, sizeof(return_underflow));
    ULVM_run(&vm, 10);
    assert(vm.faulted && vm.fault_opcode == 0x11);
}

static void test_value_stack(void)
{
    uint8_t program[] = {
        0x40, 0, 0, 0, 0, 123,
        0x39, 0,
        0x40, 0, 0, 0, 0, 0,
        0x3A, 1,
        0xFF
    };
    UnilangVM vm;
    ULVM_init(&vm, output_char, read_byte);
    ULVM_set_rom(&vm, program, sizeof(program));
    ULVM_run(&vm, 100);
    assert(!vm.faulted && !vm.running && vm.reg[1] == 123 && vm.valuesp == 0);

    uint8_t underflow[] = {0x3A, 0, 0xFF};
    ULVM_init(&vm, output_char, read_byte);
    ULVM_set_rom(&vm, underflow, sizeof(underflow));
    ULVM_run(&vm, 10);
    assert(vm.faulted && vm.fault_opcode == 0x3A && vm.valuesp == 0);

    uint8_t overflow[ULVM_VALUESTACK_SIZE * 2 + 1];
    for (unsigned i = 0; i < ULVM_VALUESTACK_SIZE; i++)
    {
        overflow[i * 2] = 0x39;
        overflow[i * 2 + 1] = 0;
    }
    overflow[ULVM_VALUESTACK_SIZE * 2] = 0x39;
    ULVM_init(&vm, output_char, read_byte);
    ULVM_set_rom(&vm, overflow, sizeof(overflow));
    ULVM_run(&vm, 1000);
    assert(vm.faulted && vm.fault_opcode == 0x39 &&
           vm.valuesp == ULVM_VALUESTACK_SIZE);
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

static void test_network_api(void)
{
    ULVMNet network;
    ULVM_net_init(&network);

    int sender = ULVM_net_open(&network, ULVM_NET_KIND_UDP, 5000, "127.0.0.1", 5001, 0);
    int receiver = ULVM_net_open(&network, ULVM_NET_KIND_UDP, 5001, "127.0.0.1", 5000, 0);
    assert(sender >= 0 && receiver >= 0);
    const char payload[] = "ping";
    assert(ULVM_net_send(&network, sender, payload, (uint32_t)sizeof(payload) - 1u) == (int)(sizeof(payload) - 1u));

    char buffer[16] = {0};
    int bytes = ULVM_net_recv(&network, receiver, buffer, sizeof(buffer));
    assert(bytes == (int)(sizeof(payload) - 1u));
    assert(memcmp(buffer, payload, sizeof(payload) - 1u) == 0);
    assert(ULVM_net_avail(&network, receiver) == 0);
    assert(ULVM_net_status(&network, receiver) == ULVM_NET_STATUS_READY);
    assert(ULVM_net_lasterror(&network) == ULVM_NET_ERR_NONE);
    assert(ULVM_net_peer(&network, sender, buffer, sizeof(buffer), NULL));
    assert(strcmp(buffer, "127.0.0.1") == 0);

    ULVM_net_close(&network, sender);
    ULVM_net_close(&network, receiver);
}

static void test_network_vm_opcodes(void)
{
    UnilangVM vm;
    static const uint8_t unsupported[] = { OP_ISFLAGSUPPORTED, ULVM_FLAG_NETWORK, 1, 4,
                                          OP_NET_LASTERROR, 5, OP_EXIT };
    ULVM_init(&vm, output_char, read_byte);
    ULVM_set_rom(&vm, unsupported, sizeof(unsupported));
    ULVM_run(&vm, 20);
    assert(!vm.faulted && vm.reg[4] == 0 && vm.reg[5] == ULVM_NET_ERR_UNSUPPORTED);

    ULVMNet net; HeadlessNetContext ctx;
    ULVM_net_init(&net); memset(&ctx, 0, sizeof(ctx));
    ctx.owner = &net; ctx.broadcast_available = 1;
    ULVM_net_set_ops(&net, &HEADLESS_NET_OPS, &ctx); ULVM_set_net(&vm, &net);
    static const uint8_t program[] = {
        0x40, 0, 0, 0, 0, ULVM_NET_KIND_UDP,
        0x40, 1, 0, 0, 0x23, 0x28, /* local port 9000 */
        0x40, 2, 0, 0, 0x23, 0x29, /* remote port 9001 */
        0x40, 3, 0, 0, 0, 0, 0, 0, /* host is NULL */
        0x40, 4, 0, 0, 0, 0,
        OP_NET_OPEN, 0, 1, 2, 3, 4, 6,
        0x40, 0, 0, 0, 0, 0,
        0x40, 1, 0, 0, 0, 10,
        0x40, 2, 0, 0, 0, 4,
        OP_NET_SEND, 0, 1, 2, 7,
        OP_NET_STATUS, 0, 8,
        OP_EXIT
    };
    ULVM_net_init(&net); memset(&ctx, 0, sizeof(ctx));
    ctx.owner = &net; ctx.broadcast_available = 1;
    ULVM_net_set_ops(&net, &HEADLESS_NET_OPS, &ctx); ULVM_set_net(&vm, &net);
    memcpy(vm.mem + 10, "ping", 4);
    ULVM_init(&vm, output_char, read_byte); ULVM_set_net(&vm, &net);
    ULVM_set_rom(&vm, program, sizeof(program));
    ULVM_run(&vm, 100);
    if (vm.faulted || vm.reg[6] != 0 || vm.reg[7] != 4)
        fprintf(stderr, "network opcode diagnostic: fault=%d opcode=%u r6=%u r7=%u r8=%u last_error=%u\n",
                vm.faulted, vm.fault_opcode, vm.reg[6], vm.reg[7], vm.reg[8], ULVM_net_lasterror(&net));
    assert(!vm.faulted && vm.reg[6] == 0 && vm.reg[7] == 4);
    assert(vm.reg[8] == ULVM_NET_STATUS_READY);

    static const uint8_t out_of_bounds[] = {
        0x40, 0, 0, 0, 0, 0,
        0x40, 1, 0, 0, 0, 255,
        0x40, 2, 0, 0, 0, 2,
        OP_NET_SEND, 0, 1, 2, 3, OP_EXIT
    };
    ULVM_init(&vm, output_char, read_byte); ULVM_set_net(&vm, &net);
    ULVM_set_rom(&vm, out_of_bounds, sizeof(out_of_bounds)); ULVM_run(&vm, 50);
    assert(!vm.faulted && vm.reg[3] == UINT32_MAX);
    assert(ULVM_net_lasterror(&net) == ULVM_NET_ERR_NO_BUFFER);
    ULVM_net_shutdown(&net);
}

static void test_headless_network_backend(void)
{
    ULVMNet net; HeadlessNetContext ctx;
    ULVM_net_init(&net); memset(&ctx, 0, sizeof(ctx));
    ctx.owner = &net; ctx.broadcast_available = 1;
    ULVM_net_set_ops(&net, &HEADLESS_NET_OPS, &ctx);

    int listener = ULVM_net_listen(&net, ULVM_NET_KIND_TCP, 7000);
    int c1 = ULVM_net_open(&net, ULVM_NET_KIND_TCP, 0, "127.0.0.1", 7000, 0);
    assert(listener == 0 && c1 == 1);
    int p1 = ULVM_net_accept(&net, listener); assert(p1 == 2);
    const char stream[] = "abcdefgh"; char received[16] = {0};
    assert(ULVM_net_send(&net, c1, stream, 8) == 3);
    assert(ULVM_net_send(&net, c1, stream + 3, 5) == 3);
    assert(ULVM_net_avail(&net, p1) == 6);
    assert(ULVM_net_recv(&net, p1, received, sizeof(received)) == 6);
    assert(memcmp(received, "abcdef", 6) == 0);
    assert(ULVM_net_option(&net, c1, ULVM_NET_OPT_FRAME_SIZE, 64));
    assert(ULVM_net_option(&net, p1, ULVM_NET_OPT_FRAME_SIZE, 64));
    assert(ULVM_net_send(&net, c1, "framed", 6) == 6);
    assert(ULVM_net_avail(&net, p1) == 6);
    assert(ULVM_net_recv(&net, p1, received, sizeof(received)) == 6);
    assert(memcmp(received, "framed", 6) == 0);

    int c2 = ULVM_net_open(&net, ULVM_NET_KIND_TCP, 0, "127.0.0.1", 7000, 0);
    int p2 = ULVM_net_accept(&net, listener); assert(c2 == 3 && p2 == 4);
    assert(ULVM_net_close(&net, c1) == 1);
    assert(ULVM_net_status(&net, p1) == ULVM_NET_STATUS_CLOSED);
    assert(ULVM_net_send(&net, c2, "ok", 2) == 2);
    assert(ULVM_net_recv(&net, p2, received, sizeof(received)) == 2);

    int http = ULVM_net_open(&net, ULVM_NET_KIND_HTTP, 0, "127.0.0.1", 80, 0);
    assert(http >= 0);
    assert(ULVM_net_http_request(&net, http, ULVM_NET_HTTP_POST, "/created",
                                 "X-Test: yes\r\n", "body", 4) == 1);
    assert(ULVM_net_http_status(&net, http) == 201);
    assert(strcmp(ctx.last_http_headers, "X-Test: yes\r\n") == 0);
    assert(ctx.last_http_method == ULVM_NET_HTTP_POST && ctx.last_http_body_len == 4);
    assert(ULVM_net_avail(&net, http) == 7);
    assert(ULVM_net_http_request(&net, http, ULVM_NET_HTTP_GET, "/missing", "", NULL, 0) == 1);
    assert(ULVM_net_http_status(&net, http) == 404);
    assert(ULVM_net_avail(&net, http) == 7);

    assert(ULVM_net_option(&net, c2, ULVM_NET_OPT_TIMEOUT_MS, 10));
    ctx.now_ms += 11; assert(ULVM_net_status(&net, c2) == ULVM_NET_STATUS_CLOSED);
    assert(ULVM_net_lasterror(&net) == ULVM_NET_ERR_TIMEOUT);

    assert(ULVM_net_discovery(&net, ULVM_NET_DISCOVERY_LISTEN, 0, 42, "") == 1);
    assert(ULVM_net_discovery_count(&net) == 1);
    assert(ULVM_net_discovery_count(&net) == 1); /* repeat announcement is deduplicated */
    ctx.now_ms += ULVM_NET_DISCOVERY_TTL_MS + 1;
    headless_net_service(&ctx, &net);
    assert(ULVM_net_discovery_count(&net) == 0);
    assert(ULVM_net_discovery(&net, 0, 0, 0, "") == 1);
    assert(ULVM_net_discovery_count(&net) == 0);
    assert(ULVM_net_discovery(&net, ULVM_NET_DISCOVERY_ANNOUNCE, 7000, 42, "game") == 1);
    ctx.broadcast_available = 0;
    assert(ULVM_net_discovery(&net, ULVM_NET_DISCOVERY_ANNOUNCE, 7000, 42, "game") == 1);
    assert(ULVM_net_lasterror(&net) == ULVM_NET_ERR_UNSUPPORTED);

    int fillers[ULVM_NET_MAX_HANDLES];
    for (unsigned i = 0; i < ULVM_NET_MAX_HANDLES; ++i)
        fillers[i] = ULVM_net_open(&net, ULVM_NET_KIND_UDP, (uint16_t)(8000 + i), NULL, 0, 0);
    assert(ULVM_net_open(&net, ULVM_NET_KIND_UDP, 9000, NULL, 0, 0) == -1);
    assert(ULVM_net_lasterror(&net) == ULVM_NET_ERR_LIMIT);
    ULVM_net_shutdown(&net);

    ULVM_net_init(&net); memset(&ctx, 0, sizeof(ctx));
    ctx.owner = &net; ctx.broadcast_available = 1;
    ULVM_net_set_ops(&net, &HEADLESS_NET_OPS, &ctx);
    listener = ULVM_net_listen(&net, ULVM_NET_KIND_TCP, 7100);
    headless_net_queue_clients(&net, listener, (uint8_t)(ULVM_NET_MAX_HANDLES + 1u));
    int accepted[ULVM_NET_MAX_HANDLES - 1u];
    for (unsigned i = 0; i < ULVM_NET_MAX_HANDLES - 1u; ++i)
    { accepted[i] = ULVM_net_accept(&net, listener); assert(accepted[i] >= 0); }
    assert(ULVM_net_accept(&net, listener) == -1);
    assert(ULVM_net_lasterror(&net) == ULVM_NET_ERR_LIMIT);
    assert(ULVM_net_close(&net, accepted[2]) == 1);
    assert(ULVM_net_status(&net, accepted[3]) == ULVM_NET_STATUS_CONNECTED);
    assert(ULVM_net_accept(&net, listener) >= 0);
    ULVM_net_shutdown(&net);
}

int main(void)
{
    test_formatted_output();
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
    test_indirect_control_flow();
    test_value_stack();
    test_audio_engine();
    test_music_markers();
    test_network_api();
    test_network_vm_opcodes();
    test_headless_network_backend();
    puts("VM feature tests passed");
    return 0;
}
