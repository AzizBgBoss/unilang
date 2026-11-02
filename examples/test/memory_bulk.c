uint32_t main(void)
{
    uint8_t src[4] = {1, 2, 3, 4};
    uint8_t dst[4] = {0, 0, 0, 0};
    const uint8_t rom[4] = {5, 6, 7, 8};

    memcpy(dst, src, 4);
    if (dst[0] != 1 || dst[3] != 4)
    {
        printf("RAM memcpy failed\n");
        return 1;
    }

    memset(dst, 9, 2);
    if (dst[0] != 9 || dst[1] != 9 || dst[2] != 3)
    {
        printf("memset failed\n");
        return 1;
    }

    memcpy(dst, rom, 4);
    if (dst[0] != 5 || dst[3] != 8)
    {
        printf("ROM memcpy failed\n");
        return 1;
    }

    printf("bulk memory ok\n");
    return 0;
}
