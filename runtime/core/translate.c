#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "machine.h"
#include "translate.h"
#include "gpu.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

typedef struct { int region; uint32_t off, len; uint8_t *orig; } applied_t;
static applied_t *g_applied; static int napplied, capplied;

static uint32_t tcrc_table[256]; static int tcrc_init_done;
static uint32_t tcrc32(const uint8_t *p, size_t n)
{
    if (!tcrc_init_done) {
        for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1; tcrc_table[i] = c; }
        tcrc_init_done = 1;
    }
    uint32_t c = 0xffffffffu; for (size_t i = 0; i < n; i++) c = tcrc_table[(c ^ p[i]) & 0xff] ^ (c >> 8); return ~c;
}
static uint32_t rd32le(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

int translate_apply_file(const char *path, int *skipped, char *msg, size_t msgsz)
{
    FILE *f = fopen(path, "rb"); int applied = 0, skip = 0;
    if (!f) { snprintf(msg, msgsz, "cannot open %s", path); return -1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(sz > 0 ? (size_t)sz : 1);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) { snprintf(msg, msgsz, "cannot read %s", path); free(buf); fclose(f); return -1; }
    fclose(f);
    if (sz < 12 || memcmp(buf, "G13T", 4) || rd32le(buf + 4) != 1) { snprintf(msg, msgsz, "%s is not a G13T v1 patch", path); free(buf); return -1; }
    uint32_t count = rd32le(buf + 8); size_t p = 12;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 16 > (size_t)sz) { snprintf(msg, msgsz, "%s is truncated", path); free(buf); return -1; }
        uint32_t region = rd32le(buf + p), off = rd32le(buf + p + 4), len = rd32le(buf + p + 8), crc = rd32le(buf + p + 12); p += 16;
        if (p + len > (size_t)sz) { snprintf(msg, msgsz, "%s is truncated", path); free(buf); return -1; }
        uint8_t *base = region == 0 ? G.rom_bank : region == 1 ? G.rom_main : NULL;
        size_t size = region == 0 ? 0x3c00000u : region == 1 ? 0x400000u : 0;
        if (!base || (size_t)off + len > size || tcrc32(base + off, len) != crc) skip++;       /* wrong ROM or already patched */
        else {
            if (napplied == capplied) { capplied = capplied ? capplied * 2 : 1024; g_applied = (applied_t *)realloc(g_applied, capplied * sizeof(applied_t)); }
            g_applied[napplied].region = (int)region; g_applied[napplied].off = off; g_applied[napplied].len = len; g_applied[napplied].orig = (uint8_t *)malloc(len); memcpy(g_applied[napplied].orig, base + off, len); napplied++;
            memcpy(base + off, buf + p, len); applied++;
        }
        p += len;
    }
    free(buf);
    if (skipped) *skipped = skip;
    snprintf(msg, msgsz, "translation %s: %d record(s) applied%s", path, applied, skip ? ", some skipped (ROM bytes differ from the ones the patch was built for)" : "");
    return applied;
}

int translate_apply_lang(const char *lang, const char *const *dirs, int ndirs, char *msg, size_t msgsz)
{
    for (int i = 0; i < ndirs; i++) {
        if (!dirs[i]) continue;
        char path[1200]; snprintf(path, sizeof path, "%s/%s/patch.g13t", dirs[i], lang);
        char opath[1200]; snprintf(opath, sizeof opath, "%s/%s/overlay.g13o", dirs[i], lang);
        FILE *f = fopen(path, "rb"), *g = fopen(opath, "rb");
        if (!f && !g) continue;
        if (f) fclose(f);
        int n = 0, sk = 0; msg[0] = 0;
        if (f) n = translate_apply_file(path, &sk, msg, msgsz);
        if (g) { fclose(g); char omsg[300]; int k = gpu_overlay_load(opath, omsg, sizeof omsg); size_t l = strlen(msg); snprintf(msg + l, msgsz - l, "%s%s", l ? "; " : "", omsg); if (n < 0 && k >= 0) n = 0; }
        return n;
    }
    snprintf(msg, msgsz, "no translation '%s' found (looked for <dir>/%s/patch.g13t)", lang, lang);
    return -1;
}

void translate_revert(void)
{
    for (int i = 0; i < napplied; i++) {
        uint8_t *base = g_applied[i].region == 0 ? G.rom_bank : g_applied[i].region == 1 ? G.rom_main : NULL;
        if (base) memcpy(base + g_applied[i].off, g_applied[i].orig, g_applied[i].len);
        free(g_applied[i].orig);
    }
    napplied = 0; gpu_overlay_clear();
}

static int exists(const char *p) { FILE *f = fopen(p, "rb"); if (!f) return 0; fclose(f); return 1; }
static void add_lang(const char *dir, const char *code, translate_lang_t *out, int *n, int max)
{
    if (*n >= max || !code[0] || code[0] == '.') return;
    for (int i = 0; i < *n; i++) if (!strcmp(out[i].code, code)) return;
    char a[1200], b[1200], c[1200]; snprintf(a, sizeof a, "%s/%s/patch.g13t", dir, code); snprintf(b, sizeof b, "%s/%s/overlay.g13o", dir, code);
    if (!exists(a) && !exists(b)) return;
    translate_lang_t *l = &out[(*n)++]; snprintf(l->code, sizeof l->code, "%s", code); snprintf(l->name, sizeof l->name, "%s", code);
    snprintf(c, sizeof c, "%s/%s/info.txt", dir, code);
    FILE *f = fopen(c, "rb");
    if (f) { char line[80] = ""; if (fgets(line, sizeof line, f)) { size_t k = strlen(line); while (k && (line[k - 1] == 10 || line[k - 1] == 13)) line[--k] = 0; if (k) snprintf(l->name, sizeof l->name, "%s", line); } fclose(f); }
}
int translate_list(const char *const *dirs, int ndirs, translate_lang_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < ndirs; i++) {
        if (!dirs[i]) continue;
#ifdef _WIN32
        char pat[1200]; snprintf(pat, sizeof pat, "%s/*", dirs[i]); WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do { if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) add_lang(dirs[i], fd.cFileName, out, &n, max); } while (FindNextFileA(h, &fd));
        FindClose(h);
#else
        DIR *d = opendir(dirs[i]); if (!d) continue; struct dirent *e;
        while ((e = readdir(d))) add_lang(dirs[i], e->d_name, out, &n, max);
        closedir(d);
#endif
    }
    return n;
}
