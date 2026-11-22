/* ROM loader: reads the user's golgo13 ROM files from a directory and builds the board regions exactly as
 * MAME's ROM_START(golgo13) (namcos12.cpp). Every file is verified (size + CRC32). Nothing is embedded. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"
#include "inflate.h"
#include "sys10.h"
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

enum { R_MAIN, R_BANK, R_SUB, R_WAVE, R_NAND };
enum { M_BYTE, M_WSWAP, M_PLAIN };
typedef struct { const char *name; int region; uint32_t off, size; int mode; uint32_t crc; } rom_ent;

static const rom_ent TAB_G13[] = {
    { "glg1vera.2l",   R_MAIN, 0x0000000, 0x200000, M_BYTE, 0xaa15abfe },
    { "glg1vera.2p",   R_MAIN, 0x0000001, 0x200000, M_BYTE, 0x37a4cf90 },
    { "glg1rom0l.10",  R_BANK, 0x0000000, 0x800000, M_BYTE, 0x2837524e },
    { "glg1rom0u.14",  R_BANK, 0x0000001, 0x800000, M_BYTE, 0x4482deff },
    { "glg1rom1l.11",  R_BANK, 0x1000000, 0x800000, M_BYTE, 0x080f3550 },
    { "glg1rom1u.15",  R_BANK, 0x1000001, 0x800000, M_BYTE, 0xb49e8613 },
    { "glg1rom2l.12",  R_BANK, 0x2000000, 0x800000, M_BYTE, 0xe1a79a87 },
    { "glg1rom2u.16",  R_BANK, 0x2000001, 0x800000, M_BYTE, 0xcda22852 },
    { "glg1fl3l.4",    R_BANK, 0x3000000, 0x200000, M_BYTE, 0x16ddc2cb },
    { "glg1fl3u.5",    R_BANK, 0x3000001, 0x200000, M_BYTE, 0xd90d72d1 },
    { "glg1fl4l.6",    R_BANK, 0x3400000, 0x200000, M_BYTE, 0x820d8133 },
    { "glg1fl4u.7",    R_BANK, 0x3400001, 0x200000, M_BYTE, 0x02d78160 },
    { "glg1fl5l.8",    R_BANK, 0x3800000, 0x200000, M_BYTE, 0x090b2508 },
    { "glg1fl5u.9",    R_BANK, 0x3800001, 0x200000, M_BYTE, 0x2231a07c },
    { "glg1vera.11s",  R_SUB,  0x0000000, 0x080000, M_WSWAP, 0x5c33f240 },
    { "glg1wave0.1",   R_WAVE, 0x0000000, 0x800000, M_PLAIN, 0x672d4f7c },
    { "glg1wave1.13",  R_WAVE, 0x0800000, 0x800000, M_PLAIN, 0x480b0a1a },
};

/* Golgo 13 Kiseki no Dandou (GLS1/VER.A): same board and layout as golgo13 (MAME ROM_START g13knd) */
static const rom_ent TAB_KND[] = {
    { "gls1vera.2e",   R_MAIN, 0x0000000, 0x200000, M_BYTE, 0x904c39a7 },
    { "gls1vera.2j",   R_MAIN, 0x0000001, 0x200000, M_BYTE, 0xf8f9d6d2 },
    { "gls1rom0l.ic10", R_BANK, 0x0000000, 0x800000, M_BYTE, 0x91e04e7e },
    { "gls1rom0u.ic14", R_BANK, 0x0000001, 0x800000, M_BYTE, 0x29f67094 },
    { "gls1rom1l.ic11", R_BANK, 0x1000000, 0x800000, M_BYTE, 0xfc69578f },
    { "gls1rom1u.ic15", R_BANK, 0x1000001, 0x800000, M_BYTE, 0xdb46d626 },
    { "gls1rom2l.ic12", R_BANK, 0x2000000, 0x800000, M_BYTE, 0x707cec76 },
    { "gls1rom2u.ic16", R_BANK, 0x2000001, 0x800000, M_BYTE, 0x970abad0 },
    { "gls1fl3l.ic4",  R_BANK, 0x3000000, 0x200000, M_BYTE, 0x3a0ffd17 },
    { "gls1fl3u.ic5",  R_BANK, 0x3000001, 0x200000, M_BYTE, 0x565567ff },
    { "gls1fl4l.ic6",  R_BANK, 0x3400000, 0x200000, M_BYTE, 0xb4591cc0 },
    { "gls1fl4u.ic7",  R_BANK, 0x3400001, 0x200000, M_BYTE, 0x679d545c },
    { "gls1fl5l.ic8",  R_BANK, 0x3800000, 0x200000, M_BYTE, 0xa5581533 },
    { "gls1fl5u.ic9",  R_BANK, 0x3800001, 0x200000, M_BYTE, 0xa8b6832a },
    { "gls1vera.11s",  R_SUB,  0x0000000, 0x080000, M_WSWAP, 0xa1e68d02 },
    { "gls1wave0.ic1", R_WAVE, 0x0000000, 0x800000, M_PLAIN, 0x9de10ef5 },
    { "gls1wave1.ic13", R_WAVE, 0x0800000, 0x800000, M_PLAIN, 0x4c92b6bf },
};

