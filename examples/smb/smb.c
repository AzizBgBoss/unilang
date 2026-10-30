// Super Mario Bros. (PICO-8 cart by Tabelombre) prototype for the unilang VM.
// Assets (sprites, flags, World 1-1 tile grid, palette) are extracted from the cart by
// extract_cart.py into smb_data.h. 128x64 RGB332 view with a camera that follows Mario in X and Y.
// Controls: Left/Right move, C jump (hold = higher), V run, Down = crouch (big Mario).
// Physics constants are the cart's own, in 8-bit fixed point (256 = 1 px).
#include "smb_data.h"

#define KEY_LEFT (1 << 2)
#define KEY_RIGHT (1 << 3)
#define KEY_JUMP (1 << 4)
#define KEY_RUN (1 << 5)
#define KEY_DOWN (1 << 1)
#define FRAME_MS 16
#define MAXE 12
#define POLE_X 1584
#define START_X 24

// entity types: 1 mario, 2 goomba, 3 mushroom, 4 coin pop, 5 squished goomba, 6 koopa, 7 shell,
// 8 fireball, 9 fire flower, 10 star, 11 1up
int ex[MAXE];
int ey[MAXE];
int edx[MAXE];
int edy[MAXE];
uint8_t et[MAXE];
uint8_t ew[MAXE];
uint8_t eh[MAXE];
uint8_t eg[MAXE];
int etm[MAXE];

uint8_t lvl[LVL_W * LVL_H];
uint8_t spawned[NENT];
uint8_t lut[16];

int camx = 0;
int camy = 48;
uint8_t prev_keys = 0;
uint32_t frame = 0;
uint32_t frame_start = 0;
int score = 0;
int coins = 0;
int hp = 1;
int nc = 0;
int nv = 1;
int run_t = 0;
int run_f = 0;
int dd = 205;
int t2 = 0;
int hit_t = 0;
int dead_t = 0;
int xflip = 0;
int crouch = 0;
int timeleft = 400;
int won = 0;
int star_t = 0;
int pole = 0;
int pole_t = 0;
int flag_y = 16;
int lives = 3;
int bump_t = 0;
int bump_x = 0;
int bump_y = 0;
int bump_tile = 0;
int bump_item = 0;
int bump_final = 3;
int cbhits = 0;

// ---- helpers ------------------------------------------------------------------------------
int tileat(int px, int py)
{
    if (px < 0 || py < 0) return 0;
    int tx = px >> 3;
    int ty = py >> 3;
    if (tx >= LVL_W || ty >= LVL_H) return 0;
    return lvl[ty * LVL_W + tx];
}

int iabs(int v)
{
    if (v < 0) return 0 - v;
    return v;
}

void spawn(uint8_t type, int x, int y, int dx, int dy)
{
    uint8_t i = 1;
    while (i < MAXE)
    {
        if (et[i] == 0)
        {
            et[i] = type;
            ex[i] = x * 256;
            ey[i] = y * 256;
            edx[i] = dx;
            edy[i] = dy;
            eg[i] = 0;
            etm[i] = 0;
            ew[i] = 7;
            eh[i] = 7;
            if (type == 3 || type == 9 || type == 10 || type == 11) { ew[i] = 6; eh[i] = 8; etm[i] = 35; }
            if (type == 6 || type == 7) { ew[i] = 6; }
            if (type == 8) { ew[i] = 4; eh[i] = 4; etm[i] = 256; }
            if (type == 4) { etm[i] = 24; }
            return;
        }
        i = i + 1;
    }
}

// block hit from below (Mario moving up)
void bump(int px, int py)
{
    if (bump_t > 0) return;
    int cx = px >> 3;
    int cy = py >> 3;
    int idx = cy * LVL_W + cx;
    int t = lvl[idx];
    int v = 0;
    uint8_t k = 0;
    while (k < NCONT)
    {
        if (cont_x[k] == cx && cont_y[k] == cy) v = cont_v[k];
        k = k + 1;
    }
    if (t == 6 && v == 0)
    {
        if (hp >= 2)
        {
            lvl[idx] = 0;
            score = score + 50;
            return;
        }
    }
    if (t == 2 || t == 6 || t == 174)
    {
        bump_x = cx;
        bump_y = cy;
        bump_t = 8;
        bump_tile = t;
        bump_final = 3;
        bump_item = 0;
        if (t == 6 && v == 0) bump_final = 6;
        if (v == 1 || v == 2 || v == 4 || v == 9 || v == 11 || (v == 0 && t != 6))
        {
            spawn(4, cx * 8 + 4, cy * 8 - 4, 0, 0 - 614);
            coins = coins + 1;
            score = score + 200;
            if (v == 4)
            {
                cbhits = cbhits + 1;
                if (cbhits < 10) bump_final = 6;
            }
        }
        if (v == 3 || v == 5)
        {
            bump_item = 1;
            if (hp >= 2) bump_item = 2;
        }
        if (v == 6) bump_item = 3;
        if (v == 7 || v == 8 || v == 10) bump_item = 4;
    }
}

