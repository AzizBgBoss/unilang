// Celeste-style precision platformer for the uniClang VM (128x64, RGB332, 1:1 pixels).
// Scrolling 512x128 world, subpixel physics, 8-way dash, variable jump, coyote time,
// jump buffering, corner correction, parallax background.
// Controls: Left/Right run, C jump (hold = higher), V + direction dash (8-way),
//           Down while falling = fast fall, Enter exits at the summit.

#define KEY_UP (1)
#define KEY_DOWN (1 << 1)
#define KEY_LEFT (1 << 2)
#define KEY_RIGHT (1 << 3)
#define KEY_JUMP (1 << 4)
#define KEY_DASH (1 << 5)
#define KEY_START (1 << 7)

#define FRAME_MS 100
#define SCREEN_W 128
#define SCREEN_H 64
#define WORLD_W 512
#define WORLD_H 128
#define NP 13
#define NS 3
#define NCP 3
#define MAXP 16

// physics, 8 fractional bits (256 = 1 px per frame)
#define MAXRUN 384
#define RUN_ACCEL 71
#define RUN_REDUCE 28
#define AIR_MULT 166
#define GRAVITY 64
#define MAXFALL 683
#define FASTFALL 1024
#define JUMP_SPEED 448
#define HALF_GRAV_THRESH 170
#define DASH_SPEED 1024
#define DASH_DIAG 724

// RGB332 palette
#define C_SKY0 0x01
#define C_SKY1 0x05
#define C_SKY2 0x29
#define C_SKY3 0x4A
#define C_FAR 0x25
#define C_NEAR 0x49
#define C_SNOW 0x92
#define C_AUR1 0x1E
#define C_AUR2 0x0D
#define C_ICE 0xBF
#define C_ROCK 0x6E
#define C_DARK 0x49
#define C_RED 0xE0
#define C_HAIR 0xE5
#define C_HAIR_EMPTY 0x4F
#define C_SKIN 0xF6
#define C_DRESS 0x33
#define C_PANTS 0x24
#define C_GOLD 0xFC
#define C_WHITE 0xFF
#define C_CYAN 0x1F

// ---- level data ----------------------------------------------------------------
const uint16_t plat_x[NP] = {0, 128, 184, 240, 296, 376, 424, 472, 424, 368, 312, 256, 192};
const uint8_t plat_y[NP] = {112, 104, 96, 88, 80, 72, 64, 56, 40, 32, 24, 16, 24};
const uint16_t plat_w[NP] = {104, 32, 32, 24, 48, 24, 32, 40, 24, 24, 32, 24, 40};
const uint8_t plat_h[NP] = {16, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};

const uint16_t spk_x[NS] = {300, 436, 200};
const uint8_t spk_y[NS] = {76, 60, 92};
const uint8_t spk_w[NS] = {16, 12, 8};

const uint16_t cp_x[NCP] = {24, 330, 372};
const uint8_t cp_y[NCP] = {105, 73, 25};

const uint8_t star_x[8] = {6, 22, 31, 47, 58, 14, 40, 27};

// ---- state ------------------------------------------------------------------------
int px = 24 * 256;
int py = 105 * 256;
int vx = 0;
int vy = 0;
int cam_x = 0;
int cam_y = 64;
int shk_x = 0;
int shk_y = 0;
int shake = 0;
uint8_t facing = 1;
uint8_t grounded = 1;
uint8_t dash_ready = 1;
uint8_t dash_t = 0;
int dash_dx = 1;
int dash_dy = 0;
uint8_t coyote = 0;
uint8_t jbuf = 0;
uint8_t var_jump = 0;
uint8_t dead_timer = 0;
uint8_t deaths = 0;
uint8_t cp = 0;
uint8_t blink = 0;
uint8_t flash = 0;
uint8_t prev_keys = 0;
bool playing = true;

uint16_t frame = 0;
uint32_t frame_start = 0;

uint8_t pn = 0;
int pp_x[MAXP];
int pp_y[MAXP];
int pp_vx[MAXP];
int pp_vy[MAXP];
uint8_t pp_life[MAXP];
uint8_t pp_col[MAXP];

// ---- drawing ------------------------------------------------------------------------
// screen-space clipped filled rectangle
void srect(int x, int y, int w, int h, uint8_t c)
{
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    int yy = y0;
    while (yy < y1)
    {
        int xx = x0;
        while (xx < x1)
        {
            setpixel(xx, yy, c);
            xx++;
        }
        yy++;
    }
}

