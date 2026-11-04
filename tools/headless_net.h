#ifndef UNILANG_HEADLESS_NET_H
#define UNILANG_HEADLESS_NET_H

/* Deterministic in-memory networking host used by the headless VM and tests.
   It never opens OS sockets or accesses the network. */
#include <stdlib.h>
#include <string.h>
#include "../C-interpreter/unilang.h"

typedef struct HeadlessNetState
{
    int listener;
    int pending_client;
    uint8_t pending_external;
    int peer_index;
    uint32_t last_activity;
    uint8_t http_done;
} HeadlessNetState;

typedef struct HeadlessNetContext
{
    ULVMNet *owner;
    uint32_t now_ms;
    uint32_t last_announce_ms;
    uint8_t broadcast_available;
    uint8_t session_emitted;
    char last_http_headers[ULVM_NET_MAX_HTTP_HEADERS];
    char last_http_path[256];
    uint8_t last_http_method;
    uint8_t last_http_body[ULVM_NET_MAX_BUFFER];
    uint32_t last_http_body_len;
} HeadlessNetContext;

static inline HeadlessNetState *headless_net_state(ULVMNetHandle *h)
{ return (HeadlessNetState *)h->host_data; }

static inline int headless_net_open(void *opaque, ULVMNetHandle *h, uint8_t kind,
                                    uint16_t local_port, const char *host,
                                    uint16_t remote_port, uint32_t flags)
{
    (void)host; (void)flags;
    HeadlessNetContext *ctx = (HeadlessNetContext *)opaque;
    HeadlessNetState *s = (HeadlessNetState *)calloc(1, sizeof(*s));
    if (!s) { h->last_error = ULVM_NET_ERR_LIMIT; return 0; }
    s->peer_index = -1; s->pending_client = -1; s->last_activity = ctx->now_ms;
    h->host_data = s; h->local_port = local_port; h->peer_port = remote_port;
    if (kind == ULVM_NET_KIND_TCP && (flags & ULVM_NET_OPEN_LISTEN))
    {
        s->listener = 1; h->status = ULVM_NET_STATUS_LISTENING;
    }
    else if (kind == ULVM_NET_KIND_TCP)
    {
        int index = (int)(h - ctx->owner->handles), found = -1;
        for (int i = 0; i < (int)ULVM_NET_MAX_HANDLES; ++i)
        {
            ULVMNetHandle *candidate = &ctx->owner->handles[i];
            HeadlessNetState *ls = candidate->active ? headless_net_state(candidate) : NULL;
            if (i != index && candidate->active && candidate->kind == ULVM_NET_KIND_TCP &&
                ls && ls->listener && candidate->local_port == remote_port && ls->pending_client < 0)
            { found = i; break; }
        }
        if (found < 0) { free(s); h->host_data = NULL; h->last_error = ULVM_NET_ERR_IO; return 0; }
        s->peer_index = found;
        headless_net_state(&ctx->owner->handles[found])->pending_client = index;
        h->peer_port = remote_port; h->status = ULVM_NET_STATUS_CONNECTED;
    }
    else if (kind == ULVM_NET_KIND_HTTP) h->status = ULVM_NET_STATUS_CONNECTED;
    else h->status = ULVM_NET_STATUS_READY;
    h->last_error = ULVM_NET_ERR_NONE;
    return 1;
}

static inline void headless_net_poll(void *opaque, ULVMNetHandle *h)
{
    HeadlessNetContext *ctx = (HeadlessNetContext *)opaque;
    HeadlessNetState *s = headless_net_state(h);
    if (!s) return;
    if (h->timeout_ms && (uint32_t)(ctx->now_ms - s->last_activity) >= h->timeout_ms)
    {
        h->last_error = ULVM_NET_ERR_TIMEOUT; h->status = ULVM_NET_STATUS_CLOSED;
        ctx->owner->last_error = ULVM_NET_ERR_TIMEOUT;
    }
}