// ---- tile collision (port of the cart's t0) ----------------------------------------------
void collide(uint8_t i)
{
    int hw = ew[i] * 128 - 3;
    int hh = eh[i] * 128 - 3;
    uint8_t k = 0;
    while (k < 3)
    {
        int ox = 0;
        int oy = 0;
        if (k == 1) { ox = 0 - hw; oy = 0 - hh; }
        if (k == 2) { ox = hw; oy = hh; }
        int vy = hh;
        if (edy[i] < 0) vy = 0 - hh;
        int px = (ex[i] + ox) >> 8;
        int py = (ey[i] + vy + edy[i]) >> 8;
        int t = tileat(px, py);
        int f = smb_flags[t];
        if ((f & 3) != 0)
        {
            if (i == 0 && edy[i] < 0) bump(px, py);
            if ((f & 1) != 0 || edy[i] < 0)
            {
                int fall = 0;
                if (edy[i] > 0) fall = 1;
                eg[i] = fall;
                int base = (((ey[i] + edy[i]) >> 8) >> 3) * 8 * 256;
                if (fall == 1) ey[i] = base + 2048 - eh[i] * 128;
                else ey[i] = base + eh[i] * 128;
                edy[i] = 0;
            }
        }
        int sx = 1;
        if (edx[i] < 0) sx = 0 - 1;
        int qx = (ex[i] + sx * hw + edx[i]) >> 8;
        int qy = (ey[i] + oy) >> 8;
        int t2v = tileat(qx, qy);
        if ((smb_flags[t2v] & 1) != 0)
        {
            int wall = (((ex[i] + hw + edx[i]) >> 8) >> 3) * 8 * 256;
            ex[i] = wall - sx * ew[i] * 128;
            if (i == 0) edx[i] = 0;
            else edx[i] = 0 - edx[i];
        }
        k = k + 1;
    }
}

// ---- rendering ---------------------------------------------------------------------------
void draw8(int s, int sx, int sy, int flip)
{
    if (sx <= 0 - 8 || sx >= 128 || sy <= 0 - 8 || sy >= 64) return;
    int gx0 = (s & 15) * 8;
    int gy0 = (s >> 4) * 8;
    int yy = 0;
    while (yy < 8)
    {
        int py = sy + yy;
        if (py >= 0 && py < 64)
        {
            int xx = 0;
            while (xx < 8)
            {
                int px = sx + xx;
                if (px >= 0 && px < 128)
                {
                    int gx = gx0 + xx;
                    if (flip == 1) gx = gx0 + 7 - xx;
                    uint8_t b = smb_gfx[(gy0 + yy) * 64 + (gx >> 1)];
                    uint8_t c = b & 15;
                    if ((gx & 1) == 1) c = b >> 4;
                    if (c != 12) setpixel(px, py, lut[c]);
                }
                xx = xx + 1;
            }
        }
        yy = yy + 1;
    }
}

void set_lut(int a, int b, int c, int d, int e)
{
    uint8_t i = 0;
    while (i < 16)
    {
        lut[i] = disp332[i];
        i = i + 1;
    }
    lut[1] = disp332[a];
    lut[2] = disp332[b];
    lut[3] = disp332[c];
    lut[4] = disp332[d];
    lut[5] = disp332[e];
}

