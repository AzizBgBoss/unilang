#define KEY_UP (1)
#define KEY_DOWN (1 << 1)
#define KEY_LEFT (1 << 2)
#define KEY_RIGHT (1 << 3)
#define KEY_A (1 << 4)
#define KEY_B (1 << 5)
#define KEY_SELECT (1 << 6)
#define KEY_START (1 << 7)

uint8 player_x = 30;
uint8 player_y = 56;
uint8 player_w = 3;
uint8 player_h = 5;
uint8 jumpTimer = 0;

uint32 score = 0;
uint8 fallspeed = 1;
char scorebuf[8];

uint8 draw_player()
{
    drawchar('I', player_x, player_y);
}

#define ENEMIES 5
uint8 enemies[ENEMIES * 2];
#define ENEMY_X(e) enemies[e * 2]
#define ENEMY_Y(e) enemies[e * 2 + 1]

uint8 draw_enemies()
{
    for (uint8 i = 0; i < ENEMIES; i++)
    {
        drawchar('A', ENEMY_X(i), ENEMY_Y(i));
    }
}

uint8 update_enemies()
{
    for (uint8 i = 0; i < ENEMIES; i++)
    {
        uint8 ey = ENEMY_Y(i);
        ey = ey + fallspeed;
        if (ey >= 64)
        {
            ey = 0;
            ENEMY_X(i) = rand(0, 60);
        }
        ENEMY_Y(i) = ey;
    }
}

uint8 abs_diff(uint8 a, uint8 b)
{
    if (a > b)
    {
        return a - b;
    }
    return b - a;
}

bool check_collision()
{
    for (uint8 i = 0; i < ENEMIES; i++)
    {
        uint8 dx = abs_diff(player_x, ENEMY_X(i));
        uint8 dy = abs_diff(player_y, ENEMY_Y(i));
        if (dx < 5)
        {
            if (dy < 7)
            {
                return true;
            }
        }
    }
    return false;
}

uint8 draw_hud()
{
    for (uint8 i = 0; i < 8; i++)
    {
        scorebuf[i] = ' ';
    }
    uint8 slen = numtostr(score, scorebuf);
    drawstring("SCORE", 0, 0);
    drawstring(scorebuf, 34, 0);
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
        printf("NES keyboard not supported!\n");
    }

    printf("Dodge the A's!");
    srand(gettime());

    while (true)
    {
        // reset a fresh round
        player_x = 30;
        player_y = 56;
        jumpTimer = 0;
        score = 0;
        fallspeed = 1;
        for (uint8 i = 0; i < ENEMIES; i++)
        {
            ENEMY_X(i) = rand(0, 60);
            ENEMY_Y(i) = (64 / ENEMIES) * i;
        }

        bool alive = true;
        while (alive)
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
                    jumpTimer = 10;
                }
            }
            if (jumpTimer > 0)
            {
                jumpTimer--;
            }

            update_enemies();

            if (check_collision())
            {
                alive = false;
            }

            draw_player();
            draw_enemies();
            draw_hud();
            refreshscreen();
            sleep(16);

            score++;
            if (score % 200 == 0)
            {
                fallspeed++;
            }
        }

        // game over screen -- wait for START to play again
        bool waiting = true;
        while (waiting)
        {
            drawstring("GAME OVER", 8, 24);
            draw_hud();
            refreshscreen();
            uint8 k = getkey();
            if (k & KEY_START)
            {
                waiting = false;
            }
            sleep(16);
        }
    }

    return 0;
}
