/* Start two copies. Set LOCAL_PORT/REMOTE_PORT to 9000/9001 in one copy and
   9001/9000 in the other; each side binds locally and targets the peer port. */
#ifndef LOCAL_PORT
#define LOCAL_PORT 9000
#endif
#ifndef REMOTE_PORT
#define REMOTE_PORT 9001
#endif

uint32_t main(void)
{
    flag(1, 1);
    char address[16] = "127.0.0.1";
    char packet[32] = "hello over UDP";
    char received[64];
    char peer_address[64];
    uint16_t peer_port;
    uint32_t socket = netopen(NET_UDP, LOCAL_PORT, REMOTE_PORT, address, 0);
    if (socket == 0xffffffff) return 1;
    printf("bound locally on %d, targeting port %d\n", netlocalport(socket), REMOTE_PORT);
    for (;;)
    {
        netsend(socket, packet, 14);
        uint32_t count = netavail(socket);
        if (count)
        {
            if (count > sizeof(received)) count = sizeof(received);
            uint32_t got = netrecv(socket, received, count);
            if (got != 0xffffffff)
            {
                netpeer(socket, peer_address, &peer_port);
                printf("%s:%d says %s\n", peer_address, peer_port, received);
            }
        }
        sleep(1000);
    }
    return 0;
}