void draw_world()
{
    // sky
    uint8_t sky = disp332[12];
    int y = 0;
    while (y < 64)
    {
        int x = 0;
        while (x < 128)
        {
            setpixel(x, y, sky);
            x = x + 1;
        }
        y = y + 1;
    }
    uint8_t q = 1;
    while (q < MAXE)
    {
        if ((et[q] == 3 || et[q] == 9 || et[q] == 10 || et[q] == 11) && etm[q] > 0)
        {
            if (et[q] == 3) set_lut(8, 2, 3, 4, 5);
            if (et[q] == 9) set_lut(1, 2, 3, 4, 5);
            if (et[q] == 10) set_lut(1, 2, 3, 4, 5);
            if (et[q] == 11) set_lut(12, 2, 3, 4, 5);
            int s0 = 139;
            if (et[q] == 9) s0 = 140;
            if (et[q] == 10) s0 = 141;
            draw8(s0, (ex[q] >> 8) - 4 - camx, (ey[q] >> 8) - 4 - camy, 0);
        }
        q = q + 1;
    }
    // tiles use the world-1 draw palette (1->4, 2->15, 8->12) and the cart's animated colour 10
    uint8_t i = 0;
    while (i < 16)
    {
        lut[i] = disp332[i];
        i = i + 1;
    }
    lut[1] = disp332[4];
    lut[2] = disp332[15];
    lut[8] = disp332[12];
    int ph = (frame / 10) % 5;
    int cyc = 10;
    if (ph == 0 || ph == 4) cyc = 9;
    if (ph == 2) cyc = 4;
    lut[10] = disp332[cyc];
    int ty = camy >> 3;
    while (ty <= ((camy + 63) >> 3) && ty < LVL_H)
    {
        int tx = camx >> 3;
        while (tx <= ((camx + 127) >> 3) && tx < LVL_W)
        {
            int t = lvl[ty * LVL_W + tx];
            if (t != 0 && !(bump_t > 0 && tx == bump_x && ty == bump_y)) draw8(t, tx * 8 - camx, ty * 8 - camy, 0);
            tx = tx + 1;
        }
        ty = ty + 1;
    }
    if (bump_t > 0)
    {
        int off = bump_t;
        if (bump_t > 4) off = 9 - bump_t;
        draw8(bump_tile, bump_x * 8 - camx, bump_y * 8 - camy - off, 0);
    }
    // flagpole pennant
    draw8(172, POLE_X - 4 - camx, flag_y - camy, 0);
}

void draw_entities()
{
    uint8_t i = 1;
    while (i < MAXE)
    {
        if (et[i] != 0)
        {
            int sx = (ex[i] >> 8) - 4 - camx;
            int sy = (ey[i] >> 8) - 4 - camy;
            int tt = et[i];
            if (tt == 2)
            {
                set_lut(0, 4, 15, 9, 5);
                draw8(107, sx, sy, (frame >> 3) & 1);
            }
            if (tt == 5)
            {
                set_lut(0, 4, 15, 9, 5);
                draw8(123, sx, sy, 0);
            }
            if (tt == 6)
            {
                set_lut(1, 9, 3, 4, 5);
                int fl = 0;
                if (edx[i] > 0) fl = 1;
                draw8(91 + ((frame >> 3) & 1), sx, sy, fl);
                draw8(75 + ((frame >> 3) & 1), sx, sy - 8, fl);
            }
            if (tt == 7)
            {
                set_lut(1, 9, 3, 4, 5);
                draw8(93, sx, sy, 0);
            }
            if (etm[i] <= 0 || tt == 2 || tt == 5 || tt == 6 || tt == 7 || tt == 8)
            {
                if (tt == 3)
                {
                    set_lut(8, 2, 3, 4, 5);
                    draw8(139, sx, sy, 0);
                }
                if (tt == 9)
                {
                    set_lut(1, 2, 3, 4, 5);
                    draw8(140, sx, sy, 0);
                }
                if (tt == 10)
                {
                    set_lut(1, 2, 3, 4, 5);
                    draw8(141, sx, sy, 0);
                }
                if (tt == 11)
                {
                    set_lut(12, 2, 3, 4, 5);
                    draw8(139, sx, sy, 0);
                }
            }
            if (tt == 4)
            {
                set_lut(1, 2, 3, 4, 5);
                draw8(192 + ((frame / 3) & 3), sx, sy, 0);
            }
            if (tt == 8)
            {
                set_lut(1, 2, 3, 4, 5);
                draw8(136, sx, sy, 0);
            }
        }
        i = i + 1;
    }
    // Mario
    if (won == 0 && (dead_t > 0 || (hit_t % 2) == 0))
    {
        set_lut(4, 8, 9, 10, 5);
        if (hp == 3) set_lut(8, 15, 9, 10, 4);
        if (star_t > 0)
        {
            int ph = (frame >> 1) % 3;
            if (ph == 0) set_lut(9, 3, 7, 9, 5);
            if (ph == 1) set_lut(9, 4, 7, 9, 5);
            if (ph == 2) set_lut(4, 1, 9, 9, 5);
        }
        int sx = (ex[0] >> 8) - 4 - camx;
        int yy = ey[0] >> 8;
        if (hp >= 2 && crouch == 0)
        {
            int bt = 80 + nv - 1;
            if (nv == 5) bt = 84;
            if (nv == 6) bt = 85;
            draw8(bt, sx, yy - camy, xflip);
            draw8(bt - 16, sx, yy - 8 - camy, xflip);
        }
        else if (hp >= 2)
        {
            draw8(87, sx, yy - 4 - camy, xflip);
            draw8(71, sx, yy - 12 - camy, xflip);
        }
        else
        {
            int s = 96 + nv - 1;
            if (dead_t > 0) s = 117;
            if (nv == 5) s = 100;
            if (nv == 6) s = 101;
            draw8(s, sx, yy - 4 - camy, xflip);
        }
    }
}