/* Golgo 13 Juusei no Requiem (GLT1/VER.A): Namco System 10 MEM(N)+MEMP3, 6 NAND dumps of 0x1080000 bytes (MAME ROM_START g13jnr) */
static const rom_ent TAB_JNR[] = {
    { "glt1vera_0.2j", R_NAND, 0 * 0x1080000u, 0x1080000, M_PLAIN, 0x24f5bb06 },
    { "glt1vera_1.1j", R_NAND, 1 * 0x1080000u, 0x1080000, M_PLAIN, 0x5ec552da },
    { "glt1vera_2.2h", R_NAND, 2 * 0x1080000u, 0x1080000, M_PLAIN, 0x0835d6ed },
    { "glt1vera_3.1h", R_NAND, 3 * 0x1080000u, 0x1080000, M_PLAIN, 0xf7486979 },
    { "glt1vera_4.2f", R_NAND, 4 * 0x1080000u, 0x1080000, M_PLAIN, 0xe60b526a },
    { "glt1vera_5.1f", R_NAND, 5 * 0x1080000u, 0x1080000, M_PLAIN, 0x0cb2df20 },
};

/* ---------- game registry: every known game is listed; only the ones with supported=1 can run ---------- */
typedef struct { game_info_t info; const rom_ent *tab; int n; const char *probe; int sys10; } game_def_t;
#define NT(t) (int)(sizeof(t) / sizeof((t)[0]))
static const game_def_t GAMES[] = {
    { { "golgo13", "Golgo 13", "GLG1/VER.A", 1999, "Namco System 12", 1, "" }, TAB_G13, NT(TAB_G13), "glg1vera.2l", 0 },
    { { "g13knd", "Golgo 13: Kiseki no Dandou", "GLS1/VER.A", 2000, "Namco System 12", 1, "" }, TAB_KND, NT(TAB_KND), "gls1vera.2e", 0 },
    { { "g13jnr", "Golgo 13: Juusei no Requiem", "GLT1/VER.A", 2001, "Namco System 10 (MP3 board)", 1, "" }, TAB_JNR, NT(TAB_JNR), "glt1vera_0.2j", 1 },
};
static const game_def_t *cur = &GAMES[0];
#define TABLE (cur->tab)
#define NROM (cur->n)
int games_count(void) { return NT(GAMES); }
const game_info_t *game_info(int i) { return i >= 0 && i < NT(GAMES) ? &GAMES[i].info : NULL; }
int game_index(const char *id) { for (int i = 0; i < NT(GAMES); i++) if (!strcmp(GAMES[i].info.id, id)) return i; return -1; }
int rom_select_game(int i) { if (i < 0 || i >= NT(GAMES)) return 1; cur = &GAMES[i]; G.sys10 = cur->sys10; return 0; }
int game_is_sys10(void) { return cur->sys10; }
static int file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }
/* Looks for the ROMs of game i: a folder (user data copy, ./roms/<id>) or a zip (./roms/<id>.zip, ./<id>.zip). 0 = none, 1 = folder, 2 = zip. */
static char g_extra[4][1100]; static int g_nextra;
void rom_add_search_dir(const char *d) { if (d && *d && g_nextra < 4) snprintf(g_extra[g_nextra++], sizeof g_extra[0], "%s", d); }
int game_find_roms(int i, const char *userroms, char *path, size_t n)
{
    if (i < 0 || i >= NT(GAMES)) return 0;
    const game_def_t *g = &GAMES[i]; char c[1100];
    const char *base[8]; int nb = 0; base[nb++] = "roms"; for (int k = 0; k < g_nextra; k++) base[nb++] = g_extra[k]; base[nb++] = userroms;   /* ./roms first, then next to the program, then the data folder */
    for (int k = 0; k < nb; k++) {
        if (!base[k] || !*base[k]) continue;
        snprintf(c, sizeof c, "%s/%s/%s", base[k], g->info.id, g->probe);
        if (file_exists(c)) { snprintf(path, n, "%s/%s", base[k], g->info.id); return 1; }
        snprintf(c, sizeof c, "%s/%s.zip", base[k], g->info.id);
        if (file_exists(c)) { snprintf(path, n, "%s", c); return 2; }
    }
    snprintf(c, sizeof c, "%s.zip", g->info.id); if (file_exists(c)) { snprintf(path, n, "%s", c); return 2; }
    return 0;
}

