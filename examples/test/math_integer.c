uint32_t main(void)
{
    int32_t high = 0;
    int32_t negative = -8;
    uint32_t logical = 0x80000000u;
    uint32_t low = 0;

    if (min(-7, 3) != -7 || max(-7, 3) != 3 || abs(-9) != 9
        || min(4u, 4u) != 4u || max(2u, 8u) != 8u)
    {
        printf("min/max/abs failed\n");
        return 1;
    }

    mul64(-2, 3, &high, &low);
    if ((uint32_t)high != 0xFFFFFFFFu || low != 0xFFFFFFFAu)
    {
        printf("signed mul64 failed\n");
        return 1;
    }
    mul64(0xFFFFFFFFu, 2u, &high, &low);
    if ((uint32_t)high != 1u || low != 0xFFFFFFFEu)
    {
        printf("unsigned mul64 failed\n");
        return 1;
    }
    if ((negative >> 2) != -2)
    {
        printf("signed right shift failed\n");
        return 1;
    }
    if ((logical >> 1) != 0x40000000u || abs(-2147483647 - 1) != 0x80000000u)
    {
        printf("shift or abs boundary failed\n");
        return 1;
    }

    printf("integer math ok\n");
    return 0;
}
