/* Unilang Pong: single player against the computer, or two players over a LAN.

   Menu:      Single player (with difficulty), Host LAN game, Join LAN game.
   Hosting:   the host shows its address and announces the game on the LAN.
   Joining:   pick a discovered game from a list, or type the host's IP.
   Controls:  Up/Down move; A/Start selects; B/Select backs out.
   Networking needs a host that enables it (desktop: run with --net).

   Network design: the host runs the whole game and sends the ball, scores and
   phase to the joiner every frame; the joiner only sends its own paddle
   position. Both ends use length-prefixed TCP frames (netoption frame size). */

#define PACK332(r, g, b) (((r) << 5) | ((g) << 2) | (b))

#define W 128
#define H 64
#define TOP 9              /* first playfield row; the score bar is above it */
#define PW 3               /* paddle width */
#define PH 12              /* paddle height */
#define BS 3               /* ball size */
#define LX 4               /* left paddle x */
#define RX 121             /* right paddle x */
#define WIN_SCORE 7
#define PORT 7777
#define GAME_ID 0x504f4e47
#define FRAME_MS 16
#define DF 30              /* frames per countdown digit */
#define NONE 0xffffffff

/* scenes */
#define S_MENU 0
#define S_PLAY 1
#define S_HOST 2
#define S_LIST 3
#define S_IP 4
#define S_CONN 5
#define S_NOTE 6

/* game modes */
#define M_SP 0
#define M_HOST 1
#define M_JOIN 2

/* match phases (sent over the network as is) */
#define PH_COUNT 0
#define PH_PLAY 1
#define PH_POINT 2
#define PH_OVER 3

/* sound and message events, one bit each, sent over the network */
#define EV_HIT 1
#define EV_WALL 2
#define EV_SCORE 4
#define EV_TICK 8
#define EV_GO 16
#define EV_WIN 32

/* notices */
#define N_NONET 1
#define N_HOSTFAIL 2
#define N_CONNFAIL 3
#define N_OPPLEFT 4
#define N_HOSTLEFT 5
#define N_SILENT 6

uint32_t cText;
uint32_t cDim;
uint32_t cYou;
uint32_t cOpp;
uint32_t cBall;
uint32_t cHi;
uint32_t cBad;

/* ball in 1/16 pixel units (top left corner), paddles in pixels */
int32_t bx;
int32_t by;
int32_t vx;
int32_t vy;
int32_t pl;
int32_t pr;
uint32_t sl;
uint32_t sr;
uint32_t phase;
uint32_t timer;
uint32_t digit;
uint32_t lastscorer;
uint32_t events;
uint32_t rally;
uint32_t mode;
uint32_t myside;
uint32_t paused;
uint32_t ai_level;
int32_t ai_err;
int32_t ai_acc;

uint32_t scene;
uint32_t note_id;
uint32_t note_next;
uint32_t net_ok;
uint32_t hlisten;
uint32_t hconn;
uint32_t conn_port;
uint32_t conn_start;
uint32_t lastrx;
uint32_t discovery_ok;

char hname[32] = "PONG HOST";
char myip[32];
char ipstr[32];
char tmp[16];
uint8_t rxb[32];
uint8_t txb[16];
uint32_t ipo[4];

/* discovered games shown in the join list */
uint32_t nsess;
char s_host[128];
char s_name[96];
uint16_t s_port[8];
char thost[64];
char tname[32];
uint16_t tport;
uint32_t tgid;

/* ---------- small helpers ---------- */

/* write v in decimal at buf[at]; returns the new write position */
uint32_t put_num(char *buf, uint32_t at, uint32_t v)
{
    if (v >= 100)
    {
        buf[at] = '0' + v / 100;
        at = at + 1;
    }
    if (v >= 10)
    {
        buf[at] = '0' + (v / 10) % 10;
        at = at + 1;
    }
    buf[at] = '0' + v % 10;
    at = at + 1;
    buf[at] = 0;
    return at;
}

/* x that centres a string of n characters (the font is 4 pixels wide) */
uint32_t cx(uint32_t n)
{
    return (W - n * 4) / 2;
}

void tri_up(uint32_t x, uint32_t y, uint32_t c)
{
    fillrect(x + 2, y, 1, 1, c);
    fillrect(x + 1, y + 1, 3, 1, c);
    fillrect(x, y + 2, 5, 1, c);
}

void tri_down(uint32_t x, uint32_t y, uint32_t c)
{
    fillrect(x, y, 5, 1, c);
    fillrect(x + 1, y + 1, 3, 1, c);
    fillrect(x + 2, y + 2, 1, 1, c);
}

void title_bar(uint32_t c)
{
    fillrect(0, 8, W, 1, cDim);
}

