// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
//
// tcp.h - Shared TCP layer for Lode native modules.
//
// C-compatible function API over libuv. The implementation lives in
// modules/tcp/src (tcp_client.cpp, tcp_server.cpp, tcp_resolve.cpp,
// tcp_addr.cpp) and is compiled into the DLLs that use it:
//   - modules/tcp (public @tcp module)
//   - modules/socket (compiles the tcp sources into socket.dll)
//   - modules/websocket (compiles the tcp sources into websocket.dll)
//
// Lifecycle:
//   tcp_client_new / tcp_client_release own one reference each. The object
//   keeps itself alive while any async work (connect, reading) is active, so a
//   consumer may call tcp_client_close and tcp_client_release in any order;
//   the object is destroyed once the connection has fully closed and the
//   consumer released it.
//
// Callbacks fire on the event loop thread, always with the consumer's ctx.
//   on_connect: TCP connection established (client side only).
//   on_data:    raw bytes received; the buffer is only valid for the call.
//   on_close:   the connection has fully closed (fires exactly once).
//   on_error:   an error occurred (connect failure, read/write error, ...).
// For server-side accepted clients the consumer MUST configure callbacks
// synchronously inside its on_accept handler, before any data is delivered.
#ifndef LODE_TCP_H
#define LODE_TCP_H

#include <stddef.h>
#include <stdint.h>

struct uv_loop_s;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tcp_client tcp_client_t;
typedef struct tcp_server tcp_server_t;

typedef void (*tcp_connect_cb)(void* ctx);
typedef void (*tcp_data_cb)(void* ctx, const char* data, size_t len);
typedef void (*tcp_close_cb)(void* ctx);
typedef void (*tcp_error_cb)(void* ctx, const char* error);
typedef void (*tcp_accept_cb)(void* ctx, tcp_client_t* client);
typedef void (*tcp_resolve_cb)(void* ctx, const char* address, const char* error);

/* --- client --- */

tcp_client_t* tcp_client_new(struct uv_loop_s* loop);
void tcp_client_retain(tcp_client_t* c);
void tcp_client_release(tcp_client_t* c);

/* Connects to host:port. Resolves the hostname, applies an optional timeout
 * (0 disables it) and returns immediately; the result arrives through
 * on_connect (success) or on_error (failure). Returns 0 on success or a
 * negative uv error code if the connect could not be started. */
int tcp_client_connect(tcp_client_t* c, const char* host, int port, uint64_t timeout_ms,
                       tcp_connect_cb on_connect, tcp_data_cb on_data,
                       tcp_close_cb on_close, tcp_error_cb on_error, void* ctx);

/* Replaces the callbacks on an already-created client (e.g. an accepted one). */
void tcp_client_set_callbacks(tcp_client_t* c, tcp_connect_cb on_connect, tcp_data_cb on_data,
                              tcp_close_cb on_close, tcp_error_cb on_error, void* ctx);

/* Queues bytes for transmission. Returns 0 if queued, or a nonzero value if
 * the client is not connected / is closing / is closed. */
int tcp_client_write(tcp_client_t* c, const char* data, size_t len);

/* Half-closes the send side (sends EOF to the peer). */
void tcp_client_shutdown(tcp_client_t* c);

/* Fully closes the connection. Idempotent. */
void tcp_client_close(tcp_client_t* c);

/* Reading is started automatically after a successful connect or accept. */
int tcp_client_start_read(tcp_client_t* c);
void tcp_client_stop_read(tcp_client_t* c);

int tcp_client_is_connected(const tcp_client_t* c);
int tcp_client_is_closing(const tcp_client_t* c);
int tcp_client_is_closed(const tcp_client_t* c);
void tcp_client_set_nodelay(tcp_client_t* c, int enabled);

/* Fills host with the IP text and *port with the numeric port. Returns 0 on
 * success or a nonzero value if the address is unavailable. */
int tcp_client_local_address(const tcp_client_t* c, char* host, size_t cap, int* port);
int tcp_client_remote_address(const tcp_client_t* c, char* host, size_t cap, int* port);

/* --- server --- */

tcp_server_t* tcp_server_new(struct uv_loop_s* loop);
void tcp_server_release(tcp_server_t* s);

/* Binds and starts listening. host may be NULL/empty for any-address. Each
 * accepted connection is delivered through on_accept as a new tcp_client_t
 * (the consumer owns one reference to it). Returns 0 or a negative uv error
 * code. */
int tcp_server_listen(tcp_server_t* s, const char* host, int port, int backlog,
                      tcp_accept_cb on_accept, tcp_error_cb on_error, void* ctx);
int tcp_server_is_listening(const tcp_server_t* s);
void tcp_server_close(tcp_server_t* s);
int tcp_server_local_address(const tcp_server_t* s, char* host, size_t cap, int* port);

/* --- resolve --- */

/* Resolves a hostname to an IP text. cb receives (address, NULL) on success
 * or (NULL, error) on failure. Returns 0 or a negative uv error code. */
int tcp_resolve(struct uv_loop_s* loop, const char* host, tcp_resolve_cb cb, void* ctx);

/* --- misc --- */

int tcp_valid_port(double value, int allow_zero);

#ifdef __cplusplus
}
#endif
#endif
