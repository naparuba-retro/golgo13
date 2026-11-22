/* Golgo 13 native port - command line runner (phase 2: headless boot).
 *   golgo13 --roms <dir> [--frames N] [--dump-ram file] [--trace-io] */
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"
#include "h8_machine.h"
#include "gpu.h"
#include "mdec.h"
#include "platform.h"
#include "menu.h"
#include "options.h"
#include "translate.h"

static int16_t *wav_buf; static size_t wav_frames, wav_cap;
static void wav_sink(const int16_t *st, size_t n)
{
    if (wav_frames + n > wav_cap) { wav_cap = (wav_cap + n) * 2; wav_buf = (int16_t *)realloc(wav_buf, wav_cap * 4); }
    memcpy(wav_buf + wav_frames * 2, st, n * 4); wav_frames += n;
}
static void wav_write(const char *path)
{
    FILE *f = fopen(path, "wb"); if (!f) return;
    uint32_t data = (uint32_t)(wav_frames * 4), rate = 88200, riff = 36 + data, fmt = 16, byte_rate = rate * 4; uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&byte_rate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(wav_buf, 4, wav_frames, f); fclose(f);
}

typedef struct { long at, dur; const char *name; } press_t;
static long seq_start, seq_step = 1, seq_n; static const char *seq_prefix = "shot";
static long shot_at[1024]; static int n_shot_at; static const char *shot_at_prefix = "shot";
static long ram_at[256]; static int n_ram_at; static const char *ram_prefix = "ram";
typedef struct { long f0, f1; uint32_t addr; int val; } poke_t; static poke_t pokes[32]; static int npoke;
static press_t presses[64]; static int npress; static int aim_x = 256, aim_y = 256;
static void scripted(plat_input_t *in, long frame)
{
    memset(in, 0, sizeof *in); in->gun_x = aim_x; in->gun_y = aim_y;
    for (int i = 0; i < npress; i++) {
        if (frame < presses[i].at || frame >= presses[i].at + presses[i].dur) continue;
        const char *n = presses[i].name;
        if (!strcmp(n, "coin")) in->coin = 1; else if (!strcmp(n, "start")) in->start = 1; else if (!strcmp(n, "trigger")) in->trigger = 1;
        else if (!strcmp(n, "sensor")) in->sensor = 1; else if (!strcmp(n, "enter")) in->enter = 1; else if (!strcmp(n, "up")) in->up = 1;
        else if (!strcmp(n, "down")) in->down = 1; else if (!strcmp(n, "service")) in->service = 1; else if (!strcmp(n, "test")) in->test = 1;
        else if (!strcmp(n, "menu")) in->menu = 1; else if (!strcmp(n, "pause")) in->pause = 1; else if (!strcmp(n, "confirm")) in->ui_zip = 1;
    }
}

/* First-run screen: asks the user for their own ROM set (zip or folder; drag & drop or file dialog), verifies it (CRC32) and
 * copies it to the application data folder. Returns 1 when the ROMs are loaded, 0 if the user quit. */
static void wrap_into(const char **lines, int *n, int max, const char *prefix, const char *text, int width)
{
    size_t len = strlen(text);
    for (size_t o = 0; o < len && *n < max; o += width) {
        static char store[16][160]; static int k; char *b = store[k++ & 15];
        snprintf(b, 160, "%s%.*s", prefix, width, text + o); lines[(*n)++] = b;
    }
}
static int rom_screen(const char *datadir)
{
    char status[600] = "", err[300]; int bad = 0; plat_input_t in;
    for (;;) {
        plat_poll(&in); if (in.quit) return 0;
        if (in.ui_zip) plat_choose(0); else if (in.ui_dir) plat_choose(1);
        if (in.drop_path[0]) {
            const char *l0[] = { "#Checking ROMs...", "", "Verifying and copying files (a few seconds)." }; plat_present_text(l0, 3);
            if (rom_import(in.drop_path, datadir, err, sizeof err) == 0 && rom_load(datadir, err, sizeof err) == 0) return 1;
            snprintf(status, sizeof status, "%s", err); bad = 1;
        }
        const char *L[40]; int n = 0;
        L[n++] = "#Golgo 13 - native port"; L[n++] = "";
        L[n++] = "The game itself is not included: this is only the engine.";
        L[n++] = "You need your own copy of the arcade ROMs: the file  golgo13.zip";
        L[n++] = "(MAME romset 'golgo13', Namco System 12).";
        L[n++] = ""; L[n++] = "+To install it, do ONE of these:";
        L[n++] = "+ 1. Drag golgo13.zip onto this window";
        L[n++] = "+ 2. Press Enter (or click, or pad A) and pick golgo13.zip";
        L[n++] = "+ 3. Press Tab (or pad Y) and pick a folder with the unzipped files";
        L[n++] = ""; L[n++] = "The files are checked, then copied to this folder:";
        wrap_into(L, &n, 30, "  ", datadir, 70);
        L[n++] = "(you can also copy the 17 unzipped files there yourself)";
        if (bad) { L[n++] = ""; wrap_into(L, &n, 38, "!", status, 70); }
        L[n++] = ""; L[n++] = "Esc: quit";
        plat_present_text(L, n); plat_sleep_us(16000);
    }
}


