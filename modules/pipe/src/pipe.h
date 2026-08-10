// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
//
// pipe.h - Shared Pipe layer for Lode native modules.
//
// C-compatible function API over libuv. The implementation lives in
// modules/pipe/src (pipe_stream.cpp, pipe_server.cpp, pipe_helpers.cpp) and is
// compiled into the DLLs that use it:
//   - modules/pipe (public @pipe module)
//   - modules/stdio (future: extracts the pipe/fallback code from stdio.cpp)
//   - modules/sys   (future: process stdio pipes)
//
// Lifecycle:
//   pipe_stream_new / pipe_stream_release own one reference each. The object
//   keeps itself alive while any async work (connect, reading) is active, so a
//   consumer may call pipe_stream_close and pipe_stream_release in any order;
//   the object is destroyed once the stream has fully closed and the consumer
//   released it.
//
// Callbacks fire on the event loop thread, always with the consumer's ctx.
//   on_connect: named-pipe connection established (client side only).
//   on_data:    raw bytes received; the buffer is only valid for the call.
//   on_close:   the stream has fully closed (fires exactly once).
//   on_error:   an error occurred (connect failure, read/write error, ...).
// For server-side accepted streams the consumer MUST configure callbacks
// synchronously inside its on_accept handler, before any data is delivered.
#ifndef LODE_PIPE_H
#define LODE_PIPE_H

#include <stddef.h>
#include <stdint.h>

struct uv_loop_s;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pipe_stream pipe_stream_t;
typedef struct pipe_server pipe_server_t;

typedef void (*pipe_connect_cb)(void* ctx);
typedef void (*pipe_data_cb)(void* ctx, const char* data, size_t len);
typedef void (*pipe_close_cb)(void* ctx);
typedef void (*pipe_error_cb)(void* ctx, const char* error);
typedef void (*pipe_accept_cb)(void* ctx, pipe_stream_t* stream);

/* --- stream --- */

pipe_stream_t* pipe_stream_new(struct uv_loop_s* loop);
void pipe_stream_retain(pipe_stream_t* s);
void pipe_stream_release(pipe_stream_t* s);

/* Opens a stream from a file descriptor (uv_pipe_open). Used by stdio. */
int pipe_stream_open_fd(pipe_stream_t* s, int fd);

/* Connects to a named pipe. The result arrives through on_connect (success)
 * or on_error (failure). Returns 0 on success or a negative uv error code if
 * the connect could not be started. */
int pipe_stream_connect(pipe_stream_t* s, const char* path,
                        pipe_connect_cb on_connect, pipe_data_cb on_data,
                        pipe_close_cb on_close, pipe_error_cb on_error, void* ctx);

/* Replaces the callbacks on an already-created stream (e.g. an accepted one). */
void pipe_stream_set_callbacks(pipe_stream_t* s, pipe_connect_cb on_connect,
                               pipe_data_cb on_data, pipe_close_cb on_close,
                               pipe_error_cb on_error, void* ctx);

/* Queues bytes for transmission. Returns 0 if queued, or a nonzero value if
 * the stream is not open / is closing / is closed. */
int pipe_stream_write(pipe_stream_t* s, const char* data, size_t len);

/* Half-closes the send side (sends EOF to the peer). */
void pipe_stream_shutdown(pipe_stream_t* s);

/* Fully closes the stream. Idempotent. */
void pipe_stream_close(pipe_stream_t* s);

/* Reading is started automatically after a successful connect or accept. */
int pipe_stream_start_read(pipe_stream_t* s);
void pipe_stream_stop_read(pipe_stream_t* s);

int pipe_stream_is_open(const pipe_stream_t* s);
int pipe_stream_is_closing(const pipe_stream_t* s);
int pipe_stream_is_closed(const pipe_stream_t* s);

/* --- server --- */

pipe_server_t* pipe_server_new(struct uv_loop_s* loop);
void pipe_server_release(pipe_server_t* s);

/* Creates the named pipe and starts listening. Each accepted connection is
 * delivered through on_accept as a new pipe_stream_t (the consumer owns one
 * reference to it). Returns 0 or a negative uv error code. */
int pipe_server_listen(pipe_server_t* s, const char* path, int backlog,
                       pipe_accept_cb on_accept, pipe_error_cb on_error, void* ctx);
int pipe_server_is_listening(const pipe_server_t* s);
void pipe_server_close(pipe_server_t* s);

/* --- misc --- */

/* Returns the libuv handle type for a file descriptor (uv_guess_handle). */
int pipe_guess_handle(int fd);

#ifdef __cplusplus
}
#endif
#endif