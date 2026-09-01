uint32 draw_rect(uint32 x, uint32 y, uint32 w, uint32 h, uint32 val) {
    uint32 yy = 0;
    while (yy < h) {
        uint32 xx = 0;
        while (xx < w) {
            setpixel(x + xx, y + yy, val);
            xx = xx + 1;
        }
        yy = yy + 1;
    }
    return 0;
}

uint32 clamp_paddle(uint32 y) {
    if (y > 64 - 12) {
        return 64 - 12;
    }
    return y;
}

uint32 main() {
    if (isflagsupported(0, 1)) {
        flag(0, 1);
        flag(1, 1);
    }

    uint32 paddle_x = 58;
    uint32 paddle_y = 26;
    uint32 paddle_w = 2;
    uint32 paddle_h = 12;
    uint32 ball_x = 12;
    uint32 ball_y = 30;
    uint32 ball_dx = 1;
    uint32 ball_dy = 1;
    uint32 score = 0;

    draw_rect(paddle_x, paddle_y, paddle_w, paddle_h, 1);
    draw_rect(ball_x, ball_y, 2, 2, 1);
    refreshscreen();

    while (1) {
        uint8 key = getkey();
        if (key == 1) {
            if (paddle_y > 0) {
                paddle_y = paddle_y - 1;
            }
        }
        if (key == 2) {
            paddle_y = paddle_y + 1;
        }

        paddle_y = clamp_paddle(paddle_y);

        if (ball_dx == 1) {
            ball_x = ball_x + 1;
        } else if (ball_x > 0) {
            ball_x = ball_x - 1;
        }

        if (ball_dy == 1) {
            ball_y = ball_y + 1;
        } else if (ball_y > 0) {
            ball_y = ball_y - 1;
        }

        if (ball_y <= 0) {
            ball_y = 0;
            ball_dy = 1;
        }
        if (ball_y >= 62) {
            ball_y = 62;
            ball_dy = 0;
        }
        if (ball_x <= 0) {
            ball_x = 0;
            ball_dx = 1;
        }
        if (ball_x >= 62) {
            ball_x = 32;
            ball_y = 32;
            ball_dx = 0;
            ball_dy = 1;
            if (score > 0) {
                score = score - 1;
            }
        }

        if (ball_x >= paddle_x) {
            if (ball_x <= paddle_x + paddle_w) {
                if (ball_y >= paddle_y) {
                    if (ball_y <= paddle_y + paddle_h) {
                        ball_dx = 0;
                        score = score + 1;
                    }
                }
            }
        }

        draw_rect(paddle_x, paddle_y, paddle_w, paddle_h, 1);
        draw_rect(ball_x, ball_y, 2, 2, 1);
        refreshscreen();
    }

    return 0;
}