// world-space rectangle (camera + shake applied)
void rect(int x, int y, int w, int h, uint8_t c)
{
    srect(x - cam_x + shk_x, y - cam_y + shk_y, w, h, c);
}

void spawn(int x, int y, int dx, int dy, uint8_t life, uint8_t col)
{
    pp_x[pn] = x << 4;
    pp_y[pn] = y << 4;
    pp_vx[pn] = dx;
    pp_vy[pn] = dy;
    pp_life[pn] = life;
    pp_col[pn] = col;
    pn++;
    if (pn >= MAXP) pn = 0;
}

void burst(int x, int y, uint8_t col)
{
    int d = 0;
    while (d < 8)
    {
        int ax = (d & 1) ? 0 : ((d & 2) ? 14 : -14);
        int ay = (d & 1) ? ((d & 2) ? 14 : -14) : 0;
        if (d >= 4)
        {
            ax = (d & 1) ? 10 : -10;
            ay = (d & 2) ? 10 : -10;
        }
        spawn(x, y, ax, ay, 14 + (d & 3), col);
        d++;
    }
}

void update_particles()
{
    uint8_t i = 0;
    while (i < MAXP)
    {
        if (pp_life[i] > 0)
        {
            pp_x[i] += pp_vx[i];
            pp_y[i] += pp_vy[i];
            pp_life[i]--;
        }
        i++;
    }
}

void draw_particles()
{
    uint8_t i = 0;
    while (i < MAXP)
    {
        if (pp_life[i] > 0 && (pp_life[i] > 5 || (pp_life[i] & 1)))
        {
            rect(pp_x[i] >> 4, pp_y[i] >> 4, 1 + (pp_life[i] > 9), 1 + (pp_life[i] > 9), pp_col[i]);
        }
        i++;
    }
}

void draw_background()
{
    srect(0, 0, SCREEN_W, 16, C_SKY0);
    srect(0, 16, SCREEN_W, 16, C_SKY1);
    srect(0, 32, SCREEN_W, 16, C_SKY2);
    srect(0, 48, SCREEN_W, 16, C_SKY3);

    int i = 0;
    while (i < 8)
    {
        int sxp = star_x[i] * 16 - (cam_x >> 4);
        sxp = ((sxp % SCREEN_W) + SCREEN_W) % SCREEN_W;
        if (((frame >> 4) + i * 5) % 7 != 0)
            srect(sxp, 3 + i * 4, 1, 1, C_WHITE);
        i++;
    }

    // aurora ribbons
    int x = 0;
    while (x < SCREEN_W)
    {
        int ph = (x * 2 + (cam_x >> 3) + frame) % 64;
        if (ph > 32) ph = 64 - ph;
        srect(x, 8 + ph / 3, 2, 3, C_AUR1);
        srect(x, 11 + ph / 3, 2, 2, C_AUR2);
        x += 2;
    }

    // parallax mountains
    int base = 64 - (cam_y >> 2);
    int sx = 0;
    while (sx < SCREEN_W)
    {
        int wx = sx + (cam_x >> 2);
        int p = wx % 96;
        if (p > 48) p = 96 - p;
        int top = base - 14 - p / 3;
        srect(sx, top, 1, 64, C_FAR);
        srect(sx, top, 1, 2, C_SNOW);
        sx++;
    }
}

void draw_spikes()
{
    uint8_t i = 0;
    while (i < NS)
    {
        int k = 0;
        while (k * 4 < spk_w[i])
        {
            rect(spk_x[i] + k * 4, spk_y[i] + 2, 4, 2, C_RED);
            rect(spk_x[i] + k * 4 + 1, spk_y[i], 2, 2, C_RED);
            k++;
        }
        i++;
    }
}

void draw_flag(int x, int y, uint8_t on)
{
    rect(x, y - 10, 1, 10, C_GOLD);
    uint8_t w = on ? 5 - ((frame >> 3) & 1) : 3;
    rect(x + 1, y - 10, w, 3, on ? C_HAIR : C_DARK);
    rect(x + 1, y - 7, w - 1, 2, on ? C_HAIR : C_DARK);
}

