/* Nonblocking HTTP POST. Run against a local plain-HTTP endpoint at 127.0.0.1:8080. */
uint32_t main(void)
{
    flag(1, 1);
    char host[16] = "127.0.0.1";
    char path[32] = "/submit";
    char headers[64] = "Content-Type: text/plain\r\nX-Game: unilang\r\n";
    char body[32] = "score=42";
    char response[128];
    uint32_t handle = netopen(NET_HTTP, 0, 8080, host, 0);
    if (handle == 0xffffffff) return 1;
    if (!nethttp(handle, NET_HTTP_POST, path, headers, body, 8))
    { netclose(handle); return 2; }

    uint32_t status = 0;
    uint32_t waited = 0;
    while (!status && waited < 5000)
    {
        status = nethttpstatus(handle);
        if (netstatus(handle) == NET_CLOSED) break;
        if (!status) { sleep(1); waited = waited + 1; }
    }
    if (!status) { netclose(handle); return 3; }
    uint32_t bytes = netavail(handle);
    if (bytes > sizeof(response)) bytes = sizeof(response);
    if (bytes) netrecv(handle, response, bytes);
    printf("HTTP %d, %d response bytes\n", status, bytes);
    netclose(handle);
    return 0;
}
