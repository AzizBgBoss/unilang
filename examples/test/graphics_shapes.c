uint32_t main(void)
{
    flag(0, 9);
    fillscreen(0);

    drawrect(-1, -1, 4, 4, 7);
    if (getpixel(0, 2) != 7 || getpixel(2, 0) != 7 ||
        getpixel(2, 2) != 7 || getpixel(1, 1) != 0)
    {
        printf("clipped drawrect failed\n");
        return 1;
    }

    drawrect(5, 5, 0, 3, 8);
    drawrect(5, 5, 3, 0, 8);
    fillcircle(5, 5, -1, 8);
    if (getpixel(5, 5) != 0)
    {
        printf("degenerate shapes failed\n");
        return 1;
    }
    drawrect(5, 5, 1, 1, 10);
    if (getpixel(5, 5) != 10)
    {
        printf("single-pixel drawrect failed\n");
        return 1;
    }

    fillscreen(0);
    fillcircle(0, 0, 2, 5);
    fillcircle(63, 63, 2, 9);
    if (getpixel(0, 0) != 5 || getpixel(2, 0) != 5 ||
        getpixel(0, 2) != 5 || getpixel(2, 2) != 0 ||
        getpixel(63, 63) != 9 || getpixel(61, 63) != 9)
    {
        printf("clipped fillcircle failed\n");
        return 1;
    }

    fillscreen(3);
    if (getpixel(0, 0) != 3 || getpixel(63, 63) != 3)
    {
        printf("fillscreen failed\n");
        return 1;
    }

    refreshscreen();
    printf("graphics shapes ok\n");
    return 0;
}
