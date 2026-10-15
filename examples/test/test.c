uint32 main() {
    if (isflagsupported(0, 1))
    {
        flag(0, 1);
    }
    else
    {
        printf("Flag 0, 1 is not supported");
        exit();
    }

    uint8 x = 0;
    uint8 y = 0;

    while (true)
    {
        setpixel(x, y, 1);
        refreshscreen();

        x = (x + 1) % 64;
        y = (y + 1) % 64;
    }
}