/* "a.b.c.d" from the four numbers in ipo[] */
void build_ip()
{
    uint32_t n = 0;
    uint32_t k = 0;
    while (k < 4)
    {
        n = put_num(ipstr, n, ipo[k]);
        if (k < 3)
        {
            ipstr[n] = '.';
            n = n + 1;
        }
        k = k + 1;
    }
    ipstr[n] = 0;
}

/* read "a.b.c.d" from s into ipo[] */
void parse_ip(char *s)
{
    uint32_t i = 0;
    uint32_t o = 0;
    uint32_t v = 0;
    for (;;)
    {
        uint32_t c = s[i];
        if (c >= 48 && c <= 57)
        {
            v = v * 10 + c - 48;
        }
        else
        {
            if (o < 4)
            {
                ipo[o] = v & 255;
            }
            o = o + 1;
            v = 0;
            if (c == 0 || o >= 4)
            {
                break;
            }
        }
        i = i + 1;
    }
}

void sound(uint32_t ev)
{
    if (ev & EV_WIN)
        beep(1046, 400);
    else if (ev & EV_SCORE)
        beep(220, 180);
    else if (ev & EV_HIT)
        beep(880, 30);
    else if (ev & EV_WALL)
        beep(440, 20);
    else if (ev & EV_GO)
        beep(1046, 120);
    else if (ev & EV_TICK)
        beep(523, 60);
}

/* ---------- networking helpers ---------- */

void net_reset()
{
    if (hconn != NONE)
    {
        netclose(hconn);
        hconn = NONE;
    }
    if (hlisten != NONE)
    {
        netclose(hlisten);
        hlisten = NONE;
    }
    if (net_ok)
    {
        netdiscovery(0, 0, GAME_ID, hname);
    }
}

uint32_t conn_alive(uint32_t h)
{
    uint32_t s = netstatus(h);
    return s == NET_CONNECTED || s == NET_READY;
}

void goto_note(uint32_t id, uint32_t next)
{
    note_id = id;
    note_next = next;
    scene = S_NOTE;
}

/* ---------- game rules (run by single player and by the host) ---------- */

void serve(uint32_t towards)
{
    bx = 63 * 16;
    by = (28 + rand(0, 15)) * 16;
    vx = 28;
    if (towards == 0)
    {
        vx = -28;
    }
    vy = rand(5, 12);
    if (rand(0, 2) == 0)
    {
        vy = -vy;
    }
    rally = 0;
}

void new_match()
{
    sl = 0;
    sr = 0;
    lastscorer = 0;
    pl = 30;
    pr = 30;
    serve(rand(0, 2));
    phase = PH_COUNT;
    timer = 3 * DF;
    digit = 3;
    events = 0;
    paused = 0;
}

void score_point(uint32_t side)
{
    lastscorer = side;
    if (side == 0)
        sl = sl + 1;
    else
        sr = sr + 1;
    events = events | EV_SCORE;
    if (sl >= WIN_SCORE || sr >= WIN_SCORE)
    {
        phase = PH_OVER;
        events = events | EV_WIN;
    }
    else
    {
        phase = PH_POINT;
        timer = 55;
    }
}

/* the ball leaves a paddle: speed grows, angle depends on where it hit */
void bounce(int32_t paddle_y, int32_t dir)
{
    int32_t off = by / 16 + 1 - (paddle_y + PH / 2);
    int32_t speed = abs(vx) + 2;
    if (speed > 60)
    {
        speed = 60;
    }
    vx = dir * speed;
    vy = off * 6;
    if (vy > 44)
        vy = 44;
    if (vy < -44)
        vy = -44;
    if (vy == 0)
    {
        vy = 4;
    }
    rally = rally + 1;
    events = events | EV_HIT;
}

void step_ball()
{
    int32_t old_left = bx;
    int32_t old_right = bx + BS * 16;
    bx = bx + vx;
    by = by + vy;
    if (by < TOP * 16)
    {
        by = TOP * 32 - by;
        vy = -vy;
        events = events | EV_WALL;
    }
    if (by > (H - BS) * 16)
    {
        by = (H - BS) * 32 - by;
        vy = -vy;
        events = events | EV_WALL;
    }
    /* the ball crossing the front face of a paddle this frame while
       overlapping it vertically is a hit; crossing without overlap is a miss */
    if (vx < 0 && old_left >= (LX + PW) * 16 && bx < (LX + PW) * 16)
    {
        if (by / 16 + BS > pl && by / 16 < pl + PH)
        {
            bx = (LX + PW) * 16;
            bounce(pl, 1);
            ai_err = rand(0, 2 * (2 - ai_level) * 3 + 1) - (2 - ai_level) * 3;
        }
    }
    if (vx > 0 && old_right <= RX * 16 && bx + BS * 16 > RX * 16)
    {
        if (by / 16 + BS > pr && by / 16 < pr + PH)
        {
            bx = (RX - BS) * 16;
            bounce(pr, -1);
        }
    }
    if (bx + BS * 16 < 0)
    {
        score_point(1);
    }
    else if (bx > W * 16)
    {
        score_point(0);
    }
}

