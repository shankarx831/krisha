// Copyright (C) Radioform / Original Authors
// Modified by Shankar (2026) for the KRISHA Architecture. Renamed namespaces and variables.
// Licensed under the GNU GPLv3.

#ifndef MOCK_PIPEWIRE_H
#define MOCK_PIPEWIRE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque structures
struct pw_main_loop;
struct pw_context;
struct pw_core;
struct pw_stream;
struct pw_loop;

struct spa_chunk {
    uint32_t offset;
    uint32_t size;
    int32_t stride;
    int32_t flags;
};

struct spa_data {
    uint32_t type;
    uint32_t flags;
    int fd;
    uint32_t mapoffset;
    uint32_t maxsize;
    void *data;
    struct spa_chunk *chunk;
};

struct spa_buffer {
    uint32_t n_datas;
    struct spa_data *datas;
    void *metas;
};

struct pw_buffer {
    struct spa_buffer *buffer;
    void *user_data;
    uint64_t size;
};

// Stream events structure
struct pw_stream_events {
#define PW_VERSION_STREAM_EVENTS 1
    uint32_t version;
    void (*destroy) (void *data);
    void (*state_changed) (void *data, int old, int state, const char *error);
    void (*control_info) (void *data, uint32_t id, void *info);
    void (*io_changed) (void *data, uint32_t id, void *area, uint32_t size);
    void (*param_changed) (void *data, uint32_t id, const void *param);
    void (*process) (void *data); // Audio callback
    void (*drained) (void *data);
};

// Functions
void pw_init(int *argc, char ***argv);
void pw_deinit(void);

struct pw_main_loop *pw_main_loop_new(const void *properties);
struct pw_loop *pw_main_loop_get_loop(struct pw_main_loop *loop);
int pw_main_loop_run(struct pw_main_loop *loop);
void pw_main_loop_destroy(struct pw_main_loop *loop);

struct pw_context *pw_context_new(struct pw_loop *main_loop, void *properties, size_t user_data_size);
void pw_context_destroy(struct pw_context *context);

struct pw_core *pw_context_connect(struct pw_context *context, void *properties, size_t user_data_size);
int pw_core_disconnect(struct pw_core *core);

struct pw_stream *pw_stream_new_simple(
    struct pw_loop *loop,
    const char *name,
    void *properties,
    const struct pw_stream_events *events,
    void *data
);
void pw_stream_destroy(struct pw_stream *stream);

int pw_stream_connect(
    struct pw_stream *stream,
    int direction,
    uint32_t target_id,
    int flags,
    const void **params,
    uint32_t n_params
);

struct pw_buffer *pw_stream_dequeue_buffer(struct pw_stream *stream);
int pw_stream_queue_buffer(struct pw_stream *stream, struct pw_buffer *buffer);

// Enums
enum pw_direction {
    PW_DIRECTION_INPUT = 0,
    PW_DIRECTION_OUTPUT = 1
};

enum pw_stream_state {
    PW_STREAM_STATE_ERROR = -1,
    PW_STREAM_STATE_UNCONNECTED = 0,
    PW_STREAM_STATE_CONNECTING = 1,
    PW_STREAM_STATE_PAUSED = 2,
    PW_STREAM_STATE_STREAMING = 3
};

#ifdef __cplusplus
}
#endif

#endif // MOCK_PIPEWIRE_H
