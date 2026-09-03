#define KEY_UP (1)
#define KEY_DOWN (1 << 1)
#define KEY_LEFT (1 << 2)
#define KEY_RIGHT (1 << 3)
#define KEY_A (1 << 4)
#define KEY_B (1 << 5)
#define KEY_SELECT (1 << 6)
#define KEY_START (1 << 7)

uint8 player_x = 0;
uint8 player_y = 0;
uint8 player_w = 2;
uint8 player_h = 3;
uint8 jumpTimer = 0;

uint8 draw_player()
{
    // Draw the player as a white square
    uint8 x = player_x;
    while (x < player_x + player_w)
    {
        uint8 y = player_y;
        while (y < player_y + player_h)
        {
            setpixel(x, y, 1); // Set pixel to white
            y++;
        }
        x++;
    }
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
        printf("NES keyboard not supported, exiting.");
        exit();
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
        refreshscreen();

        sleep(100);
    }

    return 0;
}