/* MP3 background music (System 10 MEMP3 board): decodes a track held in the board's shared RAM with minimp3 (CC0) and mixes it at 44100 Hz. */
#include <stdlib.h>
#include <string.h>
#include "mp3.h"
#define MINIMP3_IMPLEMENTATION
#include "../../third_party/minimp3/minimp3.h"

static mp3dec_t dec;
static const uint8_t *data; static size_t size, pos; static int playing, looping;
static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static int16_t buf[4096 * 2]; static int buf_n, buf_pos;      /* decoded stereo frames waiting to be mixed */
static int vol_l = 256, vol_r = 256;
static int src_rate = 44100; static uint32_t rs_pos;           /* resampler phase (16.16) */
static int16_t last_l, last_r;

void mp3_play(const uint8_t *d, size_t n, int loop)
{
    mp3dec_init(&dec); data = d; size = n; pos = 0; playing = n > 0; looping = loop; buf_n = buf_pos = 0; rs_pos = 0; last_l = last_r = 0;
    /* skip an ID3v2 tag */
    if (n > 10 && !memcmp(d, "ID3", 3)) { size_t t = ((size_t)(d[6] & 0x7f) << 21) | ((size_t)(d[7] & 0x7f) << 14) | ((size_t)(d[8] & 0x7f) << 7) | (d[9] & 0x7f); pos = t + 10; }
}
void mp3_stop(void) { playing = 0; buf_n = buf_pos = 0; }
void mp3_set_volume(int l, int r) { vol_l = l; vol_r = r; }
int  mp3_is_playing(void) { return playing; }

static int refill(void)
{
    for (int tries = 0; tries < 8; tries++) {
        if (!playing) return 0;
        if (pos >= size) { if (looping) { pos = 0; mp3dec_init(&dec); continue; } playing = 0; return 0; }
        mp3dec_frame_info_t info; int samples = mp3dec_decode_frame(&dec, data + pos, (int)(size - pos), pcm, &info);
        if (info.frame_bytes <= 0) { pos = size; continue; }
        pos += (size_t)info.frame_bytes;
        if (samples <= 0) continue;
        src_rate = info.hz ? info.hz : 44100;
        buf_n = samples; buf_pos = 0;
        for (int i = 0; i < samples; i++) { if (info.channels == 2) { buf[i * 2] = pcm[i * 2]; buf[i * 2 + 1] = pcm[i * 2 + 1]; } else buf[i * 2] = buf[i * 2 + 1] = pcm[i]; }
        return 1;
    }
    return 0;
}

void mp3_mix(int16_t *out, int n)
{
    if (!playing && buf_pos >= buf_n) return;
    uint32_t step = (uint32_t)((uint64_t)src_rate * 65536u / 44100u);
    for (int k = 0; k < n; k++) {
        if (buf_pos >= buf_n && !refill()) return;
        int16_t l = buf[buf_pos * 2], r = buf[buf_pos * 2 + 1];
        int32_t sl = out[k * 2] + ((l * vol_l) >> 8), sr = out[k * 2 + 1] + ((r * vol_r) >> 8);
        if (sl > 32767) sl = 32767; if (sl < -32768) sl = -32768; if (sr > 32767) sr = 32767; if (sr < -32768) sr = -32768;
        out[k * 2] = (int16_t)sl; out[k * 2 + 1] = (int16_t)sr;
        rs_pos += step; while (rs_pos >= 65536u) { rs_pos -= 65536u; buf_pos++; }
    }
}
