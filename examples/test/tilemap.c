// 1bpp sheet, two 8x8 cells side by side: tile 0 is empty, tile 1 is solid (padding included)
const uint8_t sheet1[16] = {
    0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
    0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF
};
const uint8_t map1[6] = {0, 1, 1, 1, 0, 1};   // 3x2 tiles
uint8_t pal[2];

uint8_t sheet2[32];   // 2bpp, 16x8 pixels: 4 bytes per row
uint8_t sheet4[128];  // 4bpp, 32x8 pixels: 16 bytes per row
uint8_t ram_map[1];

uint32_t fail(uint32_t check)
{
    printf("tilemap check %u failed\n", check);
    return 1;
}

uint32_t main(void)
{
    flag(FLAG_GRAPHICS, GFX_RGB332_128X64);
    pal[0] = 2;
    pal[1] = 9;

    // 7x5 tiles live in 8x8 sheet cells; only 7x5 pixels of each cell are drawn
    fillscreen(3);
    tilemap(map1, 3, 2, sheet1, 16, 0, 0, 7, 5, BLIT_1BPP | BLIT_PALETTE, pal, 0);
    if (getpixel(6, 0) != 2 || getpixel(7, 0) != 9 || getpixel(20, 4) != 9 ||
        getpixel(0, 5) != 9 || getpixel(7, 5) != 2)
        return fail(1);  // 1bpp 7x5 tilemap
    if (getpixel(21, 0) != 3 || getpixel(0, 10) != 3)
        return fail(2);  // tilemap padding or map edge

    // positive and negative scrolling
    fillscreen(3);
    tilemap(map1, 3, 2, sheet1, 16, 3, 2, 7, 5, BLIT_1BPP | BLIT_PALETTE, pal, 0);
    if (getpixel(3, 0) != 2 || getpixel(4, 0) != 9 || getpixel(0, 4) != 9 || getpixel(5, 4) != 2)
        return fail(3);  // positive scroll
    fillscreen(3);
    tilemap(map1, 3, 2, sheet1, 16, -5, 0, 7, 5, BLIT_1BPP | BLIT_PALETTE, pal, 0);
    if (getpixel(4, 0) != 3 || getpixel(5, 0) != 2 || getpixel(12, 0) != 9)
        return fail(4);  // negative scroll

    // transparency lets the background show through tile 0
    fillscreen(3);
    tilemap(map1, 3, 2, sheet1, 16, 0, 0, 7, 5, BLIT_1BPP | BLIT_TRANSPARENT, 0, 0);
    if (getpixel(0, 0) != 3 || getpixel(7, 0) != 1)
        return fail(5);  // transparent tilemap

    // RAM map + RAM sheet, 2bpp: tile 1 row is 3 2 1 0 0 1 2 3
    for (uint32_t r = 0; r < 8; r++)
    {
        sheet2[r * 4 + 0] = 0;
        sheet2[r * 4 + 1] = 0;
        sheet2[r * 4 + 2] = 0xE4;
        sheet2[r * 4 + 3] = 0x1B;
    }
    ram_map[0] = 1;
    fillscreen(0);
    tilemap(ram_map, 1, 1, sheet2, 16, 0, 0, 8, 8, TILEMAP_MAP_RAM | BLIT_RAM | BLIT_2BPP, 0, 0);
    if (getpixel(0, 0) != 3 || getpixel(1, 0) != 2 || getpixel(2, 0) != 1 || getpixel(3, 0) != 0 ||
        getpixel(4, 0) != 0 || getpixel(5, 0) != 1 || getpixel(6, 7) != 2 || getpixel(7, 7) != 3)
        return fail(6);  // 2bpp tilemap

    // 10x2 tiles need 16x8 cells: tile 1 starts at sheet x=16 (4bpp)
    for (uint32_t i = 0; i < 128; i++)
        sheet4[i] = 0;
    for (uint32_t r = 0; r < 8; r++)
        for (uint32_t b = 8; b < 16; b++)
            sheet4[r * 16 + b] = 0x55;
    fillscreen(3);
    tilemap(ram_map, 1, 1, sheet4, 32, 0, 0, 10, 2, TILEMAP_MAP_RAM | BLIT_RAM | BLIT_4BPP, 0, 0);
    if (getpixel(0, 0) != 5 || getpixel(9, 1) != 5 || getpixel(10, 1) != 3 || getpixel(0, 2) != 3)
        return fail(7);  // cell rounding

    // degenerate sizes draw nothing
    fillscreen(3);
    tilemap(map1, 3, 2, sheet1, 16, 0, 0, 0, 5, BLIT_1BPP, 0, 0);
    tilemap(map1, 0, 2, sheet1, 16, 0, 0, 7, 5, BLIT_1BPP, 0, 0);
    if (getpixel(0, 0) != 3)
        return fail(8);  // degenerate tilemap

    refreshscreen();
    printf("tilemap ok\n");
    return 0;
}
