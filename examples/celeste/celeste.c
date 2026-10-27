// Celeste-style precision platformer for the uniClang VM (64x64, 1-bit graphics).
// Controls: Left/Right walk, C jump, V + direction (Left/Right/Up) dash, Enter exits at the summit.

#define KEY_UP (1)
#define KEY_DOWN (1 << 1)
#define KEY_LEFT (1 << 2)
#define KEY_RIGHT (1 << 3)
#define KEY_JUMP (1 << 4)
#define KEY_DASH (1 << 5)
#define KEY_START (1 << 7)

#define FRAME_MS 16
#define MAX_PARTICLES 10
#define NUM_PLATFORMS 6
#define NUM_STARS 8
#define NUM_SNOW 6
#define SPIKE_X 28
#define SPIKE_Y 47
#define SPIKE_W 11
#define CHECKPOINT_X 45
#define CHECKPOINT_Y 25

// ---- level data (lives in ROM) --------------------------------------------
const uint8 plat_x[NUM_PLATFORMS] = {0, 7, 37, 5, 34, 7};
const uint8 plat_y[NUM_PLATFORMS] = {50, 40, 31, 22, 14, 8};
const uint8 plat_w[NUM_PLATFORMS] = {64, 17, 17, 16, 21, 18};
const uint8 plat_h[NUM_PLATFORMS] = {4, 2, 2, 2, 2, 2};

const uint8 star_x[NUM_STARS] = {6, 22, 31, 47, 58, 14, 40, 27};
const uint8 star_y[NUM_STARS] = {4, 9, 2, 12, 19, 6, 16, 25};

// 5x6 sprite, one byte per row (bit 4 = leftmost pixel)
const uint8 hair_ready[2] = {0x0E, 0x1F};
const uint8 hair_empty[2] = {0x0E, 0x11};  // hollow hair = no dash left
const uint8 body[2] = {0x0E, 0x0E};
const uint8 legs[8] = {0x0A, 0x1B,   // standing
                       0x0A, 0x1A,   // walk A
                       0x0A, 0x0B,   // walk B
                       0x0A, 0x0A};  // in the air

const uint8 burst_dx[8] = {0, 1, 2, 0, 2, 0, 1, 2};  // 0/1/2 means -1/0/+1
const uint8 burst_dy[8] = {0, 0, 0, 1, 1, 2, 2, 2};

// ---- state ------------------------------------------------------------------
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
uint8 facing = 1;
uint8 leg_pose = 0;
uint8 walking = 0;
uint8 jump_buffer = 0;   // jump pressed shortly before landing still counts
uint8 coyote = 0;        // jump still allowed shortly after leaving a ledge
uint8 fall_time = 0;     // frames spent falling (gravity speeds up)
uint8 dead_timer = 0;
uint8 deaths = 0;
uint8 blink = 0;
uint8 refill_flash = 0;
uint8 shake_x = 0;
uint8 shake_y = 0;
uint8 prev_keys = 0;
bool playing = true;

uint16 frame = 0;
uint32 frame_start = 0;
uint32 fps_start = 0;
uint16 fps_frames = 0;
uint16 fps_shown = 0;

uint8 part_next = 0;
uint8 part_x[MAX_PARTICLES];
uint8 part_y[MAX_PARTICLES];
uint8 part_dx[MAX_PARTICLES];
uint8 part_dy[MAX_PARTICLES];
uint8 part_life[MAX_PARTICLES];
uint8 snow_x[NUM_SNOW];
uint8 snow_y[NUM_SNOW];

// ---- drawing helpers ----------------------------------------------------------
// All world drawing goes through plot(): it clips to the screen and applies the
// screen shake, so nothing ever writes outside the 64x64 framebuffer.
void plot(uint8 x, uint8 y, uint8 c)
{
    if (x + shake_x < 64 && y + shake_y < 64)
    {
        setpixel(x + shake_x, y + shake_y, c);
    }
}

void draw_num2(uint8 n, uint8 x, uint8 y)
{
    if (n > 99)
    {
        n = 99;
    }
    drawchar('0' + n / 10, x, y);
    drawchar('0' + n % 10, x + 4, y);
}

// Particles live in a small ring buffer: spawning overwrites the oldest one,
// so there's no slot search (keeps the code small).
void spawn_particle(uint8 x, uint8 y, uint8 dx, uint8 dy, uint8 life)
{
    part_x[part_next] = x;
    part_y[part_next] = y;
    part_dx[part_next] = dx;
    part_dy[part_next] = dy;
    part_life[part_next] = life;
    part_next++;
    if (part_next >= MAX_PARTICLES)
    {
        part_next = 0;
    }
}

