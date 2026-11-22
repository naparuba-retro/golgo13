/* MP3 music playback (see mp3.c) */
#ifndef GOLGO_MP3_H
#define GOLGO_MP3_H
#include <stdint.h>
#include <stddef.h>
void mp3_play(const uint8_t *data, size_t size, int loop);   /* data must stay valid while playing */
void mp3_stop(void);
void mp3_set_volume(int left, int right);                    /* 0..256 */
int  mp3_is_playing(void);
void mp3_mix(int16_t *out, int n);                           /* adds n stereo samples (44100 Hz) to out */
#endif