void draw_hud()
{
    int sc = score;
    int d = 5;
    while (d >= 0)
    {
        drawchar('0' + sc % 10, 1 + d * 4, 1, 255);
        sc = sc / 10;
        d = d - 1;
    }
    drawchar('x', 32, 1, 252);
    drawchar('0' + (coins / 10) % 10, 36, 1, 252);
    drawchar('0' + coins % 10, 40, 1, 252);
    drawstring("1-1", 70, 1, 255);
    drawchar('0' + timeleft / 100, 100, 1, 255);
    drawchar('0' + (timeleft / 10) % 10, 104, 1, 255);
    drawchar('0' + timeleft % 10, 108, 1, 255);
    if (won == 1) drawstring("COURSE CLEAR", 38, 28, 255);
    if (dead_t > 0 && lives_out() == 1) drawstring("GAME OVER", 44, 28, 224);
}

int lives_out()
{
    return 0;
}

// ---- game state --------------------------------------------------------------------------
void reset_level()
{
    int i = 0;
    while (i < LVL_W * LVL_H)
    {
        lvl[i] = smb_level[i];
        i = i + 1;
    }
    uint8_t e = 0;
    while (e < NENT)
    {
        spawned[e] = 0;
        e = e + 1;
    }
    e = 0;
    while (e < MAXE)
    {
        et[e] = 0;
        e = e + 1;
    }
    et[0] = 1;
    ex[0] = START_X * 256;
    ey[0] = 92 * 256;
    edx[0] = 0;
    edy[0] = 0;
    ew[0] = 6;
    eh[0] = 7;
    hp = 1;
    nv = 1;
    camx = 0;
    camy = 48;
    dead_t = 0;
    hit_t = 0;
    crouch = 0;
    timeleft = 400;
    won = 0;
    star_t = 0;
    pole = 0;
    flag_y = 16;
    bump_t = 0;
    cbhits = 0;
}

void die()
{
    if (dead_t == 0)
    {
        dead_t = 1;
        edy[0] = 0 - 512;
        edx[0] = 0;
    }
}

