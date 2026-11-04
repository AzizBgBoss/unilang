/* Build with -D PONG_HOST=1 for the host or =0 for a joiner. The host has
   seven network slots in addition to its local player (eight total players). */
#ifndef PONG_HOST
#define PONG_HOST 1
#endif

#define MAX_PEERS 7
#define DISCOVERY_GAME 0x504f4e47

static void draw_bar(uint32_t x, uint32_t y, uint32_t height)
{
    uint32_t row = 0;
    while (row < height) { setpixel(x, y + row, 1); row = row + 1; }
}

uint32_t main(void)
{
    flag(0, 1); flag(1, 1);
    uint32_t peer[MAX_PEERS];
    uint32_t paddle[MAX_PEERS + 1];
    uint32_t count = 0, selected = 0, frame = 0;
    uint32_t ball_x = 32, ball_y = 32, dx = 1, dy = 1;
    uint32_t discovery = 0;
    char local_name[24] = "Unilang Pong";
    char host[64];
    char session_name[32];
    uint16_t session_port = 0;
    uint32_t session_game = 0;
    char state[16];
    char input[2];
    uint32_t i;
    for (i = 0; i < MAX_PEERS; i++) peer[i] = 0xffffffff;
    for (i = 0; i < MAX_PEERS + 1; i++) paddle[i] = 28;

#if PONG_HOST
    uint32_t listener = netopen(NET_TCP, 7777, 0, NULL, NET_OPEN_LISTEN);
    if (listener == 0xffffffff) return 1;
    discovery = 1;
    uint32_t discovery_result = netdiscovery(NET_DISCOVERY_LISTEN | NET_DISCOVERY_ANNOUNCE,
                                             7777, DISCOVERY_GAME, local_name);
    printf("Host on port 7777. A toggles LAN discovery; V rejects the next join; Enter disconnects the selected peer.\n");
    if (discovery_result == 0xffffffff)
        printf("LAN discovery could not start (network error %d).\n", netlasterror());
    else if (netlasterror() == NET_ERR_UNSUPPORTED)
        printf("LAN broadcast is unavailable on this host.\n");
    else
        printf("LAN session is being announced.\n");
#else
    printf("Looking for Pong sessions. A toggles discovery; arrows select; V joins.\n");
    discovery = netdiscovery(NET_DISCOVERY_LISTEN, 0, DISCOVERY_GAME, NULL);
    uint32_t connection = 0xffffffff;
#endif

    for (;;)
    {
        uint8_t keys = getkey();
        if (keys & 1) paddle[0] = paddle[0] > 1 ? paddle[0] - 1 : 1;
        if (keys & 2) paddle[0] = paddle[0] < 51 ? paddle[0] + 1 : 51;

#if PONG_HOST
        if (keys & 16)
        {
            discovery = !discovery;
            discovery_result = netdiscovery(discovery ? (NET_DISCOVERY_LISTEN | NET_DISCOVERY_ANNOUNCE) : 0,
                                            7777, DISCOVERY_GAME, local_name);
            if (discovery_result == 0xffffffff)
                printf("LAN discovery could not change state (network error %d).\n", netlasterror());
            else if (discovery) printf("LAN discovery enabled.\n");
            else printf("LAN discovery disabled.\n");
        }
        uint32_t joining = netaccept(listener);
        if (joining != 0xffffffff)
        {
            if ((keys & 32) || count >= MAX_PEERS)
            { printf("Rejected joining peer.\n"); netclose(joining); }
            else
            {
                netoption(joining, NET_OPT_FRAME_SIZE, 32);
                peer[count] = joining; count = count + 1;
                printf("Player %d joined (%d/%d remote slots).\n", count, count, MAX_PEERS);
            }
        }
        if (count && (keys & 128))
        {
            if (selected >= count) selected = count - 1;
            printf("Disconnected player %d.\n", selected + 1);
            netclose(peer[selected]);
            for (i = selected; i + 1 < count; i++) peer[i] = peer[i + 1];
            count = count - 1;
            if (selected >= count && count) selected = count - 1;
        }
        if (count && (keys & 4)) selected = selected ? selected - 1 : count - 1;
        if (count && (keys & 8)) selected = (selected + 1) % count;

        for (i = 0; i < count; i++)
        {
            if (netstatus(peer[i]) == NET_CLOSED)
            {
                netclose(peer[i]);
                for (uint32_t j = i; j + 1 < count; j++) peer[j] = peer[j + 1];
                count = count - 1; i = i - (i != 0); continue;
            }
            if (netavail(peer[i]))
            {
                uint32_t got = netrecv(peer[i], input, 1);
                if (got == 1) paddle[i + 1] = (uint8_t)input[0];
            }
        }
        ball_x = dx ? ball_x + 1 : ball_x - 1;
        ball_y = dy ? ball_y + 1 : ball_y - 1;
        if (ball_x >= 62) { ball_x = 62; dx = 0; }
        if (!ball_x) dx = 1;
        if (ball_y >= 62) { ball_y = 62; dy = 0; }
        if (!ball_y) dy = 1;
        state[0] = (char)ball_x; state[1] = (char)ball_y;
        state[2] = (char)paddle[0]; state[3] = (char)count;
        for (i = 0; i < MAX_PEERS; i++) state[4 + i] = (char)paddle[i + 1];
        for (i = 0; i < count; i++)
            if (peer[i] != 0xffffffff) netsend(peer[i], state, 4 + count);
#else
        if (keys & 16)
        {
            discovery = !discovery;
            netdiscovery(discovery ? NET_DISCOVERY_LISTEN : 0, 0, DISCOVERY_GAME, NULL);
        }
        uint32_t session_count = netdiscoverycount();
        if (session_count && (keys & 1)) selected = selected ? selected - 1 : session_count - 1;
        if (session_count && (keys & 2)) selected = (selected + 1) % session_count;
        if (discovery && (++frame % 60 == 0))
        {
            uint32_t sessions = session_count;
            printf("Open Pong sessions: %d\n", sessions);
            for (i = 0; i < sessions && i < 8; i++)
                if (netdiscoveryget(i, session_name, host, &session_port, &session_game))
                    printf("%d: %s at %s:%d\n", i, session_name, host, session_port);
        }
        if (connection == 0xffffffff && (keys & 32))
        {
            if (netdiscoverycount() && netdiscoveryget(selected, session_name, host,
                                                        &session_port, &session_game))
            {
                connection = netopen(NET_TCP, 0, session_port, host, 0);
                if (connection != 0xffffffff) netoption(connection, NET_OPT_FRAME_SIZE, 32);
            }
        }
        if (connection != 0xffffffff && netstatus(connection) == NET_CONNECTED)
        {
            input[0] = (char)paddle[0]; netsend(connection, input, 1);
            if (netavail(connection) >= 4)
            {
                uint32_t got = netrecv(connection, state, sizeof(state));
                if (got >= 4) { ball_x = (uint8_t)state[0]; ball_y = (uint8_t)state[1]; paddle[1] = (uint8_t)state[2]; }
            }
        }
#endif
        fillscreen(0);
        draw_bar(2, paddle[0], 12); draw_bar(61, paddle[1], 12);
        setpixel(ball_x, ball_y, 1);
        refreshscreen(); sleep(16);
    }
    return 0;
}
