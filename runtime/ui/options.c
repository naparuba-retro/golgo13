#include "options.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_opt[OPT_COUNT]; char g_opt_lang[32] = "", g_opt_lastgame[40] = ""; char g_opt_key[ACT_COUNT][2][32];
static char g_path[1300]; static int dirty; static unsigned long long dirty_t;

static const opt_info_t INFO[OPT_COUNT] = {
    [OPT_FULLSCREEN] = { "fullscreen", "video", 0, 0, 1 }, [OPT_INTEGER] = { "integer_scale", "video", 0, 0, 1 }, [OPT_ASPECT] = { "aspect", "video", 0, 0, 2 },
    [OPT_FILTER] = { "filter", "video", 1, 0, 1 }, [OPT_SCANLINES] = { "crt_scanlines", "video", 0, 0, 100 }, [OPT_SHOWFPS] = { "show_fps", "video", 0, 0, 1 },
    [OPT_VSYNC] = { "vsync", "video", 1, 0, 1 }, [OPT_VOLUME] = { "volume", "audio", 100, 0, 100 }, [OPT_MUTE] = { "mute", "audio", 0, 0, 1 },
    [OPT_WIIMOTE] = { "enable", "wiimote", 0, 0, 1 }, [OPT_SMOOTH] = { "smoothing", "wiimote", 0, 0, 100 }, [OPT_DEADZONE] = { "deadzone", "wiimote", 0, 0, 20 },
    [OPT_RECORD] = { "record", "game", 1, 0, 1 }, [OPT_START_LAST] = { "start_last", "game", 0, 0, 1 }, [OPT_RETICLE] = { "reticle", "game", 1, 0, 1 },
    [OPT_FREECREDIT] = { "free_credits", "game", 1, 0, 1 }, [OPT_TRIGSTART] = { "trigger_is_start", "game", 1, 0, 1 },
};
static const char *ACT[ACT_COUNT] = { "fire", "sensor", "enter", "up", "down", "start", "coin", "service", "test" };
const opt_info_t *opt_info(int id) { return &INFO[id]; }
const char *opt_action_name(int a) { return ACT[a]; }

static void trim(char *s) { size_t k = strlen(s); while (k && (s[k - 1] == '\n' || s[k - 1] == '\r' || s[k - 1] == ' ')) s[--k] = 0; }
static void now_set(void) { dirty = 1; dirty_t = 0; }

void options_init(const char *ini_path)
{
    snprintf(g_path, sizeof g_path, "%s", ini_path);
    for (int i = 0; i < OPT_COUNT; i++) g_opt[i] = INFO[i].def;
    g_opt_lang[0] = 0; g_opt_lastgame[0] = 0; memset(g_opt_key, 0, sizeof g_opt_key);
    FILE *f = fopen(g_path, "rb"); if (!f) return; char line[300], sec[24] = "";
    while (fgets(line, sizeof line, f)) {
        trim(line); if (!line[0] || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') { char *e = strchr(line, ']'); if (e) { *e = 0; snprintf(sec, sizeof sec, "%s", line + 1); } continue; }
        char *eq = strchr(line, '='); if (!eq) continue; *eq = 0; const char *k = line, *v = eq + 1;
        if (!strcmp(k, "lang") && (!sec[0] || !strcmp(sec, "game"))) { snprintf(g_opt_lang, sizeof g_opt_lang, "%s", v); continue; }
        if (!strcmp(sec, "game") && !strcmp(k, "last")) { snprintf(g_opt_lastgame, sizeof g_opt_lastgame, "%s", v); continue; }
        if (!strcmp(sec, "keys")) {
            for (int a = 0; a < ACT_COUNT; a++) for (int sl = 0; sl < 2; sl++) { char n[32]; snprintf(n, sizeof n, "%s_%d", ACT[a], sl + 1); if (!strcmp(k, n)) snprintf(g_opt_key[a][sl], 32, "%s", v); }
            continue;
        }
        for (int i = 0; i < OPT_COUNT; i++) if (!strcmp(INFO[i].section, sec) && !strcmp(INFO[i].key, k)) {
            int x = atoi(v); if (x < INFO[i].min) x = INFO[i].min; if (x > INFO[i].max) x = INFO[i].max; g_opt[i] = x;
        }
    }
    fclose(f);
}
void options_set(int id, int v) { if (v < INFO[id].min) v = INFO[id].min; if (v > INFO[id].max) v = INFO[id].max; if (g_opt[id] != v) { g_opt[id] = v; now_set(); } }
void options_set_lang(const char *c) { if (strcmp(g_opt_lang, c)) { snprintf(g_opt_lang, sizeof g_opt_lang, "%s", c); now_set(); } }
void options_set_lastgame(const char *id) { if (strcmp(g_opt_lastgame, id)) { snprintf(g_opt_lastgame, sizeof g_opt_lastgame, "%s", id); now_set(); } }
void options_set_key(int a, int s, const char *n) { snprintf(g_opt_key[a][s], 32, "%s", n); now_set(); }
void options_reset_keys(void) { memset(g_opt_key, 0, sizeof g_opt_key); now_set(); }

void options_flush(int force)
{
    if (!dirty) return;
    if (!force && ++dirty_t < 30) return;                      /* ~0.5 s of quiet (called once per UI frame) */
    char tmp[1320]; snprintf(tmp, sizeof tmp, "%s.tmp", g_path); FILE *f = fopen(tmp, "wb"); if (!f) return;
    fprintf(f, "; settings.ini : only the values that differ from the defaults are written (edited by the in-game menu).\n");
    const char *secs[] = { "video", "audio", "wiimote", "game" };
    for (int s = 0; s < 4; s++) {
        int any = 0;
        for (int i = 0; i < OPT_COUNT; i++) if (!strcmp(INFO[i].section, secs[s]) && g_opt[i] != INFO[i].def) { if (!any) fprintf(f, "[%s]\n", secs[s]); any = 1; fprintf(f, "%s=%d\n", INFO[i].key, g_opt[i]); }
        if (s == 3) {
            if (g_opt_lang[0] && strcmp(g_opt_lang, "original") != 0 ? 1 : (g_opt_lang[0] != 0)) { if (!any) fprintf(f, "[game]\n"); any = 1; fprintf(f, "lang=%s\n", g_opt_lang); }
            if (g_opt_lastgame[0]) { if (!any) fprintf(f, "[game]\n"); any = 1; fprintf(f, "last=%s\n", g_opt_lastgame); }
        }
    }
    int anyk = 0;
    for (int a = 0; a < ACT_COUNT; a++) for (int sl = 0; sl < 2; sl++) if (g_opt_key[a][sl][0]) { if (!anyk) fprintf(f, "[keys]\n"); anyk = 1; fprintf(f, "%s_%d=%s\n", ACT[a], sl + 1, g_opt_key[a][sl]); }
    fclose(f);
    remove(g_path); if (rename(tmp, g_path) != 0) { remove(tmp); return; }
    dirty = 0;
}
