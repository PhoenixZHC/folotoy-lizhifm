#include "audio_position.h"
#include <limits.h>
#include <string.h>

bool audio_resource_valid(const audio_resource_t *r)
{
    if (!r || !r->total_bytes || r->total_bytes > INT64_MAX || !r->validator[0] ||
        !memchr(r->validator, 0, sizeof(r->validator))) return false;
    for (const unsigned char *p = (const unsigned char *)r->validator; *p; ++p)
        if (*p < 32 || *p == 127) return false;
    if (r->is_etag) {
        size_t n = strlen(r->validator);
        return n >= 2 && r->validator[0] == '"' && r->validator[n - 1] == '"';
    }
    return true;
}

bool audio_resource_equal(const audio_resource_t *a, const audio_resource_t *b)
{
    return audio_resource_valid(a) && audio_resource_valid(b) &&
        a->total_bytes == b->total_bytes && a->is_etag == b->is_etag &&
        !strcmp(a->validator, b->validator);
}

bool audio_checkpoint_valid(const audio_checkpoint_t *p, uint32_t target_ms)
{
    return p && audio_resource_valid(&p->resource) && p->frame.offset < p->resource.total_bytes &&
        p->frame.time_us <= (uint64_t)target_ms * 1000 &&
        (uint64_t)target_ms * 1000 - p->frame.time_us <= 65000000;
}

void audio_sample_clock_advance(audio_sample_clock_t *c, unsigned samples, unsigned rate)
{
    if (!c || !rate) return;
    if (c->rate != rate) c->remainder = 0;
    uint64_t scaled = (uint64_t)samples * 1000000 + c->remainder;
    c->us += scaled / rate;
    c->remainder = (uint32_t)(scaled % rate);
    c->rate = rate;
}

bool audio_mp3_frame_parse(const uint8_t *h, size_t n, audio_mp3_frame_t *f)
{
    if (!h || !f || n < 4 || h[0] != 0xff || (h[1] & 0xe0) != 0xe0) return false;
    unsigned version = (h[1] >> 3) & 3, layer = (h[1] >> 1) & 3;
    unsigned bitrate = h[2] >> 4, rate_index = (h[2] >> 2) & 3;
    if (version == 1 || layer != 1 || !bitrate || bitrate == 15 || rate_index == 3) return false;
    static const unsigned rates[] = {44100, 48000, 32000};
    static const unsigned mpeg1[] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
    static const unsigned mpeg2[] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
    unsigned rate = rates[rate_index] / (version == 3 ? 1 : version == 2 ? 2 : 4);
    unsigned kbps = version == 3 ? mpeg1[bitrate] : mpeg2[bitrate];
    f->rate = rate; f->samples = version == 3 ? 1152 : 576;
    f->bytes = (version == 3 ? 144000U : 72000U) * kbps / rate + ((h[2] >> 1) & 1);
    return true;
}

void audio_anchor_record(audio_anchor_history_t *h, audio_frame_anchor_t a)
{
    if (!h) return;
    for (unsigned i = 0; i < h->count; ++i) {
        uint64_t t = h->anchors[i].time_us;
        if ((t > a.time_us ? t - a.time_us : a.time_us - t) < 1000000) return;
    }
    h->anchors[h->next] = a;
    h->next = (h->next + 1) % AUDIO_POSITION_ANCHORS;
    if (h->count < AUDIO_POSITION_ANCHORS) ++h->count;
}

bool audio_anchor_find(const audio_anchor_history_t *h, uint32_t target_ms, audio_frame_anchor_t *out)
{
    if (!h || !out) return false;
    uint64_t target = (uint64_t)target_ms * 1000;
    uint64_t before = target >= 1000000 ? target - 1000000 : 0;
    bool found = false;
    for (unsigned i = 0; i < h->count; ++i) {
        audio_frame_anchor_t a = h->anchors[i];
        if (a.time_us <= before && (!found || a.time_us > out->time_us)) { *out = a; found = true; }
    }
    return found && target - out->time_us <= 65000000;
}