void update_mario(uint8_t keys)
{
    if (dead_t > 0)
    {
        dead_t = dead_t + 1;
        edy[0] = edy[0] + 31;
        ey[0] = ey[0] + edy[0];
        if (dead_t > 150) reset_level();
        return;
    }
    if (won == 1) return;
    if (pole == 1)
    {
        ex[0] = (POLE_X) * 256;
        ey[0] = ey[0] + 256;
        flag_y = flag_y + 2;
        if (flag_y > 76) flag_y = 76;
        nv = 5;
        if (ey[0] + eh[0] * 128 >= 88 * 256)
        {
            pole = 2;
            pole_t = 0;
            ex[0] = (POLE_X + 6) * 256;
            xflip = 1;
        }
        return;
    }
    if (pole == 2)
    {
        flag_y = flag_y + 2;
        if (flag_y > 76) flag_y = 76;
        pole_t = pole_t + 1;
        if (pole_t > 25)
        {
            pole = 3;
            xflip = 0;
            edx[0] = 0;
        }
        return;
    }
    int nb = 0;
    if ((keys & KEY_RIGHT) != 0) nb = nb + 1;
    if ((keys & KEY_LEFT) != 0) nb = nb - 1;
    if (pole == 3) nb = 1;
    if (star_t > 0) star_t = star_t - 1;
    if (hp == 3 && crouch == 0 && (keys & KEY_RUN) != 0 && (prev_keys & KEY_RUN) == 0)
    {
        int nfb = 0;
        uint8_t f = 1;
        while (f < MAXE)
        {
            if (et[f] == 8) nfb = nfb + 1;
            f = f + 1;
        }
        if (nfb < 2)
        {
            int dir = 1;
            if (xflip == 1) dir = 0 - 1;
            spawn(8, (ex[0] >> 8) + dir * 5, (ey[0] >> 8), dir * 410, 0);
        }
    }
    if (hp >= 2 && eg[0] == 1)
    {
        if ((keys & KEY_DOWN) != 0 && crouch == 0)
        {
            ey[0] = ey[0] + 1024;
            eh[0] = 7;
            crouch = 1;
        }
        if ((keys & KEY_DOWN) == 0 && crouch == 1)
        {
            ey[0] = ey[0] - 1024;
            eh[0] = 15;
            crouch = 0;
        }
    }
    if (hp < 2) eh[0] = 7;
    if (nb != 0 && !(eg[0] == 1 && crouch == 1))
    {
        t2 = nb;
        xflip = 0;
        if (nb < 0) xflip = 1;
    }
    else nb = 0;
    int gr = 51;
    if (eg[0] == 1)
    {
        dd = 205;
        if ((keys & KEY_RUN) != 0) dd = 336;
    }
    if (pole == 0 && (keys & KEY_JUMP) != 0 && (prev_keys & KEY_JUMP) == 0 && eg[0] == 1)
    {
        int extra = 0;
        if (iabs(edx[0]) >= 307) extra = 85;
        edy[0] = 0 - 512 - extra;
        nc = 6;
    }
    if (edy[0] < 0 && nc < 25)
    {
        if ((keys & KEY_JUMP) != 0) gr = 10;
        if (nc == 7) nv = 5;
    }
    edx[0] = edx[0] + nb * 10;
    if (edx[0] > dd) edx[0] = dd;
    if (edx[0] < 0 - dd) edx[0] = 0 - dd;
    nc = nc + 1;
    if (nc > 36) nc = 36;
    if (eg[0] == 1)
    {
        if (edx[0] != 0)
        {
            nv = 2 + run_f;
            run_t = run_t + 1;
            int lim = 3;
            if (iabs(edx[0]) > 128) lim = 2;
            if (run_t > lim)
            {
                run_f = (run_f + 1) % 3;
                run_t = 0;
            }
        }
        else nv = 1;
        if (nb == 0)
        {
            edx[0] = (edx[0] * 235) / 256;
            t2 = 0;
        }
        if (t2 * edx[0] < 0)
        {
            nv = 6;
            edx[0] = (edx[0] * 251) / 256;
        }
        if (iabs(edx[0]) < 10) edx[0] = 0;
    }
    eg[0] = 0;
    edy[0] = edy[0] + gr;
    if (edy[0] > 576) edy[0] = 576;
    collide(0);
    if (crouch == 1) nv = 7;
    if (hit_t > 0) hit_t = hit_t - 1;
    ex[0] = ex[0] + edx[0];
    ey[0] = ey[0] + edy[0];
    // level bounds (cart: x in [camx+4, end])
    if (ex[0] < (camx + 4) * 256) ex[0] = (camx + 4) * 256;
    if (ex[0] > (LVL_W * 8 - 4) * 256) ex[0] = (LVL_W * 8 - 4) * 256;
    if ((ey[0] >> 8) > 140) die();
    if (pole == 0 && (ex[0] >> 8) + 3 >= POLE_X + 2 && (ex[0] >> 8) < POLE_X + 12 && (ey[0] >> 8) < 86)
    {
        pole = 1;
        edx[0] = 0;
        edy[0] = 0;
        score = score + 2000;
        flag_y = 16;
    }
    if (pole == 3 && (ex[0] >> 8) >= 1632) won = 1;
}

