uint32 draw_line(uint32 x, uint32 y, uint32 len, uint32 val) {
    uint32 i = 0;
    while (i < len) {
        setpixel(x + i, y, val);
        i = i + 1;
    }
    return 0;
}

uint32 main() {
    if (isflagsupported(0, 1)) {
        flag(0, 1);
        flag(1, 1);
    }

    uint32 x = 10;
    uint32 y = 10;
    uint32 dx = 1;
    uint32 dy = 1;
    uint32 step = 0;

    while (step < 10) {
        draw_line(x, y, 8, 1);
        refreshscreen();

        if (x >= 50) {
            dx = 0;
        }
        if (x <= 0) {
            dx = 1;
        }
        if (y >= 50) {
            dy = 0;
        }
        if (y <= 0) {
            dy = 1;
        }

        if (dx == 1) {
            x = x + 1;
        } else {
            x = x - 1;
        }

        if (dy == 1) {
            y = y + 1;
        } else {
            y = y - 1;
        }

        step = step + 1;
        sleep(60);
    }

    while (1) {
        uint8 key = getkey();

        if (key == 1) {
            y = y - 1;
        }
        if (key == 2) {
            y = y + 1;
        }
        if (key == 4) {
            x = x - 1;
        }
        if (key == 8) {
            x = x + 1;
        }

        if (x > 63) {
            x = 63;
        }
        if (y > 63) {
            y = 63;
        }
        if (x < 0) {
            x = 0;
        }
        if (y < 0) {
            y = 0;
        }

        draw_line(x, y, 5, 1);
        refreshscreen();
        sleep(50);
    }

    return 0;
}