/* the computer opponent drives the right paddle */
void ai_move()
{
    int32_t target = (TOP + H) / 2 - PH / 2;
    if (phase == PH_PLAY && vx > 0)
    {
        target = by / 16 + 1 - PH / 2 + ai_err;
    }
    int32_t quarters = 3;
    if (ai_level == 1)
        quarters = 5;
    if (ai_level == 2)
        quarters = 8;
    ai_acc = ai_acc + quarters;
    while (ai_acc >= 4)
    {
        ai_acc = ai_acc - 4;
        if (target > pr + 1)
            pr = pr + 1;
        else if (target < pr - 1)
            pr = pr - 1;
    }
}

void clamp_paddles()
{
    if (pl < TOP)
        pl = TOP;
    if (pl > H - PH)
        pl = H - PH;
    if (pr < TOP)
        pr = TOP;
    if (pr > H - PH)
        pr = H - PH;
}

/* advance the match by one frame */
void tick()
{
    events = 0;
    if (phase == PH_COUNT)
    {
        timer = timer - 1;
        uint32_t d = (timer + DF - 1) / DF;
        if (timer == 0)
        {
            phase = PH_PLAY;
            digit = 0;
            events = events | EV_GO;
        }
        else if (d != digit)
        {
            digit = d;
            events = events | EV_TICK;
        }
    }
    else if (phase == PH_POINT)
    {
        timer = timer - 1;
        if (timer == 0)
        {
            serve(lastscorer == 0 ? 1 : 0);
            phase = PH_COUNT;
            timer = 2 * DF;
            digit = 2;
        }
    }
    else if (phase == PH_PLAY)
    {
        step_ball();
    }
}

/* ---------- network protocol ---------- */

/* host to joiner: 14 byte state frame */
void send_state()
{
    int32_t sx = bx;
    int32_t sy = by;
    if (sx < 0)
        sx = 0;
    if (sy < 0)
        sy = 0;
    txb[0] = 1;
    txb[1] = phase;
    txb[2] = digit;
    txb[3] = sl;
    txb[4] = sr;
    txb[5] = lastscorer;
    txb[6] = events;
    txb[7] = rally;
    txb[8] = (sx >> 8) & 255;
    txb[9] = sx & 255;
    txb[10] = (sy >> 8) & 255;
    txb[11] = sy & 255;
    txb[12] = pl;
    netsend(hconn, txb, 13);
}

/* joiner: apply every state frame that has arrived; returns how many */
uint32_t recv_state()
{
    uint32_t got = 0;
    uint32_t n = 0;
    while (netavail(hconn) && n < 8)
    {
        uint32_t len = netrecv(hconn, rxb, 32);
        n = n + 1;
        if (len >= 13 && rxb[0] == 1)
        {
            phase = rxb[1];
            digit = rxb[2];
            sl = rxb[3];
            sr = rxb[4];
            lastscorer = rxb[5];
            events = events | rxb[6];
            rally = rxb[7];
            bx = (rxb[8] << 8) | rxb[9];
            by = (rxb[10] << 8) | rxb[11];
            pl = rxb[12];
            got = got + 1;
        }
    }
    return got;
}

/* host: take the joiner's paddle position from its input frames */
void recv_input()
{
    uint32_t n = 0;
    while (netavail(hconn) && n < 8)
    {
        uint32_t len = netrecv(hconn, rxb, 32);
        n = n + 1;
        if (len >= 2 && rxb[0] == 2)
        {
            pr = rxb[1];
        }
    }
}

/* ---------- drawing ---------- */