void draw_level()
{
    uint8_t i = 0;
    while (i < NP)
    {
        rect(plat_x[i], plat_y[i], plat_w[i], plat_h[i], C_ROCK);
        rect(plat_x[i], plat_y[i] + plat_h[i] - 2, plat_w[i], 2, C_DARK);
        rect(plat_x[i], plat_y[i], plat_w[i], 2, C_ICE);
        i++;
    }
    draw_spikes();
    i = 1;
    while (i < NCP)
    {
        draw_flag(cp_x[i] + 3, cp_y[i] + 7, cp >= i);
        i++;
    }
    draw_flag(216, 24, 1);
}

void draw_player()
{
    int x = px >> 8;
    int y = py >> 8;
    uint8_t hair = dash_ready ? C_HAIR : C_HAIR_EMPTY;
    uint8_t hc = dash_t > 0 ? C_WHITE : hair;
    rect(x, y, 6, 3, hc);
    rect(facing ? x - 1 : x + 6, y + 1 + (grounded ? 0 : 1), 1, 2, hc);
    rect(x + 1, y + 2, 4, 2, C_SKIN);
    rect(facing ? x + 4 : x + 1, y + 3, 1, 1, 0x02);
    rect(x + 1, y + 4, 4, 2, dash_t > 0 ? C_WHITE : C_DRESS);
    if (!grounded)
        rect(x + 1, y + 6, 4, 1, C_PANTS);
    else if (vx != 0)
        rect(x + 1 + ((frame >> 2) & 1) * 2, y + 6, 2, 1, C_PANTS);
    else
        rect(x + 1, y + 6, 4, 1, C_PANTS);
}

void draw_hud(uint8_t on)
{
    drawchar('D', 2, 2, C_HAIR);
    drawchar('0' + deaths / 10, 8, 2, C_WHITE);
    drawchar('0' + deaths % 10, 12, 2, C_WHITE);
    srect(SCREEN_W - 9, 2, 7, 1, on ? C_GOLD : C_CYAN);
    srect(SCREEN_W - 9, 8, 7, 1, on ? C_GOLD : C_CYAN);
    srect(SCREEN_W - 9, 2, 1, 7, on ? C_GOLD : C_CYAN);
    srect(SCREEN_W - 3, 2, 1, 7, on ? C_GOLD : C_CYAN);
    if (on) srect(SCREEN_W - 8, 3, 5, 5, (flash & 2) ? C_WHITE : C_GOLD);
}

// ---- timing ---------------------------------------------------------------------------
void frame_wait()
{
    uint32_t now = gettime();
    while (now - frame_start < FRAME_MS)
    {
        sleep(1);
        now = gettime();
    }
    if (now - frame_start > FRAME_MS * 4)
        frame_start = now;
    else
        frame_start = frame_start + FRAME_MS;
    frame++;
}

// ---- collision --------------------------------------------------------------------------
// hitbox is 6 wide, 7 tall
bool solid(int x, int y)
{
    if (x < 0 || x + 6 > WORLD_W) return true;
    uint8_t i = 0;
    while (i < NP)
    {
        if (x + 6 > (int)plat_x[i] && x < (int)plat_x[i] + (int)plat_w[i]
            && y + 7 > (int)plat_y[i] && y < (int)plat_y[i] + (int)plat_h[i])
            return true;
        i++;
    }
    return false;
}

bool hits(int x, int y, int l, int t, int w, int h)
{
    return x + 6 > l && x < l + w && y + 7 > t && y < t + h;
}

int approach(int v, int t, int a)
{
    if (v < t)
    {
        v += a;
        if (v > t) v = t;
    }
    else
    {
        v -= a;
        if (v < t) v = t;
    }
    return v;
}

void update_cam(uint8_t snap)
{
    int tx = (px >> 8) + 3 - 64 + (facing ? 14 : -14);
    int ty = (py >> 8) + 3 - 32;
    if (tx < 0) tx = 0;
    if (tx > WORLD_W - SCREEN_W) tx = WORLD_W - SCREEN_W;
    if (ty < 0) ty = 0;
    if (ty > WORLD_H - SCREEN_H) ty = WORLD_H - SCREEN_H;
    int dx = tx - cam_x;
    int dy = ty - cam_y;
    if (snap || (dx < 6 && dx > -6)) cam_x = tx; else cam_x += dx / 6;
    if (snap || (dy < 6 && dy > -6)) cam_y = ty; else cam_y += dy / 5;
}

