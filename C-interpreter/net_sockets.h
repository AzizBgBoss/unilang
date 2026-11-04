#ifndef UNILANG_DESKTOP_NET_SOCKETS_H
#define UNILANG_DESKTOP_NET_SOCKETS_H

/* Nonblocking desktop socket backend for ULVMNet. Define ULVM_NET_SOCKET_WSA
   on Windows and link ws2_32; POSIX hosts need no extra library. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET ULVMNetSocket;
#define ULVM_NET_INVALID_SOCKET INVALID_SOCKET
#define ulvm_net_close_socket closesocket
static inline int ulvm_net_would_block(void) { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
static inline int ULVM_net_socket_init(void) { WSADATA data; return WSAStartup(MAKEWORD(2, 2), &data) == 0; }
static inline void ULVM_net_socket_quit(void) { WSACleanup(); }
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
typedef int ULVMNetSocket;
#define ULVM_NET_INVALID_SOCKET (-1)
#define ulvm_net_close_socket close
static inline int ulvm_net_would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
static inline int ULVM_net_socket_init(void) { return 1; }
static inline void ULVM_net_socket_quit(void) { }
#endif

typedef struct ULVMDesktopSocketState
{
    ULVMNetSocket fd;
    uint8_t listener;
    uint8_t eof;
    uint8_t http_header_done;
    struct sockaddr_storage remote;
    socklen_t remote_len;
    uint8_t http_tx[4096];
    uint32_t http_tx_len;
    uint32_t http_tx_sent;
    uint8_t http_raw[ULVM_NET_MAX_BUFFER + ULVM_NET_MAX_HTTP_HEADERS];
    uint32_t http_raw_len;
    uint8_t framed_tx[ULVM_NET_MAX_BUFFER + 2u];
    uint32_t framed_tx_len;
    uint32_t framed_tx_sent;
    uint32_t last_activity_ms;
} ULVMDesktopSocketState;

typedef struct ULVMDesktopNetContext
{
    ULVMNet *owner;
    ULVMNetSocket discovery_fd;
    uint8_t discovery_flags;
    uint16_t game_port;
    uint32_t game_id;
    uint32_t last_announce_ms;
    char name[32];
} ULVMDesktopNetContext;

static inline void ULVM_desktop_net_context_init(ULVMDesktopNetContext *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->discovery_fd = ULVM_NET_INVALID_SOCKET;
}

static inline uint32_t ULVM_desktop_net_now(void)
{
    return SDL_GetTicks();
}

static inline void ULVM_desktop_net_abort(ULVMNetHandle *h)
{
    ULVMDesktopSocketState *s = (ULVMDesktopSocketState *)h->host_data;
    if (s)
    {
        if (s->fd != ULVM_NET_INVALID_SOCKET) ulvm_net_close_socket(s->fd);
        free(s);
    }
    h->host_data = NULL;
}

static inline int ulvm_net_set_nonblocking(ULVMNetSocket fd)
{
#if defined(_WIN32)
    u_long enabled = 1;
    return ioctlsocket(fd, FIONBIO, &enabled) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static inline int ulvm_net_resolve_numeric(const char *host, uint16_t port,
                                            int socktype, struct addrinfo **out)
{
    struct addrinfo hints;
    char service[8];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    hints.ai_flags = AI_NUMERICHOST;
    snprintf(service, sizeof(service), "%u", (unsigned)port);
    return getaddrinfo(host, service, &hints, out) == 0;
}

static inline void ulvm_net_endpoint_text(const struct sockaddr *sa, char *host,
                                           size_t host_size, uint16_t *port)
{
    if (!host || host_size == 0) return;
    host[0] = 0;
    if (sa->sa_family == AF_INET)
    {
        const struct sockaddr_in *a = (const struct sockaddr_in *)sa;
        inet_ntop(AF_INET, &a->sin_addr, host, (socklen_t)host_size);
        if (port) *port = ntohs(a->sin_port);
    }
    else if (sa->sa_family == AF_INET6)
    {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)sa;
        inet_ntop(AF_INET6, &a->sin6_addr, host, (socklen_t)host_size);
        if (port) *port = ntohs(a->sin6_port);
    }
}

static inline int ULVM_desktop_net_open(void *ctx, ULVMNetHandle *h, uint8_t kind,
                                        uint16_t local_port, const char *remote_host,
                                        uint16_t remote_port, uint32_t flags)
{
    (void)ctx;
    int type = kind == ULVM_NET_KIND_UDP ? SOCK_DGRAM : SOCK_STREAM;
    if (kind == ULVM_NET_KIND_HTTP && remote_port == 0) remote_port = 80;
    h->remote_port = remote_port;
    h->peer_port = remote_port;
    if (kind != ULVM_NET_KIND_UDP && !(flags & ULVM_NET_OPEN_LISTEN) &&
        (!remote_host || !*remote_host))
    { h->last_error = ULVM_NET_ERR_BAD_KIND; return 0; }
    ULVMDesktopSocketState *state = (ULVMDesktopSocketState *)calloc(1, sizeof(*state));
    if (!state) { h->last_error = ULVM_NET_ERR_LIMIT; return 0; }
    state->fd = ULVM_NET_INVALID_SOCKET;
    if (kind == ULVM_NET_KIND_TCP && (flags & ULVM_NET_OPEN_LISTEN))
    {
        struct addrinfo *list = NULL, hints;
        char service[8];
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        snprintf(service, sizeof(service), "%u", (unsigned)local_port);
        if (getaddrinfo(NULL, service, &hints, &list) != 0) goto io_error;
        for (struct addrinfo *it = list; it; it = it->ai_next)
        {
            state->fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
            if (state->fd == ULVM_NET_INVALID_SOCKET) continue;
            int yes = 1;
            setsockopt(state->fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
            if (bind(state->fd, it->ai_addr, (socklen_t)it->ai_addrlen) == 0 &&
                listen(state->fd, (int)ULVM_NET_MAX_HANDLES) == 0) break;
            ulvm_net_close_socket(state->fd); state->fd = ULVM_NET_INVALID_SOCKET;
        }
        freeaddrinfo(list);
        if (state->fd == ULVM_NET_INVALID_SOCKET || !ulvm_net_set_nonblocking(state->fd)) goto io_error;
        struct sockaddr_storage bound; socklen_t bound_len = sizeof(bound);
        if (getsockname(state->fd, (struct sockaddr *)&bound, &bound_len) == 0)
        {
            if (bound.ss_family == AF_INET) h->local_port = ntohs(((struct sockaddr_in *)&bound)->sin_port);
            else if (bound.ss_family == AF_INET6) h->local_port = ntohs(((struct sockaddr_in6 *)&bound)->sin6_port);
        }
        state->listener = 1; h->status = ULVM_NET_STATUS_LISTENING;
    }
    else
    {
        struct addrinfo *remote = NULL, hints;
        if (remote_host && *remote_host)
        {
            if (!ulvm_net_resolve_numeric(remote_host, remote_port, type, &remote))
            { h->last_error = ULVM_NET_ERR_IO; free(state); return 0; }
        }
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = remote ? remote->ai_family : AF_INET;
        hints.ai_socktype = type;
        hints.ai_flags = AI_PASSIVE;
        char service[8]; snprintf(service, sizeof(service), "%u", (unsigned)local_port);
        struct addrinfo *local = NULL;
        if (getaddrinfo(NULL, service, &hints, &local) != 0) { if (remote) freeaddrinfo(remote); goto io_error; }
        state->fd = socket(hints.ai_family, type, 0);
        if (state->fd == ULVM_NET_INVALID_SOCKET || !ulvm_net_set_nonblocking(state->fd))
        { freeaddrinfo(local); if (remote) freeaddrinfo(remote); goto io_error; }
        int yes = 1;
        if (flags & ULVM_NET_OPEN_BROADCAST)
            setsockopt(state->fd, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof(yes));
        if (bind(state->fd, local->ai_addr, (socklen_t)local->ai_addrlen) != 0)
        { freeaddrinfo(local); if (remote) freeaddrinfo(remote); goto io_error; }
        freeaddrinfo(local);
        struct sockaddr_storage bound; socklen_t bound_len = sizeof(bound);
        if (getsockname(state->fd, (struct sockaddr *)&bound, &bound_len) == 0)
        {
            if (bound.ss_family == AF_INET) h->local_port = ntohs(((struct sockaddr_in *)&bound)->sin_port);
            else if (bound.ss_family == AF_INET6) h->local_port = ntohs(((struct sockaddr_in6 *)&bound)->sin6_port);
        }
        if (remote)
        {
            memcpy(&state->remote, remote->ai_addr, remote->ai_addrlen);
            state->remote_len = (socklen_t)remote->ai_addrlen;
            memcpy(h->peer_host, remote_host, strlen(remote_host) + 1);
            h->peer_port = remote_port;
            h->status = kind == ULVM_NET_KIND_UDP ? ULVM_NET_STATUS_READY : ULVM_NET_STATUS_CONNECTING;
            if (kind != ULVM_NET_KIND_UDP)
            {
                int rc = connect(state->fd, remote->ai_addr, (socklen_t)remote->ai_addrlen);
                if (rc == 0) h->status = ULVM_NET_STATUS_CONNECTED;
                else if (!ulvm_net_would_block()) { freeaddrinfo(remote); goto io_error; }
            }
            freeaddrinfo(remote);
        }
        else h->status = ULVM_NET_STATUS_READY;
    }
    h->host_data = state;
    state->last_activity_ms = ULVM_desktop_net_now();
    h->last_error = ULVM_NET_ERR_NONE;
    return 1;
io_error:
    if (state->fd != ULVM_NET_INVALID_SOCKET) ulvm_net_close_socket(state->fd);
    free(state);
    h->last_error = ULVM_NET_ERR_IO;
    return 0;
}

static inline void ULVM_desktop_net_poll(void *ctx, ULVMNetHandle *h)
{
    (void)ctx;
    ULVMDesktopSocketState *s = (ULVMDesktopSocketState *)h->host_data;
    if (!s || s->listener) return;
    uint32_t now = ULVM_desktop_net_now();
    if (h->timeout_ms && h->status != ULVM_NET_STATUS_CLOSED &&
        (uint32_t)(now - s->last_activity_ms) >= h->timeout_ms)
    {
        h->last_error = ULVM_NET_ERR_TIMEOUT;
        if (ctx && ((ULVMDesktopNetContext *)ctx)->owner)
            ((ULVMDesktopNetContext *)ctx)->owner->last_error = ULVM_NET_ERR_TIMEOUT;
        h->status = ULVM_NET_STATUS_CLOSED; ULVM_desktop_net_abort(h); return;
    }
    if (h->status == ULVM_NET_STATUS_CLOSED) return;
    if (h->status == ULVM_NET_STATUS_CONNECTING)
    {
        fd_set writable; FD_ZERO(&writable); FD_SET(s->fd, &writable);
        struct timeval zero_wait; zero_wait.tv_sec = 0; zero_wait.tv_usec = 0;
#if defined(_WIN32)
        int ready = select(0, NULL, &writable, NULL, &zero_wait);
#else
        int ready = select(s->fd + 1, NULL, &writable, NULL, &zero_wait);
#endif
        if (ready <= 0) return;
        int error = 0; socklen_t size = sizeof(error);
        if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, (char *)&error, &size) == 0 && error == 0)
            h->status = ULVM_NET_STATUS_CONNECTED;
        else if (error) { h->status = ULVM_NET_STATUS_CLOSED; h->last_error = ULVM_NET_ERR_IO; ULVM_desktop_net_abort(h); }
        if (h->status != ULVM_NET_STATUS_CONNECTED) return;
    }
    if (h->kind == ULVM_NET_KIND_HTTP)
    {
        if (s->http_tx_sent < s->http_tx_len)
        {
            int n = (int)send(s->fd, (const char *)s->http_tx + s->http_tx_sent,
                              (int)(s->http_tx_len - s->http_tx_sent),
#if defined(MSG_NOSIGNAL)
                              MSG_NOSIGNAL
#else
                              0
#endif
                              );
            if (n > 0) { s->http_tx_sent += (uint32_t)n; s->last_activity_ms = now; }
            else if (n < 0 && !ulvm_net_would_block())
            { h->last_error = ULVM_NET_ERR_IO; h->status = ULVM_NET_STATUS_CLOSED; ULVM_desktop_net_abort(h); return; }
        }
        if (h->rx_len < ULVM_NET_MAX_BUFFER && s->http_raw_len < sizeof(s->http_raw))
        {
            int n = (int)recv(s->fd, (char *)s->http_raw + s->http_raw_len,
                              (int)(sizeof(s->http_raw) - s->http_raw_len), 0);
            if (n > 0) { s->http_raw_len += (uint32_t)n; s->last_activity_ms = now; }
            else if (n == 0) s->eof = 1;
            else if (!ulvm_net_would_block())
            { h->last_error = ULVM_NET_ERR_IO; h->status = ULVM_NET_STATUS_CLOSED; ULVM_desktop_net_abort(h); return; }
        }
        if (!s->http_header_done)
        {
            uint32_t end = 0;
            for (uint32_t i = 0; i + 3u < s->http_raw_len; ++i)
                if (s->http_raw[i] == '\r' && s->http_raw[i + 1] == '\n' &&
                    s->http_raw[i + 2] == '\r' && s->http_raw[i + 3] == '\n')
                { end = i + 4u; break; }
            if (!end)
            {
                if (s->http_raw_len >= ULVM_NET_MAX_HTTP_HEADERS)
                { h->last_error = ULVM_NET_ERR_NO_BUFFER; h->status = ULVM_NET_STATUS_CLOSED; ULVM_desktop_net_abort(h); }
                else if (s->eof)
                { h->last_error = ULVM_NET_ERR_IO; h->status = ULVM_NET_STATUS_CLOSED; ULVM_desktop_net_abort(h); }
                return;
            }
            unsigned status = 0;
            if (s->http_raw_len < sizeof(s->http_raw)) s->http_raw[s->http_raw_len] = 0;
            if (sscanf((const char *)s->http_raw, "HTTP/%*u.%*u %u", &status) != 1)
            { h->last_error = ULVM_NET_ERR_IO; h->status = ULVM_NET_STATUS_CLOSED; ULVM_desktop_net_abort(h); return; }
            h->http_status = (uint16_t)status;
            memmove(s->http_raw, s->http_raw + end, s->http_raw_len - end);
            s->http_raw_len -= end;
            s->http_header_done = 1;
        }
        if (s->http_header_done && h->rx_len < ULVM_NET_MAX_BUFFER && s->http_raw_len)
        {
            uint32_t n = s->http_raw_len;
            if (n > ULVM_NET_MAX_BUFFER - h->rx_len) n = ULVM_NET_MAX_BUFFER - h->rx_len;
            memcpy(h->rx + h->rx_len, s->http_raw, n);
            h->rx_len += n;
            memmove(s->http_raw, s->http_raw + n, s->http_raw_len - n);
            s->http_raw_len -= n;
        }
        if (s->eof) { h->status = ULVM_NET_STATUS_CLOSED; ULVM_desktop_net_abort(h); }
        else h->status = h->rx_len ? ULVM_NET_STATUS_READY : ULVM_NET_STATUS_CONNECTED;
        return;
    }

    if (s->framed_tx_sent < s->framed_tx_len)
    {
        int n = (int)send(s->fd, (const char *)s->framed_tx + s->framed_tx_sent,
                          (int)(s->framed_tx_len - s->framed_tx_sent),
#if defined(MSG_NOSIGNAL)
                          MSG_NOSIGNAL
#else
                          0
#endif
                          );
        if (n > 0)
        {
            s->framed_tx_sent += (uint32_t)n;
            s->last_activity_ms = now;
            if (s->framed_tx_sent == s->framed_tx_len)
                s->framed_tx_len = s->framed_tx_sent = 0;
        }
        else if (n < 0 && !ulvm_net_would_block())
        { h->last_error = ULVM_NET_ERR_IO; h->status = ULVM_NET_STATUS_CLOSED; ULVM_desktop_net_abort(h); return; }
    }

    if (h->rx_len >= ULVM_NET_MAX_BUFFER) return;
    uint32_t room = ULVM_NET_MAX_BUFFER - h->rx_len;
    int got;
    if (h->kind == ULVM_NET_KIND_UDP)
    {
        struct sockaddr_storage from; socklen_t from_len = sizeof(from);
        got = (int)recvfrom(s->fd, (char *)h->rx + h->rx_len, (int)room, 0,
                            (struct sockaddr *)&from, &from_len);
        if (got >= 0)
        {
            s->last_activity_ms = now;
            h->rx_len += (uint32_t)got;
            if (s->remote_len == 0)
            {
                memcpy(&s->remote, &from, from_len);
                s->remote_len = from_len;
            }
            ulvm_net_endpoint_text((struct sockaddr *)&from, (char *)h->peer_host,
                                   sizeof(h->peer_host), &h->peer_port);
            h->remote_port = h->peer_port;
            h->status = ULVM_NET_STATUS_READY;
        }
    }
    else
    {
        got = (int)recv(s->fd, (char *)h->rx + h->rx_len, (int)room, 0);
        if (got > 0) { s->last_activity_ms = now; h->rx_len += (uint32_t)got; h->status = ULVM_NET_STATUS_READY; }
        else if (got == 0) { s->eof = 1; h->status = ULVM_NET_STATUS_CLOSED; ULVM_desktop_net_abort(h); }
    }
    if (got < 0 && !ulvm_net_would_block())
    { h->last_error = ULVM_NET_ERR_IO; h->status = ULVM_NET_STATUS_CLOSED; ULVM_desktop_net_abort(h); }
}

static inline int ULVM_desktop_http_request(void *ctx, ULVMNetHandle *h, uint8_t method,
                                            const char *path, const char *headers,
                                            const void *body, uint32_t body_len)
{
    (void)ctx;
    ULVMDesktopSocketState *s = (ULVMDesktopSocketState *)h->host_data;
    if (!s || h->kind != ULVM_NET_KIND_HTTP || (method != ULVM_NET_HTTP_GET && method != ULVM_NET_HTTP_POST))
    { h->last_error = ULVM_NET_ERR_BAD_KIND; return 0; }
    if (h->status == ULVM_NET_STATUS_CLOSED)
    { h->last_error = ULVM_NET_ERR_IO; return 0; }
    if (!path || path[0] != '/' || strchr(path, '\r') || strchr(path, '\n') ||
        !headers || strlen(headers) >= ULVM_NET_MAX_HTTP_HEADERS ||
        body_len > ULVM_NET_MAX_TRANSFER || (!body && body_len) || s->http_tx_len)
    { h->last_error = ULVM_NET_ERR_NO_BUFFER; return 0; }
    const char *verb = method == ULVM_NET_HTTP_GET ? "GET" : "POST";
    char host_header[96];
    int ipv6_host = strchr((const char *)h->peer_host, ':') != NULL;
    if (h->remote_port == 80 && ipv6_host)
        snprintf(host_header, sizeof(host_header), "[%s]", (const char *)h->peer_host);
    else if (h->remote_port == 80)
        snprintf(host_header, sizeof(host_header), "%s", (const char *)h->peer_host);
    else if (ipv6_host)
        snprintf(host_header, sizeof(host_header), "[%s]:%u", (const char *)h->peer_host, (unsigned)h->remote_port);
    else
        snprintf(host_header, sizeof(host_header), "%s:%u", (const char *)h->peer_host, (unsigned)h->remote_port);
    int n = snprintf((char *)s->http_tx, sizeof(s->http_tx),
                     "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nContent-Length: %u\r\n%s%s",
                     verb, path, host_header, (unsigned)body_len, headers,
                     (strlen(headers) >= 2 && strcmp(headers + strlen(headers) - 2, "\r\n") == 0)
                         ? "\r\n" : "\r\n\r\n");
    if (n < 0 || (size_t)n + body_len > sizeof(s->http_tx))
    { h->last_error = ULVM_NET_ERR_NO_BUFFER; s->http_tx_len = 0; return 0; }
    if (body_len) memcpy(s->http_tx + n, body, body_len);
    s->http_tx_len = (uint32_t)n + body_len;
    s->http_tx_sent = 0;
    h->http_status = 0;
    s->http_header_done = 0;
    s->http_raw_len = 0;
    h->last_error = ULVM_NET_ERR_NONE;
    return 1;
}

static inline int ULVM_desktop_net_send(void *ctx, ULVMNetHandle *h, const void *data, uint32_t len)
{
    (void)ctx;
    ULVMDesktopSocketState *s = (ULVMDesktopSocketState *)h->host_data;
    if (!s || s->listener) return -1;
    if (h->status == ULVM_NET_STATUS_CONNECTING) return 0;
    if (h->kind == ULVM_NET_KIND_UDP && s->remote_len == 0)
    { h->last_error = ULVM_NET_ERR_BAD_KIND; return -1; }
    if (h->frame_size)
    {
        if (len > h->frame_size) { h->last_error = ULVM_NET_ERR_NO_BUFFER; return -1; }
        if (s->framed_tx_len) return 0;
        s->framed_tx[0] = (uint8_t)(len >> 8); s->framed_tx[1] = (uint8_t)len;
        memcpy(s->framed_tx + 2, data, len);
        s->framed_tx_len = len + 2u; s->framed_tx_sent = 0;
        ULVM_desktop_net_poll(ctx, h);
        s->last_activity_ms = ULVM_desktop_net_now();
        return (int)len;
    }
    int sent = h->kind == ULVM_NET_KIND_UDP
        ? (int)sendto(s->fd, (const char *)data, (int)len, 0, (struct sockaddr *)&s->remote, s->remote_len)
        : (int)send(s->fd, (const char *)data, (int)len,
#if defined(MSG_NOSIGNAL)
                    MSG_NOSIGNAL
#else
                    0
#endif
                    );
    if (sent < 0 && ulvm_net_would_block()) return 0;
    if (sent < 0) { h->last_error = ULVM_NET_ERR_IO; return -1; }
    s->last_activity_ms = ULVM_desktop_net_now();
    return sent;
}

static inline int ULVM_desktop_net_recv(void *ctx, ULVMNetHandle *h, void *dest, uint32_t cap)
{
    (void)ctx;
    ULVM_desktop_net_poll(ctx, h);
    if (cap > h->rx_len) cap = h->rx_len;
    if (cap == 0) return 0;
    memcpy(dest, h->rx, cap);
    memmove(h->rx, h->rx + cap, h->rx_len - cap);
    h->rx_len -= cap;
    if (!h->rx_len && h->kind != ULVM_NET_KIND_UDP && h->status == ULVM_NET_STATUS_READY)
        h->status = ULVM_NET_STATUS_CONNECTED;
    return (int)cap;
}

static inline int ULVM_desktop_net_accept(void *ctx, ULVMNetHandle *listener, ULVMNetHandle *peer)
{
    (void)ctx;
    ULVMDesktopSocketState *ls = (ULVMDesktopSocketState *)listener->host_data;
    if (!ls || !ls->listener) { listener->last_error = ULVM_NET_ERR_BAD_KIND; return -1; }
    struct sockaddr_storage addr; socklen_t addr_len = sizeof(addr);
    ULVMNetSocket fd = accept(ls->fd, (struct sockaddr *)&addr, &addr_len);
    if (fd == ULVM_NET_INVALID_SOCKET)
        return ulvm_net_would_block() ? 0 : (listener->last_error = ULVM_NET_ERR_IO, -1);
    if (!ulvm_net_set_nonblocking(fd)) { ulvm_net_close_socket(fd); listener->last_error = ULVM_NET_ERR_IO; return -1; }
    ULVMDesktopSocketState *ps = (ULVMDesktopSocketState *)calloc(1, sizeof(*ps));
    if (!ps) { ulvm_net_close_socket(fd); listener->last_error = ULVM_NET_ERR_LIMIT; return -1; }
    ps->fd = fd; ps->remote = addr; ps->remote_len = addr_len;
    peer->host_data = ps; peer->kind = ULVM_NET_KIND_TCP; peer->status = ULVM_NET_STATUS_CONNECTED;
    ulvm_net_endpoint_text((struct sockaddr *)&addr, (char *)peer->peer_host, sizeof(peer->peer_host), &peer->peer_port);
    peer->remote_port = peer->peer_port; peer->local_port = listener->local_port;
    return 1;
}

static inline int ULVM_desktop_net_option(void *ctx, ULVMNetHandle *h, uint8_t option, uint32_t value)
{
    (void)ctx;
    ULVMDesktopSocketState *s = (ULVMDesktopSocketState *)h->host_data;
    if (option == ULVM_NET_OPT_TIMEOUT_MS && s && !s->listener)
    { h->timeout_ms = value; s->last_activity_ms = ULVM_desktop_net_now(); return 1; }
    if (option == ULVM_NET_OPT_FRAME_SIZE && h->kind == ULVM_NET_KIND_TCP && value <= ULVM_NET_MAX_BUFFER)
    { h->frame_size = (uint16_t)value; return 1; }
    if (option == ULVM_NET_OPT_FRAME_SIZE && h->kind == ULVM_NET_KIND_TCP)
    { h->last_error = ULVM_NET_ERR_NO_BUFFER; return 0; }
    if (option == ULVM_NET_OPT_HTTP_HEADERS && h->kind == ULVM_NET_KIND_HTTP)
    { h->last_error = ULVM_NET_ERR_UNSUPPORTED; return 0; }
    if (option == ULVM_NET_OPT_UDP_BROADCAST && h->kind == ULVM_NET_KIND_UDP && s)
    {
        int enabled = value != 0;
        return setsockopt(s->fd, SOL_SOCKET, SO_BROADCAST, (const char *)&enabled, sizeof(enabled)) == 0;
    }
    if (option == ULVM_NET_OPT_TCP_NODELAY && h->kind == ULVM_NET_KIND_TCP && s)
    {
        int enabled = value != 0;
        return setsockopt(s->fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&enabled, sizeof(enabled)) == 0;
    }
    h->last_error = ULVM_NET_ERR_UNSUPPORTED;
    return 0;
}

static inline void ULVM_desktop_net_close(void *ctx, ULVMNetHandle *h)
{
    (void)ctx;
    ULVMDesktopSocketState *s = (ULVMDesktopSocketState *)h->host_data;
    if (s) { if (s->fd != ULVM_NET_INVALID_SOCKET) ulvm_net_close_socket(s->fd); free(s); }
    h->host_data = NULL;
}

static inline int ULVM_desktop_net_peer(void *ctx, ULVMNetHandle *h, char *host, size_t size, uint16_t *port)
{
    (void)ctx;
    if (host && size) snprintf(host, size, "%s", (char *)h->peer_host);
    if (port) *port = h->peer_port;
    return 1;
}

static inline int ULVM_desktop_net_localip(void *ctx, char *address, size_t size)
{
    (void)ctx;
    if (!address || size < 10) return 0;
    ULVMNetSocket fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd != ULVM_NET_INVALID_SOCKET)
    {
        struct sockaddr_in route, local; socklen_t length = sizeof(local);
        memset(&route, 0, sizeof(route)); route.sin_family = AF_INET;
        route.sin_port = htons(9); inet_pton(AF_INET, "192.0.2.1", &route.sin_addr);
        if (connect(fd, (struct sockaddr *)&route, sizeof(route)) == 0 &&
            getsockname(fd, (struct sockaddr *)&local, &length) == 0 &&
            local.sin_addr.s_addr != htonl(INADDR_ANY))
        {
            int ok = inet_ntop(AF_INET, &local.sin_addr, address, (socklen_t)size) != NULL;
            ulvm_net_close_socket(fd);
            if (ok) return 1;
        }
        else ulvm_net_close_socket(fd);
    }
    snprintf(address, size, "127.0.0.1");
    return 1;
}

static inline void ulvm_net_write_u32(uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t)(n >> 24); p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8); p[3] = (uint8_t)n;
}

static inline uint32_t ulvm_net_read_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static inline int ulvm_desktop_discovery_send(ULVMDesktopNetContext *ctx, uint8_t type)
{
    uint8_t packet[13 + 31];
    size_t name_len = strlen(ctx->name);
    if (name_len > 31) name_len = 31;
    memcpy(packet, "ULND", 4); packet[4] = 1; packet[5] = type;
    ulvm_net_write_u32(packet + 6, ctx->game_id);
    packet[10] = (uint8_t)(ctx->game_port >> 8); packet[11] = (uint8_t)ctx->game_port;
    packet[12] = (uint8_t)name_len;
    memcpy(packet + 13, ctx->name, name_len);
    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET; target.sin_port = htons(ULVM_NET_DISCOVERY_PORT);
    target.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    int rc = (int)sendto(ctx->discovery_fd, (const char *)packet,
                         (int)(13 + name_len), 0,
                         (struct sockaddr *)&target, sizeof(target));
    return rc == (int)(13 + name_len);
}

static inline int ULVM_desktop_net_discovery(void *opaque, ULVMNet *net,
                                             uint8_t flags, uint16_t game_port,
                                             uint32_t game_id, const char *name)
{
    ULVMDesktopNetContext *ctx = (ULVMDesktopNetContext *)opaque;
    if (!ctx || !net) return 0;
    if (flags == 0)
    {
        int bye_ok = 1;
        if (ctx->discovery_fd != ULVM_NET_INVALID_SOCKET &&
            (ctx->discovery_flags & ULVM_NET_DISCOVERY_ANNOUNCE))
            bye_ok = ulvm_desktop_discovery_send(ctx, 2);
        if (ctx->discovery_fd != ULVM_NET_INVALID_SOCKET) ulvm_net_close_socket(ctx->discovery_fd);
        ctx->discovery_fd = ULVM_NET_INVALID_SOCKET; ctx->discovery_flags = 0;
        memset(net->sessions, 0, sizeof(net->sessions));
        return bye_ok ? 1 : 2;
    }
    if (ctx->discovery_fd == ULVM_NET_INVALID_SOCKET)
    {
        ctx->discovery_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (ctx->discovery_fd == ULVM_NET_INVALID_SOCKET) return 0;
        int yes = 1;
        setsockopt(ctx->discovery_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
        int broadcast = 1;
        if (flags & ULVM_NET_DISCOVERY_ANNOUNCE)
            setsockopt(ctx->discovery_fd, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast));
        struct sockaddr_in local;
        memset(&local, 0, sizeof(local)); local.sin_family = AF_INET;
        local.sin_port = htons(ULVM_NET_DISCOVERY_PORT); local.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(ctx->discovery_fd, (struct sockaddr *)&local, sizeof(local)) != 0 ||
            !ulvm_net_set_nonblocking(ctx->discovery_fd))
        {
            ulvm_net_close_socket(ctx->discovery_fd); ctx->discovery_fd = ULVM_NET_INVALID_SOCKET;
            return 0;
        }
    }
    ctx->discovery_flags = flags; ctx->game_port = game_port; ctx->game_id = game_id;
    snprintf(ctx->name, sizeof(ctx->name), "%s", name ? name : "");
    uint32_t now = ULVM_desktop_net_now();
    int degraded = 0;
    if ((flags & ULVM_NET_DISCOVERY_ANNOUNCE) &&
        (ctx->last_announce_ms == 0 || (uint32_t)(now - ctx->last_announce_ms) >= 1000u))
    {
        if (!ulvm_desktop_discovery_send(ctx, 1)) degraded = 1;
        ctx->last_announce_ms = now;
    }
    if (flags & ULVM_NET_DISCOVERY_LISTEN)
    {
        for (int i = 0; i < 16; ++i)
        {
            uint8_t packet[13 + 31]; struct sockaddr_in from; socklen_t from_len = sizeof(from);
            int got = (int)recvfrom(ctx->discovery_fd, (char *)packet, sizeof(packet), 0,
                                    (struct sockaddr *)&from, &from_len);
            if (got < 0)
            { if (ulvm_net_would_block()) break; degraded = 1; break; }
            if (got < 13 || memcmp(packet, "ULND", 4) != 0 || packet[4] != 1 ||
                (packet[5] != 1 && packet[5] != 2) || packet[12] > 31 || got != 13 + packet[12])
                continue;
            char host[ULVM_NET_MAX_HOST]; uint16_t ignored_port = 0;
            ulvm_net_endpoint_text((struct sockaddr *)&from, host, sizeof(host), &ignored_port);
            uint32_t incoming_id = ulvm_net_read_u32(packet + 6);
            uint16_t incoming_port = (uint16_t)(((uint16_t)packet[10] << 8) | packet[11]);
            if (incoming_port == 0) continue;
            int found = -1, free_slot = -1;
            for (uint32_t j = 0; j < ULVM_NET_MAX_SESSIONS; ++j)
            {
                ULVMNetSession *session = &net->sessions[j];
                if (!session->active && free_slot < 0) free_slot = (int)j;
                if (session->active && session->game_id == incoming_id &&
                    session->port == incoming_port && strcmp(session->host, host) == 0)
                { found = (int)j; break; }
            }
            if (packet[5] == 2)
            { if (found >= 0) net->sessions[found].active = 0; continue; }
            if (found < 0) found = free_slot;
            if (found < 0) continue;
            ULVMNetSession *session = &net->sessions[found];
            memset(session, 0, sizeof(*session)); session->active = 1;
            snprintf(session->host, sizeof(session->host), "%s", host);
            size_t n = packet[12]; memcpy(session->name, packet + 13, n); session->name[n] = 0;
            session->port = incoming_port; session->game_id = incoming_id;
            session->last_seen_ms = now;
        }
    }
    for (uint32_t i = 0; i < ULVM_NET_MAX_SESSIONS; ++i)
        if (net->sessions[i].active && (uint32_t)(now - net->sessions[i].last_seen_ms) > ULVM_NET_DISCOVERY_TTL_MS)
            net->sessions[i].active = 0;
    return degraded ? 2 : 1;
}

static inline void ULVM_desktop_net_shutdown(void *opaque, ULVMNet *net)
{
    ULVMDesktopNetContext *ctx = (ULVMDesktopNetContext *)opaque;
    if (ctx && ctx->discovery_fd != ULVM_NET_INVALID_SOCKET)
    {
        if (ctx->discovery_flags & ULVM_NET_DISCOVERY_ANNOUNCE)
            ulvm_desktop_discovery_send(ctx, 2);
        ulvm_net_close_socket(ctx->discovery_fd);
        ctx->discovery_fd = ULVM_NET_INVALID_SOCKET;
    }
    if (net) memset(net->sessions, 0, sizeof(net->sessions));
}

static inline void ULVM_desktop_net_service(void *opaque, ULVMNet *net)
{
    if (!net || !net->discovery_flags) return;
    int rc = ULVM_desktop_net_discovery(opaque, net, net->discovery_flags,
                                        net->discovery_game_port,
                                        net->discovery_game_id,
                                        net->discovery_name);
    if (rc == 2) net->last_error = ULVM_NET_ERR_UNSUPPORTED;
}

static const ULVMNetOps ULVM_DESKTOP_NET_OPS = {
    ULVM_desktop_net_open, ULVM_desktop_net_poll, ULVM_desktop_net_send,
    ULVM_desktop_net_recv, ULVM_desktop_net_accept, ULVM_desktop_net_option,
    ULVM_desktop_http_request, ULVM_desktop_net_close,
    ULVM_desktop_net_peer, ULVM_desktop_net_localip,
    ULVM_desktop_net_discovery, ULVM_desktop_net_shutdown,
    ULVM_desktop_net_service
};

#endif