static inline int headless_net_send(void *opaque, ULVMNetHandle *h,
                                    const void *data, uint32_t len)
{
    HeadlessNetContext *ctx = (HeadlessNetContext *)opaque;
    HeadlessNetState *s = headless_net_state(h);
    if (!s || h->status == ULVM_NET_STATUS_CLOSED) { h->last_error = ULVM_NET_ERR_IO; return -1; }
    if (h->kind == ULVM_NET_KIND_UDP)
    {
        for (int i = 0; i < (int)ULVM_NET_MAX_HANDLES; ++i)
        {
            ULVMNetHandle *r = &ctx->owner->handles[i];
            if (r != h && r->active && r->kind == ULVM_NET_KIND_UDP &&
                r->local_port == h->remote_port && r->rx_len + len <= ULVM_NET_MAX_BUFFER)
            { memcpy(r->rx + r->rx_len, data, len); r->rx_len += len; r->status = ULVM_NET_STATUS_READY; }
        }
        return (int)len;
    }
    if (s->peer_index < 0 || s->peer_index >= (int)ULVM_NET_MAX_HANDLES)
    { h->last_error = ULVM_NET_ERR_IO; return -1; }
    ULVMNetHandle *peer = &ctx->owner->handles[s->peer_index];
    uint32_t frame_bytes = h->frame_size ? len + 2u : len;
    if (!peer->active || peer->rx_len + frame_bytes > ULVM_NET_MAX_BUFFER)
    { h->last_error = ULVM_NET_ERR_NO_BUFFER; return -1; }
    if (h->frame_size)
    {
        if (len > h->frame_size || len > 65535u) { h->last_error = ULVM_NET_ERR_NO_BUFFER; return -1; }
        peer->rx[peer->rx_len++] = (uint8_t)(len >> 8);
        peer->rx[peer->rx_len++] = (uint8_t)len;
        memcpy(peer->rx + peer->rx_len, data, len); peer->rx_len += len;
        peer->status = ULVM_NET_STATUS_READY; s->last_activity = ctx->now_ms;
        return (int)len;
    }
    /* A fixed short write makes partial transfer handling reproducible. */
    uint32_t count = len > 3u ? 3u : len;
    memcpy(peer->rx + peer->rx_len, data, count); peer->rx_len += count;
    peer->status = ULVM_NET_STATUS_READY; s->last_activity = ctx->now_ms;
    return (int)count;
}

static inline int headless_net_recv(void *opaque, ULVMNetHandle *h, void *dest, uint32_t cap)
{
    (void)opaque;
    uint32_t n = h->rx_len < cap ? h->rx_len : cap;
    if (!n) return 0;
    memcpy(dest, h->rx, n); memmove(h->rx, h->rx + n, h->rx_len - n);
    h->rx_len -= n; return (int)n;
}

static inline int headless_net_accept(void *opaque, ULVMNetHandle *listener, ULVMNetHandle *peer)
{
    HeadlessNetContext *ctx = (HeadlessNetContext *)opaque;
    HeadlessNetState *ls = headless_net_state(listener);
    if (!ls || !ls->listener) { listener->last_error = ULVM_NET_ERR_BAD_KIND; return -1; }
    if (ls->pending_client < 0 && !ls->pending_external) return 0;
    int child_index = (int)(peer - ctx->owner->handles);
    if (ls->pending_client < 0)
    {
        HeadlessNetState *ps = (HeadlessNetState *)calloc(1, sizeof(*ps));
        if (!ps) { listener->last_error = ULVM_NET_ERR_LIMIT; return -1; }
        ps->peer_index = -1; ps->pending_client = -1; ps->last_activity = ctx->now_ms;
        peer->host_data = ps; peer->kind = ULVM_NET_KIND_TCP;
        peer->status = ULVM_NET_STATUS_CONNECTED; peer->local_port = listener->local_port;
        peer->peer_port = (uint16_t)(40000 + child_index);
        snprintf((char *)peer->peer_host, sizeof(peer->peer_host), "127.0.0.1");
        --ls->pending_external; return 1;
    }
    ULVMNetHandle *client = &ctx->owner->handles[ls->pending_client];
    HeadlessNetState *cs = headless_net_state(client);
    HeadlessNetState *ps = (HeadlessNetState *)calloc(1, sizeof(*ps));
    if (!ps) { listener->last_error = ULVM_NET_ERR_LIMIT; return -1; }
    ps->peer_index = ls->pending_client; ps->pending_client = -1; ps->last_activity = ctx->now_ms;
    peer->host_data = ps; peer->kind = ULVM_NET_KIND_TCP; peer->status = ULVM_NET_STATUS_CONNECTED;
    peer->local_port = listener->local_port; peer->peer_port = (uint16_t)(40000 + ls->pending_client);
    snprintf((char *)peer->peer_host, sizeof(peer->peer_host), "127.0.0.1");
    cs->peer_index = child_index; client->peer_port = listener->local_port;
    ls->pending_client = -1; return 1;
}

static inline int headless_net_option(void *opaque, ULVMNetHandle *h, uint8_t option, uint32_t value)
{
    HeadlessNetContext *ctx = (HeadlessNetContext *)opaque;
    HeadlessNetState *s = headless_net_state(h);
    if (option == ULVM_NET_OPT_TIMEOUT_MS && s && !s->listener)
    { h->timeout_ms = value; s->last_activity = ctx->now_ms; return 1; }
    if (option == ULVM_NET_OPT_FRAME_SIZE && h->kind == ULVM_NET_KIND_TCP && value <= ULVM_NET_MAX_BUFFER)
    { h->frame_size = (uint16_t)value; return 1; }
    if (option == ULVM_NET_OPT_UDP_BROADCAST && h->kind == ULVM_NET_KIND_UDP) return 1;
    if (option == ULVM_NET_OPT_TCP_NODELAY && h->kind == ULVM_NET_KIND_TCP) return 1;
    h->last_error = ULVM_NET_ERR_UNSUPPORTED; return 0;
}

