#define KEY_UP (1)
#define KEY_DOWN (1 << 1)
#define KEY_LEFT (1 << 2)
#define KEY_RIGHT (1 << 3)
#define KEY_JUMP (1 << 4)
#define KEY_DASH (1 << 5)
#define KEY_START (1 << 7)

uint8 player_x = 3;
uint8 player_y = 44;
uint8 previous_y = 44;
uint8 grounded = 1;
uint8 jump_left = 0;
uint8 dash_left = 0;
uint8 dash_ready = 1;
uint8 dash_up = 0;
uint8 dash_leftward = 0;
uint8 checkpoint_x = 3;
uint8 checkpoint_y = 44;

uint8 draw_solid_rect(uint8 x, uint8 y, uint8 width, uint8 height, uint8 color)
{
    uint8 row = 0;
    while (row < height)
    {
        uint8 col = 0;
        while (col < width)
        {
            setpixel(x + col, y + row, color);
            col++;
        }
        row++;
    }
    return 0;
}

uint8 draw_rect(uint8 x, uint8 y, uint8 width, uint8 height)
{
    return draw_solid_rect(x, y, width, height, 1);
}

bool overlaps(uint8 x, uint8 y, uint8 left, uint8 top, uint8 width, uint8 height)
{
    return x + 5 > left && x < left + width && y + 6 > top && y < top + height;
}

uint8 try_land(uint8 left, uint8 top, uint8 width)
{
    if (player_x + 5 > left && player_x < left + width
        && previous_y + 6 <= top && player_y + 6 >= top)
    {
        player_y = top - 6;
        grounded = 1;
        dash_ready = 1;
    }
    return 0;
}

uint8 draw_level()
{
    draw_rect(0, 50, 64, 4);
    draw_rect(7, 40, 17, 2);
    draw_rect(37, 31, 17, 2);
    draw_rect(5, 22, 16, 2);
    draw_rect(34, 14, 21, 2);
    draw_rect(7, 8, 18, 2);

    setpixel(28, 47, 1);
    setpixel(29, 48, 1);
    setpixel(30, 49, 1);
    setpixel(31, 47, 1);
    setpixel(32, 48, 1);
    setpixel(33, 49, 1);
    setpixel(34, 47, 1);
    setpixel(35, 48, 1);
    setpixel(36, 49, 1);
    drawchar('C', 45, 24);
    drawchar('G', 14, 1);
    return 0;
}

uint8 draw_player()
{
    draw_solid_rect(player_x, player_y, 5, 6, 0);
    draw_rect(player_x + 1, player_y + 1, 3, 4);
    setpixel(player_x + 2, player_y + 2, 0);
    return 0;
}

uint8 draw_hud()
{
    draw_rect(0, 50, 64, 14);
    draw_solid_rect(1, 51, 62, 12, 0);
    drawstring("C JUMP  L/R+V", 5, 51);
    if (dash_ready)
    {
        drawstring("UP+V UP READY", 5, 57);
    }
    else
    {
        drawstring("UP+V UP EMPTY", 5, 57);
    }
    return 0;
}

uint32 main()
{
    if (!isflagsupported(0, 1) || !isflagsupported(1, 1))
    {
        printf("Celeste needs 64x64 graphics and keyboard support.\n");
        exit();
    }
    flag(0, 1);
    flag(1, 1);

    bool playing = true;
    while (playing)
    {
        uint8 keys = getkey();
        previous_y = player_y;

        if ((keys & KEY_JUMP) && grounded)
        {
            jump_left = 8;
            grounded = 0;
        }
        if ((keys & KEY_DASH) && dash_ready && dash_left == 0
            && (keys & (KEY_UP | KEY_LEFT | KEY_RIGHT)))
        {
            dash_left = 4;
            dash_ready = 0;
            dash_up = (keys & KEY_UP) != 0;
            dash_leftward = (keys & KEY_LEFT) != 0;
            jump_left = 0;
            grounded = 0;
        }

        bool up_dashing = dash_left > 0 && dash_up;
        if (dash_left > 0)
        {
            if (dash_up)
            {
                if (player_y >= 3)
                {
                    player_y = player_y - 3;
                }
                else
                {
                    player_y = 0;
                }
            }
            else if (dash_leftward)
            {
                if (player_x >= 3)
                {
                    player_x = player_x - 3;
                }
                else
                {
                    player_x = 0;
                }
            }
            else if (player_x <= 56)
            {
                player_x = player_x + 3;
            }
            else
            {
                player_x = 59;
            }
            dash_left--;
        }
        else if (keys & KEY_LEFT)
        {
            if (player_x > 0)
            {
                player_x--;
            }
        }
        else if (keys & KEY_RIGHT)
        {
            if (player_x < 59)
            {
                player_x++;
            }
        }

        if (up_dashing)
        {
            grounded = 0;
        }
        else if (jump_left > 0)
        {
            if (player_y > 0)
            {
                player_y--;
            }
            jump_left--;
        }
        else if (player_y < 44)
        {
            player_y++;
        }

        grounded = 0;
        try_land(0, 50, 64);
        try_land(7, 40, 17);
        try_land(37, 31, 17);
        try_land(5, 22, 16);
        try_land(34, 14, 21);
        try_land(7, 8, 18);

        if (player_x >= 43 && player_x <= 51 && player_y >= 24 && player_y <= 28)
        {
            checkpoint_x = 45;
            checkpoint_y = 25;
        }

        if (overlaps(player_x, player_y, 28, 47, 9, 3) || player_y >= 51)
        {
            player_x = checkpoint_x;
            player_y = checkpoint_y;
            previous_y = checkpoint_y;
            jump_left = 0;
            dash_left = 0;
            dash_up = 0;
            grounded = 1;
            dash_ready = 1;
        }

        if (overlaps(player_x, player_y, 13, 1, 5, 4))
        {
            playing = false;
        }

        draw_level();
        draw_player();
        draw_hud();
        refreshscreen();
        sleep(16);
    }

    draw_rect(0, 50, 64, 14);
    draw_solid_rect(1, 51, 62, 12, 0);
    drawstring("SUMMIT! ENTER", 7, 54);
    refreshscreen();
    while (true)
    {
        if (getkey() & KEY_START)
        {
            exit();
        }
        sleep(16);
    }
    return 0;
}