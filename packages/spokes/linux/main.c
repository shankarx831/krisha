/**
 * @file main.c
 * @brief PipeWire virtual sink daemon spoke implementation wrapping the C++ DSP Core.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>
#include <spa/utils/result.h>

#include "radioform_dsp.h"

// Global Engine Context (allocated at startup, freed at teardown)
static radioform_dsp_engine_t* g_engine = NULL;
static struct pw_main_loop* g_loop = NULL;

// Lock-free atomic/volatile state variables for the real-time processing loop
static volatile bool g_bypass = false;
static const uint32_t g_channels = 2;
static const uint32_t g_sample_rate = 48000;

/**
 * Audio Stream Process Callback (Runs in the PipeWire Realtime Thread)
 * 
 * CRITICAL REAL-TIME SAFETY:
 * - NO heap allocations (malloc/free/calloc)
 * - NO locking primitives (mutexes/semaphores)
 * - NO blocking system calls or logs (printf/write/sleep)
 */
static void on_stream_process(void *data) {
    struct pw_stream *stream = (struct pw_stream *)data;
    struct pw_buffer *b = pw_stream_dequeue_buffer(stream);
    if (!b) return;

    struct spa_buffer *buf = b->buffer;
    if (buf->n_datas == 0) {
        pw_stream_queue_buffer(stream, b);
        return;
    }

    float *samples = (float *)buf->datas[0].data;
    if (!samples) {
        pw_stream_queue_buffer(stream, b);
        return;
    }

    uint32_t n_bytes = buf->datas[0].chunk->size;
    uint32_t n_samples = n_bytes / sizeof(float);
    uint32_t n_frames = n_samples / g_channels;

    if (g_engine && !g_bypass) {
        // Process audio in-place using the optimized universal DSP engine
        radioform_dsp_process_interleaved(g_engine, samples, samples, n_frames);
    }

    pw_stream_queue_buffer(stream, b);
}

// Signal handler for clean termination
static void handle_signal(int sig) {
    if (g_loop) {
        // Request loop to quit safely
        // In mock or production, this signals main thread to terminate
        printf("\nShutting down Radioform PipeWire virtual sink...\n");
        // For simulation/mock purposes, we call exit or stop loop if pw_main_loop_quit was defined
        exit(0);
    }
}

int main(int argc, char **argv) {
    printf("==================================================\n");
    printf("Radioform Linux Spoke: PipeWire Daemon Starting\n");
    printf("==================================================\n");

    // Handle signals
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Initialize the frozen universal DSP engine context
    g_engine = radioform_dsp_create(g_sample_rate);
    if (!g_engine) {
        fprintf(stderr, "Fatal: Failed to create Radioform DSP context.\n");
        return EXIT_FAILURE;
    }

    // Apply a standard flat preset
    radioform_preset_t preset;
    radioform_dsp_preset_init_flat(&preset);
    radioform_dsp_apply_preset(g_engine, &preset);
    radioform_dsp_set_bypass(g_engine, false);

    // Initialize PipeWire client
    pw_init(&argc, &argv);

    g_loop = pw_main_loop_new(NULL);
    if (!g_loop) {
        fprintf(stderr, "Fatal: Failed to create PipeWire main loop.\n");
        radioform_dsp_destroy(g_engine);
        return EXIT_FAILURE;
    }

    struct pw_context *context = pw_context_new(pw_main_loop_get_loop(g_loop), NULL, 0);
    if (!context) {
        fprintf(stderr, "Fatal: Failed to create PipeWire context.\n");
        pw_main_loop_destroy(g_loop);
        radioform_dsp_destroy(g_engine);
        return EXIT_FAILURE;
    }

    struct pw_core *core = pw_context_connect(context, NULL, 0);
    if (!core) {
        fprintf(stderr, "Fatal: Failed to connect to PipeWire core.\n");
        pw_context_destroy(context);
        pw_main_loop_destroy(g_loop);
        radioform_dsp_destroy(g_engine);
        return EXIT_FAILURE;
    }

    // Configure stream callbacks
    struct pw_stream_events events = {
        .version = PW_VERSION_STREAM_EVENTS,
        .process = on_stream_process
    };

    // Create stream
    struct pw_stream *stream = pw_stream_new_simple(
        pw_main_loop_get_loop(g_loop),
        "Radioform Virtual Sink",
        NULL,
        &events,
        NULL
    );

    if (!stream) {
        fprintf(stderr, "Fatal: Failed to create PipeWire virtual stream.\n");
        pw_core_disconnect(core);
        pw_context_destroy(context);
        pw_main_loop_destroy(g_loop);
        radioform_dsp_destroy(g_engine);
        return EXIT_FAILURE;
    }

    printf("Daemon running and connected to PipeWire. Signal received is required to stop.\n");

    // Run the main loop
    pw_main_loop_run(g_loop);

    // Clean up resources upon termination
    pw_stream_destroy(stream);
    pw_core_disconnect(core);
    pw_context_destroy(context);
    pw_main_loop_destroy(g_loop);
    radioform_dsp_destroy(g_engine);
    pw_deinit();

    printf("Teardown complete. Exiting.\n");
    return EXIT_SUCCESS;
}
