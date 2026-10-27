uint32_t triangle_wave(uint32_t phase, uint32_t period)
{
    uint32_t position = phase % period;
    uint32_t half = period / 2;
    if (position < half)
        return position;
    return period - position;
}

uint32_t draw_sky(uint32_t time)
{
    uint32_t y = 0;
    while (y < 43)
    {
        uint32_t x = 0;
        uint32_t color = 2 + y / 9;
        while (x < 128)
        {
            setpixel(x, y, color);
            x = x + 1;
        }
        y = y + 1;
    }

    uint32_t star = 0;
    while (star < 38)
    {
        uint32_t x = (star * 47 + star * star * 13 + 19) % 128;
        y = (star * 29 + star * star * 7 + 3) % 35;
        uint32_t twinkle = (time / (90 + star % 5 * 35) + star * 7) % 11;
        if (twinkle < 8)
        {
            uint32_t color = 193 + twinkle * 8;
            setpixel(x, y, color);
            if (star % 9 == 0 && x < 127)
                setpixel(x + 1, y, color);
            if (star % 7 == 0 && y < 42)
                setpixel(x, y + 1, color);
        }
        star = star + 1;
    }
    return 0;
}

uint32_t draw_aurora(uint32_t time)
{
    uint32_t x = 0;
    while (x < 128)
    {
        uint32_t wave = triangle_wave(x * 3 + time / 2, 96);
        uint32_t first = 8 + wave / 5 + x % 7 / 2;
        uint32_t second_wave = triangle_wave(x * 2 + time / 3 + 23, 112);
        uint32_t second = 17 + second_wave / 6;
        uint32_t shimmer = (x + time / 12) % 8;
        uint32_t color = 0;
        uint32_t band = 0;

        while (band < 6)
        {
            uint32_t y = first + band;
            if (y < 43)
            {
                if (band < 2)
                    color = 54 + shimmer;
                else if (band < 4)
                    color = 86 + shimmer;
                else
                    color = 118 + shimmer;
                setpixel(x, y, color);
            }
            band = band + 1;
        }

        band = 0;
        while (band < 4)
        {
            uint32_t y = second + band;
            if (y < 43)
            {
                if (band < 2)
                    color = 174 + shimmer;
                else
                    color = 206 + shimmer;
                setpixel(x, y, color);
            }
            band = band + 1;
        }
        x = x + 1;
    }
    return 0;
}

uint32_t draw_planet(uint32_t time)
{
    uint32_t dx = 0;
    while (dx < 21)
    {
        uint32_t dy = 0;
        while (dy < 9)
        {
            if (dx * dx / 4 + dy * dy <= 64)
            {
                uint32_t color = 0;
                if (dx < 7)
                    color = 253;
                else if (dx < 14)
                    color = 221;
                else
                    color = 189;

                uint32_t top = 17 - dy;
                uint32_t bottom = 17 + dy;
                if (dx < 128 - 96)
                {
                    setpixel(96 + dx, top, color);
                    setpixel(96 + dx, bottom, color);
                }
                if (dx <= 96)
                {
                    setpixel(96 - dx, top, color);
                    setpixel(96 - dx, bottom, color);
                }
            }
            dy = dy + 1;
        }
        dx = dx + 1;
    }

    dx = 0;
    while (dx < 20)
    {
        uint32_t ring_y = 16 + dx / 5;
        uint32_t color = (time / 80 + dx) % 5 < 3 ? 250 : 242;
        if (96 + dx < 128)
        {
            setpixel(96 + dx, ring_y, color);
            if (ring_y > 0)
                setpixel(96 + dx, ring_y - 1, color);
        }
        if (dx <= 96)
        {
            setpixel(96 - dx, ring_y, color);
            if (ring_y > 0)
                setpixel(96 - dx, ring_y - 1, color);
        }
        dx = dx + 1;
    }
    return 0;
}

uint32_t draw_mountains(uint32_t time)
{
    uint32_t x = 0;
    while (x < 128)
    {
        uint32_t back = triangle_wave(x + time / 16, 64);
        uint32_t back_ridge = 38 + back / 5;
        uint32_t front = triangle_wave(x * 2 + time / 24 + 17, 92);
        uint32_t front_ridge = 49 + front / 9;
        uint32_t y = back_ridge;

        while (y < 64)
        {
            uint32_t color = 38;
            if (y < back_ridge + 2)
                color = 147;
            setpixel(x, y, color);
            y = y + 1;
        }

        y = front_ridge;
        while (y < 64)
        {
            uint32_t color = 10;
            if (y < front_ridge + 2)
                color = 59;
            else if ((x + y) % 11 == 0)
                color = 18;
            setpixel(x, y, color);
            y = y + 1;
        }

        if (x % 13 == 4 && (time / 45 + x) % 28 < 9)
        {
            uint32_t beacon = 252;
            if (front_ridge > 1)
                setpixel(x, front_ridge - 1, beacon);
        }
        x = x + 1;
    }
    return 0;
}

uint32_t draw_comet(uint32_t time)
{
    uint32_t x = (time / 35) % 160;
    uint32_t y = 10 + (x / 20) % 9;
    uint32_t trail = 0;
    while (trail < 12)
    {
        if (x >= trail && x - trail < 128)
        {
            uint32_t color = 222;
            if (trail < 3)
                color = 255;
            setpixel(x - trail, y + trail / 4, color);
        }
        trail = trail + 1;
    }
    return 0;
}

uint32_t main()
{
    if (!isflagsupported(0, 6) || !isflagsupported(1, 1))
    {
        printf("Flag 6 graphics or keyboard is not supported");
        exit();
    }

    flag(0, 6);
    flag(1, 1);
    while (true)
    {
        uint32_t time = gettime();
        draw_sky(time);
        draw_aurora(time);
        draw_planet(time);
        draw_mountains(time);
        draw_comet(time);
        refreshscreen();
        if (getkey() & 128)
            exit();
    }
    return 0;
}
