#define KEY_UP (1)
#define KEY_DOWN (1 << 1)
#define KEY_LEFT (1 << 2)
#define KEY_RIGHT (1 << 3)
#define KEY_A (1 << 4)
#define KEY_B (1 << 5)
#define KEY_SELECT (1 << 6)
#define KEY_START (1 << 7)

// Visible ASCII 32..126 from font4x6_basic.h, 4x6, one uint4 row per entry.
// Each row uses the font encoding: bit 0 is the leftmost pixel.
const uint4 glyph_rows[570] = {
    0, 0, 0, 0, 0, 0, 2, 2, 2, 0, 2, 0, 5, 5, 0, 0, 0, 0, 5, 7, 5, 7, 5, 0,
    6, 3, 6, 7, 2, 0, 5, 4, 2, 1, 5, 0, 6, 3, 6, 5, 6, 0, 2, 2, 0, 0, 0, 0,
    2, 1, 1, 1, 2, 0, 2, 4, 4, 4, 2, 0, 5, 2, 5, 0, 0, 0, 0, 2, 7, 2, 0, 0,
    0, 0, 0, 0, 2, 1, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 1, 0, 4, 4, 2, 1, 1, 0,
    7, 5, 5, 5, 7, 0, 3, 2, 2, 2, 7, 0, 7, 4, 7, 1, 7, 0, 7, 4, 7, 4, 7, 0,
    5, 5, 7, 4, 4, 0, 7, 1, 7, 4, 7, 0, 7, 1, 7, 5, 7, 0, 7, 4, 4, 4, 4, 0,
    7, 5, 7, 5, 7, 0, 7, 5, 7, 4, 7, 0, 0, 0, 2, 0, 2, 0, 0, 0, 2, 0, 2, 1,
    4, 2, 1, 2, 4, 0, 0, 7, 0, 7, 0, 0, 1, 2, 4, 2, 1, 0, 7, 4, 6, 0, 2, 0,
    2, 5, 5, 1, 6, 0, 2, 5, 7, 5, 5, 0, 3, 5, 3, 5, 3, 0, 6, 1, 1, 1, 6, 0,
    3, 5, 5, 5, 3, 0, 7, 1, 3, 1, 7, 0, 7, 1, 3, 1, 1, 0, 6, 1, 5, 5, 6, 0,
    5, 5, 7, 5, 5, 0, 7, 2, 2, 2, 7, 0, 4, 4, 4, 5, 2, 0, 5, 5, 3, 5, 5, 0,
    1, 1, 1, 1, 7, 0, 5, 7, 7, 5, 5, 0, 7, 5, 5, 5, 5, 0, 2, 5, 5, 5, 2, 0,
    7, 5, 7, 1, 1, 0, 6, 5, 5, 3, 6, 0, 3, 5, 3, 5, 5, 0, 6, 1, 7, 4, 3, 0,
    7, 2, 2, 2, 2, 0, 5, 5, 5, 5, 7, 0, 5, 5, 5, 2, 2, 0, 5, 5, 7, 7, 5, 0,
    5, 5, 2, 5, 5, 0, 5, 5, 2, 2, 2, 0, 7, 4, 2, 1, 7, 0, 6, 2, 2, 2, 6, 0,
    1, 1, 2, 4, 4, 0, 3, 2, 2, 2, 3, 0, 2, 5, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0,
    1, 2, 0, 0, 0, 0, 0, 6, 5, 5, 6, 0, 1, 3, 5, 5, 3, 0, 0, 6, 1, 1, 6, 0,
    4, 6, 5, 5, 6, 0, 0, 2, 5, 3, 6, 0, 4, 2, 7, 2, 2, 0, 0, 2, 5, 6, 4, 2,
    1, 1, 3, 5, 5, 0, 0, 2, 0, 2, 2, 0, 0, 2, 0, 2, 2, 1, 1, 5, 3, 5, 5, 0,
    2, 2, 2, 2, 4, 0, 0, 5, 7, 5, 5, 0, 0, 3, 5, 5, 5, 0, 0, 2, 5, 5, 2, 0,
    0, 3, 5, 5, 3, 1, 0, 6, 5, 5, 6, 4, 0, 2, 5, 1, 1, 0, 0, 6, 1, 4, 3, 0,
    2, 7, 2, 2, 4, 0, 0, 5, 5, 5, 6, 0, 0, 5, 5, 2, 2, 0, 0, 5, 5, 7, 5, 0,
    0, 5, 2, 2, 5, 0, 0, 5, 5, 6, 4, 2, 0, 7, 4, 1, 7, 0, 6, 2, 1, 2, 6, 0,
    2, 2, 2, 2, 2, 0, 3, 2, 4, 2, 3, 0, 0, 3, 6, 0, 0, 0
};

uint8 draw_char(uint8 ch, uint8 ox, uint8 oy)
{
    if (ch < 32)
    {
        ch = 32;
    }
    if (ch > 126)
    {
        ch = 63;
    }
    uint32 glyph = ch;
    for (uint8 row = 0; row < 6; row++)
    {
        uint4 bits = glyph_rows[(glyph - 32) * 6 + row];
        for (uint8 col = 0; col < 4; col++)
        {
            if (bits & (1 << col))
            {
                setpixel(ox + col, oy + row, 1);
            }
        }
    }
}

uint8 draw_string(const char text[], uint8 ox, uint8 oy)
{
    uint8 i = 0;
    char ch = romread(text + i * 8, 8);
    while (ch != 0)
    {
        draw_char(ch, ox + i * 4, oy);
        i++;
        ch = romread(text + i * 8, 8);
    }
}

uint8 player_x = 0;
uint8 player_y = 0;
uint8 player_w = 3;
uint8 player_h = 5;
uint8 jumpTimer = 0;

uint8 draw_player()
{
    // Draw the player as a white square
    draw_char('I', player_x, player_y);
}

uint32 main()
{
    if (isflagsupported(0, 1))
    {
        flag(0, 1);
    }
    else
    {
        printf("64x64@1 graphics are not supported, exiting.");
        exit();
    }

    if (isflagsupported(1, 1))
    {
        flag(1, 1);
    }
    else
    {
        printf("NES keyboard not supported!");
    }

    printf("Video game 1");

    while (true)
    {
        uint8 keys = getkey();

        // Gravity
        if (jumpTimer > 0)
        {
            if (player_y > 0)
            {
                player_y--;
            }
        }
        else
        {
            if (player_y < 64 - player_h)
            {
                player_y++;
            }
        }

        // Movement
        if (keys & KEY_LEFT)
        {
            if (player_x > 0)
            {
                player_x--;
            }
        }
        else if (keys & KEY_RIGHT)
        {
            if (player_x < 64 - player_w)
            {
                player_x++;
            }
        }
        if (keys & KEY_A)
        {
            if (jumpTimer == 0)
            {
                jumpTimer = 10; // Set jump duration
            }
        }

        if (jumpTimer > 0)
        {
            jumpTimer--;
        }
        
        draw_player();
        draw_char((player_x % 10) + '0', 10, 0);
        refreshscreen();
    }

    return 0;
}