void update_entities()
{
    if (bump_t > 0)
    {
        bump_t = bump_t - 1;
        if (bump_t == 0)
        {
            lvl[bump_y * LVL_W + bump_x] = bump_final;
            if (bump_item == 1) spawn(3, bump_x * 8 + 4, bump_y * 8 + 4, 0, 0 - 51);
            if (bump_item == 2) spawn(9, bump_x * 8 + 4, bump_y * 8 + 4, 0, 0 - 51);
            if (bump_item == 3) spawn(10, bump_x * 8 + 4, bump_y * 8 + 4, 0, 0 - 51);
            if (bump_item == 4) spawn(11, bump_x * 8 + 4, bump_y * 8 + 4, 0, 0 - 51);
            bump_item = 0;
        }
    }
    uint8_t i = 1;
    while (i < MAXE)
    {
        int tt = et[i];
        if (tt != 0)
        {
            int gone = 0;
            int hurt = 0;
            if (tt == 2 || tt == 3 || tt == 6 || tt == 7 || tt == 8 || tt == 9 || tt == 10 || tt == 11)
            {
                eg[i] = 0;
                int isitem = 0;
                if (tt == 3 || tt == 9 || tt == 10 || tt == 11) isitem = 1;
                if (isitem == 1 && etm[i] > 0)
                {
                    etm[i] = etm[i] - 1;
                    ey[i] = ey[i] + edy[i];
                    if (etm[i] == 0)
                    {
                        if (tt == 3 || tt == 11) edx[i] = 128;
                        if (tt == 10) edx[i] = 154;
                    }
                }
                else
                {
                    if ((tt == 2 || tt == 6) && edx[i] == 0) edx[i] = 0 - 64;
                    if (tt == 7 && etm[i] > 0) etm[i] = etm[i] - 1;
                    if (tt != 9)
                    {
                        edy[i] = edy[i] + 51;
                        if (edy[i] > 576) edy[i] = 576;
                        int odx = edx[i];
                        collide(i);
                        if (tt == 8)
                        {
                            if (eg[i] == 1) edy[i] = 0 - 294;
                            if (odx * edx[i] < 0) gone = 1;
                            etm[i] = etm[i] - 1;
                            if (etm[i] <= 0) gone = 1;
                        }
                        if (tt == 10 && eg[i] == 1) edy[i] = 0 - 614;
                        ex[i] = ex[i] + edx[i];
                        ey[i] = ey[i] + edy[i];
                    }
                }
                // enemies hit by fireballs / sliding shells
                if (tt == 8 || (tt == 7 && iabs(edx[i]) > 256))
                {
                    uint8_t j = 1;
                    while (j < MAXE)
                    {
                        if (j != i && (et[j] == 2 || et[j] == 6 || (et[j] == 7 && tt == 8)))
                        {
                            if (iabs(ex[j] - ex[i]) * 2 < (ew[j] + ew[i]) * 256 && iabs(ey[j] - ey[i]) * 2 < (eh[j] + eh[i]) * 256)
                            {
                                et[j] = 0;
                                score = score + 100;
                                if (tt == 8) gone = 1;
                            }
                        }
                        j = j + 1;
                    }
                }
                // contact with Mario
                if (dead_t == 0 && pole == 0 && iabs(ex[0] - ex[i]) * 2 < (ew[0] + ew[i]) * 256 && iabs(ey[0] + edy[0] - ey[i]) * 2 < (eh[0] + eh[i]) * 256)
                {
                    if (isitem == 1 && etm[i] == 0)
                    {
                        gone = 1;
                        score = score + 1000;
                        if (tt == 11) lives = lives + 1;
                        if (tt == 10) star_t = 600;
                        if ((tt == 3 || tt == 9) && hp == 1)
                        {
                            hp = 2;
                            ey[0] = ey[0] - 1024;
                            eh[0] = 15;
                        }
                        else if (tt == 9 && hp == 2) hp = 3;
                    }
                    if (tt == 2 || tt == 6 || tt == 7)
                    {
                        int stomp = 0;
                        if (edy[0] > 0 && ey[0] + eh[0] * 128 < ey[i] + 512) stomp = 1;
                        if (star_t > 0)
                        {
                            gone = 1;
                            score = score + 100;
                        }
                        else if (tt == 7 && edx[i] == 0)
                        {
                            edx[i] = 512;
                            if (ex[i] < ex[0]) edx[i] = 0 - 512;
                            etm[i] = 12;
                            if (stomp == 1) edy[0] = 0 - 384;
                            score = score + 400;
                        }
                        else if (stomp == 1)
                        {
                            edy[0] = 0 - 384;
                            score = score + 100;
                            if (tt == 2)
                            {
                                et[i] = 5;
                                etm[i] = 48;
                                edx[i] = 0;
                            }
                            if (tt == 6)
                            {
                                et[i] = 7;
                                edx[i] = 0;
                                etm[i] = 0;
                            }
                            if (tt == 7) edx[i] = 0;
                        }
                        else if (hit_t == 0 && etm[i] == 0 || (hit_t == 0 && tt != 7)) hurt = 1;
                    }
                }
            }
            if (tt == 4)
            {
                edy[i] = edy[i] + 51;
                ey[i] = ey[i] + edy[i];
                etm[i] = etm[i] - 1;
                if (etm[i] <= 0) gone = 1;
            }
            if (tt == 5)
            {
                etm[i] = etm[i] - 1;
                if (etm[i] <= 0) gone = 1;
            }
            if (hurt == 1)
            {
                if (hp >= 2)
                {
                    hp = 1;
                    eh[0] = 7;
                    ey[0] = ey[0] + 1024;
                    hit_t = 60;
                    crouch = 0;
                }
                else die();
            }
            if (ex[i] < (camx - 40) * 256 || ex[i] > (camx + 200) * 256 || (ey[i] >> 8) > 140) gone = 1;
            if (gone == 1) et[i] = 0;
        }
        i = i + 1;
    }
    // spawn level entities as they approach the right edge of the screen
    uint8_t e = 0;
    while (e < NENT)
    {
        if (spawned[e] == 0 && ent_x[e] < camx + 136)
        {
            spawned[e] = 1;
            int nt = ent_type[e];
            if (nt >= 1 && nt <= 3)
            {
                spawn(2, ent_x[e] + 4, ent_y[e] + 4, 0 - 64, 0);
                if (nt >= 2) spawn(2, ent_x[e] + 16, ent_y[e] + 4, 0 - 64, 0);
                if (nt == 3) spawn(2, ent_x[e] + 28, ent_y[e] + 4, 0 - 64, 0);
            }
            if (nt >= 4 && nt <= 6)
            {
                spawn(6, ent_x[e] + 4, ent_y[e] + 12, 0 - 64, 0);
                if (nt >= 5) spawn(6, ent_x[e] + 16, ent_y[e] + 12, 0 - 64, 0);
                if (nt == 6) spawn(6, ent_x[e] + 28, ent_y[e] + 12, 0 - 64, 0);
            }
        }
        e = e + 1;
    }
}