void respawn()
{
    px = cp_x[cp] * 256;
    py = cp_y[cp] * 256;
    vx = 0;
    vy = 0;
    dash_t = 0;
    dash_ready = 1;
    var_jump = 0;
    coyote = 0;
    jbuf = 0;
    blink = 14;
    update_cam(1);
}

void update_player(uint8_t keys)
{
    int mx = 0;
    if (keys & KEY_RIGHT) mx = 1;
    if (keys & KEY_LEFT) mx = mx - 1;
    if (mx > 0) facing = 1;
    if (mx < 0) facing = 0;

    if ((keys & KEY_JUMP) && !(prev_keys & KEY_JUMP)) jbuf = 5;
    else if (jbuf > 0) jbuf--;
    if (grounded) coyote = 6;
    else if (coyote > 0) coyote--;

    // dash
    if ((keys & KEY_DASH) && !(prev_keys & KEY_DASH) && dash_ready && dash_t == 0)
    {
        int dx = mx;
        int dy = 0;
        if (keys & KEY_UP) dy = -1;
        if (keys & KEY_DOWN) dy = 1;
        if (dx == 0 && dy == 0) dx = facing ? 1 : -1;
        int sp = (dx != 0 && dy != 0) ? DASH_DIAG : DASH_SPEED;
        dash_dx = dx;
        dash_dy = dy;
        vx = dx * sp;
        vy = dy * sp;
        dash_t = 8;
        dash_ready = 0;
        var_jump = 0;
        jbuf = 0;
        shake = 3;
        burst((px >> 8) + 3, (py >> 8) + 3, C_ICE);
    }

    if (dash_t > 0)
    {
        spawn((px >> 8) + 2, (py >> 8) + 3, 0, 0, 7, (dash_t & 1) ? C_HAIR : C_WHITE);
        dash_t--;
        if (dash_t == 0)
        {
            vx = vx / 2;
            vy = vy / 2;
            if (dash_dy == 0) vy = 0;
        }
    }
    else
    {
        // horizontal: accelerate / decelerate, softer in the air
        int mult = grounded ? 256 : AIR_MULT;
        int target = mx * MAXRUN;
        if (mx != 0 && ((vx > MAXRUN && mx > 0) || (vx < -MAXRUN && mx < 0)))
            vx = approach(vx, target, RUN_REDUCE * mult / 256);
        else
            vx = approach(vx, target, RUN_ACCEL * mult / 256);

        // jump
        if (jbuf > 0 && coyote > 0)
        {
            vy = -JUMP_SPEED;
            var_jump = 12;
            jbuf = 0;
            coyote = 0;
            grounded = 0;
            spawn((px >> 8) + 1, (py >> 8) + 6, -6, -2, 6, C_ICE);
            spawn((px >> 8) + 4, (py >> 8) + 6, 6, -2, 6, C_ICE);
        }

        // variable jump height: hold to keep full speed
        if (var_jump > 0)
        {
            if (keys & KEY_JUMP)
            {
                if (vy > -JUMP_SPEED) vy = -JUMP_SPEED;
                var_jump--;
            }
            else var_jump = 0;
        }

        // gravity (floaty at the apex while holding jump, fast fall on Down)
        if (!grounded)
        {
            int g = GRAVITY;
            if (vy < HALF_GRAV_THRESH && vy > -HALF_GRAV_THRESH && (keys & KEY_JUMP)) g = g / 2;
            int mf = MAXFALL;
            if (keys & KEY_DOWN) mf = FASTFALL;
            vy = approach(vy, mf, g);
        }
    }

    // move X with pixel-exact collision
    int npx = px + vx;
    int cur = px >> 8;
    int ty = py >> 8;
    int target = npx >> 8;
    while (cur != target)
    {
        int step = target > cur ? 1 : -1;
        if (solid(cur + step, ty))
        {
            vx = 0;
            npx = cur << 8;
            break;
        }
        cur += step;
    }
    px = npx;

    // move Y with pixel-exact collision and head-bump corner correction
    int npy = py + vy;
    int cy = py >> 8;
    int tx = px >> 8;
    int targety = npy >> 8;
    while (cy != targety)
    {
        int step = targety > cy ? 1 : -1;
        if (solid(tx, cy + step))
        {
            bool fixed = false;
            if (step < 0)
            {
                int k = 1;
                while (k <= 3 && !fixed)
                {
                    if (!solid(tx - k, cy + step))
                    {
                        tx = tx - k;
                        fixed = true;
                    }
                    else if (!solid(tx + k, cy + step))
                    {
                        tx = tx + k;
                        fixed = true;
                    }
                    k++;
                }
            }
            if (!fixed)
            {
                if (step > 0 && vy > 700)
                {
                    spawn(tx + 1, cy + 6, -8, -3, 6, C_ICE);
                    spawn(tx + 4, cy + 6, 8, -3, 6, C_ICE);
                }
                vy = 0;
                npy = cy << 8;
                var_jump = 0;
                break;
            }
            px = tx << 8;
        }
        cy += step;
    }
    py = npy;

    grounded = solid(px >> 8, (py >> 8) + 1) && vy >= 0;
    if (grounded && dash_t == 0 && !dash_ready)
    {
        dash_ready = 1;
        flash = 8;
    }

    // checkpoints
    uint8_t c = cp + 1;
    if (c < NCP && hits(px >> 8, py >> 8, cp_x[c] - 4, cp_y[c] - 8, 14, 16))
    {
        cp = c;
        burst(cp_x[c] + 3, cp_y[c], C_GOLD);
    }

    // death: spikes or falling out of the world
    uint8_t i = 0;
    bool dead = (py >> 8) > WORLD_H - 4;
    while (i < NS)
    {
        if (hits(px >> 8, py >> 8, spk_x[i], spk_y[i] + 1, spk_w[i], 3)) dead = true;
        i++;
    }
    if (dead)
    {
        burst((px >> 8) + 3, (py >> 8) + 3, C_HAIR);
        burst((px >> 8) + 3, (py >> 8) + 3, C_WHITE);
        dead_timer = 22;
        shake = 6;
        if (deaths < 99) deaths++;
    }

    if (hits(px >> 8, py >> 8, 210, 8, 14, 18)) playing = false;
    prev_keys = keys;
}