void draw_play()
{
    /* score bar: who is who is always spelled out */
    uint32_t left_c = cOpp;
    uint32_t right_c = cOpp;
    if (myside == 0)
        left_c = cYou;
    else
        right_c = cYou;
    if (myside == 0)
        drawstring("YOU", 2, 1, left_c);
    else if (mode == M_SP)
        drawstring("CPU", 2, 1, left_c);
    else
        drawstring("OPP", 2, 1, left_c);
    if (myside == 1)
        drawstring("YOU", 114, 1, right_c);
    else if (mode == M_SP)
        drawstring("CPU", 114, 1, right_c);
    else
        drawstring("OPP", 114, 1, right_c);
    drawchar('0' + sl, 18, 1, left_c);
    drawchar('0' + sr, 106, 1, right_c);
    drawstring("FIRST TO 7", 44, 1, cDim);
    fillrect(0, 8, W, 1, cDim);

    /* court */
    uint32_t y = TOP;
    while (y < H)
    {
        fillrect(63, y, 1, 2, cDim);
        y = y + 4;
    }
    fillrect(LX, pl, PW, PH, left_c);
    fillrect(RX, pr, PW, PH, right_c);
    if (phase == PH_COUNT || phase == PH_PLAY)
    {
        fillrect(bx / 16, by / 16, BS, BS, cBall);
    }

    /* messages */
    if (phase == PH_COUNT)
    {
        drawstring("GET READY", cx(9), 18, cText);
        if (digit > 0)
        {
            drawchar('0' + digit, 62, 26, cHi);
        }
        if (sl + sr == 0)
        {
            if (myside == 0)
                drawstring("YOU ARE THE LEFT PADDLE", cx(23), 49, cYou);
            else
                drawstring("YOU ARE THE RIGHT PADDLE", cx(24), 49, cYou);
            drawstring("UP/DOWN: MOVE  B/SELECT: LEAVE", cx(30), 57, cDim);
        }
    }
    else if (phase == PH_POINT)
    {
        if (lastscorer == myside)
            drawstring("YOU SCORE!", cx(10), 22, cYou);
        else if (mode == M_SP)
            drawstring("CPU SCORES", cx(10), 22, cOpp);
        else
            drawstring("OPP SCORES", cx(10), 22, cOpp);
    }
    else if (phase == PH_OVER)
    {
        uint32_t winner = 1;
        if (sl >= WIN_SCORE)
            winner = 0;
        if (winner == myside)
            drawstring("YOU WIN!", cx(8), 18, cYou);
        else
            drawstring("YOU LOSE", cx(8), 18, cOpp);
        drawchar('0' + sl, 56, 28, cText);
        drawchar('-', 62, 28, cText);
        drawchar('0' + sr, 68, 28, cText);
        if (mode == M_SP)
        {
            drawstring("A/START: PLAY AGAIN", cx(19), 49, cHi);
            drawstring("B/SELECT: MENU", cx(14), 57, cDim);
        }
        else if (mode == M_HOST)
        {
            drawstring("A/START: REMATCH", cx(16), 49, cHi);
            drawstring("B/SELECT: MENU", cx(14), 57, cDim);
        }
        else
        {
            drawstring("WAITING FOR HOST", cx(16), 49, cDim);
            drawstring("B/SELECT: LEAVE", cx(15), 57, cDim);
        }
    }
    if (phase == PH_PLAY && rally >= 4)
    {
        uint32_t n = 0;
        tmp[0] = 'R';
        tmp[1] = 'A';
        tmp[2] = 'L';
        tmp[3] = 'L';
        tmp[4] = 'Y';
        tmp[5] = ' ';
        put_num(tmp, 6, rally);
        drawstring(tmp, 50, 58, cDim);
    }
    if (paused)
    {
        drawstring("PAUSED", cx(6), 22, cHi);
        drawstring("START: RESUME  B/SELECT: MENU", cx(29), 52, cDim);
    }
}