static uint32_t crc_table[256];
static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1; crc_table[i] = c; }
}
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu; for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8); return ~c;
}

void rom_unload(void)
{
    free(G.rom_main); free(G.rom_bank); free(G.rom_sub); free(G.rom_wave); free(G.nand);
    G.rom_main = G.rom_bank = G.rom_sub = G.rom_wave = G.nand = NULL;
}

int rom_load(const char *dir, char *err, size_t errsz)
{
    crc_init(); rom_unload();
    if (cur->sys10) { G.nand = calloc(6, 0x1080000); G.rom_main = calloc(1, 0x400000); }
    else { G.rom_main = calloc(1, 0x400000);  G.rom_bank = calloc(1, 0x3c00000);
    G.rom_sub  = calloc(1, 0x80000);   G.rom_wave = calloc(1, 0x1000000); }
    uint8_t *reg[5] = { G.rom_main, G.rom_bank, G.rom_sub, G.rom_wave, G.nand };
    if (!G.rom_main || (cur->sys10 ? !G.nand : (!G.rom_bank || !G.rom_sub || !G.rom_wave))) { snprintf(err, errsz, "out of memory"); return 1; }
    for (size_t i = 0; i < NROM; i++) {
        const rom_ent *e = &TABLE[i];
        char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, e->name);
        FILE *f = fopen(path, "rb");
        if (!f) { snprintf(err, errsz, "missing ROM file: %s", e->name); return 2; }
        uint8_t *buf = malloc(e->size);
        size_t n = buf ? fread(buf, 1, e->size, f) : 0;
        int extra = fgetc(f); fclose(f);
        if (n != e->size || extra != EOF) { snprintf(err, errsz, "bad size for %s", e->name); free(buf); return 3; }
        if (crc32(buf, n) != e->crc) { snprintf(err, errsz, "bad CRC32 for %s (not the expected %s dump)", e->name, cur->info.version); free(buf); return 4; }
        uint8_t *d = reg[e->region] + e->off;
        if (e->mode == M_BYTE) { for (uint32_t k = 0; k < e->size; k++) d[(size_t)k * 2] = buf[k]; }
        else if (e->mode == M_WSWAP) { for (uint32_t k = 0; k + 1 < e->size; k += 2) { d[k] = buf[k + 1]; d[k + 1] = buf[k]; } }
        else memcpy(d, buf, e->size);
        free(buf);
    }
    if (cur->sys10) sys10_build_bios();
    return 0;
}