void burst(uint8 x, uint8 y, uint8 life)
{
    uint8 d = 0;
    while (d < 8)
    {
        spawn_particle(x, y, burst_dx[d], burst_dy[d], life + (d & 1) * 3);
        d++;
    }
}

void dust()
{
    spawn_particle(player_x, player_y + 5, 0, 0, 5);
    spawn_particle(player_x + 4, player_y + 5, 2, 0, 5);
}

void update_particles()
{
    uint8 i = 0;
    while (i < MAX_PARTICLES)
    {
        if (part_life[i] > 0)
        {
            part_x[i] = part_x[i] + part_dx[i] - 1;
            part_y[i] = part_y[i] + part_dy[i] - 1;
            part_life[i]--;
        }
        i++;
    }
}

void draw_particles()
{
    uint8 i = 0;
    while (i < MAX_PARTICLES)
    {
        // flicker while fading out
        if (part_life[i] > 0 && (part_life[i] > 6 || (part_life[i] & 1)))
        {
            plot(part_x[i], part_y[i], 1);
        }
        i++;
    }
}

void update_snow()
{
    uint8 i = 0;
    while (i < NUM_SNOW)
    {
        if (frame % (3 + (i & 1)) == 0)
        {
            snow_y[i]++;
            if ((frame >> 5) & 1)
            {
                snow_x[i]++;
            }
            else
            {
                snow_x[i]--;
            }
        }
        if (snow_x[i] >= 64 || snow_y[i] >= 50)
        {
            snow_x[i] = rand(0, 64);
            snow_y[i] = 0;
        }
        i++;
    }
}

void draw_sky()
{
    uint8 i = 0;
    while (i < NUM_STARS)
    {
        if (((frame >> 4) + i * 5) % 7 != 0)  // each star twinkles off briefly
        {
            plot(star_x[i], star_y[i], 1);
        }
        i++;
    }
    i = 0;
    while (i < NUM_SNOW)
    {
        plot(snow_x[i], snow_y[i], 1);
        i++;
    }
}

// solid top edge, checkerboard underneath
void draw_platform(uint8 x, uint8 y, uint8 w, uint8 h)
{
    uint8 r = 0;
    while (r < h)
    {
        uint8 i = 0;
        while (i < w)
        {
            if (r == 0 || ((x + i + r) & 1) == 0)
            {
                plot(x + i, y + r, 1);
            }
            i++;
        }
        r++;
    }
}

void draw_spikes()
{
    uint8 k = 0;
    while (k < 3)
    {
        uint8 r = 0;
        while (r < 3)
        {
            uint8 c = 0;
            while (c < 3)
            {
                if (r > 0 || c == 1)
                {
                    plot(SPIKE_X + k * 4 + c, SPIKE_Y + r, 1);
                }
                c++;
            }
            r++;
        }
        k++;
    }
}

void draw_flag(uint8 x, uint8 y, uint8 full)
{
    uint8 i = 0;
    while (i < 6)
    {
        plot(x, y + i, 1);
        i++;
    }
    uint8 rows = 1;
    if (full)
    {
        rows = 3;
    }
    uint8 r = 0;
    while (r < rows)
    {
        uint8 w = 3 - r;
        if (!full)
        {
            w = 1;
        }
        else if (((frame >> 3) & 1) && w > 1)
        {
            w--;  // the pennant waves
        }
        uint8 c = 0;
        while (c < w)
        {
            plot(x + 1 + c, y + r, 1);
            c++;
        }
        r++;
    }
}

void draw_level()
{
    uint8 i = 0;
    while (i < NUM_PLATFORMS)
    {
        draw_platform(plat_x[i], plat_y[i], plat_w[i], plat_h[i]);
        i++;
    }
    draw_spikes();
    draw_flag(50, 25, checkpoint_y == CHECKPOINT_Y);  // checkpoint
    draw_flag(15, 2, 1);                              // summit
}

