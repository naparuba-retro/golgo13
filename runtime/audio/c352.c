#include <string.h>
#include "c352.h"

enum { FLG_BUSY = 0x8000, FLG_KEYON = 0x4000, FLG_KEYOFF = 0x2000, FLG_LOOPTRG = 0x1000, FLG_LOOPHIST = 0x0800, FLG_FM = 0x0400,
       FLG_PHASERL = 0x0200, FLG_PHASEFL = 0x0100, FLG_PHASEFR = 0x0080, FLG_LDIR = 0x0040, FLG_LINK = 0x0020, FLG_NOISE = 0x0010,
       FLG_MULAW = 0x0008, FLG_FILTER = 0x0004, FLG_REVLOOP = 0x0003, FLG_LOOP = 0x0002, FLG_REVERSE = 0x0001 };

typedef struct {
    uint32_t pos, counter; int16_t sample, last_sample;
    uint16_t vol_f, vol_r; uint8_t curr_vol[4];
    uint16_t freq, flags, wave_bank, wave_start, wave_end, wave_loop;
} voice_t;

static voice_t v_[32]; static int16_t mulaw[256]; static uint16_t rnd, control;
static const uint8_t *rom_; static size_t rom_size_;
#define RING 262144u                               /* stereo frames (~3 s) */
static int16_t ring[RING * 2]; static size_t rd, wr;
static uint64_t produced;                           /* samples generated so far */

static inline int8_t rom_byte(uint32_t a) { a &= 0xffffff; return a < rom_size_ ? (int8_t)rom_[a] : 0; }

void c352_init(const uint8_t *rom, size_t rom_size)
{
    rom_ = rom; rom_size_ = rom_size;
    int j = 0;
    for (int i = 0; i < 128; i++) { mulaw[i] = (int16_t)(j << 5); if (i < 16) j += 1; else if (i < 24) j += 2; else if (i < 48) j += 4; else if (i < 100) j += 8; else j += 16; }
    for (int i = 0; i < 128; i++) mulaw[i + 128] = (int16_t)((~mulaw[i]) & 0xffe0);
    c352_reset();
}
void c352_reset(void) { memset(v_, 0, sizeof v_); rnd = 0x1234; control = 0; rd = wr = 0; produced = 0; }

static void fetch_sample(voice_t *v)
{
    v->last_sample = v->sample;
    if (v->flags & FLG_NOISE) {
        rnd = (uint16_t)((rnd >> 1) ^ ((-(rnd & 1)) & 0xfff6));
        v->sample = (int16_t)rnd;
    } else {
        int8_t s = rom_byte(v->pos);
        v->sample = (v->flags & FLG_MULAW) ? mulaw[(uint8_t)s] : (int16_t)(s << 8);
        uint16_t pos = (uint16_t)(v->pos & 0xffff);
        if ((v->flags & FLG_LOOP) && (v->flags & FLG_REVERSE)) {
            if ((v->flags & FLG_LDIR) && pos == v->wave_loop) v->flags &= ~FLG_LDIR;                 /* backwards -> forwards */
            else if (!(v->flags & FLG_LDIR) && pos == v->wave_end) v->flags |= FLG_LDIR;             /* forwards -> backwards */
            v->pos += (v->flags & FLG_LDIR) ? -1 : 1;
        } else if (pos == v->wave_end) {
            if ((v->flags & FLG_LINK) && (v->flags & FLG_LOOP)) { v->pos = ((uint32_t)v->wave_start << 16) | v->wave_loop; v->flags |= FLG_LOOPHIST; }
            else if (v->flags & FLG_LOOP) { v->pos = (v->pos & 0xff0000) | v->wave_loop; v->flags |= FLG_LOOPHIST; }
            else { v->flags |= FLG_KEYOFF; v->flags &= ~FLG_BUSY; v->sample = 0; }
        } else v->pos += (v->flags & FLG_REVERSE) ? -1 : 1;
    }
}
static inline void ramp(voice_t *v, int ch, uint8_t val)
{
    int16_t d = (int16_t)(v->curr_vol[ch] - val);
    if (d != 0) v->curr_vol[ch] = (uint8_t)(v->curr_vol[ch] + (d > 0 ? -1 : 1));
}