static inline void headless_net_close(void *opaque, ULVMNetHandle *h)
{
    HeadlessNetContext *ctx = (HeadlessNetContext *)opaque;
    HeadlessNetState *s = headless_net_state(h);
    if (s && s->peer_index >= 0 && s->peer_index < (int)ULVM_NET_MAX_HANDLES)
    {
        ULVMNetHandle *peer = &ctx->owner->handles[s->peer_index];
        if (peer->active && peer != h) peer->status = ULVM_NET_STATUS_CLOSED;
    }
    free(s); h->host_data = NULL;
}

static inline int headless_net_http(void *opaque, ULVMNetHandle *h, uint8_t method,
                                    const char *path, const char *headers,
                                    const void *body, uint32_t body_len)
{
    HeadlessNetContext *ctx = (HeadlessNetContext *)opaque;
    if (h->kind != ULVM_NET_KIND_HTTP || !path || !headers || body_len > ULVM_NET_MAX_BUFFER)
    { h->last_error = ULVM_NET_ERR_BAD_KIND; return 0; }
    snprintf(ctx->last_http_path, sizeof(ctx->last_http_path), "%s", path);
    snprintf(ctx->last_http_headers, sizeof(ctx->last_http_headers), "%s", headers);
    ctx->last_http_method = method; ctx->last_http_body_len = body_len;
    if (body_len) memcpy(ctx->last_http_body, body, body_len);
    const char *response = !strcmp(path, "/created") ? "created" :
                           !strcmp(path, "/missing") ? "missing" : "ok";
    h->http_status = !strcmp(path, "/created") ? 201 :
                     !strcmp(path, "/missing") ? 404 : 200;
    h->rx_len = (uint32_t)strlen(response); memcpy(h->rx, response, h->rx_len);
    h->status = ULVM_NET_STATUS_READY;
    return 1;
}

static inline int headless_net_peer(void *opaque, ULVMNetHandle *h, char *host, size_t size, uint16_t *port)
{
    (void)opaque;
    if (host && size) snprintf(host, size, "%s", h->peer_host[0] ? (char *)h->peer_host : "127.0.0.1");
    if (port) *port = h->peer_port;
    return 1;
}

static inline int headless_net_localip(void *opaque, char *host, size_t size)
{ (void)opaque; if (size < 10) return 0; snprintf(host, size, "127.0.0.1"); return 1; }

static inline int headless_net_discovery(void *opaque, ULVMNet *net, uint8_t flags,
                                         uint16_t port, uint32_t game, const char *name)
{
    HeadlessNetContext *ctx = (HeadlessNetContext *)opaque;
    if (!ctx->broadcast_available && (flags & ULVM_NET_DISCOVERY_ANNOUNCE)) return 2;
    if (flags & ULVM_NET_DISCOVERY_LISTEN)
    {
        ULVMNetSession *s = &net->sessions[0];
        if (!s->active && !ctx->session_emitted)
        {
            memset(s, 0, sizeof(*s)); s->active = 1; s->port = 7777; s->game_id = game;
            s->last_seen_ms = ctx->now_ms; snprintf(s->host, sizeof(s->host), "127.0.0.1");
            snprintf(s->name, sizeof(s->name), "loopback-session");
            ctx->session_emitted = 1;
        }
        else if (s->game_id == game) s->last_seen_ms = ctx->now_ms;
    }
    else if (!flags) { memset(net->sessions, 0, sizeof(net->sessions)); ctx->session_emitted = 0; }
    if (flags & ULVM_NET_DISCOVERY_ANNOUNCE) ctx->last_announce_ms = ctx->now_ms;
    (void)port; (void)name; return 1;
}

static inline void headless_net_queue_clients(ULVMNet *net, int listener, uint8_t count)
{
    const ULVMNetHandle *h = ulvm_net_get_handle(net, listener);
    if (!h || !h->active || h->kind != ULVM_NET_KIND_TCP) return;
    HeadlessNetState *s = headless_net_state((ULVMNetHandle *)h);
    if (s && s->listener) s->pending_external = count;
}

static inline void headless_net_shutdown(void *opaque, ULVMNet *net)
{ (void)opaque; (void)net; }
static inline void headless_net_service(void *opaque, ULVMNet *net)
{
    HeadlessNetContext *ctx = (HeadlessNetContext *)opaque;
    for (uint32_t i = 0; i < ULVM_NET_MAX_SESSIONS; ++i)
        if (net->sessions[i].active &&
            (uint32_t)(ctx->now_ms - net->sessions[i].last_seen_ms) > ULVM_NET_DISCOVERY_TTL_MS)
            memset(&net->sessions[i], 0, sizeof(net->sessions[i]));
}

static const ULVMNetOps HEADLESS_NET_OPS = {
    headless_net_open, headless_net_poll, headless_net_send, headless_net_recv,
    headless_net_accept, headless_net_option, headless_net_http, headless_net_close,
    headless_net_peer, headless_net_localip, headless_net_discovery,
    headless_net_shutdown, headless_net_service
};

#endif