/* ---- language choice: Original / any translation found; remembered in settings.ini (data folder) ---- */
static char g_gameid[40]; static char g_dirs[4][1200]; static const char *g_dirp[4]; static int g_nd;
static char g_settings[1200], g_lang[32]; static long script_tick;
static void merge_script(plat_input_t *in, long tick)       /* scripted presses (tests) are OR-ed into the real input */
{
    plat_input_t s; scripted(&s, tick);
    in->coin |= s.coin; in->start |= s.start; in->trigger |= s.trigger; in->sensor |= s.sensor; in->enter |= s.enter; in->up |= s.up; in->down |= s.down;
    in->service |= s.service; in->test |= s.test; in->menu |= s.menu; in->pause |= s.pause; in->ui_zip |= s.ui_zip;
}
static void lang_dirs_init(const char *argv0, const char *tr_dir)
{
    char exedir[1100] = ""; g_nd = 0;
    if (argv0) { snprintf(exedir, sizeof exedir, "%s", argv0); char *sl = strrchr(exedir, 47), *bs = strrchr(exedir, 92); if (bs > sl) sl = bs; if (sl) strcpy(sl, "/translations"); else exedir[0] = 0; }
    if (tr_dir) snprintf(g_dirs[g_nd++], 1200, "%s", tr_dir);
    snprintf(g_dirs[g_nd++], 1200, "translations");
    if (exedir[0]) snprintf(g_dirs[g_nd++], 1200, "%s", exedir);
    { const char *ov = getenv("GOLGO13_DATA"); snprintf(g_dirs[g_nd++], 1200, "%s%stranslations", ov ? ov : plat_pref_dir(), ov ? "/" : ""); }
    if (g_gameid[0] && strcmp(g_gameid, "golgo13"))         /* other games keep their translations in a sub-folder named after the game id */
        for (int i = 0; i < g_nd; i++) { size_t k = strlen(g_dirs[i]); snprintf(g_dirs[i] + k, sizeof g_dirs[i] - k, "/%s", g_gameid); }
    for (int i = 0; i < g_nd; i++) g_dirp[i] = g_dirs[i];
    { const char *ov = getenv("GOLGO13_DATA"); snprintf(g_settings, sizeof g_settings, "%s%ssettings.ini", ov ? ov : plat_pref_dir(), ov ? "/" : ""); }
}
static int g_opts_ready;
static void opts_init_once(void) { if (!g_opts_ready) { g_opts_ready = 1; options_init(g_settings); } }
static int settings_load(char *lang, size_t n)               /* returns 1 if a language was saved in settings.ini */
{
    opts_init_once(); if (!g_opt_lang[0]) return 0; snprintf(lang, n, "%s", g_opt_lang); return 1;
}
static void settings_save(void) { opts_init_once(); options_set_lang(g_lang[0] ? g_lang : "original"); options_flush(1); }
static void apply_language(const char *code)
{
    translate_revert(); g_lang[0] = 0;
    if (code && *code && strcmp(code, "original")) {
        char msg[400]; int n = translate_apply_lang(code, g_dirp, g_nd, msg, sizeof msg); fprintf(stderr, "%s\n", msg);
        if (n >= 0) snprintf(g_lang, sizeof g_lang, "%s", code);
    } else fprintf(stderr, "language: original\n");
    menu_set_ui_lang(g_lang);
}
static int g_lang_changed;
#ifdef _WIN32
#include <process.h>
static void setenv_nolast(void) { _putenv("GOLGO13_NOLAST=1"); }
static void spawn_self(const char *exe) { _spawnl(_P_NOWAIT, exe, exe, (char *)NULL); }
#else
#include <unistd.h>
static void setenv_nolast(void) { setenv("GOLGO13_NOLAST", "1", 1); }
static void spawn_self(const char *exe) { execl(exe, exe, (char *)NULL); }
#endif
static void menu_apply_lang_cb(const char *code) { apply_language(code); settings_save(); g_lang_changed = 1; }
static menu_ctx_t g_mctx;
static void menu_ctx_fill(void)
{
    translate_lang_t langs[16]; int n = translate_list(g_dirp, g_nd, langs, 16); static char codes[16][32], names[16][100];
    g_mctx.n = n; for (int i = 0; i < n; i++) { snprintf(codes[i], 32, "%s", langs[i].code); snprintf(names[i], 100, "%s", langs[i].name); g_mctx.code[i] = codes[i]; g_mctx.name[i] = names[i]; }
    g_mctx.apply_lang = menu_apply_lang_cb;
}
/* Modal menu. Returns the chosen language code ("original" or a code), or NULL if closed without choosing. */
static const char *language_menu(void)
{
    static char pick[32]; translate_lang_t langs[16]; int n = translate_list(g_dirp, g_nd, langs, 16);
    int sel = 0, prev_up = 1, prev_down = 1; plat_input_t in;
    for (int i = 0; i < n; i++) if (!strcmp(langs[i].code, g_lang)) sel = i + 1;
    for (;;) {
        script_tick++; plat_poll(&in); merge_script(&in, script_tick);
        if (in.quit) return NULL;
        if (in.up && !prev_up && sel > 0) sel--; if (in.down && !prev_down && sel < n) sel++;
        prev_up = in.up; prev_down = in.down;
        if (in.ui_zip) { if (sel == 0) snprintf(pick, sizeof pick, "original"); else snprintf(pick, sizeof pick, "%s", langs[sel - 1].code); return pick; }
        if (in.menu || in.ui_dir) return NULL;
        const char *L[24]; char items[17][100]; int k = 0;
        L[k++] = "#Language / Langue"; L[k++] = "";
        snprintf(items[0], sizeof items[0], "%s Original (Japanese)", sel == 0 ? "+>" : "  "); L[k++] = items[0];
        for (int i = 0; i < n; i++) { snprintf(items[i + 1], sizeof items[i + 1], "%s %s", sel == i + 1 ? "+>" : "  ", langs[i].name); L[k++] = items[i + 1]; }
        L[k++] = ""; L[k++] = "Up / Down: choose     Enter / click / pad A: confirm"; L[k++] = "F4 / pad Y: close     (the game restarts when the language changes)";
        plat_present_text(L, k); plat_sleep_us(16000);
    }
}

/* ---- game selector: every known game is listed; availability = ROMs found in the data folder / ./roms (folder or zip) ---- */
static void lastgame_path(char *out, size_t n) { const char *ov = getenv("GOLGO13_DATA"); snprintf(out, n, "%s%slastgame.txt", ov ? ov : plat_pref_dir(), ov ? "/" : ""); }
static int game_menu(const char *userroms)
{
    int n = games_count(), sel = 0, prev_up = 1, prev_down = 1; plat_input_t in; char msg[300] = "", st_path[8][1100]; int st[8];
    { char lp[1300], id[40] = ""; lastgame_path(lp, sizeof lp); FILE *f = fopen(lp, "rb"); if (f) { if (fgets(id, sizeof id, f)) { size_t k = strlen(id); while (k && (id[k-1] == 10 || id[k-1] == 13)) id[--k] = 0; } fclose(f); }
      int g = id[0] ? game_index(id) : -1; if (g >= 0) sel = g; }
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++) st[i] = game_find_roms(i, userroms, st_path[i], sizeof st_path[i]);
    for (;;) {
        plat_poll(&in); if (in.quit) return -1;
        if (in.up && !prev_up && sel > 0) sel--; if (in.down && !prev_down && sel < n - 1) sel++;
        prev_up = in.up; prev_down = in.down;
        if (in.ui_zip) {
            const game_info_t *g = game_info(sel);
            if (!g->supported) snprintf(msg, sizeof msg, "%s cannot be started yet. %s", g->title, g->note);
            else { char lp[1300]; lastgame_path(lp, sizeof lp); FILE *f = fopen(lp, "wb"); if (f) { fprintf(f, "%s\n", g->id); fclose(f); } return sel; }
        }
        const char *L[40]; static char buf[8][2][200]; int k = 0;
        L[k++] = "#Golgo 13 - choose a game"; L[k++] = "";
        for (int i = 0; i < n; i++) {
            const game_info_t *g = game_info(i);
            snprintf(buf[i][0], 200, "%s%s%s  (%d)", g->supported ? "+" : "", i == sel ? "> " : "  ", g->title, g->year);
            const char *where = st[i] == 1 ? "ROMs: folder" : st[i] == 2 ? "ROMs: zip" : "ROMs: not found";
            snprintf(buf[i][1], 200, "    %s | %s | %s", g->board, where, g->supported ? "playable" : "not playable yet");
            L[k++] = buf[i][0]; L[k++] = buf[i][1]; L[k++] = "";
        }
        if (msg[0]) { static char m[2][160]; snprintf(m[0], 160, "!%.150s", msg); L[k++] = m[0]; L[k++] = ""; }
        L[k++] = "Up / Down: choose     Enter / click / pad A: start     Esc: quit";
        L[k++] = "ROMs: data folder or ./roms  (folder <id>/ or <id>.zip)";
        plat_present_text(L, k); plat_sleep_us(16000);
    }
}

