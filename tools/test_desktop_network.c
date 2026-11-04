/* POSIX loopback integration tests for the SDL desktop socket callbacks.
   The only endpoint used is a local HTTP fixture started by this test. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#if defined(__APPLE__)
#include <signal.h>
#endif

static uint32_t test_ticks(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}
#define ULVM_NET_NOW_MS() test_ticks()
#define ULVM_MEM_SIZE 8192
#include "../C-interpreter/unilang.h"
#include "../C-interpreter/net_sockets.h"

static int fixture_fd;
static uint16_t fixture_port;
static char fixture_requests[4][2048];

static void pause_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void test_http_request_bytes(void)
{
    const char *headers[] = {"", "A: b", "A: b\r\n"};
    const char *suffix[] = {"\r\n", "\r\n\r\n", "\r\n"};
    for (unsigned i = 0; i < 3; ++i)
    {
        ULVMNetHandle h; ULVMDesktopSocketState state;
        memset(&h, 0, sizeof(h)); memset(&state, 0, sizeof(state));
        h.kind = ULVM_NET_KIND_HTTP; h.status = ULVM_NET_STATUS_CONNECTED;
        h.remote_port = 80; strcpy((char *)h.peer_host, "example.test"); h.host_data = &state;
        assert(ULVM_desktop_http_request(NULL, &h, ULVM_NET_HTTP_POST,
                                         "/submit", headers[i], "body", 4));
        char expected[1024];
        int n = snprintf(expected, sizeof(expected),
                         "POST /submit HTTP/1.0\r\nHost: example.test\r\n"
                         "Connection: close\r\nContent-Length: 4\r\n%s%sbody",
                         headers[i], suffix[i]);
        assert(n > 0 && state.http_tx_len == (uint32_t)n);
        assert(memcmp(state.http_tx, expected, (size_t)n) == 0);
    }
}

static void *http_fixture(void *unused)
{
    (void)unused;
    for (unsigned request = 0; request < 4; ++request)
    {
        int fd = accept(fixture_fd, NULL, NULL); assert(fd >= 0);
        size_t used = 0, needed = 0;
        while (used + 1 < sizeof(fixture_requests[request]))
        {
            ssize_t n = recv(fd, fixture_requests[request] + used,
                             sizeof(fixture_requests[request]) - used - 1, 0);
            assert(n > 0); used += (size_t)n; fixture_requests[request][used] = 0;
            char *end = strstr(fixture_requests[request], "\r\n\r\n");
            if (end)
            {
                char *length = strstr(fixture_requests[request], "Content-Length:");
                needed = (size_t)(end + 4 - fixture_requests[request]);
                if (length) needed += (size_t)strtoul(length + 15, NULL, 10);
                if (used >= needed) break;
            }
        }
        assert(strstr(fixture_requests[request], "Host: 127.0.0.1") != NULL);
        if (request == 3) pause_ms(100);
        char large_headers[700];
        memset(large_headers, 'x', sizeof(large_headers));
        large_headers[sizeof(large_headers) - 1] = 0;
        const char *response = request == 0
            ? "HTTP/1.0 201 Created\r\nContent-Length: 7\r\nConnection: close\r\n\r\ncreated"
            : request == 1
                ? "HTTP/1.0 404 Not Found\r\nContent-Length: 7\r\nConnection: close\r\n\r\nmissing"
                : request == 2 ? NULL
                : "HTTP/1.0 200 OK\r\nContent-Length: 4\r\nConnection: close\r\n\r\nslow";
        char large_response[900];
        if (request == 2)
        {
            int n = snprintf(large_response, sizeof(large_response),
                             "HTTP/1.0 200 OK\r\nX-Pad: %s\r\nContent-Length: 4\r\n\r\nlarge", large_headers);
            assert(n > 512 && (size_t)n < sizeof(large_response));
            response = large_response;
        }
        size_t left = strlen(response); const char *p = response;
        while (left) { ssize_t n = send(fd, p, left, 0); if (n <= 0) break; p += n; left -= (size_t)n; }
        close(fd);
    }
    close(fixture_fd); return NULL;
}

static int open_http(ULVMNet *net, uint16_t port, const char *path,
                     uint8_t method, const char *headers, const char *body)
{
    int handle = ULVM_net_open(net, ULVM_NET_KIND_HTTP, 0, "127.0.0.1", port, 0);
    assert(handle >= 0);
    uint32_t len = body ? (uint32_t)strlen(body) : 0;
    assert(ULVM_net_http_request(net, handle, method, path, headers,
                                 body, len) == 1);
    uint32_t started = test_ticks();
    while (!ULVM_net_http_status(net, handle) && test_ticks() - started < 3000u)
        pause_ms(1);
    return handle;
}

static void test_http(void)
{
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    fixture_fd = socket(AF_INET, SOCK_STREAM, 0); assert(fixture_fd >= 0);
    int yes = 1; setsockopt(fixture_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); addr.sin_port = 0;
    assert(bind(fixture_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    assert(listen(fixture_fd, 4) == 0);
    socklen_t size = sizeof(addr); assert(getsockname(fixture_fd, (struct sockaddr *)&addr, &size) == 0);
    fixture_port = ntohs(addr.sin_port);
    pthread_t thread; assert(pthread_create(&thread, NULL, http_fixture, NULL) == 0);

    ULVMNet net; ULVMDesktopNetContext context;
    ULVM_net_init(&net); ULVM_desktop_net_context_init(&context); context.owner = &net;
    ULVM_net_set_ops(&net, &ULVM_DESKTOP_NET_OPS, &context);
    int post = open_http(&net, fixture_port, "/created", ULVM_NET_HTTP_POST,
                         "Content-Type: text/plain\r\nX-Test: yes\r\n", "score=42");
    assert(ULVM_net_http_status(&net, post) == 201);
    uint32_t started = test_ticks();
    while (!ULVM_net_avail(&net, post) && test_ticks() - started < 3000u) pause_ms(1);
    char body[16] = {0}; assert(ULVM_net_recv(&net, post, body, sizeof(body)) == 7);
    assert(memcmp(body, "created", 7) == 0);

    int get = open_http(&net, fixture_port, "/missing", ULVM_NET_HTTP_GET, "", NULL);
    assert(ULVM_net_http_status(&net, get) == 404);
    started = test_ticks();
    while (!ULVM_net_avail(&net, get) && test_ticks() - started < 3000u) pause_ms(1);
    memset(body, 0, sizeof(body)); assert(ULVM_net_recv(&net, get, body, sizeof(body)) == 7);
    assert(memcmp(body, "missing", 7) == 0);
    assert(strstr(fixture_requests[0], "POST /created HTTP/1.0") != NULL);
    assert(strstr(fixture_requests[0], "X-Test: yes") != NULL);
    assert(strstr(fixture_requests[0], "score=42") != NULL);
    assert(strstr(fixture_requests[1], "GET /missing HTTP/1.0") != NULL);
    int large = open_http(&net, fixture_port, "/large", ULVM_NET_HTTP_GET, "", NULL);
    assert(ULVM_net_http_status(&net, large) == 200);
    started = test_ticks();
    while (!ULVM_net_avail(&net, large) && test_ticks() - started < 3000u) pause_ms(1);
    assert(ULVM_net_avail(&net, large) == 5);
    memset(body, 0, sizeof(body)); assert(ULVM_net_recv(&net, large, body, sizeof(body)) == 5);
    assert(memcmp(body, "large", 5) == 0);
    int timed = ULVM_net_open(&net, ULVM_NET_KIND_HTTP, 0, "127.0.0.1", fixture_port, 0);
    assert(timed >= 0);
    assert(ULVM_net_http_request(&net, timed, ULVM_NET_HTTP_GET, "/timeout", "", NULL, 0) == 1);
    assert(ULVM_net_option(&net, timed, ULVM_NET_OPT_TIMEOUT_MS, 15));
    uint32_t timeout_start = test_ticks();
    while (ULVM_net_status(&net, timed) != ULVM_NET_STATUS_CLOSED &&
           test_ticks() - timeout_start < 3000u) pause_ms(1);
    assert(ULVM_net_lasterror(&net) == ULVM_NET_ERR_TIMEOUT);
    assert(pthread_join(thread, NULL) == 0);
    assert(strstr(fixture_requests[3], "GET /timeout HTTP/1.0") != NULL);
    ULVM_net_shutdown(&net);
}

static int accept_wait(ULVMNet *net, int listener)
{
    uint32_t start = test_ticks(); int peer;
    do { peer = ULVM_net_accept(net, listener); if (peer >= 0) return peer; pause_ms(1); }
    while (test_ticks() - start < 3000u);
    return -1;
}

static void test_tcp_udp(void)
{
    ULVMNet net; ULVMDesktopNetContext context;
    ULVM_net_init(&net); ULVM_desktop_net_context_init(&context); context.owner = &net;
    ULVM_net_set_ops(&net, &ULVM_DESKTOP_NET_OPS, &context);
    int listener = ULVM_net_listen(&net, ULVM_NET_KIND_TCP, 0); assert(listener >= 0);
    uint16_t port = (uint16_t)ULVM_net_localport(&net, listener); assert(port != 0);
    int client1 = ULVM_net_open(&net, ULVM_NET_KIND_TCP, 0, "127.0.0.1", port, 0); assert(client1 >= 0);
    int peer1 = accept_wait(&net, listener); assert(peer1 >= 0);
    assert(ULVM_net_option(&net, client1, ULVM_NET_OPT_FRAME_SIZE, 128));
    assert(ULVM_net_option(&net, peer1, ULVM_NET_OPT_FRAME_SIZE, 128));
    /* send itself polls a newly opened CONNECTING handle before writing. */
    assert(ULVM_net_send(&net, client1, "early", 5) == 5);
    uint32_t start = test_ticks();
    while (ULVM_net_status(&net, client1) == ULVM_NET_STATUS_CONNECTING && test_ticks() - start < 3000u) pause_ms(1);
    assert(ULVM_net_status(&net, client1) == ULVM_NET_STATUS_CONNECTED);
    assert(ULVM_net_option(&net, client1, ULVM_NET_OPT_TCP_NODELAY, 1));
    start = test_ticks(); while (ULVM_net_avail(&net, peer1) != 5 && test_ticks() - start < 3000u) pause_ms(1);
    char buffer[32] = {0}; assert(ULVM_net_recv(&net, peer1, buffer, sizeof(buffer)) == 5);
    assert(memcmp(buffer, "early", 5) == 0);
    assert(ULVM_net_send(&net, client1, "framed", 6) == 6);
    start = test_ticks(); while (ULVM_net_avail(&net, peer1) != 6 && test_ticks() - start < 3000u) pause_ms(1);
    assert(ULVM_net_recv(&net, peer1, buffer, sizeof(buffer)) == 6);
    assert(memcmp(buffer, "framed", 6) == 0);
    uint16_t remote_port = 0; char remote_host[64];
    assert(ULVM_net_peer(&net, peer1, remote_host, sizeof(remote_host), &remote_port) == 1);
    assert(!strcmp(remote_host, "127.0.0.1") && remote_port != 0);

    int client2 = ULVM_net_open(&net, ULVM_NET_KIND_TCP, 0, "127.0.0.1", port, 0); assert(client2 >= 0);
    int peer2 = accept_wait(&net, listener); assert(peer2 >= 0);
    assert(ULVM_net_close(&net, client1) == 1);
    assert(ULVM_net_status(&net, peer2) == ULVM_NET_STATUS_CONNECTED);
    assert(ULVM_net_option(&net, client2, ULVM_NET_OPT_TIMEOUT_MS, 1));
    pause_ms(3); assert(ULVM_net_status(&net, client2) == ULVM_NET_STATUS_CLOSED);
    assert(ULVM_net_lasterror(&net) == ULVM_NET_ERR_TIMEOUT);
    ULVM_net_shutdown(&net);

    /* A reset discovered during framed-send polling frees host_data. */
    ULVM_net_init(&net); ULVM_desktop_net_context_init(&context); context.owner = &net;
    ULVM_net_set_ops(&net, &ULVM_DESKTOP_NET_OPS, &context);
    listener = ULVM_net_listen(&net, ULVM_NET_KIND_TCP, 0); assert(listener >= 0);
    port = (uint16_t)ULVM_net_localport(&net, listener);
    client1 = ULVM_net_open(&net, ULVM_NET_KIND_TCP, 0, "127.0.0.1", port, 0); assert(client1 >= 0);
    peer1 = accept_wait(&net, listener); assert(peer1 >= 0);
    start = test_ticks(); while (ULVM_net_status(&net, client1) == ULVM_NET_STATUS_CONNECTING && test_ticks() - start < 3000u) pause_ms(1);
    assert(ULVM_net_option(&net, client1, ULVM_NET_OPT_FRAME_SIZE, 128));
    ULVMDesktopSocketState *peer_state = (ULVMDesktopSocketState *)net.handles[peer1].host_data;
    struct linger reset = {1, 0};
    assert(setsockopt(peer_state->fd, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)) == 0);
    ULVM_net_close(&net, peer1); pause_ms(5);
    assert(ULVM_net_send(&net, client1, "reset", 5) == -1);
    ULVM_net_shutdown(&net);

    ULVM_net_init(&net); ULVM_desktop_net_context_init(&context); context.owner = &net;
    ULVM_net_set_ops(&net, &ULVM_DESKTOP_NET_OPS, &context);
    int receiver = ULVM_net_open(&net, ULVM_NET_KIND_UDP, 34567, NULL, 0, 0); assert(receiver >= 0);
    int sender = ULVM_net_open(&net, ULVM_NET_KIND_UDP, 0, "127.0.0.1", 34567, 0); assert(sender >= 0);
    assert(ULVM_net_localport(&net, sender) != 34567);
    assert(ULVM_net_send(&net, sender, "a", 1) == 1);
    assert(ULVM_net_send(&net, sender, "bc", 2) == 2);
    start = test_ticks(); while (ULVM_net_avail(&net, receiver) < 1 && test_ticks() - start < 3000u) pause_ms(1);
    (void)ULVM_net_avail(&net, receiver); (void)ULVM_net_avail(&net, receiver);
    assert(ULVM_net_avail(&net, receiver) == 3);
    memset(buffer, 0, sizeof(buffer)); assert(ULVM_net_recv(&net, receiver, buffer, sizeof(buffer)) == 3);
    assert(memcmp(buffer, "abc", 3) == 0);
    assert(ULVM_net_close(&net, sender) == 1); assert(ULVM_net_close(&net, receiver) == 1);
    ULVM_net_shutdown(&net);
}

int main(void)
{
#if defined(__APPLE__)
    signal(SIGPIPE, SIG_IGN);
#endif
    test_http_request_bytes(); test_http(); test_tcp_udp();
    puts("Desktop loopback networking tests passed");
    return 0;
}
