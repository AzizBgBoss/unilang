uint32_t main() {
    if (isflagsupported(0, 1))
    {
        flag(0, 1);
    }
    else
    {
        printf("Flag 0, 1 is not supported");
        exit();
    }

    uint8_t x = 0;
    uint8_t y = 0;

    while (true)
    {
        setpixel(x, y, 1);
        refreshscreen();

        x = (x + 1) % 64;
        y = (y + 1) % 64;
    }
}