/* ---- session recorder / replay: one record of 6 bytes per emulated frame (gun x, gun y, button bits), file "G13R" ----
 * The emulation is deterministic, so a recorded session replayed with --replay reproduces exactly the same game, frame for frame. */
static FILE *g_rec; static long g_rec_n; static unsigned char *g_play; static long g_play_n;
static unsigned in_bits(const plat_input_t *in)
{
    return (in->trigger ? 1u : 0) | (in->sensor ? 2u : 0) | (in->enter ? 4u : 0) | (in->up ? 8u : 0) | (in->down ? 16u : 0) | (in->start ? 32u : 0) | (in->coin ? 64u : 0) | (in->service ? 128u : 0) | (in->test ? 256u : 0);
}
static void rec_close(void) { if (g_rec) { fclose(g_rec); g_rec = NULL; } }
static void rec_open(const char *dir, const char *lang)
{
    rec_close(); fs_mkdir_p(dir);
    char path[1300], ts[40]; time_t t = time(NULL); struct tm *lt = localtime(&t); strftime(ts, sizeof ts, "%Y%m%d_%H%M%S", lt);
    snprintf(path, sizeof path, "%s/session_%s.g13r", dir, ts);
    g_rec = fopen(path, "wb"); g_rec_n = 0; if (!g_rec) return;
    char head[32] = "G13R"; unsigned ver = 1; memcpy(head + 4, &ver, 4); snprintf(head + 8, 24, "%s", lang && *lang ? lang : "original"); fwrite(head, 1, 32, g_rec);
    fprintf(stderr, "recording session: %s\n", path);
}
static void rec_frame(const plat_input_t *in)
{
    if (!g_rec) return;
    unsigned short b[3] = { (unsigned short)in->gun_x, (unsigned short)in->gun_y, (unsigned short)in_bits(in) }; fwrite(b, 2, 3, g_rec);
    if (++g_rec_n % 300 == 0) fflush(g_rec);
}