void draw_player()
{
    uint8 row = 0;
    while (row < 6)
    {
        uint8 bits;
        if (row < 2)
        {
            if (dash_ready)
            {
                bits = hair_ready[row];
            }
            else
            {
                bits = hair_empty[row];
            }
        }
        else if (row < 4)
        {
            bits = body[row - 2];
        }
        else
        {
            bits = legs[leg_pose * 2 + row - 4];
        }
        uint8 col = 0;
        while (col < 5)
        {
            uint8 bit = col;
            if (facing)
            {
                bit = 4 - col;
            }
            if ((bits >> bit) & 1)
            {
                plot(player_x + col, player_y + row, 1);
            }
            col++;
        }
        row++;
    }
    plot(player_x + 1 + facing * 2, player_y + 2, 0);  // eye
    if (dash_ready)
    {
        plot(player_x + 5 - facing * 6, player_y + 1, 1);  // ponytail trails behind
    }
}

void draw_hud()
{
    drawstring("FPS", 1, 57);
    draw_num2(fps_shown, 14, 57);
    drawchar('D', 26, 57);
    draw_num2(deaths, 31, 57);
    drawstring("DASH", 40, 57);
    // dash pip: filled when ready, hollow when empty, blinks when it recharges
    uint8 r = 0;
    while (r < 5)
    {
        uint8 c = 0;
        while (c < 5)
        {
            if (r == 0 || r == 4 || c == 0 || c == 4 || (dash_ready && (refill_flash & 2) == 0))
            {
                plot(57 + c, 58 + r, 1);
            }
            c++;
        }
        r++;
    }
}

// ---- timing -------------------------------------------------------------------
// Waits out the rest of the 16 ms frame (sleeping 1 ms at a time so a slow
// device doesn't busy-spin) and keeps the FPS counter.
void frame_wait()
{
    uint32 now = gettime();
    while (now - frame_start < FRAME_MS)
    {
        sleep(1);
        now = gettime();
    }
    if (now - frame_start > FRAME_MS * 4)
    {
        frame_start = now;  // fell far behind: resync instead of racing to catch up
    }
    else
    {
        frame_start = frame_start + FRAME_MS;
    }
    fps_frames++;
    if (now - fps_start >= 1000)
    {
        fps_shown = fps_frames;
        fps_frames = 0;
        fps_start = now;
    }
    frame++;
}

// ---- game logic ---------------------------------------------------------------
bool overlaps(uint8 x, uint8 y, uint8 left, uint8 top, uint8 width, uint8 height)
{
    return x + 5 > left && x < left + width && y + 6 > top && y < top + height;
}

void try_land(uint8 left, uint8 top, uint8 width)
{
    if (player_x + 5 > left && player_x < left + width
        && previous_y + 6 <= top && player_y + 6 >= top)
    {
        player_y = top - 6;
        grounded = 1;
        fall_time = 0;
        if (!dash_ready)
        {
            refill_flash = 8;
        }
        dash_ready = 1;
    }
}

void respawn()
{
    player_x = checkpoint_x;
    player_y = checkpoint_y;
    previous_y = checkpoint_y;
    jump_left = 0;
    dash_left = 0;
    dash_up = 0;
    grounded = 1;
    dash_ready = 1;
    fall_time = 0;
    coyote = 0;
    jump_buffer = 0;
    blink = 12;
}

