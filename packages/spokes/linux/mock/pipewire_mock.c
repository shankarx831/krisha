#include "pipewire/pipewire.h"
#include <stdio.h>
#include <stdlib.h>

void pw_init(int *argc, char ***argv) {}
void pw_deinit(void) {}

struct pw_main_loop *pw_main_loop_new(const void *properties) {
    return (struct pw_main_loop *)malloc(1);
}

struct pw_loop *pw_main_loop_get_loop(struct pw_main_loop *loop) {
    return (struct pw_loop *)loop;
}

int pw_main_loop_run(struct pw_main_loop *loop) {
    printf("[Mock PipeWire Loop] running...\n");
    return 0;
}

void pw_main_loop_destroy(struct pw_main_loop *loop) {
    free(loop);
}

struct pw_context *pw_context_new(struct pw_loop *main_loop, void *properties, size_t user_data_size) {
    return (struct pw_context *)malloc(1);
}

void pw_context_destroy(struct pw_context *context) {
    free(context);
}

struct pw_core *pw_context_connect(struct pw_context *context, void *properties, size_t user_data_size) {
    return (struct pw_core *)malloc(1);
}

int pw_core_disconnect(struct pw_core *core) {
    free(core);
    return 0;
}

struct pw_stream *pw_stream_new_simple(
    struct pw_loop *loop,
    const char *name,
    void *properties,
    const struct pw_stream_events *events,
    void *data
) {
    return (struct pw_stream *)malloc(1);
}

void pw_stream_destroy(struct pw_stream *stream) {
    free(stream);
}

int pw_stream_connect(
    struct pw_stream *stream,
    int direction,
    uint32_t target_id,
    int flags,
    const void **params,
    uint32_t n_params
) {
    return 0;
}

struct pw_buffer *pw_stream_dequeue_buffer(struct pw_stream *stream) {
    return NULL;
}

int pw_stream_queue_buffer(struct pw_stream *stream, struct pw_buffer *buffer) {
    return 0;
}
