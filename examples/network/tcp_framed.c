/* Compile with -D HOST_MODE=1 for the listener and =0 for the joining client. */
#ifndef HOST_MODE
#define HOST_MODE 1
#endif

uint32_t main(void)
{
    flag(1, 1);
    char address[16] = "127.0.0.1";
    char message[16] = "ping from game";
    char received[32];
#if HOST_MODE
    uint32_t listener = netopen(NET_TCP, 7777, 0, NULL, NET_OPEN_LISTEN);
    if (listener == 0xffffffff) return 1;
    uint32_t peer = 0xffffffff;
    for (;;)
    {
        if (peer == 0xffffffff)
        {
            peer = netaccept(listener);
            if (peer != 0xffffffff) netoption(peer, NET_OPT_FRAME_SIZE, 128);
        }
        if (peer != 0xffffffff && netavail(peer))
        {
            uint32_t count = netrecv(peer, received, sizeof(received));
            if (count != 0xffffffff) netsend(peer, received, count);
        }
        sleep(1);
    }
#else
    uint32_t peer = netopen(NET_TCP, 0, 7777, address, 0);
    if (peer == 0xffffffff) return 2;
    netoption(peer, NET_OPT_FRAME_SIZE, 128);
    while (netstatus(peer) == NET_CONNECTING) sleep(1);
    for (;;)
    {
        netsend(peer, message, 14);
        if (netavail(peer))
        {
            uint32_t count = netrecv(peer, received, sizeof(received));
            if (count != 0xffffffff) printf("echo: %s\n", received);
        }
        sleep(1000);
    }
#endif
    return 0;
}
