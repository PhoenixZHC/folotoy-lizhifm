#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_POSITION_ANCHORS 64
#define AUDIO_POSITION_VALIDATOR_BYTES 128

typedef struct {
    uint64_t total_bytes;
    char validator[AUDIO_POSITION_VALIDATOR_BYTES];
    bool is_etag;
} audio_resource_t;

typedef struct {
    uint64_t offset;
    uint64_t time_us;
} audio_frame_anchor_t;

typedef struct {
    audio_resource_t resource;
    audio_frame_anchor_t frame;
} audio_checkpoint_t;

typedef struct {
    audio_frame_anchor_t anchors[AUDIO_POSITION_ANCHORS];
    unsigned count;
    unsigned next;
} audio_anchor_history_t;

typedef struct { uint64_t us; uint32_t remainder, rate; } audio_sample_clock_t;
typedef struct { unsigned rate, samples, bytes; } audio_mp3_frame_t;

bool audio_resource_valid(const audio_resource_t *resource);
bool audio_resource_equal(const audio_resource_t *a, const audio_resource_t *b);
bool audio_checkpoint_valid(const audio_checkpoint_t *checkpoint, uint32_t target_ms);
void audio_sample_clock_advance(audio_sample_clock_t *clock, unsigned samples, unsigned rate);
bool audio_mp3_frame_parse(const uint8_t *header, size_t length, audio_mp3_frame_t *frame);
void audio_anchor_record(audio_anchor_history_t *history, audio_frame_anchor_t frame);
bool audio_anchor_find(const audio_anchor_history_t *history, uint32_t target_ms,
                       audio_frame_anchor_t *frame);