void update_player(uint8 keys)
{
    bool was_grounded = grounded;
    previous_y = player_y;

    if (keys & KEY_LEFT)
    {
        facing = 0;
    }
    else if (keys & KEY_RIGHT)
    {
        facing = 1;
    }

    // jump with a short buffer (press a little early) and coyote time (a little late)
    if ((keys & KEY_JUMP) && !(prev_keys & KEY_JUMP))
    {
        jump_buffer = 4;
    }
    else if (jump_buffer > 0)
    {
        jump_buffer--;
    }
    if (grounded)
    {
        coyote = 3;
    }
    else if (coyote > 0)
    {
        coyote--;
    }
    if (jump_buffer > 0 && coyote > 0 && dash_left == 0)
    {
        jump_left = 8;
        grounded = 0;
        coyote = 0;
        jump_buffer = 0;
        fall_time = 0;
        dust();
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
    walking = 0;
    if (dash_left > 0)
    {
        spawn_particle(player_x + 2, player_y + 3, 1, 1, 4);  // afterimage
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
        walking = 1;
        if (player_x > 0)
        {
            player_x--;
        }
    }
    else if (keys & KEY_RIGHT)
    {
        walking = 1;
        if (player_x < 59)
        {
            player_x++;
        }
    }

    if (up_dashing)
    {
        grounded = 0;
        fall_time = 0;
    }
    else if (jump_left > 0)
    {
        if (player_y > 0)
        {
            player_y--;
        }
        jump_left--;
        fall_time = 0;
    }
    else if (player_y < 44)
    {
        // gravity: one pixel per frame at first, two once the fall gets going
        if (fall_time < 20)
        {
            fall_time++;
        }
        uint8 step = 1;
        if (fall_time > 6)
        {
            step = 2;
        }
        if (player_y + step > 44)
        {
            player_y = 44;
        }
        else
        {
            player_y = player_y + step;
        }
    }

    grounded = 0;
    uint8 i = 0;
    while (i < NUM_PLATFORMS)
    {
        try_land(plat_x[i], plat_y[i], plat_w[i]);
        i++;
    }
    if (grounded && !was_grounded)
    {
        dust();
    }

    if (!grounded)
    {
        leg_pose = 3;
    }
    else if (walking)
    {
        leg_pose = 1 + ((frame >> 2) & 1);
    }
    else
    {
        leg_pose = 0;
    }

    if (player_x >= 43 && player_x <= 51 && player_y >= 24 && player_y <= 28
        && checkpoint_y != CHECKPOINT_Y)
    {
        checkpoint_x = CHECKPOINT_X;
        checkpoint_y = CHECKPOINT_Y;
        burst(51, 26, 8);
    }

    if (overlaps(player_x, player_y, SPIKE_X, SPIKE_Y, SPIKE_W, 3) || player_y >= 51)
    {
        burst(player_x + 2, player_y + 3, 10);
        dead_timer = 16;
        if (deaths < 99)
        {
            deaths++;
        }
    }

    if (overlaps(player_x, player_y, 13, 1, 5, 4))
    {
        playing = false;
    }
    prev_keys = keys;
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
    srand(gettime());
    frame_start = gettime();
    fps_start = frame_start;

    uint8 i = 0;
    while (i < NUM_SNOW)
    {
        snow_x[i] = rand(0, 64);
        snow_y[i] = rand(0, 50);
        i++;
    }

    // One loop drives all three screens: 0 = title, 1 = playing, 2 = summit.
    uint8 keys = 0;
    uint8 state = 0;
    player_x = 30;  // the title screen shows the climber in the middle
    player_y = 38;
    while (true)
    {
        keys = getkey();
        shake_x = 0;
        shake_y = 0;

        if (state == 0)
        {
            if (keys & KEY_JUMP)
            {
                state = 1;
                respawn();
                prev_keys = keys;  // the key that started the game isn't also a jump
            }
        }
        else if (state == 1)
        {
            if (dead_timer > 0)
            {
                if (dead_timer > 10)
                {
                    shake_x = dead_timer & 1;  // quick jolt when you die
                    shake_y = (dead_timer >> 1) & 1;
                }
                dead_timer--;
                if (dead_timer == 0)
                {
                    respawn();
                }
            }
            else
            {
                update_player(keys);
            }
            if (!playing)
            {
                state = 2;
            }
            if (refill_flash > 0)
            {
                refill_flash--;
            }
        }
        else if (keys & KEY_START)
        {
            exit();
        }
        if (state == 2 && frame % 24 == 0)
        {
            burst(rand(10, 54), rand(8, 30), 9);  // fireworks
        }
        update_particles();
        update_snow();

        draw_sky();
        if (state == 1)
        {
            draw_level();
        }
        draw_particles();
        if (state == 1)
        {
            if (dead_timer == 0 && (blink == 0 || (blink & 2) == 0))
            {
                draw_player();
            }
            if (blink > 0)
            {
                blink--;
            }
            shake_x = 0;
            shake_y = 0;
            draw_hud();
        }
        else if (state == 0)
        {
            drawstring("CELESTE", 18, 10);
            i = 0;
            while (i < 28)
            {
                plot(18 + i, 17, 1);
                i++;
            }
            drawstring("C JUMP", 20, 24);
            drawstring("V DASH", 20, 31);
            draw_player();
            if ((frame >> 4) & 1)
            {
                drawstring("PRESS C", 18, 52);
            }
        }
        else
        {
            drawstring("SUMMIT!", 18, 18);
            drawstring("DEATHS", 14, 30);
            draw_num2(deaths, 40, 30);
            drawstring("ENTER EXIT", 12, 46);
        }
        refreshscreen();
        frame_wait();
    }
    return 0;
}