/* ---- test bot (tooling): plays the game reactively from the display, records the inputs as a .g13r session ---- */
typedef struct { unsigned char *tpl; int tw, th, tx, ty; FILE *out; int win, n_pick, ss_run, state, leave; long t0; int on; long takeover; } bot_t;
static bot_t g_bot;
static int bot_open(bot_t *b, char *spec)     /* template.ppm,out.g13r,mode[,first_story] */
{
    char *tpl = strtok(spec, ","), *out = strtok(NULL, ","), *mode = strtok(NULL, ","), *first = strtok(NULL, ","), *tk = strtok(NULL, ",");
    FILE *f = fopen(tpl, "rb"); if (!f) { fprintf(stderr, "bot: cannot open template %s", tpl); fputc(10, stderr); return 0; }
    int w, h, mx; if (fscanf(f, "P6 %d %d %d", &w, &h, &mx) != 3) return 0;
    fgetc(f); b->tw = w; b->th = h; b->tx = 60; b->ty = 18; b->tpl = (unsigned char *)malloc((size_t)w * h * 3); if (fread(b->tpl, 3, (size_t)w * h, f) != (size_t)w * h) return 0; fclose(f);
    b->out = fopen(out, "wb"); if (!b->out) return 0; char head[32] = "G13R"; unsigned ver = 1; memcpy(head + 4, &ver, 4); snprintf(head + 8, 24, "original"); fwrite(head, 1, 32, b->out);
    b->win = mode && !strcmp(mode, "win"); b->n_pick = first ? atoi(first) : 0; b->takeover = tk ? atol(tk) : 0; b->on = 1; return 1;
}
static int bot_story_select(bot_t *b, const uint8_t *fb, int w, int h)
{
    if (w != 512 || h != 480) return 0;
    long d = 0; int n = 0;
    for (int y = 0; y < b->th; y += 3) for (int x = 0; x < b->tw; x += 3) {
        const uint8_t *p = fb + ((b->ty + y) * w + b->tx + x) * 4, *q = b->tpl + ((size_t)y * b->tw + x) * 3;
        d += abs(p[0] - q[0]) + abs(p[1] - q[1]) + abs(p[2] - q[2]); n++;
    }
    return d / (3 * n) < 9;
}
static int bot_ring_col(const uint8_t *fb, int w, int green, int *cx, int *cy)
{
    int x0 = 9999, y0 = 9999, x1 = -1, y1 = -1, n = 0;
    for (int y = 90; y < 400; y++) for (int x = 0; x < 430; x++) {
        const uint8_t *p = fb + (y * w + x) * 4;
        int hit = green ? (p[1] > 160 && p[0] < 110 && p[2] < 90) : (p[0] > 150 && p[1] < 70 && p[2] < 70);
        if (hit) { n++; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    }
    if (n < (green ? 250 : 600) || n > 3000 || x1 - x0 < 40 || x1 - x0 > 140 || y1 - y0 < 40 || y1 - y0 > 150) return 0;
    *cx = (x0 + x1) / 2; *cy = (y0 + y1) / 2; return 1;
}
static int bot_ring(const uint8_t *fb, int w, int h, int *cx, int *cy)    /* 'TARGET' ring of the missions (red in the first game, green in the second): centre of its bounding box */
{
    if (w != 512 || h != 480) return 0;
    return bot_ring_col(fb, w, 0, cx, cy) || bot_ring_col(fb, w, 1, cx, cy);
}
static int g_bot_ring, g_bot_rx, g_bot_ry;
static void bot_record(bot_t *b, const plat_input_t *in);
static void bot_step(bot_t *b, plat_input_t *in, long f, int ss)
{
    memset(in, 0, sizeof *in);
    static const int TX[5] = { 213, 277, 343, 408, 473 }, TY[4] = { 193, 250, 309, 366 };
    int story = b->n_pick % 20, row = story / 5, col = story % 5;
    in->gun_x = 30; in->gun_y = 32;
    if (f >= 2000 && ((f - 2000) % 500) < 6) in->coin = 1;
    if (ss) b->ss_run++; else b->ss_run = 0;
    switch (b->state) {
    case 0: if (b->ss_run >= 40) { b->state = 1; b->t0 = f; } break;
    case 1: in->gun_x = (int)(TX[col] / 1.0004); in->gun_y = TY[row] * 511 / 479; if (f - b->t0 >= 40 && f - b->t0 < 48) in->trigger = 1; if (f - b->t0 >= 48) { b->state = 2; b->leave = 0; } break;
    case 2: in->gun_x = (int)(TX[col] / 1.0004); in->gun_y = TY[row] * 511 / 479;
            if (!ss) { if (++b->leave >= 30) { fprintf(stderr, "bot: story %d picked at frame %ld", story, f); fputc(10, stderr); b->n_pick++; b->state = 0; b->ss_run = 0; } } else { b->leave = 0; if (f - b->t0 > 700) b->state = 0; }
            break;
    }
    if (b->win && !ss && b->state != 1) {                 /* in a mission: hold the scope, steer the aim point onto the target with the game's own variables, fire when aligned (g13knd addresses) */
        static long ring_since, last_f; static float gx = 256, gy = 256; static int ok_run, cool;
        if (f != last_f + 1) { ring_since = f; gx = 256; gy = 256; ok_run = 0; cool = 0; } last_f = f;
        in->sensor = 1; in->gun_x = (int)gx; in->gun_y = (int)gy;
        if (machine_peek32(0x803bcd18u) == 1u) {                          /* a target is active in the scope view */
            int tx = (int)machine_peek32(0x803bcd10u), ty = (int)machine_peek32(0x803bcd14u), ax = (int)machine_peek32(0x803bc874u), ay = (int)machine_peek32(0x803bc878u);
            int ex = tx - ax, ey = ty - ay;
            { float gn = (ex > 30 || ex < -30 || ey > 30 || ey < -30) ? 0.10f : 0.04f, dx = gn * (float)ex, dy = gn * (float)ey; if (dx > 6) dx = 6; if (dx < -6) dx = -6; if (dy > 6) dy = 6; if (dy < -6) dy = -6; gx += dx; gy += dy; }
            if (gx < 0) gx = 0; if (gx > 511) gx = 511; if (gy < 0) gy = 0; if (gy > 511) gy = 511;
            in->gun_x = (int)gx; in->gun_y = (int)gy;
            if (ex > -4 && ex < 4 && ey > -4 && ey < 4) ok_run++; else ok_run = 0;
            if (getenv("G13_BOTDBG") && f % 20 == 0) fprintf(stderr, "botdbg f=%ld target=%d,%d aim=%d,%d err=%d,%d gun=%d,%d ok=%d\n", f, tx, ty, ax, ay, ex, ey, in->gun_x, in->gun_y, ok_run);
            if (cool > 0) cool--; else if (ok_run >= 3) { in->trigger = 1; if (ok_run >= 8) { cool = 240; ok_run = 0; } }
        }
    }
    if (!ss && b->state != 1 && f >= 2600 && ((f - 2600) % 1700) < 4 && !(b->win && machine_peek32(0x803bcd18u) == 1u)) in->start = 1;
    bot_record(b, in);
}
static void bot_record(bot_t *b, const plat_input_t *in)
{
    if (b->out) { unsigned short r[3] = { (unsigned short)in->gun_x, (unsigned short)in->gun_y, (unsigned short)(in->trigger | (in->sensor << 1) | (in->enter << 2) | (in->up << 3) | (in->down << 4) | (in->start << 5) | (in->coin << 6) | (in->service << 7) | (in->test << 8)) }; fwrite(r, 2, 3, b->out); }
}
static int replay_load(const char *path)
{
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", path); return 0; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); unsigned char *b = (unsigned char *)malloc((size_t)sz);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz || sz < 32 || memcmp(b, "G13R", 4)) { fclose(f); free(b); fprintf(stderr, "%s is not a G13R session\n", path); return 0; }
    fclose(f); g_play = b + 32; g_play_n = (sz - 32) / 6; fprintf(stderr, "replay: %s, %ld frames (recorded with language '%s')\n", path, g_play_n, (const char *)(b + 8)); return 1;
}
static void replay_get(plat_input_t *in, long frame)
{
    unsigned short b[3] = { 256, 256, 0 };
    if (frame >= 0 && frame < g_play_n) memcpy(b, g_play + frame * 6, 6);
    in->gun_x = b[0]; in->gun_y = b[1]; unsigned f = b[2];
    in->trigger = f & 1; in->sensor = (f >> 1) & 1; in->enter = (f >> 2) & 1; in->up = (f >> 3) & 1; in->down = (f >> 4) & 1; in->start = (f >> 5) & 1; in->coin = (f >> 6) & 1; in->service = (f >> 7) & 1; in->test = (f >> 8) & 1;
}

extern int rt_code_is_stub;
int main(int argc, char **argv)
{
    int want_wiimote = getenv("GOLGO13_WIIMOTE") != NULL; const char *game_opt = NULL, *menu_shots_dir = NULL, *dump_regions_dir = NULL;
    const char *lang = getenv("GOLGO13_LANG"), *tr_dir = NULL, *replay_path = NULL; const char *dump_aux = NULL, *card_art_path = NULL; const char *roms = NULL, *dump = NULL, *dump_vram = NULL, *import_src = NULL; long frames = 0; int trace = 0, headless = 0, noh8 = 0, noaudio = 0; const char *wav_path = NULL; long autoquit = 0, nframes = 0; const char *shot_p = NULL; uint64_t shot_f = 0, log_f = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--roms") && i + 1 < argc) roms = argv[++i];
        else if (!strcmp(argv[i], "--shotseq") && i + 1 < argc) {            /* start:step:count:prefix  (with --press; test tool) */
            char *a = argv[++i]; seq_start = atol(a); char *c = strchr(a, ':');
            if (c) { seq_step = atol(c + 1); c = strchr(c + 1, ':'); if (c) { seq_n = atol(c + 1); c = strchr(c + 1, ':'); if (c) seq_prefix = c + 1; } }
            if (seq_step < 1) seq_step = 1;
        }
        else if (!strcmp(argv[i], "--shotat") && i + 1 < argc) {                /* f1,f2,...:prefix  (screenshots at these frames; headless) */
            char *a = argv[++i]; char *c = strrchr(a, 58); if (c) { shot_at_prefix = c + 1; *c = 0; }
            for (char *t = strtok(a, ","); t && n_shot_at < 1024; t = strtok(NULL, ",")) shot_at[n_shot_at++] = atol(t);
        }
        else if (!strcmp(argv[i], "--poke") && i + 1 < argc && npoke < 32) {   /* from-to:addr:value  (test tool: writes a RAM byte at every frame in the range) */
            char *a = argv[++i]; poke_t *p = &pokes[npoke++]; p->f0 = atol(a); char *d = strchr(a, 45), *c = strchr(a, 58);
            p->f1 = d && (!c || d < c) ? atol(d + 1) : p->f0; if (c) { p->addr = (uint32_t)strtoul(c + 1, &c, 16); if (c && *c == 58) p->val = atoi(c + 1); }
        }
        else if (!strcmp(argv[i], "--ramat") && i + 1 < argc) {               /* f1,f2,...:prefix -> dump RAM at those frames (headless) */
            char *a = argv[++i]; char *c = strrchr(a, 58); if (c) { ram_prefix = c + 1; *c = 0; }
            for (char *t = strtok(a, ","); t && n_ram_at < 256; t = strtok(NULL, ",")) ram_at[n_ram_at++] = atol(t);
        }
        else if (!strcmp(argv[i], "--ramseq") && i + 1 < argc) {              /* start:step:count:prefix -> dump RAM at those frames (needs --press) */
            char *a = argv[++i]; long st = atol(a), sp = 1, cn = 1; char *c = strchr(a, ':');
            if (c) { sp = atol(c + 1); c = strchr(c + 1, ':'); if (c) { cn = atol(c + 1); c = strchr(c + 1, ':'); if (c) ram_prefix = c + 1; } }
            for (long k = 0; k < cn && n_ram_at < 256; k++) ram_at[n_ram_at++] = st + k * (sp < 1 ? 1 : sp);
        }
        else if (!strcmp(argv[i], "--lang") && i + 1 < argc) lang = argv[++i];
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc) replay_path = argv[++i];
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) game_opt = argv[++i];
        else if (!strcmp(argv[i], "--menu-shots") && i + 1 < argc) menu_shots_dir = argv[++i];
        else if (!strcmp(argv[i], "--dump-regions") && i + 1 < argc) dump_regions_dir = argv[++i];
        else if (!strcmp(argv[i], "--wiimote")) want_wiimote = 1;
        else if (!strcmp(argv[i], "--wiimote-pair")) return plat_wiimote_pair() ? 0 : 1;
        else if (!strcmp(argv[i], "--bot") && i + 1 < argc) { if (!bot_open(&g_bot, argv[++i])) return 1; }
        else if (!strcmp(argv[i], "--translations") && i + 1 < argc) tr_dir = argv[++i];
        else if (!strcmp(argv[i], "--import") && i + 1 < argc) import_src = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--texdump") && i + 1 < argc) {                 /* dir:from:to  (frames) */
            static char td[1024]; snprintf(td, sizeof td, "%s", argv[++i]); char *c1 = strrchr(td, 58);
            if (c1) { char *c2 = c1; *c2 = 0; char *c0 = strrchr(td, 58); if (c0) { *c0 = 0; gpu_texdump_from = (uint64_t)atol(c0 + 1); gpu_texdump_to = (uint64_t)atol(c2 + 1); } }
            gpu_texdump_dir = td;
        }
        else if (!strcmp(argv[i], "--quadcap") && i + 1 < argc) {                 /* file:frame */
            static char qc[1024]; snprintf(qc, sizeof qc, "%s", argv[++i]); char *cc = strrchr(qc, 58);
            if (cc) { *cc = 0; gpu_quadcap_frame = (uint64_t)atol(cc + 1); gpu_quadcap_path = qc; }
        }
        else if (!strcmp(argv[i], "--traceload")) gpu_trace_loads = 1;
        else if (!strcmp(argv[i], "--dump-aux") && i + 1 < argc) dump_aux = argv[++i];
        else if (!strcmp(argv[i], "--dump-vram") && i + 1 < argc) dump_vram = argv[++i];
        else if (!strcmp(argv[i], "--card-art") && i + 1 < argc) card_art_path = argv[++i];      /* tooling: save the display after --frames as a menu card picture */
        else if (!strcmp(argv[i], "--dump-ram") && i + 1 < argc) dump = argv[++i];
        else if (!strcmp(argv[i], "--press") && i + 1 < argc && npress < 64) {      /* frame:duration:name */
            char *a = argv[++i]; presses[npress].at = atol(a); char *c = strchr(a, ':');
            if (c) { presses[npress].dur = atol(c + 1); char *d = strchr(c + 1, ':'); presses[npress].name = d ? d + 1 : ""; npress++; }
        }
        else if (!strcmp(argv[i], "--aim") && i + 1 < argc) { char *a = argv[++i]; aim_x = atoi(a); char *c = strchr(a, ','); if (c) aim_y = atoi(c + 1); }
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--nodraw")) gpu_nodraw = 1;
        else if (!strcmp(argv[i], "--noh8")) noh8 = 1;
        else if (!strcmp(argv[i], "--noaudio")) noaudio = 1;
        else if (!strcmp(argv[i], "--headless")) headless = 1;
        else if (!strcmp(argv[i], "--autoquit") && i + 1 < argc) autoquit = atol(argv[++i]);
        else if (!strcmp(argv[i], "--trace-io")) trace = 1;
        else if (!strcmp(argv[i], "--tracepix") && i + 2 < argc) { gpu_trace_x = atoi(argv[i + 1]); gpu_trace_y = atoi(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "--primlog") && i + 1 < argc) log_f = (uint64_t)atol(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc) { shot_f = (uint64_t)atol(argv[i + 1]); shot_p = argv[i + 2]; i += 2; }
        else if (!strcmp(argv[i], "--watch") && i + 1 < argc) machine_watch((uint32_t)strtoul(argv[++i], NULL, 16));
        else { fprintf(stderr, "usage: %s --roms DIR [--frames N] [--dump-ram FILE]\n", argv[0]); return 2; }
    }
    machine_init();
    if (menu_shots_dir) {                                            /* tooling: one picture per menu page, no window */
        lang_dirs_init(argc > 0 ? argv[0] : NULL, tr_dir); opts_init_once(); strcpy(g_opt_lang, lang && *lang ? lang : g_opt_lang);
        char exedir[1100] = ""; if (argc > 0) { snprintf(exedir, sizeof exedir, "%s", argv[0]); char *sl = strrchr(exedir, 47), *bs = strrchr(exedir, 92); if (bs > sl) sl = bs; if (sl) *sl = 0; }
        char ur[1200]; { const char *ov = getenv("GOLGO13_DATA"); snprintf(ur, sizeof ur, "%s%sroms", ov ? ov : "", ov ? "/" : "./"); }
        menu_init(exedir, ""); if (!menu_available()) { fprintf(stderr, "menu fonts missing (run tools/ui/bake_font.py)\n"); return 1; }
        menu_ctx_fill(); { const char *e = getenv("GOLGO13_UIROMS"); if (e) menu_set_roms_dir(e); } return menu_shots(menu_shots_dir, &g_mctx, ur);
    }
    char err[256];
    char datadir[1100] = ""; int window = (frames == 0 && !headless);
    if (window && !plat_init("Golgo 13 (native port)", 1280, 720)) { fprintf(stderr, "cannot create window\n"); return 1; }
    if (window) {
        char exedir[1100] = ""; if (argc > 0) { snprintf(exedir, sizeof exedir, "%s", argv[0]); char *sl = strrchr(exedir, 47), *bs = strrchr(exedir, 92); if (bs > sl) sl = bs; if (sl) *sl = 0; else exedir[0] = 0; }
        lang_dirs_init(argc > 0 ? argv[0] : NULL, tr_dir); opts_init_once();
        { char d1[1200], d2[1200]; snprintf(d1, sizeof d1, "%s/roms", exedir[0] ? exedir : "."); snprintf(d2, sizeof d2, "%s/../roms", exedir[0] ? exedir : ".");
          rom_add_search_dir(d1); rom_add_search_dir(d2);
          char shown[1200];
          int have_cwd = 0; { char probe[1200]; snprintf(probe, sizeof probe, "roms"); have_cwd = fs_dir_exists(probe); }
          snprintf(shown, sizeof shown, "%s", have_cwd ? "roms" : d1); if (!have_cwd) fs_mkdir_p(d1);
          char full[1300]; fs_abs_path(shown, full, sizeof full); menu_set_roms_dir(full); }
        { char self[1300]; fs_abs_path(argv[0], self, sizeof self); menu_set_exe_path(self); }
        menu_init(exedir, plat_pref_dir()); menu_apply_options(); menu_set_ui_lang(lang && *lang ? lang : g_opt_lang); if (g_opt[OPT_WIIMOTE]) want_wiimote = 1;
    }
    if (window && want_wiimote) plat_wiimote_enable();
    char userroms[1200];
    { const char *ov = getenv("GOLGO13_DATA");                        /* override of the per-user data folder (tests, portable use) */
      snprintf(userroms, sizeof userroms, "%s%sroms", ov ? ov : plat_pref_dir(), ov ? "/" : ""); }
    int gi = 0;
    if (game_opt) { gi = game_index(game_opt); if (gi < 0) { fprintf(stderr, "unknown game '%s'\n", game_opt); return 2; } }
    else if (window && !roms && !import_src && !replay_path) {
        lang_dirs_init(argc > 0 ? argv[0] : NULL, tr_dir);
        opts_init_once();
        if (menu_available()) {
            menu_ctx_fill(); g_mctx.in_game = 0; int direct = -1;
            if (g_opt[OPT_START_LAST] && g_opt_lastgame[0] && !getenv("GOLGO13_NOLAST")) { int g = game_index(g_opt_lastgame); char tp[1100]; if (g >= 0 && game_info(g)->supported && game_find_roms(g, userroms, tp, sizeof tp)) direct = g; }
            gi = direct >= 0 ? direct : menu_select_game(userroms, &g_mctx);
            if (g_lang_changed) g_lang_changed = 0;
        } else gi = game_menu(userroms);
        if (gi < 0) { plat_shutdown(); return 0; }
    }
    rom_select_game(gi); snprintf(g_gameid, sizeof g_gameid, "%s", game_info(gi)->id);
    if (dump_regions_dir) {                                           /* tooling (tools/setup.py): write the ROM regions the recompiler reads */
        char found[1100], err2[256]; int st = game_find_roms(gi, userroms, found, sizeof found);
        if (!st) { fprintf(stderr, "no ROMs found for %s\n", game_info(gi)->id); return 1; }
        if (st == 2) { char tmpd[1200]; snprintf(tmpd, sizeof tmpd, "%s/%s", userroms, game_info(gi)->id); if (rom_import(found, tmpd, err2, sizeof err2)) { fprintf(stderr, "import failed: %s\n", err2); return 1; } snprintf(found, sizeof found, "%s", tmpd); }
        if (rom_load(found, err2, sizeof err2)) { fprintf(stderr, "ROM error: %s\n", err2); return 1; }
        fs_mkdir_p(dump_regions_dir); char fn[1300]; FILE *f;
        snprintf(fn, sizeof fn, "%s/maincpu.bin", dump_regions_dir); if ((f = fopen(fn, "wb"))) { fwrite(G.rom_main, 1, 0x400000, f); fclose(f); }
        if (G.rom_sub) { snprintf(fn, sizeof fn, "%s/sub.bin", dump_regions_dir); if ((f = fopen(fn, "wb"))) { fwrite(G.rom_sub, 1, 0x80000, f); fclose(f); } }
        fprintf(stderr, "regions of %s written to %s\n", game_info(gi)->id, dump_regions_dir); return 0;
    }
    if (game_code_select(game_info(gi)->id)) { fprintf(stderr, "this build has no recompiled code for %s\n", game_info(gi)->title); if (window) plat_shutdown(); return 1; }
    snprintf(datadir, sizeof datadir, "%s/%s", userroms, game_info(gi)->id);
    if (import_src) {                                                 /* --import <zip|folder>: copy the ROMs into the data folder */
        if (rom_import(import_src, datadir, err, sizeof err)) { fprintf(stderr, "import failed: %s\n", err); return 1; }
        fprintf(stderr, "ROMs imported into %s\n", datadir);
    }
    if (roms) { if (rom_load(roms, err, sizeof err)) { fprintf(stderr, "ROM error: %s\n", err); return 1; } }
    else {
        char found[1100]; int st = game_find_roms(gi, userroms, found, sizeof found), ok = 0;
        if (st == 1) ok = rom_load(found, err, sizeof err) == 0;
        if (!ok && st == 2) {
            if (window) { const char *l0[] = { "#Checking ROMs...", "", "Verifying and copying files (a few seconds)." }; plat_present_text(l0, 3); }
            ok = rom_import(found, datadir, err, sizeof err) == 0 && rom_load(datadir, err, sizeof err) == 0;
        }
        if (!ok) ok = rom_load(datadir, err, sizeof err) == 0;
        if (!ok) {
            if (!window || gi != 0) { fprintf(stderr, "ROM error: %s (use --roms DIR or --import ZIP)\n", err); if (window) plat_shutdown(); return 1; }
            if (!rom_screen(datadir)) { plat_shutdown(); return 0; }
        }
    }
    if (replay_path && replay_load(replay_path) && frames == 0 && headless) frames = g_play_n + 60;
    lang_dirs_init(argc > 0 ? argv[0] : NULL, tr_dir);
    { char saved[32] = ""; int have = settings_load(saved, sizeof saved);
      (void)have; apply_language((lang && *lang) ? lang : saved); }
    G.max_frames = (uint64_t)frames; G.shot_path = shot_p; G.shot_frame = shot_f; G.log_frame = log_f; G.prof_noh8 = noh8; G.prof_noaudio = noaudio; G.trace_io = trace;
    machine_reset();
    if (rt_code_is_stub && window) {                                  /* a build without recompiled code (e.g. downloaded from CI): say what to do */
        const char *L[] = { "#This build has no game code", "", "The game code is generated from your own ROMs and is never published.", "Run this once in the project folder, then start the game again:", "", "    python tools/setup.py", "", "Click or press Enter to quit." };
        plat_input_t qi; do { plat_poll(&qi); plat_present_text(L, 8); plat_sleep_us(16000); } while (!qi.quit && !qi.ui_zip && !qi.pause);
        plat_shutdown(); return 1;
    }
    if (wav_path) G.audio_sink = wav_sink;
    if (window) {                                /* interactive: window + keyboard/mouse, 60 Hz pacing */
        plat_show_cursor(0);
        if (plat_audio_init(88200)) G.audio_sink = plat_audio_push; else fprintf(stderr, "no audio device\n");
        static uint8_t buf[1024 * 512 * 4]; plat_input_t in; int w = 512, h = 480, restart_self = 0;
        uint64_t next = plat_time_us(), t_start = next;
        char sess_dir[1300]; { const char *ov = getenv("GOLGO13_DATA"); snprintf(sess_dir, sizeof sess_dir, "%s%ssessions", ov ? ov : plat_pref_dir(), ov ? "/" : ""); }
        { translate_lang_t tl[2]; char tmp[32] = ""; if (!(lang && *lang) && !settings_load(tmp, sizeof tmp) && translate_list(g_dirp, g_nd, tl, 2) > 0) {
              if (menu_available()) { menu_ctx_fill(); g_lang_changed = 0; menu_options(&g_mctx, 4); if (!g_opt_lang[0]) { options_set_lang(g_lang[0] ? g_lang : "original"); options_flush(1); } if (g_lang_changed) machine_reset(); }
              else { const char *c = language_menu(); if (c) { apply_language(c); settings_save(); machine_reset(); } } } }
        /* Fast-forward phase: the ROM check + the NOTICE screen (its 10 s countdown) run unpaced, hidden behind a short gag (menu_notice). */
        int n0 = 0, n1 = 480, boot_end = 480;                          /* NOTICE screen: first / last frame, end of the unpaced start-up */
        if (!strcmp(g_gameid, "golgo13")) { n0 = 1950; n1 = 2600; boot_end = 2600; } else if (!strcmp(g_gameid, "g13knd")) { n0 = 2100; n1 = 2650; boot_end = 2650; }
        const uint64_t GAG_US = 4200000; uint64_t gag_t0 = 0, gag_last = 0; int use_gag = menu_available() && !getenv("GOLGO13_NOGAG");
        enum { BOOT_FRAMES = 1800 };    /* the arcade board spends ~30 s on its own ROM check (black/white screen): run it unpaced */
        void (*real_sink)(const int16_t *, size_t) = G.audio_sink; if (real_sink) G.audio_sink = NULL;
        if (!g_play_n && g_opt[OPT_RECORD]) rec_open(sess_dir, g_lang);
        for (;;) {
            script_tick++; plat_poll(&in); merge_script(&in, script_tick); if (in.quit || (autoquit && script_tick >= autoquit)) break;
            if (in.pause && menu_available()) {
                menu_ctx_fill(); g_mctx.in_game = 1; g_lang_changed = 0; int r = menu_pause(&g_mctx, buf, w, h);
                options_flush(1);
                if (r == MENU_QUIT) break;
                if (r == MENU_CHANGE_GAME) { restart_self = 1; break; }
                if (r == MENU_RESET || g_lang_changed) { machine_reset(); nframes = 0; gag_t0 = 0; if (real_sink) G.audio_sink = NULL; if (!g_play_n && g_opt[OPT_RECORD]) rec_open(sess_dir, g_lang); }
                next = plat_time_us(); continue;
            }
            if (in.menu && menu_available()) {
                menu_ctx_fill(); g_lang_changed = 0; menu_options(&g_mctx, 4);
                if (g_lang_changed) { machine_reset(); nframes = 0; gag_t0 = 0; if (real_sink) G.audio_sink = NULL; if (!g_play_n && g_opt[OPT_RECORD]) rec_open(sess_dir, g_lang); }
                next = plat_time_us(); continue;
            }
            if (in.menu) {
                const char *c = language_menu();
                if (c && strcmp(c, g_lang[0] ? g_lang : "original")) { apply_language(c); settings_save(); machine_reset(); nframes = 0; gag_t0 = 0; if (real_sink) G.audio_sink = NULL; if (!g_play_n) rec_open(sess_dir, g_lang); next = plat_time_us(); continue; }
                next = plat_time_us(); continue;
            }
            int booting = nframes < (use_gag ? boot_end : BOOT_FRAMES);
            if (use_gag && !g_play_n && nframes >= n0 && (nframes < n1 || gag_t0 == 0 || plat_time_us() - gag_t0 < GAG_US)) {
                /* the NOTICE countdown gag: the original screen keeps running (unpaced) under a short animation */
                uint64_t nowu = plat_time_us(); if (!gag_t0) gag_t0 = nowu;
                if (in.ui_zip || in.sensor) gag_t0 = nowu - GAG_US;                         /* click / Enter / right click (scope button) skips the animation */
                double tp = (double)(nowu - gag_t0) / (double)GAG_US; if (tp > 1) tp = 1;
                double ep = (double)(nframes - n0) / (double)(n1 - n0); if (ep < 0) ep = 0; if (ep > 1) ep = 1;
                if (nowu - gag_last >= 33000) { gpu_get_display(buf, &w, &h); menu_notice(buf, w, h, (float)(tp < ep ? tp : ep), (unsigned)(nowu / 1000)); gag_last = nowu; }
                if (nframes >= n1 || (double)(nframes - n0) >= tp * (n1 - n0) + 1) { plat_sleep_us(2000); if (nframes >= n1 && tp >= 1.0) { /* done */ } else continue; }
                booting = 1;
            }
            if (!booting && real_sink && !G.audio_sink) G.audio_sink = real_sink;
            nframes++;
            if (g_play_n) { int q = in.quit, m = in.menu; replay_get(&in, nframes - 1); in.quit = q; in.menu = m; }
            { static const struct { const char *id; uint32_t a[2]; int n; } FC[] = {      /* credit counter of each game (found by diffing RAM with 0/1/2 coins) */
                  { "golgo13", { 0x803b9344u }, 1 }, { "g13knd", { 0x803bc22cu }, 1 }, { "g13jnr", { 0x800c5c8eu, 0x800e6ad8u }, 2 } };
              int n = 0; const uint32_t *ad = NULL;
              if (g_opt[OPT_FREECREDIT] && !g_play_n && !booting) for (unsigned k = 0; k < sizeof FC / sizeof *FC; k++) if (!strcmp(FC[k].id, g_gameid)) { ad = FC[k].a; n = FC[k].n; }
              machine_set_free_credit(ad, n, 9); }
            if (!g_play_n) {                                                 /* "simple mode": free credits + the trigger doubles as Start */
                if (g_opt[OPT_TRIGSTART] && !booting && (in.trigger || in.sensor)) in.start = 1;   /* fire or scope button = Start (also skips the briefing pictures) */
            }
            machine_set_input(in.gun_x, in.gun_y, in.trigger, in.sensor, in.enter, in.up, in.down, in.start, in.coin, in.service, in.test);
            if (!g_play_n) rec_frame(&in);
            if (machine_run_frames(1)) break;
            if (booting) {                                            /* progress screen, no pacing */
                if (use_gag && nframes % 20 == 0 && !(nframes >= n0 && nframes < n1)) { gpu_get_display(buf, &w, &h); plat_present(buf, w, h); }
                if (!use_gag && nframes % 15 == 0) {
                    char bar[64], pct[64]; int k = (int)(nframes * 40 / BOOT_FRAMES);
                    for (int i = 0; i < 40; i++) bar[i] = i < k ? '#' : '-'; bar[40] = 0;
                    snprintf(pct, sizeof pct, " %d%%", (int)(nframes * 100 / BOOT_FRAMES));
                    char line[128]; snprintf(line, sizeof line, "[%s]%s", bar, pct);
                    const char *L[] = { "#Starting the game...", "", "The arcade board runs its own startup checks.", "This takes a few seconds.", "", line };
                    plat_present_text(L, 6);
                }
                next = plat_time_us(); continue;
            }
            gpu_get_display(buf, &w, &h); plat_present(buf, w, h);
            next += 16667; uint64_t now = plat_time_us();
            if (now + 100000 < next) next = now;                      /* far behind: do not try to catch up */
            else if (next > now) { plat_sleep_us(next - now); while (plat_time_us() < next) {} }
        }
        rec_close();
        { double secs = (plat_time_us() - t_start) / 1e6; fprintf(stderr, "window mode: %ld frames in %.2f s = %.1f fps\n", nframes, secs, nframes / (secs > 0 ? secs : 1)); }
        options_flush(1); plat_shutdown();
        if (restart_self) { setenv_nolast(); spawn_self(argv[0]); }
    } else if (npress || g_play_n || g_bot.on) {                                /* headless with scripted or replayed inputs */
        plat_input_t in; long f = 0;
        while (!G.fatal && f < frames) {
            scripted(&in, f); if (g_play_n) replay_get(&in, f);
            if (g_bot.on && f < g_bot.takeover) bot_record(&g_bot, &in);
            else if (g_bot.on) { static uint8_t bbuf[1024 * 512 * 4]; int bw = 0, bh = 0; gpu_get_display(bbuf, &bw, &bh); g_bot_ring = bot_ring(bbuf, bw, bh, &g_bot_rx, &g_bot_ry); bot_step(&g_bot, &in, f, bot_story_select(&g_bot, bbuf, bw, bh)); }
            for (int k = 0; k < npoke; k++) if (f >= pokes[k].f0 && f <= pokes[k].f1) machine_poke8(pokes[k].addr, (uint8_t)pokes[k].val);
            f++;
            machine_set_input(in.gun_x, in.gun_y, in.trigger, in.sensor, in.enter, in.up, in.down, in.start, in.coin, in.service, in.test);
            machine_run_frames(1);
            for (int k = 0; k < n_shot_at; k++) if (shot_at[k] == f) { char nm[1100]; snprintf(nm, sizeof nm, "%s%05ld.ppm", shot_at_prefix, f); machine_save_shot(nm); }
            for (int k = 0; k < n_ram_at; k++) if (ram_at[k] == f) { char nm[1100]; snprintf(nm, sizeof nm, "%s%05ld.bin", ram_prefix, f); machine_dump_ram(nm); }
            if (seq_n && f >= seq_start && (f - seq_start) % seq_step == 0 && (f - seq_start) / seq_step < seq_n) {
                char nm[1100]; snprintf(nm, sizeof nm, "%s%04ld.ppm", seq_prefix, f); machine_save_shot(nm);
            }
        }
    } else machine_run();
    fprintf(stderr, "stopped: frame %llu, cycles %llu, fatal=%d, gp0 words %llu, dma2 lists %llu blocks %llu, irq stat %08x mask %08x\n",
            (unsigned long long)G.frame, (unsigned long long)CPU.cycles, G.fatal, (unsigned long long)G.gp0_words,
            (unsigned long long)G.dma2_lists, (unsigned long long)G.dma2_blocks, G.irq_stat, G.irq_mask);
    fprintf(stderr, "blocks executed: %llu, instructions: %llu (avg %.2f per block)\n", (unsigned long long)G.nblocks, (unsigned long long)CPU.cycles, G.nblocks ? (double)CPU.cycles / G.nblocks : 0.0);
    { extern uint64_t gpu_px_flat, gpu_px_gour, gpu_px_tex, gpu_px_slow, gpu_spans;
      fprintf(stderr, "gpu spans %llu: pixels flat %llu, gouraud %llu, textured(fast) %llu, generic(semi/mask) %llu\n", (unsigned long long)gpu_spans, (unsigned long long)gpu_px_flat, (unsigned long long)gpu_px_gour, (unsigned long long)gpu_px_tex, (unsigned long long)gpu_px_slow); }
    fprintf(stderr, "mdec macroblocks decoded: %llu\n", (unsigned long long)mdec_blocks);
    machine_dump_state(); h8m_dump();
    if (wav_path) wav_write(wav_path);
    if (card_art_path) {                                                  /* menu card picture: the game'"'"'s own screen, cropped to the card ratio ("G13I" w h + RGBA) */
        static uint8_t cb[1024 * 512 * 4]; int cw = 0, ch = 0; gpu_get_display(cb, &cw, &ch);
        int th = cw * 300 / 360; if (th > ch) th = ch; int y0 = (ch - th) / 2; FILE *cf = fopen(card_art_path, "wb");
        if (cf) { uint32_t hd[3] = { 0x49333147u, (uint32_t)cw, (uint32_t)th }; fwrite(hd, 4, 3, cf); fwrite(cb + (size_t)y0 * cw * 4, 4, (size_t)cw * th, cf); fclose(cf); }
    }
    if (dump) machine_dump_ram(dump);
    if (dump_aux) { char nm[1200]; FILE *af; snprintf(nm, sizeof nm, "%s.shared", dump_aux); if ((af = fopen(nm, "wb"))) { fwrite(G.sharedram, 1, sizeof G.sharedram, af); fclose(af); }
                    snprintf(nm, sizeof nm, "%s.eeprom", dump_aux); if ((af = fopen(nm, "wb"))) { fwrite(G.eeprom, 1, sizeof G.eeprom, af); fclose(af); } }
    if (dump_vram) { FILE *vf = fopen(dump_vram, "wb"); if (vf) { fwrite(gpu_vram(), 2, 1024 * 1024, vf); fclose(vf); } }
    return G.fatal ? 1 : 0;
}
