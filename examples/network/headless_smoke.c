/* End-to-end networking contract check for tools/headless.c. The headless
   backend is deterministic, local-only, and never opens real sockets. */
static uint32_t fail(uint32_t code)
{ printf("NETWORK SMOKE FAIL %d\n", code); return code; }

uint32_t main(void)
{
    char address[16] = "127.0.0.1";
    char path[16] = "/created";
    char headers[32] = "X-Test: yes\r\n";
    char body[8] = "body";
    char received[24];
    char session[32];
    char host[64];
    uint16_t port;
    uint32_t game_id;
    uint32_t listener = netopen(NET_TCP, 7700, 0, NULL, NET_OPEN_LISTEN);
    uint32_t client = netopen(NET_TCP, 0, 7700, address, 0);
    if (listener == 0xffffffff || client == 0xffffffff) return fail(1);
    uint32_t peer = netaccept(listener);
    if (peer == 0xffffffff) return fail(2);
    if (!netoption(client, NET_OPT_FRAME_SIZE, 64) ||
        !netoption(peer, NET_OPT_FRAME_SIZE, 64)) return fail(3);
    if (netsend(client, body, 4) != 4) return fail(4);
    if (netavail(peer) != 4 || netrecv(peer, received, 24) != 4) return fail(5);

    uint32_t http = netopen(NET_HTTP, 0, 80, address, 0);
    if (http == 0xffffffff || !nethttp(http, NET_HTTP_POST, path,
                                      headers, body, 4)) return fail(6);
    if (nethttpstatus(http) != 201 || netavail(http) != 7) return fail(7);
    if (netrecv(http, received, 24) != 7) return fail(8);

    if (!netdiscovery(NET_DISCOVERY_LISTEN, 0, 1234, NULL)) return fail(9);
    if (netdiscoverycount() != 1) return fail(10);
    if (!netdiscoveryget(0, session, host, &port, &game_id)) return fail(11);
    if (port != 7777 || game_id != 1234) return fail(14);
    if (netlocalport(listener) != 7700 || netpeer(client, host, &port) == 0xffffffff)
        return fail(12);
    if (port != 7700) return fail(15);
    if (!netdiscovery(0, 0, 0, NULL)) return fail(12);
    netclose(http); netclose(peer); netclose(client); netclose(listener);
    printf("NETWORK SMOKE PASS\n");
    return 0;
}