uint32_t main(void)
{
    if (isflagsupported(FLAG_GRAPHICS, GFX_RGB332_128X64))
    {
        flag(FLAG_GRAPHICS, GFX_RGB332_128X64);
        cText = PACK332(7, 7, 3);
        cDim = PACK332(3, 3, 1);
        cYou = PACK332(2, 7, 1);
        cOpp = PACK332(7, 3, 1);
        cBall = PACK332(7, 7, 3);
        cHi = PACK332(7, 6, 0);
        cBad = PACK332(7, 1, 0);
    }
    else
    {
        flag(FLAG_GRAPHICS, GFX_MONO_128X64);
        cText = 1;
        cDim = 1;
        cYou = 1;
        cOpp = 1;
        cBall = 1;
        cHi = 1;
        cBad = 1;
    }
    flag(FLAG_KEYBOARD, 1);
    if (isflagsupported(FLAG_AUDIO, AUDIO_BEEP))
    {
        flag(FLAG_AUDIO, AUDIO_BEEP);
    }
    net_ok = isflagsupported(FLAG_NETWORK, 1);
    srand(gettime());

    hlisten = NONE;
    hconn = NONE;
    ai_level = 1;
    scene = S_MENU;
    uint32_t sel = 0;
    uint32_t scroll = 0;
    uint32_t frame = 0;
    uint32_t keys = getkey();
    uint32_t prev = keys;
    uint32_t pressed = 0;
    uint32_t rep = 0;
    uint32_t octet = 3;
    uint32_t i;
    uint32_t k;

    for (;;)
    {
        keys = getkey();
        pressed = keys & ~prev & 255;
        prev = keys;
        frame = frame + 1;

        /* ===== main menu ===== */
        if (scene == S_MENU)
        {
            if (pressed & KEY_UP)
                sel = sel > 0 ? sel - 1 : 2;
            if (pressed & KEY_DOWN)
                sel = sel < 2 ? sel + 1 : 0;
            if (sel == 0)
            {
                if ((pressed & KEY_LEFT) && ai_level > 0)
                    ai_level = ai_level - 1;
                if ((pressed & KEY_RIGHT) && ai_level < 2)
                    ai_level = ai_level + 1;
            }
            if (pressed & (KEY_A | KEY_START))
            {
                if (sel == 0)
                {
                    mode = M_SP;
                    myside = 0;
                    new_match();
                    scene = S_PLAY;
                }
                else if (!net_ok)
                {
                    goto_note(N_NONET, S_MENU);
                }
                else if (sel == 1)
                {
                    net_reset();
                    hlisten = netopen(NET_TCP, PORT, 0, NULL, NET_OPEN_LISTEN);
                    if (hlisten == NONE)
                    {
                        goto_note(N_HOSTFAIL, S_MENU);
                    }
                    else
                    {
                        discovery_ok = netdiscovery(NET_DISCOVERY_ANNOUNCE, PORT, GAME_ID, hname);
                        if (netlasterror() != 0)
                            discovery_ok = NONE;
                        netlocalip(myip);
                        scene = S_HOST;
                    }
                }
                else
                {
                    net_reset();
                    netdiscovery(NET_DISCOVERY_LISTEN, 0, GAME_ID, hname);
                    nsess = 0;
                    sel = 0;
                    scroll = 0;
                    scene = S_LIST;
                }
            }

            drawstring("UNILANG PONG", cx(12), 2, cHi);
            fillrect(0, 8, W, 1, cDim);
            /* a little court sketch next to the title */
            fillrect(2, 2, 2, 5, cYou);
            fillrect(124, 2, 2, 5, cOpp);
            uint32_t row = 0;
            while (row < 3)
            {
                uint32_t ry = 17 + row * 10;
                uint32_t c = cText;
                if (row > 0 && !net_ok)
                    c = cDim;
                if (row == sel)
                {
                    c = cHi;
                    if (row > 0 && !net_ok)
                        c = cBad;
                    drawchar('>', 8, ry, c);
                }
                if (row == 0)
                    drawstring("SINGLE PLAYER", 18, ry, c);
                else if (row == 1)
                    drawstring("HOST LAN GAME", 18, ry, c);
                else
                    drawstring("JOIN LAN GAME", 18, ry, c);
                row = row + 1;
            }
            /* difficulty of the computer opponent */
            uint32_t lc = cDim;
            if (sel == 0)
            {
                lc = cHi;
                drawchar('<', 82, 17, lc);
                drawchar('>', 118, 17, lc);
            }
            if (ai_level == 0)
                drawstring("EASY", 94, 17, lc);
            else if (ai_level == 1)
                drawstring("NORMAL", 90, 17, lc);
            else
                drawstring("HARD", 94, 17, lc);

            if (sel == 0)
                drawstring("LEFT/RIGHT: DIFFICULTY", cx(22), 49, cText);
            else if (!net_ok)
                drawstring("NEEDS NETWORKING (--NET)", cx(24), 49, cBad);
            else if (sel == 1)
                drawstring("START A GAME FOR A FRIEND", cx(25), 49, cText);
            else
                drawstring("FIND A GAME ON YOUR LAN", cx(23), 49, cText);
            drawstring("UP/DOWN  A/START: SELECT", cx(24), 57, cDim);
        }

        /* ===== waiting for a joiner ===== */
        else if (scene == S_HOST)
        {
            uint32_t p = netaccept(hlisten);
            if (p != NONE)
            {
                hconn = p;
                netoption(hconn, NET_OPT_FRAME_SIZE, 32);
                netoption(hconn, NET_OPT_TCP_NODELAY, 1);
                netclose(hlisten);
                hlisten = NONE;
                netdiscovery(0, 0, GAME_ID, hname);
                mode = M_HOST;
                myside = 0;
                new_match();
                scene = S_PLAY;
                lastrx = gettime();
            }
            else if (pressed & (KEY_B | KEY_SELECT))
            {
                net_reset();
                scene = S_MENU;
                sel = 1;
            }
            drawstring("HOSTING LAN GAME", cx(16), 1, cHi);
            fillrect(0, 8, W, 1, cDim);
            drawstring("WAITING FOR A PLAYER", cx(20), 14, cText);
            uint32_t dots = (frame / 20) % 4;
            k = 0;
            while (k < dots)
            {
                drawchar('.', 48 + k * 8, 20, cText);
                k = k + 1;
            }
            drawstring("YOUR ADDRESS", cx(12), 30, cDim);
            drawstring(myip, cx(15), 37, cHi);
            drawstring("PORT 7777", cx(9), 44, cDim);
            if (discovery_ok == NONE)
                drawstring("NOT ON LAN LIST: SHARE ADDRESS", cx(30), 51, cBad);
            else
                drawstring("VISIBLE TO OTHERS ON THE LAN", cx(28), 51, cYou);
            drawstring("B/SELECT: CANCEL", cx(16), 58, cDim);
        }

        /* ===== list of games found on the LAN ===== */
        else if (scene == S_LIST)
        {
            uint32_t cnt = netdiscoverycount();
            if (cnt == NONE)
                cnt = 0;
            nsess = 0;
            i = 0;
            while (i < cnt && i < 8)
            {
                if (netdiscoveryget(i, tname, thost, &tport, &tgid) == 1 && tgid == GAME_ID)
                {
                    memcpy(&s_host[nsess * 16], thost, 16);
                    s_host[nsess * 16 + 15] = 0;
                    memcpy(&s_name[nsess * 12], tname, 12);
                    s_name[nsess * 12 + 11] = 0;
                    s_port[nsess] = tport;
                    nsess = nsess + 1;
                }
                i = i + 1;
            }
            if (pressed & KEY_UP)
                sel = sel > 0 ? sel - 1 : nsess;
            if (pressed & KEY_DOWN)
                sel = sel < nsess ? sel + 1 : 0;
            if (sel > nsess)
                sel = nsess;
            if (sel < scroll)
                scroll = sel;
            if (sel >= scroll + 4)
                scroll = sel - 3;

            if (pressed & (KEY_B | KEY_SELECT))
            {
                net_reset();
                scene = S_MENU;
                sel = 2;
            }
            else if (pressed & (KEY_A | KEY_START))
            {
                if (sel < nsess)
                {
                    memcpy(ipstr, &s_host[sel * 16], 16);
                    conn_port = s_port[sel];
                    netdiscovery(0, 0, GAME_ID, hname);
                    hconn = netopen(NET_TCP, 0, conn_port, ipstr, 0);
                    if (hconn == NONE)
                    {
                        goto_note(N_CONNFAIL, S_LIST);
                    }
                    else
                    {
                        netoption(hconn, NET_OPT_FRAME_SIZE, 32);
                        netoption(hconn, NET_OPT_TCP_NODELAY, 1);
                        conn_start = gettime();
                        scene = S_CONN;
                    }
                }
                else
                {
                    /* manual entry starts from this computer's own address */
                    netlocalip(myip);
                    parse_ip(myip);
                    octet = 3;
                    netdiscovery(0, 0, GAME_ID, hname);
                    scene = S_IP;
                }
            }

            drawstring("JOIN LAN GAME", cx(13), 1, cHi);
            fillrect(0, 8, W, 1, cDim);
            uint32_t r = 0;
            while (r < 4)
            {
                uint32_t idx = scroll + r;
                uint32_t ry = 12 + r * 8;
                uint32_t c = cText;
                if (idx == sel)
                {
                    c = cHi;
                    drawchar('>', 2, ry, c);
                }
                if (idx < nsess)
                {
                    k = 0;
                    while (k < 15 && s_host[idx * 16 + k])
                    {
                        tmp[k] = s_host[idx * 16 + k];
                        k = k + 1;
                    }
                    tmp[k] = 0;
                    drawstring(tmp, 10, ry, c);
                    k = 0;
                    while (k < 12 && s_name[idx * 12 + k])
                    {
                        tmp[k] = s_name[idx * 12 + k];
                        k = k + 1;
                    }
                    tmp[k] = 0;
                    drawstring(tmp, 76, ry, cDim);
                }
                else if (idx == nsess)
                {
                    drawstring("MANUAL IP ENTRY...", 10, ry, c);
                }
                r = r + 1;
            }
            if (nsess == 0)
            {
                uint32_t dots = (frame / 20) % 4;
                drawstring("SEARCHING FOR GAMES", 10, 40, cDim);
                k = 0;
                while (k < dots)
                {
                    drawchar('.', 90 + k * 4, 40, cDim);
                    k = k + 1;
                }
                drawstring("ASK A FRIEND TO HOST", cx(20), 48, cDim);
            }
            else
            {
                tmp[0] = '0' + nsess;
                tmp[1] = 0;
                drawstring(tmp, 10, 48, cYou);
                if (nsess == 1)
                    drawstring("GAME FOUND", 18, 48, cYou);
                else
                    drawstring("GAMES FOUND", 18, 48, cYou);
            }
            drawstring("A/START: JOIN  B/SELECT: BACK", cx(29), 58, cDim);
        }

        /* ===== typing the host's IP address ===== */
        else if (scene == S_IP)
        {
            if (pressed & KEY_LEFT)
                octet = octet > 0 ? octet - 1 : 3;
            if (pressed & KEY_RIGHT)
                octet = octet < 3 ? octet + 1 : 0;
            if (keys & (KEY_UP | KEY_DOWN))
                rep = rep + 1;
            else
                rep = 0;
            if ((pressed & KEY_UP) || ((keys & KEY_UP) && rep > 14 && rep % 3 == 0))
                ipo[octet] = ipo[octet] < 255 ? ipo[octet] + 1 : 0;
            if ((pressed & KEY_DOWN) || ((keys & KEY_DOWN) && rep > 14 && rep % 3 == 0))
                ipo[octet] = ipo[octet] > 0 ? ipo[octet] - 1 : 255;
            if (pressed & (KEY_B | KEY_SELECT))
            {
                netdiscovery(NET_DISCOVERY_LISTEN, 0, GAME_ID, hname);
                scene = S_LIST;
            }
            else if (pressed & (KEY_A | KEY_START))
            {
                build_ip();
                conn_port = PORT;
                hconn = netopen(NET_TCP, 0, conn_port, ipstr, 0);
                if (hconn == NONE)
                {
                    goto_note(N_CONNFAIL, S_IP);
                }
                else
                {
                    netoption(hconn, NET_OPT_FRAME_SIZE, 32);
                    netoption(hconn, NET_OPT_TCP_NODELAY, 1);
                    conn_start = gettime();
                    scene = S_CONN;
                }
            }
            drawstring("HOST IP ADDRESS", cx(15), 1, cHi);
            fillrect(0, 8, W, 1, cDim);
            drawstring("LEFT/RIGHT: SELECT OCTET", cx(24), 14, cDim);
            k = 0;
            while (k < 4)
            {
                uint32_t ox = 12 + k * 28;
                uint32_t c = cText;
                uint32_t digits = put_num(tmp, 0, ipo[k]);
                if (k == octet)
                {
                    c = cHi;
                    tri_up(ox + 3, 22, cHi);
                    tri_down(ox + 3, 38, cHi);
                    fillrect(ox - 1, 36, 15, 1, cHi);
                }
                drawstring(tmp, ox, 29, c);
                if (k < 3)
                    drawchar('.', ox + digits * 4, 29, cDim);
                k = k + 1;
            }
            drawstring("UP/DOWN: EDIT OCTET", cx(19), 46, cText);
            drawstring("B/SELECT: BACK", cx(14), 52, cText);
            drawstring("A/START: CONNECT", cx(16), 58, cDim);
        }

        /* ===== connecting to a host ===== */
        else if (scene == S_CONN)
        {
            uint32_t st = netstatus(hconn);
            uint32_t waited = gettime() - conn_start;
            if (st == 0)
            {
                net_reset();
                goto_note(N_CONNFAIL, S_LIST);
            }
            else if (recv_state() > 0)
            {
                mode = M_JOIN;
                myside = 1;
                scene = S_PLAY;
                lastrx = gettime();
                paused = 0;
            }
            else if (waited > 8000)
            {
                net_reset();
                goto_note(N_CONNFAIL, S_LIST);
            }
            else if (pressed & (KEY_B | KEY_SELECT))
            {
                net_reset();
                scene = S_LIST;
                netdiscovery(NET_DISCOVERY_LISTEN, 0, GAME_ID, hname);
            }
            drawstring("JOINING GAME", cx(12), 1, cHi);
            fillrect(0, 8, W, 1, cDim);
            drawstring("CONNECTING TO", cx(13), 20, cText);
            drawstring(ipstr, cx(15), 28, cHi);
            if (st == 3 || st == 4)
                drawstring("CONNECTED! WAITING FOR HOST", cx(27), 38, cYou);
            else
                drawstring("PLEASE WAIT", cx(11), 38, cDim);
            uint32_t dots = (frame / 15) % 4;
            k = 0;
            while (k < dots)
            {
                drawchar('.', 56 + k * 6, 45, cText);
                k = k + 1;
            }
            drawstring("B/SELECT: CANCEL", cx(16), 58, cDim);
        }

        /* ===== notices and errors ===== */
        else if (scene == S_NOTE)
        {
            if (pressed & (KEY_A | KEY_START | KEY_B | KEY_SELECT))
            {
                if (pressed & (KEY_B | KEY_SELECT))
                    note_next = S_MENU;
                if (note_next == S_LIST)
                {
                    netdiscovery(NET_DISCOVERY_LISTEN, 0, GAME_ID, hname);
                    sel = 0;
                    scroll = 0;
                }
                if (note_next == S_HOST)
                {
                    /* the opponent left: open the game for a new player */
                    hlisten = netopen(NET_TCP, PORT, 0, NULL, NET_OPEN_LISTEN);
                    if (hlisten == NONE)
                    {
                        note_next = S_MENU;
                    }
                    else
                    {
                        discovery_ok = netdiscovery(NET_DISCOVERY_ANNOUNCE, PORT, GAME_ID, hname);
                        netlocalip(myip);
                    }
                }
                if (note_next == S_MENU)
                {
                    net_reset();
                    sel = 0;
                }
                scene = note_next;
            }
            drawstring("NOTICE", cx(6), 1, cHi);
            fillrect(0, 8, W, 1, cDim);
            if (note_id == N_NONET)
            {
                drawstring("NETWORKING IS OFF", cx(17), 18, cBad);
                drawstring("START THE GAME WITH THE", cx(23), 30, cText);
                drawstring("--NET OPTION TO PLAY", cx(20), 38, cText);
                drawstring("OVER THE LAN", cx(12), 46, cText);
            }
            else if (note_id == N_HOSTFAIL)
            {
                drawstring("CANNOT START HOSTING", cx(20), 18, cBad);
                drawstring("PORT 7777 IS BUSY OR", cx(20), 30, cText);
                drawstring("BLOCKED. IS ANOTHER", cx(19), 38, cText);
                drawstring("GAME ALREADY HOSTING?", cx(21), 46, cText);
            }
            else if (note_id == N_CONNFAIL)
            {
                drawstring("COULD NOT CONNECT", cx(17), 18, cBad);
                drawstring("CHECK THE ADDRESS AND", cx(21), 30, cText);
                drawstring("THAT THE HOST IS STILL", cx(22), 38, cText);
                drawstring("WAITING FOR A PLAYER", cx(20), 46, cText);
            }
            else if (note_id == N_OPPLEFT)
            {
                drawstring("OPPONENT LEFT", cx(13), 18, cBad);
                drawstring("THE MATCH WAS ENDED", cx(19), 30, cText);
                drawstring("A/START: WAIT FOR PLAYER", cx(24), 46, cHi);
            }
            else if (note_id == N_HOSTLEFT)
            {
                drawstring("LOST THE HOST", cx(13), 18, cBad);
                drawstring("THE HOST LEFT OR THE", cx(20), 30, cText);
                drawstring("CONNECTION WAS LOST", cx(19), 38, cText);
            }
            drawstring("A/START: CONTINUE", cx(17), 52, cHi);
            drawstring("B/SELECT: MENU", cx(14), 58, cDim);
        }

        /* ===== the match ===== */
        else if (scene == S_PLAY)
        {
            int32_t move = 0;
            if (keys & KEY_UP)
                move = -2;
            if (keys & KEY_DOWN)
                move = 2;
            if (myside == 0)
                pl = pl + move;
            else
                pr = pr + move;
            clamp_paddles();

            if (pressed & (KEY_B | KEY_SELECT))
            {
                net_reset();
                scene = S_MENU;
                sel = mode == M_SP ? 0 : (mode == M_HOST ? 1 : 2);
            }
            else if (mode == M_JOIN)
            {
                /* the host owns the game; we draw what it tells us */
                events = 0;
                if (recv_state() > 0)
                {
                    lastrx = gettime();
                    sound(events);
                }
                txb[0] = 2;
                txb[1] = pr;
                netsend(hconn, txb, 2);
                if (!conn_alive(hconn))
                {
                    net_reset();
                    goto_note(N_HOSTLEFT, S_LIST);
                }
                else if (gettime() - lastrx > 6000)
                {
                    net_reset();
                    goto_note(N_HOSTLEFT, S_LIST);
                }
            }
            else
            {
                if (mode == M_HOST)
                {
                    recv_input();
                    clamp_paddles();
                }
                if (mode == M_SP && (pressed & KEY_START) && phase != PH_OVER)
                    paused = !paused;
                if (phase == PH_OVER && (pressed & (KEY_A | KEY_START)))
                    new_match();
                if (!paused)
                {
                    if (mode == M_SP)
                        ai_move();
                    clamp_paddles();
                    tick();
                    sound(events);
                }
                if (mode == M_HOST)
                {
                    send_state();
                    if (!conn_alive(hconn))
                    {
                        net_reset();
                        goto_note(N_OPPLEFT, S_HOST);
                    }
                }
            }
            if (scene == S_PLAY)
            {
                draw_play();
            }
        }

        refreshscreen();
        sleep(FRAME_MS);
    }
    return 0;
}