static void gen_one(void)
{
    int out[4] = { 0, 0, 0, 0 };
    for (int j = 0; j < 32; j++) {
        voice_t *v = &v_[j]; int16_t s = 0;
        if (v->flags & FLG_BUSY) {
            int32_t next = (int32_t)(v->counter + v->freq);
            if (next & 0x10000) fetch_sample(v);
            if (((uint32_t)next ^ v->counter) & 0x18000) {
                ramp(v, 0, (uint8_t)(v->vol_f >> 8)); ramp(v, 1, (uint8_t)(v->vol_f & 0xff));
                ramp(v, 2, (uint8_t)(v->vol_r >> 8)); ramp(v, 3, (uint8_t)(v->vol_r & 0xff));
            }
            v->counter = (uint32_t)next & 0xffff;
            s = v->sample;
            if ((v->flags & FLG_FILTER) == 0) s = (int16_t)(v->last_sample + (int)(((int64_t)v->counter * (v->sample - v->last_sample)) >> 16));
        }
        out[0] += (((v->flags & FLG_PHASEFL) ? -s : s) * v->curr_vol[0]) >> 8;
        out[2] += (((v->flags & FLG_PHASERL) ? -s : s) * v->curr_vol[2]) >> 8;
        out[1] += (((v->flags & FLG_PHASEFR) ? -s : s) * v->curr_vol[1]) >> 8;
        out[3] += (((v->flags & FLG_PHASEFR) ? -s : s) * v->curr_vol[3]) >> 8;
    }
    size_t w = wr & (RING - 1);
    ring[w * 2] = (int16_t)(out[0] >> 3); ring[w * 2 + 1] = (int16_t)(out[1] >> 3);
    wr++; if (wr - rd > RING) rd = wr - RING;
    produced++;
}

void c352_catchup(uint64_t h8_states)
{
    uint64_t due = h8_states * C352_RATE / 16934400ull;
    while (produced < due) gen_one();
}
size_t c352_take(int16_t *dst, size_t max_frames)
{
    size_t n = 0;
    while (rd != wr && n < max_frames) { size_t r = rd & (RING - 1); dst[n * 2] = ring[r * 2]; dst[n * 2 + 1] = ring[r * 2 + 1]; rd++; n++; }
    return n;
}

static uint16_t *field(voice_t *v, int i)
{
    switch (i) { case 0: return &v->vol_f; case 1: return &v->vol_r; case 2: return &v->freq; case 3: return &v->flags;
                 case 4: return &v->wave_bank; case 5: return &v->wave_start; case 6: return &v->wave_end; default: return &v->wave_loop; }
}
uint16_t c352_read(uint32_t offset)
{
    if (offset < 0x100) return *field(&v_[offset / 8], (int)(offset % 8));
    if (offset == 0x200) return control;
    return 0;
}
void c352_write(uint32_t offset, uint16_t data, uint16_t mask)
{
    if (offset < 0x100) { uint16_t *f = field(&v_[offset / 8], (int)(offset % 8)); *f = (uint16_t)((*f & ~mask) | (data & mask)); }
    else if (offset == 0x200) control = (uint16_t)((control & ~mask) | (data & mask));
    else if (offset == 0x202) {                      /* execute key-ons / key-offs (16-bit writes only) */
        if (mask != 0xffff) return;
        for (int i = 0; i < 32; i++) {
            voice_t *v = &v_[i];
            if (v->flags & FLG_KEYON) {
                v->pos = ((uint32_t)v->wave_bank << 16) | v->wave_start; v->sample = 0; v->last_sample = 0; v->counter = 0xffff;
                v->flags |= FLG_BUSY; v->flags &= (uint16_t)~(FLG_KEYON | FLG_LOOPHIST);
                v->curr_vol[0] = v->curr_vol[1] = v->curr_vol[2] = v->curr_vol[3] = 0;
            }
            if (v->flags & FLG_KEYOFF) { v->flags &= (uint16_t)~(FLG_BUSY | FLG_KEYOFF); v->counter = 0xffff; }
        }
    }
}
