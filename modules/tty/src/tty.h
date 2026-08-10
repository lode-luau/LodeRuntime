// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
//
// tty.h - Shared TTY layer for Lode native modules.
//
// C-compatible function API over libuv. The implementation lives in
// modules/tty/src (tty_stream.cpp, tty_helpers.cpp) and is compiled into the
// DLLs that use it:
//   - modules/tty (public @tty module)
//   - modules/stdio (future: extracts the tty/fallback code from stdio.cpp)
//   - modules/sys   (future: process stdio tty streams)
//
// Lifecycle:
//   tty_stream_new / tty_stream_release own one reference each. The object
//   keeps itself alive while any async work (reading) is active, so a consumer
//   may call tty_stream_close and tty_stream_release in any order; the object
//   is destroyed once the stream has fully closed and the consumer released it.
//
// Callbacks fire on the event loop thread, always with the consumer's ctx.
//   on_data:  raw bytes received; the buffer is only valid for the call.
//   on_close: the stream has fully closed (fires exactly once).
//   on_error: an error occurred (read/write error, ...).
#ifndef LODE_TTY_H
#define LODE_TTY_H

#include <stddef.h>
#include <stdint.h>

struct uv_loop_s;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tty_stream tty_stream_t;

typedef void (*tty_data_cb)(void* ctx, const char* data, size_t len);
typedef void (*tty_close_cb)(void* ctx);
typedef void (*tty_error_cb)(void* ctx, const char* error);

/* --- stream --- */

/* Creates a TTY stream from a file descriptor. readable is nonzero for a
 * readable stream (e.g. stdin), zero for a write-only stream (stdout/stderr). */
tty_stream_t* tty_stream_new(struct uv_loop_s* loop, int fd, int readable);
void tty_stream_retain(tty_stream_t* s);
void tty_stream_release(tty_stream_t* s);

/* Queues bytes for transmission. Returns 0 if queued, or a nonzero value if
 * the stream is not open / is closing / is closed. */
int tty_stream_write(tty_stream_t* s, const char* data, size_t len);

/* Starts reading. Data arrives through on_data; EOF/errors through on_close /
 * on_error. Returns 0 or a negative uv error code. */
int tty_stream_start_read(tty_stream_t* s, tty_data_cb on_data,
                          tty_close_cb on_close, tty_error_cb on_error, void* ctx);
void tty_stream_stop_read(tty_stream_t* s);

/* Terminal control. Returns 0 on success or a nonzero value on failure. */
int tty_stream_get_window_size(tty_stream_t* s, int* width, int* height);
int tty_stream_set_mode(tty_stream_t* s, int mode);

/* Fully closes the stream. Idempotent. */
void tty_stream_close(tty_stream_t* s);

int tty_stream_is_open(const tty_stream_t* s);
int tty_stream_is_closing(const tty_stream_t* s);
int tty_stream_is_closed(const tty_stream_t* s);

/* --- misc --- */

/* Returns the libuv handle type for a file descriptor (uv_guess_handle). */
int tty_guess_handle(int fd);

#ifdef __cplusplus
}
#endif
#endif