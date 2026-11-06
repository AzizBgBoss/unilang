/* Checks the predefined constants and macros against their documented values.
 * A failed check prints its name and makes main return 1, which tools/regress.py
 * reports as a failure. */

#define CHECK(cond, msg)         \
    if (!(cond))                 \
    {                            \
        printf("%s\n", msg);     \
        return 1;                \
    }

uint32_t main(void)
{
    /* keyboard bits */
    CHECK(KEY_UP == 1 && KEY_DOWN == 2 && KEY_LEFT == 4 && KEY_RIGHT == 8, "dpad bits");
    CHECK(KEY_A == 16 && KEY_B == 32 && KEY_SELECT == 64 && KEY_START == 128, "button bits");
    CHECK(KEY_DPAD == (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT), "KEY_DPAD");
    CHECK(KEY_ANY == (KEY_DPAD | KEY_A | KEY_B | KEY_SELECT | KEY_START), "KEY_ANY");

    /* flags, audio and graphics modes */
    CHECK(FLAG_GRAPHICS == 0 && FLAG_KEYBOARD == 1 && FLAG_AUDIO == 2 && FLAG_NETWORK == 15, "flag indices");
    CHECK(AUDIO_OFF == 0 && AUDIO_BEEP == 1 && AUDIO_ENGINE == 2, "audio modes");
    CHECK(GFX_OFF == 0 && GFX_MONO_64X64 == 1 && GFX_RGB332_64X64 == 4, "graphics modes 0-4");
    CHECK(GFX_MONO_128X64 == 6 && GFX_RGB332_128X64 == 9 && GFX_PAL8_128X64 == 10, "graphics modes 6-10");
    CHECK(GFX_RGB555_128X128 == 11 && GFX_RGB555_256X128 == 12, "graphics modes 11-12");

    /* color macros */
    CHECK(RGB332(255, 0, 0) == COLOR_RED && RGB332(0, 255, 0) == COLOR_GREEN, "RGB332 red/green");
    CHECK(RGB332(0, 0, 255) == COLOR_BLUE && RGB332(255, 255, 255) == COLOR_WHITE, "RGB332 blue/white");
    CHECK(RGB332(255, 128, 0) == COLOR_ORANGE && RGB332(128, 128, 128) == COLOR_GRAY, "RGB332 orange/gray");
    CHECK(RGB332(255, 255, 0) == COLOR_YELLOW && RGB332(0, 255, 255) == COLOR_CYAN, "RGB332 yellow/cyan");
    CHECK(RGB332(255, 0, 255) == COLOR_MAGENTA && RGB332(0, 0, 0) == COLOR_BLACK, "RGB332 magenta/black");
    CHECK(RGB555(255, 0, 0) == COLOR555_RED && RGB555(0, 255, 0) == COLOR555_GREEN, "RGB555 red/green");
    CHECK(RGB555(0, 0, 255) == COLOR555_BLUE && RGB555(255, 255, 255) == COLOR555_WHITE, "RGB555 blue/white");
    CHECK(RGB555(255, 128, 0) == COLOR555_ORANGE && RGB555(128, 128, 128) == COLOR555_GRAY, "RGB555 orange/gray");
    CHECK(RGB555(255, 255, 0) == COLOR555_YELLOW && RGB555(0, 255, 255) == COLOR555_CYAN, "RGB555 yellow/cyan");
    CHECK(RGB555(255, 0, 255) == COLOR555_MAGENTA && RGB555(0, 0, 0) == COLOR555_BLACK, "RGB555 magenta/black");

    /* other macros */
    CHECK(BIT(0) == 1 && BIT(7) == 128 && BIT(31) == 0x80000000, "BIT");
    CHECK(MIN(3, 5) == 3 && MIN(5, 3) == 3 && MAX(3, 5) == 5 && MAX(5, 3) == 5, "MIN/MAX");
    int32_t neg = -7;
    int32_t pos = 5;
    CHECK(MIN(neg, pos) == neg && MAX(neg, pos) == pos, "MIN/MAX with signed variables");
    CHECK(ABS(-7) == 7 && ABS(7) == 7 && ABS(0) == 0, "ABS of constants");
    CHECK(ABS(neg) == 7 && ABS(pos) == 5, "ABS of signed variables");
    CHECK(INT8_MAX == 127 && INT16_MAX == 32767 && INT32_MAX == 2147483647, "INT*_MAX");
    CHECK(INT8_MIN == 0xFFFFFF80 && INT16_MIN == 0xFFFF8000 && INT32_MIN == 0x80000000, "INT*_MIN");

    /* the names are accepted by flag() and isflagsupported(), and draw the expected pixel */
    uint32_t supported = isflagsupported(FLAG_GRAPHICS, GFX_RGB332_64X64);
    CHECK(supported <= 1, "isflagsupported with named constants");
    flag(FLAG_GRAPHICS, GFX_RGB332_64X64);
    fillscreen(COLOR_RED);
    CHECK(getpixel(0, 0) == COLOR_RED, "COLOR_RED pixel");
    fillscreen(RGB332(0, 255, 255));
    CHECK(getpixel(63, 63) == COLOR_CYAN, "RGB332 pixel");
    flag(FLAG_KEYBOARD, 1);

    return 0;
}
