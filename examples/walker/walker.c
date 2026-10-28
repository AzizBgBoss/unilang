// Walker: a tour of graphics flag 0, modes 1 to 8, five seconds each.
//   1 mono 64x64      2 gray 64x64      3 RGB332 64x64
//   4 mono 128x64     5 gray 128x64     6 RGB332 128x64
//   7 RGB555 128x128  8 RGB555 256x128
// A kid walks through a scrolling dusk landscape. Press Enter to quit.

#define RGB332(r, g, b) (((r) << 5) | ((g) << 2) | (b))
#define RGB555(r, g, b) (((r) << 10) | ((g) << 5) | (b))

// palette slots (16 per color class)
#define C_SUN 0
#define C_FAR 1
#define C_NEAR 2
#define C_GROUND 3
#define C_GRASS 4
#define C_SKIN 5
#define C_HAIR 6
#define C_SHIRT 7
#define C_PANTS 8
#define C_SHOE 9
#define C_CLOUD 10
#define C_WHITE 11
#define C_PATH 12
#define C_SHADOW 13
#define C_BAR 14

const uint16_t pal[64] = {
    // class 0, mono: 0 black, 1 white, 2 checker, 3 sparse dots
    1, 3, 0, 0, 1, 1, 1, 2, 1, 1, 2, 1, 3, 0, 1, 0,
    // class 1, 2-bit gray: 0 to 3
    3, 1, 0, 1, 2, 3, 2, 2, 3, 2, 3, 3, 2, 0, 3, 0,
    // class 2, RGB332
    RGB332(7, 7, 1), RGB332(3, 2, 2), RGB332(1, 3, 1), RGB332(3, 2, 0),
    RGB332(1, 6, 0), RGB332(7, 5, 2), RGB332(2, 1, 0), RGB332(7, 1, 1),
    RGB332(1, 2, 3), RGB332(1, 1, 1), RGB332(7, 6, 3), 255,
    RGB332(4, 3, 1), RGB332(0, 1, 1), 255, 0,
    // class 3, RGB555
    RGB555(31, 29, 6), RGB555(14, 10, 18), RGB555(3, 13, 6), RGB555(17, 11, 5),
    RGB555(7, 24, 4), RGB555(31, 23, 17), RGB555(9, 5, 2), RGB555(31, 5, 6),
    RGB555(4, 9, 22), RGB555(5, 4, 3), RGB555(31, 28, 29), 32767,
    RGB555(21, 15, 8), RGB555(1, 3, 5), 32767, 0
};

// per mode (index 1 to 8): width, height, color class
const uint16_t pw[9] = {0, 64, 64, 64, 128, 128, 128, 128, 256};
const uint16_t ph[9] = {0, 64, 64, 64, 64, 64, 64, 128, 128};
const uint8_t pcl[9] = {0, 0, 1, 2, 0, 1, 2, 3, 3};

// 2x2 ordered dither thresholds and the 4 frame leg positions (in sprite units)
const uint8_t bay[4] = {51, 153, 204, 102};
const uint8_t legl[4] = {0, 4, 8, 4};
const uint8_t legr[4] = {8, 4, 0, 4};

uint16_t skyc[128];
uint32_t SW;
uint32_t SH;
uint32_t CL;
uint32_t SC;
uint32_t GY;
uint32_t SUNX;
uint32_t SUNY;
uint32_t SUNR2;

uint32_t tri(uint32_t p, uint32_t period)
{
    uint32_t pos = p % period;
    uint32_t half = period / 2;
    if (pos < half)
        return pos;
    return period - pos;
}

// plot one pixel by palette slot; mono dithering patterns are handled here
uint32_t plot(uint32_t x, uint32_t y, uint32_t idx)
{
    if (x < SW && y < SH)
    {
        uint32_t v = pal[CL * 16 + idx];
        if (CL == 0)
        {
            if (v == 2)
                v = (x + y) & 1;
            else if (v == 3)
                v = x & y & 1;
        }
        setpixel(x, y, v);
    }
    return 0;
}

uint32_t rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t idx)
{
    uint32_t j = 0;
    while (j < h)
    {
        uint32_t i = 0;
        while (i < w)
        {
            plot(x + i, y + j, idx);
            i = i + 1;
        }
        j = j + 1;
    }
    return 0;
}

// one gradient value per row, quantized to the current mode's color depth
uint32_t build_sky()
{
    uint32_t y = 0;
    while (y < SH)
    {
        uint32_t t = y * 255 / GY;
        if (t > 255)
            t = 255;
        uint32_t r = 10 + 240 * t / 256;
        uint32_t g = 15 + 135 * t / 256;
        uint32_t b = 70 + 20 * t / 256;
        uint32_t v = 0;
        if (CL == 0)
            v = (r * 3 + g * 6 + b) / 10;
        else if (CL == 1)
            v = ((r * 3 + g * 6 + b) / 10) / 64;
        else if (CL == 2)
            v = ((r / 32) << 5) | ((g / 32) << 2) | (b / 64);
        else
            v = ((r / 8) << 10) | ((g / 8) << 5) | (b / 8);
        skyc[y] = v;
        y = y + 1;
    }
    return 0;
}

uint32_t sky(uint32_t x, uint32_t y)
{
    uint32_t dx = x - SUNX;
    uint32_t dy = y - SUNY;
    if (dx * dx + dy * dy < SUNR2)
    {
        plot(x, y, C_SUN);
    }
    else
    {
        uint32_t v = skyc[y];
        if (CL == 0)
            v = v > bay[((y & 1) << 1) | (x & 1)];
        setpixel(x, y, v);
    }
    return 0;
}

