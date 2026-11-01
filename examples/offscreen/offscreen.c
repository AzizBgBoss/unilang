uint32_t main(void)
{
    flag(0, 1);
    fillrect(-8, -8, 24, 24, 1);
    drawline(-100, 32, 200, 32, 1);
    drawline(32, -100, 32, 200, 1);
    drawchar('A', -2, 3, 1);
    drawchar('B', 61, 58, 1);
    refreshscreen();
    sleep(16);
    return 0;
}