uint32_t main()
{
    if (!isflagsupported(0, 6) || !isflagsupported(1, 1))
    {
        printf("Celeste needs 128x64 color graphics and keyboard support.\n");
        exit();
    }
    flag(0, 6);
    flag(1, 1);
    srand(gettime());
    frame_start = gettime();

    // 0 = title, 1 = playing, 2 = summit
    uint8_t state = 0;
    uint8_t keys = 0;
    update_cam(1);
    while (true)
    {
        keys = getkey();
        shk_x = 0;
        shk_y = 0;
        if (shake > 0)
        {
            shk_x = (shake & 1) ? 1 : -1;
            shk_y = (shake & 2) ? 1 : -1;
            shake--;
        }

        if (state == 0)
        {
            if (keys & KEY_JUMP)
            {
                state = 1;
                respawn();
                prev_keys = keys;
            }
        }
        else if (state == 1)
        {
            if (dead_timer > 0)
            {
                dead_timer--;
                if (dead_timer == 0) respawn();
            }
            else
            {
                update_player(keys);
                update_cam(0);
            }
            if (!playing) state = 2;
            if (flash > 0) flash--;
            if (blink > 0) blink--;
        }
        else
        {
            if (keys & KEY_START) exit();
            if (frame % 20 == 0)
                burst(cam_x + rand(20, 108), cam_y + rand(8, 36), (frame & 32) ? C_GOLD : C_CYAN);
        }
        update_particles();

        draw_background();
        draw_level();
        draw_particles();
        if (state != 1 || (dead_timer == 0 && (blink & 2) == 0)) draw_player();

        if (state == 1)
        {
            draw_hud(dash_ready);
        }
        else if (state == 0)
        {
            drawstring("CELESTE", 46, 8, C_GOLD);
            srect(34, 16, 60, 1, C_CYAN);
            drawstring("C JUMP  V DASH", 28, 20, C_ICE);
            if ((frame >> 4) & 1) drawstring("PRESS C", 46, 50, C_WHITE);
        }
        else
        {
            drawstring("SUMMIT!", 46, 14, C_GOLD);
            drawstring("DEATHS", 36, 26, C_ICE);
            drawchar('0' + deaths / 10, 76, 26, C_WHITE);
            drawchar('0' + deaths % 10, 80, 26, C_WHITE);
            drawstring("ENTER EXIT", 38, 50, C_CYAN);
        }
        refreshscreen();
        frame_wait();
    }
    return 0;
}
