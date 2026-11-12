#include "audio_position.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failed;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)
int main(void)
{
    audio_sample_clock_t c = {0};
    /* 一小时44.1kHz计时不能逐帧取整，旧实现每帧丢约0.122ms。 */
    for (unsigned i = 0; i < 137813; ++i) audio_sample_clock_advance(&c, 1152, 44100);
    CHECK(c.us == (uint64_t)137813 * 1152 * 1000000 / 44100);
    uint64_t before = c.us;
    audio_sample_clock_advance(&c, 480, 48000); CHECK(c.us == before + 10000);
    audio_sample_clock_advance(&c, 480, 0); CHECK(c.us == before + 10000);
    uint8_t h[] = {0xff,0xfb,0x90,0x64}; audio_mp3_frame_t f;
    CHECK(audio_mp3_frame_parse(h,4,&f) && f.rate == 44100 && f.samples == 1152 && f.bytes == 417);
    h[2] |= 2; CHECK(audio_mp3_frame_parse(h,4,&f) && f.bytes == 418);
    h[1] = 0xf3; h[2] = 0x80; CHECK(audio_mp3_frame_parse(h,4,&f) && f.rate == 22050 && f.samples == 576 && f.bytes == 208);
    h[1] = 0xfb; h[2] = 0; CHECK(!audio_mp3_frame_parse(h,4,&f));
    h[2] = 0xfc; CHECK(!audio_mp3_frame_parse(h,4,&f));
    CHECK(!audio_mp3_frame_parse(h,3,&f));
    audio_anchor_history_t history = {0}; audio_frame_anchor_t anchor;
    CHECK(!audio_anchor_find(&history, 10000, &anchor));
    for (unsigned i = 0; i < 90; ++i) audio_anchor_record(&history, (audio_frame_anchor_t){100+i*16000, (uint64_t)i*1000000});
    CHECK(history.count == 64);
    CHECK(audio_anchor_find(&history,75000,&anchor) && anchor.time_us == 74000000 && anchor.offset == 1184100);
    CHECK(!audio_anchor_find(&history,15000,&anchor));
    CHECK(!audio_anchor_find(&history,200000,&anchor));
    audio_checkpoint_t p = {.resource={.total_bytes=2000000,.validator="\"etag\"",.is_etag=true},.frame={500000,30000000}};
    CHECK(audio_checkpoint_valid(&p,32000));
    CHECK(!audio_checkpoint_valid(&p,29000));
    CHECK(!audio_checkpoint_valid(&p,100000));
    audio_resource_t other = p.resource;
    CHECK(audio_resource_equal(&p.resource,&other));
    other.total_bytes++; CHECK(!audio_resource_equal(&p.resource,&other));
    strcpy(other.validator,"\"changed\""); CHECK(!audio_resource_equal(&p.resource,&other));
    p.frame.offset=p.resource.total_bytes; CHECK(!audio_checkpoint_valid(&p,32000));
    strcpy(p.resource.validator,"W/\"weak\""); CHECK(!audio_resource_valid(&p.resource));
    strcpy(p.resource.validator,"\"inject\r\n\""); CHECK(!audio_resource_valid(&p.resource));
    memset(p.resource.validator,'x',sizeof(p.resource.validator)); CHECK(!audio_resource_valid(&p.resource));
    printf("audio position: %u passed, %u failed\n", checks-failed,failed);
    return failed ? 1 : 0;
}