/* ---------- import: copy the verified ROM files from a user folder or zip into the application data folder ---------- */
void fs_mkdir_p(const char *path)
{
    char tmp[1024]; snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) if (*p == '/' || *p == 92) { char c = *p; *p = 0; MKDIR(tmp); *p = c; }
    MKDIR(tmp);
}
static int store(int idx, const uint8_t *buf, size_t n, const char *dest, char *err, size_t es)
{
    const rom_ent *e = &TABLE[idx];
    if (n != e->size) { snprintf(err, es, "bad size for %s", e->name); return 3; }
    if (crc32(buf, n) != e->crc) { snprintf(err, es, "bad CRC32 for %s (not the expected %s dump)", e->name, cur->info.version); return 4; }
    char path[1100]; snprintf(path, sizeof path, "%s/%s", dest, e->name);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(buf, 1, n, f) != n) { snprintf(err, es, "cannot write %s", path); if (f) fclose(f); return 5; }
    fclose(f); return 0;
}
static int find_rom(const char *name)
{
    const char *b = name, *p;
    for (p = name; *p; p++) if (*p == '/' || *p == 92) b = p + 1;
    for (int i = 0; i < NROM; i++) {
        const char *a = TABLE[i].name, *c = b; int ok = 1;
        for (; *a && *c; a++, c++) { char x = *a, y = *c; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) { ok = 0; break; } }
        if (ok && !*a && !*c) return i;
    }
    return -1;
}
static unsigned rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static int import_zip(const char *zip, const char *dest, char *err, size_t es)
{
    FILE *f = fopen(zip, "rb"); if (!f) { snprintf(err, es, "cannot open %s", zip); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); long tail = sz < 70000 ? sz : 70000;
    uint8_t *tb = malloc((size_t)tail); fseek(f, sz - tail, SEEK_SET);
    if (!tb || fread(tb, 1, (size_t)tail, f) != (size_t)tail) { snprintf(err, es, "cannot read zip"); free(tb); fclose(f); return 1; }
    long eo = -1; for (long i = tail - 22; i >= 0; i--) if (rd32(tb + i) == 0x06054b50u) { eo = i; break; }
    if (eo < 0) { snprintf(err, es, "not a zip file"); free(tb); fclose(f); return 1; }
    unsigned nent = rd16(tb + eo + 10); uint32_t cdsz = rd32(tb + eo + 12), cdoff = rd32(tb + eo + 16); free(tb);
    if (cdoff == 0xffffffffu || nent == 0xffffu) { snprintf(err, es, "zip64 is not supported"); fclose(f); return 1; }
    uint8_t *cd = malloc(cdsz); fseek(f, (long)cdoff, SEEK_SET);
    if (!cd || fread(cd, 1, cdsz, f) != cdsz) { snprintf(err, es, "bad zip directory"); free(cd); fclose(f); return 1; }
    int found[64] = {0}, rc = 0; size_t p = 0;
    for (unsigned k = 0; k < nent && !rc; k++) {
        if (p + 46 > cdsz || rd32(cd + p) != 0x02014b50u) { snprintf(err, es, "bad zip directory"); rc = 1; break; }
        unsigned meth = rd16(cd + p + 10), fnl = rd16(cd + p + 28), exl = rd16(cd + p + 30), cml = rd16(cd + p + 32);
        uint32_t csz = rd32(cd + p + 20), usz = rd32(cd + p + 24), lho = rd32(cd + p + 42);
        char name[300]; unsigned nl = fnl < 299 ? fnl : 299; memcpy(name, cd + p + 46, nl); name[nl] = 0;
        p += 46 + fnl + exl + cml;
        int idx = find_rom(name); if (idx < 0) continue;
        uint8_t lh[30]; fseek(f, (long)lho, SEEK_SET);
        if (fread(lh, 1, 30, f) != 30 || rd32(lh) != 0x04034b50u) { snprintf(err, es, "bad zip entry %s", name); rc = 1; break; }
        fseek(f, (long)(lho + 30 + rd16(lh + 26) + rd16(lh + 28)), SEEK_SET);
        uint8_t *cb = malloc(csz ? csz : 1), *ub = malloc(usz ? usz : 1);
        if (!cb || !ub || fread(cb, 1, csz, f) != csz) { snprintf(err, es, "cannot read %s", name); rc = 1; }
        else if (meth == 0 && csz == usz) memcpy(ub, cb, csz);
        else if (meth == 8) { size_t out = 0; if (rt_inflate(cb, csz, ub, usz, &out) || out != usz) { snprintf(err, es, "corrupt data for %s", name); rc = 1; } }
        else { snprintf(err, es, "unsupported zip compression for %s", name); rc = 1; }
        if (!rc) rc = store(idx, ub, usz, dest, err, es);
        if (!rc) found[idx] = 1;
        free(cb); free(ub);
    }
    free(cd); fclose(f);
    if (!rc) for (int i = 0; i < NROM; i++) if (!found[i]) { snprintf(err, es, "missing ROM file in zip: %s", TABLE[i].name); return 2; }
    return rc;
}
static int import_dir(const char *src, const char *dest, char *err, size_t es)
{
    for (int i = 0; i < NROM; i++) {
        char path[1100]; snprintf(path, sizeof path, "%s/%s", src, TABLE[i].name);
        FILE *f = fopen(path, "rb"); if (!f) { snprintf(err, es, "missing ROM file: %s", TABLE[i].name); return 2; }
        uint8_t *b = malloc(TABLE[i].size + 1); size_t n = b ? fread(b, 1, TABLE[i].size + 1, f) : 0; fclose(f);
        int rc = b ? store(i, b, n, dest, err, es) : 1; free(b); if (rc) return rc;
    }
    return 0;
}
/* src: a .zip file or a folder holding the 17 files. On success the files are in dest and rom_load(dest) can be called. */
int rom_import(const char *src, const char *dest, char *err, size_t errsz)
{
    crc_init(); fs_mkdir_p(dest);
    size_t n = strlen(src);
    int is_zip = n > 4 && (src[n-4] == '.') && (src[n-3] | 32) == 'z' && (src[n-2] | 32) == 'i' && (src[n-1] | 32) == 'p';
    return is_zip ? import_zip(src, dest, err, errsz) : import_dir(src, dest, err, errsz);
}

#include <sys/stat.h>
int fs_dir_exists(const char *path) { struct stat st; return stat(path, &st) == 0 && (st.st_mode & S_IFDIR) != 0; }
void fs_abs_path(const char *path, char *out, size_t n)
{
#ifdef _WIN32
    if (!_fullpath(out, path, n)) snprintf(out, n, "%s", path);
#else
    char tmp[4096]; if (realpath(path, tmp)) snprintf(out, n, "%s", tmp); else snprintf(out, n, "%s", path);
#endif
}
