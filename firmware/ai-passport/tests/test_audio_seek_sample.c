#include "audio_position.h"
#include "mp3dec.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 只由主机命令读取用户提供的真实MP3片段，不链接到固件入口。 */
int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb"); assert(file);
    unsigned char data[262144]; size_t length = fread(data, 1, sizeof(data), file); fclose(file);
    size_t offset = 0;
    if (length >= 10 && !memcmp(data,"ID3",3))
        offset = 10 + ((data[6]&127)<<21) + ((data[7]&127)<<14) + ((data[8]&127)<<7) + (data[9]&127);
    size_t offsets[400]; unsigned count = 0;
    audio_mp3_frame_t header;
    while (count < 400 && offset + 4 <= length && audio_mp3_frame_parse(data+offset,length-offset,&header) && offset+header.bytes <= length) {
        offsets[count++]=offset; offset+=header.bytes;
    }
    assert(count >= 25);
    HMP3Decoder decoder=MP3InitDecoder(); assert(decoder);
    short pcm[2304], reference[2304]; int samples = 0;
    const unsigned target=24, start=8;
    for (unsigned i=0;i<=target;++i) {
        unsigned char *p=data+offsets[i]; int left=(int)(length-offsets[i]);
        int err=MP3Decode(decoder,&p,&left,pcm,0); assert(err==0);
        if(i==target){MP3FrameInfo f;MP3GetLastFrameInfo(decoder,&f);samples=f.outputSamps;memcpy(reference,pcm,(size_t)samples*sizeof(short));}
    }
    MP3FreeDecoder(decoder); decoder=MP3InitDecoder(); assert(decoder);
    unsigned reservoir_underflows=0;
    for (unsigned i=start;i<=target;++i) {
        unsigned char *p=data+offsets[i]; int left=(int)(length-offsets[i]);
        assert(audio_mp3_frame_parse(p,(size_t)left,&header));
        int before=left;int err=MP3Decode(decoder,&p,&left,pcm,0);
        assert(before-left==(int)header.bytes);
        if(err==ERR_MP3_MAINDATA_UNDERFLOW){++reservoir_underflows;assert(i<target);}
        else assert(err==0);
    }
    assert(!memcmp(reference,pcm,(size_t)samples*sizeof(short)));
    MP3FreeDecoder(decoder);
    printf("Real MP3 seek: frame boundaries and warmed PCM match; %u frames, %u reservoir warmups\n",count,reservoir_underflows);
    return 0;
}
