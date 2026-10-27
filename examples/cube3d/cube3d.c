// Three rotating wireframe cubes projected onto the 128x64 RGB332 display.
// Coordinates and trigonometry use 8-bit fixed point (256 = 1.0).

#define SCREEN_W 128
#define SCREEN_H 64
#define FIXED 256
#define CAMERA_Z 96
#define FOCAL_LENGTH 76

const int16_t sine_table[64] = {
    256, 281, 306, 330, 354, 377, 398, 419,
    437, 454, 469, 482, 493, 501, 507, 511,
    512, 511, 507, 501, 493, 482, 469, 454,
    437, 419, 398, 377, 354, 330, 306, 281,
    256, 231, 206, 182, 158, 135, 114, 93,
    75, 58, 43, 30, 19, 11, 5, 1,
    0, 1, 5, 11, 19, 30, 43, 58,
    75, 93, 114, 135, 158, 182, 206, 231
};

int projected_x[8];
int projected_y[8];

int sine(uint32_t phase)
{
    return sine_table[(phase >> 2) & 63] - FIXED;
}

int cosine(uint32_t phase)
{
    return sine((phase + 64) & 255);
}

void project_vertex(uint32_t vertex, uint32_t angle_x, uint32_t angle_y,
                    int size, int center_x, int center_y)
{
    int x = (vertex & 1) ? size : -size;
    int y = (vertex & 2) ? size : -size;
    int z = (vertex & 4) ? size : -size;
    int sx = sine(angle_x);
    int cx = cosine(angle_x);
    int sy = sine(angle_y);
    int cy = cosine(angle_y);

    int rotated_x = (x * cy + z * sy) / FIXED;
    int rotated_z = (-x * sy + z * cy) / FIXED;
    int rotated_y = (y * cx - rotated_z * sx) / FIXED;
    rotated_z = (y * sx + rotated_z * cx) / FIXED;

    int depth = CAMERA_Z + rotated_z;
    projected_x[vertex] = center_x + rotated_x * FOCAL_LENGTH / depth;
    projected_y[vertex] = center_y - rotated_y * FOCAL_LENGTH / depth;
}

void draw_line(int x0, int y0, int x1, int y1, uint8_t color)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int step_x = dx < 0 ? -1 : 1;
    int step_y = dy < 0 ? -1 : 1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    int error = dx - dy;
    while (true)
    {
        if (x0 >= 0 && x0 < SCREEN_W && y0 >= 0 && y0 < SCREEN_H)
            setpixel(x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        int doubled_error = error * 2;
        if (doubled_error > -dy)
        {
            error = error - dy;
            x0 = x0 + step_x;
        }
        if (doubled_error < dx)
        {
            error = error + dx;
            y0 = y0 + step_y;
        }
    }
}

void cube_edge(uint32_t a, uint32_t b, uint8_t color)
{
    draw_line(projected_x[a], projected_y[a],
              projected_x[b], projected_y[b], color);
}

void draw_cube(int center_x, int center_y, int size,
               uint32_t angle_x, uint32_t angle_y, uint8_t tint)
{
    uint32_t vertex = 0;
    while (vertex < 8)
    {
        project_vertex(vertex, angle_x, angle_y, size, center_x, center_y);
        vertex = vertex + 1;
    }

    // Near and far faces, followed by the four connecting edges.
    cube_edge(0, 1, tint + 32);
    cube_edge(1, 3, tint + 40);
    cube_edge(3, 2, tint + 48);
    cube_edge(2, 0, tint + 40);
    cube_edge(4, 5, tint + 16);
    cube_edge(5, 7, tint + 24);
    cube_edge(7, 6, tint + 32);
    cube_edge(6, 4, tint + 24);
    cube_edge(0, 4, tint + 8);
    cube_edge(1, 5, tint + 16);
    cube_edge(2, 6, tint + 8);
    cube_edge(3, 7, tint + 16);
}

void draw_background(uint32_t time)
{
    int y = 0;
    while (y < SCREEN_H)
    {
        int x = 0;
        uint8_t shade = 1 + y / 12;
        while (x < SCREEN_W)
        {
            setpixel(x, y, shade);
            x = x + 1;
        }
        y = y + 1;
    }

    int star = 0;
    while (star < 30)
    {
        int x = (star * 47 + star * star * 13 + 19) % SCREEN_W;
        int sy = (star * 29 + star * star * 7 + 3) % SCREEN_H;
        if ((time / (100 + star % 5 * 40) + star * 3) % 9 < 6)
            setpixel(x, sy, 0xD6);
        star = star + 1;
    }
}

void main()
{
    if (!isflagsupported(0, 6) || !isflagsupported(1, 1))
    {
        printf("Flag 6 graphics or keyboard is not supported");
        exit();
    }

    flag(0, 6);
    flag(1, 1);
    while (true)
    {
        uint32_t time = gettime() / 20;
        draw_background(time);

        draw_cube(34, 33, 15, time * 3, time * 2, 0x20);
        draw_cube(65, 30, 18, time * 2 + 64, time * 3 + 80, 0x80);
        draw_cube(97, 34, 14, time * 4 + 128, time * 2 + 160, 0xC0);

        refreshscreen();
        if (getkey() & 128)
            exit();
    }
}