// sky, two parallax hill layers, grass and a scrolling checkered path
uint32_t draw_world(uint32_t dist)
{
    uint32_t x = 0;
    while (x < SW)
    {
        uint32_t fr = GY - 8 * SC - tri(x + dist / 4, 80 * SC) / 2;
        uint32_t nr = GY - 3 * SC - tri(x + dist / 2 + 37, 44 * SC) / 4;
        if (nr < fr)
            fr = nr;
        uint32_t y = 0;
        while (y < SH)
        {
            if (y < fr)
                sky(x, y);
            else if (y < nr)
                plot(x, y, C_FAR);
            else if (y < GY)
                plot(x, y, C_NEAR);
            else if (y < GY + SC)
                plot(x, y, C_GRASS);
            else if (((x + dist) / (6 * SC) + y / (4 * SC)) % 2 == 0)
                plot(x, y, C_PATH);
            else
                plot(x, y, C_GROUND);
            y = y + 1;
        }
        x = x + 1;
    }
    return 0;
}

uint32_t draw_clouds(uint32_t dist)
{
    uint32_t span = SW + 40 * SC;
    uint32_t i = 0;
    while (i < 3)
    {
        uint32_t cx = ((i * 61 * SC + 4 * span) - dist / 6) % span;
        cx = cx - 20 * SC;
        uint32_t cy = (4 + i * 4) * SC;
        rect(cx + 3 * SC, cy, 6 * SC, 2 * SC, C_CLOUD);
        rect(cx, cy + 2 * SC, 12 * SC, 2 * SC, C_CLOUD);
        i = i + 1;
    }
    return 0;
}

// the walker stays centered; the world scrolls past him
uint32_t draw_walker(uint32_t dist)
{
    uint32_t f = (dist / (3 * SC)) % 4;
    uint32_t x0 = SW / 2 - 5 * SC;
    uint32_t fy = GY + 4 * SC;
    uint32_t ty = fy - 20 * SC - ((f & 1) ^ 1) * SC;
    uint32_t lx = x0 + legl[f] * SC;
    uint32_t rx = x0 + legr[f] * SC;
    rect(x0 + SC, fy, 9 * SC, SC, C_SHADOW);
    rect(lx, fy - 9 * SC, 2 * SC, 7 * SC, C_PANTS);
    rect(rx, fy - 9 * SC, 2 * SC, 7 * SC, C_PANTS);
    rect(lx, fy - 2 * SC, 3 * SC, 2 * SC, C_SHOE);
    rect(rx, fy - 2 * SC, 3 * SC, 2 * SC, C_SHOE);
    rect(x0 + SC, ty + 6 * SC, 8 * SC, 7 * SC, C_SHIRT);
    rect(rx, ty + 7 * SC, 2 * SC, 6 * SC, C_SKIN);
    rect(lx, ty + 7 * SC, 2 * SC, 6 * SC, C_SKIN);
    rect(x0 + 2 * SC, ty + 2 * SC, 6 * SC, 4 * SC, C_SKIN);
    rect(x0 + 2 * SC, ty, 6 * SC, 2 * SC, C_HAIR);
    rect(x0 + 6 * SC, ty + 3 * SC, SC, SC, C_HAIR);
    return 0;
}

uint32_t main()
{
    if (!isflagsupported(0, 8))
    {
        printf("This host does not support graphics flag 0 value 8. Rebuild C-interpreter/main.\n");
        exit();
    }
    flag(1, 1);

    uint32_t start = gettime();
    uint32_t cur = 0;
    while (true)
    {
        uint32_t el = gettime() - start;
        uint32_t mode = 1 + el / 5000;
        if (mode > 8)
            break;

        if (mode != cur)
        {
            cur = mode;
            if (mode == 1)
                flag(0, 1);
            else if (mode == 2)
                flag(0, 2);
            else if (mode == 3)
                flag(0, 3);
            else if (mode == 4)
                flag(0, 4);
            else if (mode == 5)
                flag(0, 5);
            else if (mode == 6)
                flag(0, 6);
            else if (mode == 7)
                flag(0, 7);
            else
                flag(0, 8);
            SW = pw[mode];
            SH = ph[mode];
            CL = pcl[mode];
            SC = SH / 64;
            GY = SH * 3 / 4;
            SUNX = SW * 5 / 8;
            SUNY = GY - 16 * SC;
            SUNR2 = 49 * SC * SC;
            build_sky();
            printf("flag 0 = %d\n", mode);
        }

        uint32_t lt = el % 5000;
        uint32_t dist = lt * 20 * SC / 1000;
        draw_world(dist);
        draw_clouds(dist);
        draw_walker(dist);
        rect(0, SH - SC, SW * lt / 5000, SC, C_BAR);

        uint32_t wc = pal[CL * 16 + C_WHITE];
        if (mode == 1)
            drawstring("1 MONO 64x64", 1, 1, wc);
        else if (mode == 2)
            drawstring("2 GRAY 64x64", 1, 1, wc);
        else if (mode == 3)
            drawstring("3 RGB332 64x64", 1, 1, wc);
        else if (mode == 4)
            drawstring("4 MONO 128x64", 1, 1, wc);
        else if (mode == 5)
            drawstring("5 GRAY 128x64", 1, 1, wc);
        else if (mode == 6)
            drawstring("6 RGB332 128x64", 1, 1, wc);
        else if (mode == 7)
            drawstring("7 RGB555 128x128", 1, 1, wc);
        else
            drawstring("8 RGB555 256x128", 1, 1, wc);

        refreshscreen();
        if (getkey() & 128)
            exit();
    }
    return 0;
}