void update_camera()
{
    int sx = (ex[0] >> 8) - camx;
    if (sx > 60) camx = camx + sx - 60;
    if (camx > LVL_W * 8 - 128) camx = LVL_W * 8 - 128;
    int my = ey[0] >> 8;
    int rel = my - camy;
    if (rel < 22) camy = my - 22;
    if (rel > 42) camy = my - 42;
    if (camy < 0) camy = 0;
    if (camy > 112 - 64) camy = 112 - 64;
}

void frame_wait()
{
    uint32_t now = gettime();
    while (now - frame_start < FRAME_MS)
    {
        sleep(1);
        now = gettime();
    }
    if (now - frame_start > FRAME_MS * 4) frame_start = now;
    else frame_start = frame_start + FRAME_MS;
    frame++;
}

uint32_t main()
{
    if (!isflagsupported(0, 6) || !isflagsupported(1, 1))
    {
        printf("Needs 128x64 color graphics and keyboard support.\n");
        exit();
    }
    flag(0, 6);
    flag(1, 1);
    frame_start = gettime();
    reset_level();
    while (true)
    {
        uint8_t keys = getkey();
        update_mario(keys);
        update_entities();
        update_camera();
        if (frame % 25 == 0 && dead_t == 0 && won == 0 && pole == 0)
        {
            timeleft = timeleft - 1;
            if (timeleft <= 0) die();
        }
        prev_keys = keys;
        draw_world();
        draw_entities();
        draw_hud();
        refreshscreen();
        frame_wait();
    }
    return 0;
}
