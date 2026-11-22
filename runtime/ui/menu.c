/* Launcher menu: game selector, options, pause. Immediate-mode: every frame the page redraws itself and registers its clickable items;
 * input (keyboard, pad, mouse, Wiimote pointer) is processed against the items of the previous frame. */
#include "menu.h"
#include "ui.h"
#include "options.h"
#include "platform.h"
#include "machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INK UI_RGB(16, 14, 18)
#define PAPER UI_RGB(244, 238, 220)
#define PAPER2 UI_RGB(190, 175, 140)
#define RED UI_RGB(214, 28, 36)
#define YEL UI_RGB(255, 208, 40)
#define NAVY UI_RGB(14, 20, 46)
#define GREEN UI_RGB(20, 140, 60)

/* ---------------------------------------------------------------- strings (English / French) */
enum { S_CHOOSE, S_TAGLINE, S_OPTIONS, S_ROMS_OK, S_ROMS_MISSING, S_NOT_PLAYABLE, S_PLAY, S_MOVE, S_PICK, S_QUIT, S_BACK, S_CHANGE, S_TAB, S_CLOSE,
       S_T_IMAGE, S_T_SOUND, S_T_CONTROLS, S_T_WIIMOTE, S_T_LANG, S_T_GAME, S_FULLSCREEN, S_INTEGER, S_ASPECT, S_ASPECT43, S_ASPECTFILL, S_FILTER, S_SHARP, S_SMOOTH,
       S_SCAN, S_FPS, S_VSYNC, S_VOLUME, S_MUTE, S_WM_ENABLE, S_WM_STATE, S_WM_OFF, S_WM_SEARCH, S_WM_OK, S_SMOOTHING, S_DEADZONE, S_PAIR, S_CALIB, S_LANG_ORIG,
       S_START_LAST, S_RECORD, S_RETICLE, S_RESET_KEYS, S_PRESS_KEY, S_ACT_FIRE, S_ACT_SENSOR, S_ACT_ENTER, S_ACT_UP, S_ACT_DOWN, S_ACT_START, S_ACT_COIN, S_ACT_SERVICE, S_ACT_TEST,
       S_PAUSE, S_RESUME, S_CHANGE_GAME, S_RESET, S_QUIT_GAME, S_SURE, S_YES, S_NO, S_SUB_OPT, S_SUB_CTRL, S_ROMHELP, S_KEYHINT, S_PAIRING, S_PLAYBTN, S_PICKED, S_FREE, S_TRIGSTART, S_N };
static const char *STR[S_N][2] = {
    [S_CHOOSE] = { "CHOOSE YOUR MISSION", "CHOISIS TA MISSION" }, [S_TAGLINE] = { "One game, one target, one shot.", "Un jeu, une cible, un seul tir." },
    [S_OPTIONS] = { "OPTIONS", "OPTIONS" }, [S_ROMS_OK] = { "ROMS OK", "ROMS OK" }, [S_ROMS_MISSING] = { "NO ROMS", "ROMS MANQUANTES" },
    [S_NOT_PLAYABLE] = { "NOT PLAYABLE YET", "PAS ENCORE JOUABLE" }, [S_PLAY] = { "Enter / A: PLAY", "Entrée / A : JOUER" }, [S_MOVE] = { "move", "choisir" },
    [S_PICK] = { "play", "jouer" }, [S_QUIT] = { "quit", "quitter" }, [S_BACK] = { "back", "retour" }, [S_CHANGE] = { "change", "changer" }, [S_TAB] = { "tab", "onglet" }, [S_CLOSE] = { "close", "fermer" },
    [S_T_IMAGE] = { "VIDEO", "IMAGE" }, [S_T_SOUND] = { "SOUND", "SON" }, [S_T_CONTROLS] = { "CONTROLS", "COMMANDES" }, [S_T_WIIMOTE] = { "WIIMOTE", "WIIMOTE" },
    [S_T_LANG] = { "LANGUAGE", "LANGUE" }, [S_T_GAME] = { "GAME", "JEU" },
    [S_FULLSCREEN] = { "Fullscreen", "Plein écran" }, [S_INTEGER] = { "Integer scaling", "Mise à l'échelle entière" }, [S_ASPECT] = { "Picture shape", "Forme de l'image" },
    [S_ASPECT43] = { "4:3 (original)", "4:3 (original)" }, [S_ASPECTFILL] = { "Fill window", "Remplir la fenêtre" }, [S_FILTER] = { "Filter", "Filtre" },
    [S_SHARP] = { "Sharp pixels", "Pixels nets" }, [S_SMOOTH] = { "Smooth", "Lissé" }, [S_SCAN] = { "CRT scanlines", "Lignes de balayage (CRT)" }, [S_FPS] = { "Show frames per second", "Afficher les images / seconde" },
    [S_VSYNC] = { "Vertical sync", "Synchronisation verticale" }, [S_VOLUME] = { "Volume", "Volume" }, [S_MUTE] = { "Mute", "Couper le son" },
    [S_WM_ENABLE] = { "Use the Wiimote", "Utiliser la Wiimote" }, [S_WM_STATE] = { "Status", "État" }, [S_WM_OFF] = { "off", "désactivée" }, [S_WM_SEARCH] = { "press a button / 1+2", "appuie sur un bouton / 1+2" },
    [S_WM_OK] = { "connected", "connectée" }, [S_SMOOTHING] = { "Smoothing", "Lissage du viseur" }, [S_DEADZONE] = { "Dead zone", "Zone morte" }, [S_PAIR] = { "PAIR A WIIMOTE", "APPAIRER" }, [S_CALIB] = { "CALIBRATE", "CALIBRER" },
    [S_LANG_ORIG] = { "Original (Japanese)", "Original (japonais)" }, [S_START_LAST] = { "Start the last game directly", "Lancer directement le dernier jeu" },
    [S_RECORD] = { "Record sessions (replays)", "Enregistrer les sessions (replays)" }, [S_RETICLE] = { "Show the crosshair", "Afficher le viseur" },
    [S_RESET_KEYS] = { "[ Reset keys ]", "[ Réinitialiser les touches ]" }, [S_PRESS_KEY] = { "press a key...", "appuie sur une touche..." },
    [S_ACT_FIRE] = { "Fire (trigger)", "Tir (gâchette)" }, [S_ACT_SENSOR] = { "Scope (sensor)", "Lunette (capteur)" }, [S_ACT_ENTER] = { "Confirm", "Valider" },
    [S_ACT_UP] = { "Up", "Haut" }, [S_ACT_DOWN] = { "Down", "Bas" }, [S_ACT_START] = { "Start", "Start" }, [S_ACT_COIN] = { "Coin", "Pièce" }, [S_ACT_SERVICE] = { "Service", "Service" }, [S_ACT_TEST] = { "Test", "Test" },
    [S_PAUSE] = { "PAUSE", "PAUSE" }, [S_RESUME] = { "RESUME", "REPRENDRE" }, [S_CHANGE_GAME] = { "CHANGE GAME", "CHANGER DE JEU" }, [S_RESET] = { "RESTART", "RÉINITIALISER" }, [S_QUIT_GAME] = { "QUIT", "QUITTER" },
    [S_SURE] = { "Quit for real?", "Quitter pour de bon ?" }, [S_YES] = { "YES", "OUI" }, [S_NO] = { "NO", "NON" },
    [S_SUB_OPT] = { "Rifle settings... and more.", "Réglages du fusil... et le reste." }, [S_SUB_CTRL] = { "Mouse, keyboard, pad, Wiimote.", "Souris, clavier, manette." },
    [S_ROMHELP] = { "Put the ROM folder or zip in the data folder (roms/<game>).", "Mets le dossier ou le zip des ROM dans le dossier de données (roms/<jeu>)." },
    [S_PLAYBTN] = { "PLAY", "JOUER" }, [S_PICKED] = { "Now press PLAY", "Appuie maintenant sur JOUER" },
    [S_FREE] = { "Credits always at maximum", "Crédits toujours au maximum" }, [S_TRIGSTART] = { "Trigger also acts as Start", "La gâchette sert aussi de Start" },
    [S_KEYHINT] = { "Enter: change a key   Del: clear", "Entrée : changer une touche   Suppr : effacer" }, [S_PAIRING] = { "Pairing... press 1+2 on the Wiimote", "Appairage... appuie sur 1+2 sur la Wiimote" },
};
static char g_uilang[32];
void menu_set_ui_lang(const char *code) { snprintf(g_uilang, sizeof g_uilang, "%s", code ? code : ""); }
static int fr(void) { return !strcmp(g_uilang[0] ? g_uilang : g_opt_lang, "fr"); }
static const char *T(int id) { return STR[id][fr() ? 1 : 0]; }

/* ---------------------------------------------------------------- state */
static uint32_t *g_buf; static uint32_t g_bg[UI_W * UI_H]; static int g_bg_ok; static int g_tick;
static char g_dirs[4][1200]; static const char *g_dirp[4]; static int g_nd;
static ui_image_t *g_card[8], *g_notice;
typedef struct { float x, y, w, h; int kind, a, b; const char *desc; } item_t;
#define NTABS 6
enum { IK_BTN, IK_CHECK, IK_CHOICE, IK_SLIDER, IK_KEY, IK_CARD, IK_TAB, IK_LANG, IK_INFO };
static item_t it[96]; static int nit, focus; static const menu_ctx_t *g_ctx;
static int g_mx, g_my, g_mouse_in, g_moved, g_click, g_rclick; static int g_nav[24], g_nnav; static int g_raw[8], g_nraw;
static int g_quit, g_tab, g_capturing = -1; static char g_msg[200]; static int g_msg_t;
static const uint8_t *g_pause_img; static int g_pause_w, g_pause_h;

void menu_init(const char *exe_dir, const char *pref_dir)
{
    g_nd = 0;
    if (exe_dir && *exe_dir) snprintf(g_dirs[g_nd++], 1200, "%s/data/ui", exe_dir);
    snprintf(g_dirs[g_nd++], 1200, "data/ui");
    if (pref_dir && *pref_dir) snprintf(g_dirs[g_nd++], 1200, "%sui", pref_dir);
    for (int i = 0; i < g_nd; i++) g_dirp[i] = g_dirs[i];
    ui_load_fonts(g_dirp, g_nd);
    for (int d = 0; d < g_nd && !g_notice; d++) { char p[1300]; snprintf(p, sizeof p, "%s/cards/notice.rgba", g_dirs[d]); g_notice = ui_image_load(p); }
    for (int i = 0; i < games_count() && i < 8; i++) {
        for (int d = 0; d < g_nd && !g_card[i]; d++) { char p[1300]; snprintf(p, sizeof p, "%s/cards/%s.rgba", g_dirs[d], game_info(i)->id); g_card[i] = ui_image_load(p); }
    }
}
int menu_available(void) { return ui_has_fonts(); }

void menu_apply_options(void)
{
    plat_video_t v = { g_opt[OPT_FULLSCREEN], g_opt[OPT_INTEGER], g_opt[OPT_ASPECT], g_opt[OPT_FILTER], g_opt[OPT_SCANLINES], g_opt[OPT_SHOWFPS], g_opt[OPT_VSYNC], g_opt[OPT_RETICLE] };
    plat_video_set(&v); plat_audio_set(g_opt[OPT_VOLUME], g_opt[OPT_MUTE]); plat_wiimote_params(g_opt[OPT_SMOOTH], g_opt[OPT_DEADZONE]);
    for (int a = 0; a < ACT_COUNT; a++) for (int s = 0; s < 2; s++) plat_bind(a, s, g_opt_key[a][s]);
}
void menu_tick_flush(void) { options_flush(0); }

/* ---------------------------------------------------------------- drawing helpers */
static void draw_bg(void)
{
    if (!g_bg_ok) {
        uint32_t *save = g_buf; g_buf = g_bg; ui_begin(g_bg); ui_clip_none();
        ui_fill_rect(0, 0, UI_W, UI_H, NAVY);
        ui_halftone(0, 0, UI_W, UI_H, UI_RGB(30, 44, 96), 14, 45, 3, 0.15f, 0.95f);
        ui_speed_lines(UI_W * 0.5f, UI_H * 0.45f, UI_RGBA(255, 255, 255, 14), 70, 3, 0.12f);
        unsigned s = 11; int x = 0;
        #define R(n) (s = s * 1664525u + 1013904223u, (int)((s >> 8) % (unsigned)(n)))
        while (x < UI_W) {
            int bw = 28 + R(42), bh = 30 + R(90); ui_fill_rect((float)x, (float)(UI_H - bh), (float)bw, (float)bh, UI_RGB(8, 10, 24));
            for (int yy = UI_H - bh + 8; yy < UI_H - 6; yy += 14) for (int xx = x + 5; xx < x + bw - 6; xx += 11) if (R(100) < 16) ui_fill_rect((float)xx, (float)yy, 4, 6, UI_RGB(255, 214, 90));
            x += bw + R(6);
        }
        g_buf = save; g_bg_ok = 1;
    }
    ui_begin(g_buf); ui_clip_none(); memcpy(g_buf, g_bg, sizeof g_bg);
}
static void text_o(int face, int size, float x, float y, uint32_t c, const char *s, int anchor, int ow) { ui_text_outlined(face, size, x, y, c, INK, ow, s, anchor); }

static void header(const char *title, const char *sub)
{
    float X[4] = { 30, 620, 580, 30 }, Y[4] = { 22, 22, 96, 96 }; ui_fill_quad(X, Y, INK);
    float X2[4] = { 26, 610, 570, 26 }, Y2[4] = { 18, 18, 90, 90 }; ui_fill_quad(X2, Y2, RED);
    text_o(UI_TITLE, 54, 52, 24, PAPER, title, UI_LEFT, 3);
    if (sub) ui_text(UI_BODY, 22, 640, 48, PAPER, sub, UI_LEFT);
    text_o(UI_TITLE, 72, UI_W - 40, 8, YEL, "13", UI_RIGHT, 4); text_o(UI_TITLE, 46, UI_W - 130, 22, PAPER, "GOLGO", UI_RIGHT, 3);
}
static void hint_bar(const char *const *kv, int n)           /* pairs: key, label */
{
    ui_fill_rect(0, UI_H - 44, UI_W, 44, INK); float x = 24;
    for (int i = 0; i + 1 < n * 2; i += 2) {
        float w = (float)ui_text_width(UI_TITLE, 18, kv[i]) + 20; ui_fill_para(x, UI_H - 39, w, 31, 0, YEL);
        ui_text(UI_TITLE, 18, x + 10, UI_H - 38, INK, kv[i], UI_LEFT); ui_text(UI_BODY, 18, x + w + 8, UI_H - 36, PAPER, kv[i + 1], UI_LEFT);
        x += w + 12 + ui_text_width(UI_BODY, 18, kv[i + 1]) + 26;
    }
}
static void panel(float x, float y, float w, float h, float skew, uint32_t col, int dots)       /* comic panel: hard shadow, thick border */
{
    ui_fill_para(x + 12, y + 12, w, h, skew, INK); ui_fill_para(x - 7, y - 7, w + 14, h + 14, skew, INK);
    ui_fill_para(x, y, w, h, skew, col);
    if (dots) { float X[4], Y[4]; ui_para(x, y, w, h, skew, X, Y); ui_clip_quad(X, Y); ui_halftone(x, y, w, h, PAPER2, 12, 45, 2, 0.0f, 0.3f); ui_clip_none(); }
}
static int reg(float x, float y, float w, float h, int kind, int a, int b, const char *desc)
{
    if (nit < 96) { it[nit] = (item_t){ x, y, w, h, kind, a, b, desc }; return nit++; }
    return 95;
}
static void focus_row(float x, float y, float w, float h)
{
    float X[4] = { x + 12, x + w, x + w - 12, x }, Y[4] = { y, y, y + h, y + h }; ui_fill_quad(X, Y, YEL); ui_stroke_quad(X, Y, 3, INK);
}
static void bubble(float x, float y, float w, float h, const char *text, float tailx)
{
    ui_fill_para(x + 5, y + 5, w, h, 0, INK); float X[3] = { x + 40, x + 90, tailx }, Y[3] = { y + h - 4, y + h - 4, y + h + 26 };
    float TX[4] = { X[0], X[1], X[2], X[2] }, TY[4] = { Y[0], Y[1], Y[2], Y[2] }; ui_fill_quad(TX, TY, INK);
    ui_fill_para(x - 4, y - 4, w + 8, h + 8, 0, INK); ui_fill_para(x, y, w, h, 0, PAPER);
    ui_text(UI_BODY, 22, x + 18, y + (h - ui_text_height(UI_BODY, 22)) / 2.0f, INK, text, UI_LEFT);
}

/* ---------------------------------------------------------------- rows of the options sheet */
static float row_y, row_x0, row_w;
static void rows_begin(float y) { row_y = y; row_x0 = 392; row_w = 790; }
static int row_open(const char *label, int kind, int a, int b, const char *desc)
{
    int i = reg(row_x0, row_y - 4, row_w, 50, kind, a, b, desc);
    if (focus == i) focus_row(row_x0 - 8, row_y - 4, row_w, 50);
    ui_text(UI_BODY, 24, row_x0 + 18, row_y + 6, INK, label, UI_LEFT); return i;
}
static void row_check(const char *label, int opt, const char *desc)
{
    row_open(label, IK_CHECK, opt, 0, desc); float x = row_x0 + row_w - 70;
    ui_fill_para(x, row_y + 4, 30, 30, 0, INK); ui_fill_para(x + 3, row_y + 7, 24, 24, 0, PAPER);
    if (g_opt[opt]) { ui_stroke_quad((float[]){ x + 7, x + 14, x + 14, x + 7 }, (float[]){ row_y + 20, row_y + 28, row_y + 28, row_y + 20 }, 5, RED); ui_stroke_quad((float[]){ x + 14, x + 26, x + 26, x + 14 }, (float[]){ row_y + 28, row_y + 10, row_y + 10, row_y + 28 }, 5, RED); }
    row_y += 56;
}
static void row_choice(const char *label, int opt, const char *const *vals, int n, const char *desc)
{
    row_open(label, IK_CHOICE, opt, n, desc); char t[80]; snprintf(t, sizeof t, "%s  >", vals[g_opt[opt] % n]);
    ui_text(UI_TITLE, 28, row_x0 + row_w - 40, row_y + 3, RED, t, UI_RIGHT); row_y += 56;
}
static void row_slider(const char *label, int opt, const char *desc)
{
    int i = row_open(label, IK_SLIDER, opt, 0, desc); float x = row_x0 + row_w - 330, w = 230, v = (float)(g_opt[opt] - opt_info(opt)->min) / (float)(opt_info(opt)->max - opt_info(opt)->min);
    it[i].x = x - 14; it[i].w = w + 28; (void)i;
    ui_fill_para(x, row_y + 12, w, 14, 0, INK); ui_fill_para(x + 3, row_y + 15, (w - 6) * v, 8, 0, RED);
    float kx = x + w * v; ui_fill_para(kx - 13, row_y + 6, 26, 26, 0, INK); ui_fill_para(kx - 10, row_y + 9, 20, 20, 0, PAPER);
    char t[16]; snprintf(t, sizeof t, "%d", g_opt[opt]); ui_text(UI_TITLE, 24, row_x0 + row_w - 30, row_y + 5, INK, t, UI_RIGHT); row_y += 56;
}
static void row_info(const char *label, const char *val)
{
    reg(row_x0, row_y - 4, row_w, 50, IK_INFO, 0, 0, NULL); ui_text(UI_BODY, 24, row_x0 + 18, row_y + 6, INK, label, UI_LEFT); ui_text(UI_TITLE, 28, row_x0 + row_w - 40, row_y + 3, RED, val, UI_RIGHT); row_y += 56;
}
static void row_button(const char *label, int id, const char *desc)
{
    int i = reg(row_x0 + 100, row_y, row_w - 200, 46, IK_BTN, id, 0, desc); float x = row_x0 + 100, w = row_w - 200;
    ui_fill_para(x + 7, row_y + 7, w, 46, 14, INK); ui_fill_para(x, row_y, w, 46, 14, focus == i ? YEL : PAPER);
    ui_stroke_para(x, row_y, w, 46, 14, 4, INK); ui_text(UI_TITLE, 30, x + w / 2, row_y + 4, INK, label, UI_CENTER); row_y += 62;
}

/* ---------------------------------------------------------------- pages */
enum { PG_SELECT, PG_OPTIONS, PG_PAUSE };
enum { BT_OPTIONS = 1, BT_RESUME, BT_CHANGE, BT_RESET, BT_QUIT, BT_YES, BT_NO, BT_PAIR, BT_CALIB, BT_RESETKEYS, BT_PLAY, BT_DONE };
static int g_picked = -1;
static int g_confirm_quit;
static int options_last_index(void) { return g_opt_lastgame[0] ? game_index(g_opt_lastgame) : -1; }
typedef struct { const char *id, *name; uint32_t accent; } gstyle_t;
static const gstyle_t GST[] = { { "golgo13", "GOLGO 13", UI_RGB(214, 28, 36) }, { "g13knd", "LE MIRACLE", UI_RGB(20, 160, 140) }, { "g13jnr", "REQUIEM", UI_RGB(230, 140, 20) } };
static const char *g_userroms; static int g_roms[8];

static void card(int gi, float x, float y, int focused)
{
    const game_info_t *g = game_info(gi); const gstyle_t *st = NULL; for (unsigned k = 0; k < sizeof GST / sizeof *GST; k++) if (!strcmp(GST[k].id, g->id)) st = &GST[k];
    uint32_t acc = st ? st->accent : UI_RGB(120, 120, 140); char name[48]; snprintf(name, sizeof name, "%s", st ? st->name : g->title);
    float w = 360, h = 470, sk = 26; int ok = g_roms[gi] != 0 && g->supported;
    if (focused) y -= 14;
    float X[4], Y[4]; ui_fill_para(x + (focused ? 14 : 9), y + (focused ? 14 : 9), w, h, sk, INK); ui_fill_para(x - 7, y - 7, w + 14, h + 14, sk, focused ? YEL : INK);
    if (focused) ui_fill_para(x - 3, y - 3, w + 6, h + 6, sk, INK);
    ui_para(x, y, w, h, sk, X, Y); ui_fill_quad(X, Y, PAPER); ui_clip_quad(X, Y);
    ui_halftone(x, y, w, h, acc, 10, 30, 1, 0.0f, 0.55f);
    if (g_card[gi]) ui_image_draw(g_card[gi], x, y, w, 300, !ok);
    else { ui_fill_rect(x, y, w, 300, UI_RGB(20, 24, 48)); ui_halftone(x, y, w, 300, acc, 9, 45, 3, 0.1f, 0.9f); text_o(UI_TITLE, 72, x + w / 2, y + 100, PAPER, "13", UI_CENTER, 4); }
    ui_fill_rect(x, y + 300, w, 6, INK); ui_fill_rect(x, y + 306, w, 130, PAPER);
    float PX[4] = { x, x + w, x + w, x }, PY[4] = { y + h - 36, y + h - 52, y + h, y + h }; ui_fill_quad(PX, PY, acc);
    text_o(UI_TITLE, 46, x + 18, y + 312, PAPER, name, UI_LEFT, 3);
    ui_text(UI_BODY, 20, x + 20, y + 366, INK, g->title, UI_LEFT);
    char b[100], bd[60]; snprintf(bd, sizeof bd, "%s", g->board); { char *pp = strstr(bd, " ("); if (pp) *pp = 0; } snprintf(b, sizeof b, "%s  -  %d", bd, g->year); ui_text(UI_BODY, 16, x + 20, y + 396, UI_RGB(60, 60, 70), b, UI_LEFT);
    ui_clip_none(); ui_stroke_quad(X, Y, 4, INK); if (gi == g_picked) ui_stroke_quad(X, Y, 8, RED);
    const char *stt = ok ? T(S_ROMS_OK) : !g->supported ? T(S_NOT_PLAYABLE) : T(S_ROMS_MISSING); uint32_t sc = ok ? GREEN : RED;
    int sw = ui_text_width(UI_TITLE, 28, stt) + 24; float sx = x + 60, sy = y + 262;
    ui_fill_para(sx, sy, (float)sw, 40, 0, sc); ui_fill_para(sx + 4, sy + 4, (float)sw - 8, 32, 0, PAPER); ui_text(UI_TITLE, 28, sx + sw / 2.0f, sy + 5, sc, stt, UI_CENTER);
}

static void draw_select(void)
{
    draw_bg(); header(T(S_CHOOSE), T(S_TAGLINE));
    int n = games_count(); if (n > 3) n = 3;
    static const float xs[3] = { 60, 460, 860 };
    for (int i = 0; i < n; i++) { int id = reg(xs[i] - 10, 118, 400, 490, IK_CARD, i, 0, NULL); card(i, xs[i], 130, focus == id); }
    { int pb = reg(440, 606, 400, 56, IK_BTN, BT_PLAY, 0, NULL); int fo = focus == pb;
      ui_fill_para(448, 614, 400, 56, 18, INK); ui_fill_para(440, 606, 400, 56, 18, fo ? YEL : RED); ui_stroke_para(440, 606, 400, 56, 18, 5, INK);
      text_o(UI_TITLE, 44, 640, 608, fo ? INK : PAPER, T(S_PLAYBTN), UI_CENTER, fo ? 0 : 3); }
    int ob = reg(UI_W - 330, 112, 294, 40, IK_BTN, BT_OPTIONS, 0, NULL);
    ui_fill_para(UI_W - 330, 112, 294, 40, 12, focus == ob ? YEL : INK); ui_text(UI_TITLE, 24, UI_W - 183, 118, focus == ob ? INK : YEL, "F1  OPTIONS", UI_CENTER);
    if (g_msg[0] && g_msg_t > 0) { int tw = ui_text_width(UI_BODY, 22, g_msg) + 40; bubble(640 - tw / 2.0f, 548, (float)tw, 46, g_msg, 600); }
    const char *h[] = { "< >", T(S_MOVE), "Enter", fr() ? "choisir" : "pick", "F1", T(S_OPTIONS), "Esc", T(S_QUIT) }; hint_bar(h, 4);
}

static const char *TABN[6] = { 0 };
static void draw_options(void)
{
    draw_bg(); header(T(S_OPTIONS), g_tab == 2 || g_tab == 3 ? T(S_SUB_CTRL) : T(S_SUB_OPT));
    int tabs[6] = { S_T_IMAGE, S_T_SOUND, S_T_CONTROLS, S_T_WIIMOTE, S_T_LANG, S_T_GAME };
    for (int i = 0; i < 6; i++) {
        int on = i == g_tab; float x = 34 + (on ? 22 : 0), y = 122 + i * 78; (void)TABN;
        reg(x - 20, y, 270, 60, IK_TAB, i, 0, NULL);
        ui_fill_para(x + 8, y + 8, 254, 58, 24, INK); ui_fill_para(x, y, 254, 58, 24, on ? YEL : PAPER); ui_stroke_para(x, y, 254, 58, 24, 4, INK);
        ui_text(UI_TITLE, 30, x + 118, y + 10, on ? RED : INK, T(tabs[i]), UI_CENTER);
    }
    panel(360, 124, 840, 504, 18, PAPER, 1);
    rows_begin(146);
    switch (g_tab) {
    case 0: {
        const char *as[2] = { T(S_ASPECT43), T(S_ASPECTFILL) }, *fl[2] = { T(S_SHARP), T(S_SMOOTH) };
        row_check(T(S_FULLSCREEN), OPT_FULLSCREEN, fr() ? "Plein écran (F11 aussi)." : "Fullscreen (F11 works too).");
        row_check(T(S_INTEGER), OPT_INTEGER, fr() ? "Agrandit par multiples entiers : pixels parfaits." : "Scale by whole numbers: perfect pixels.");
        row_choice(T(S_ASPECT), OPT_ASPECT, as, 2, fr() ? "4:3 garde les proportions de l'arcade." : "4:3 keeps the arcade proportions.");
        row_choice(T(S_FILTER), OPT_FILTER, fl, 2, fr() ? "Net : pixels francs. Lissé : adouci." : "Sharp: crisp pixels. Smooth: softened.");
        row_slider(T(S_SCAN), OPT_SCANLINES, fr() ? "Intensité des lignes du tube cathodique (0 = aucune)." : "Strength of the CRT scanlines (0 = none).");
        row_check(T(S_FPS), OPT_SHOWFPS, fr() ? "Compteur en bas à gauche." : "Counter in the bottom-left corner.");
        row_check(T(S_VSYNC), OPT_VSYNC, fr() ? "Évite le déchirement de l'image." : "Avoids screen tearing."); break; }
    case 1:
        row_slider(T(S_VOLUME), OPT_VOLUME, fr() ? "Volume général." : "Master volume.");
        row_check(T(S_MUTE), OPT_MUTE, fr() ? "Coupe tout le son." : "Silences everything."); break;
    case 2: {
        int acts[9] = { S_ACT_FIRE, S_ACT_SENSOR, S_ACT_ENTER, S_ACT_UP, S_ACT_DOWN, S_ACT_START, S_ACT_COIN, S_ACT_SERVICE, S_ACT_TEST };
        for (int a = 0; a < 9; a++) {
            float y = 146 + a * 46;
            int i0 = reg(392, y, 300, 42, IK_INFO, 0, 0, NULL); (void)i0;
            ui_text(UI_BODY, 22, 410, y + 6, INK, T(acts[a]), UI_LEFT);
            for (int sl = 0; sl < 2; sl++) {
                float kx = 740 + sl * 210; int i = reg(kx, y, 190, 40, IK_KEY, a, sl, NULL); const char *nm = g_opt_key[a][sl];
                char shown[40]; if (g_capturing == i) snprintf(shown, sizeof shown, "%s", T(S_PRESS_KEY)); else if (nm[0]) snprintf(shown, sizeof shown, "%.30s", nm); else snprintf(shown, sizeof shown, "%s", sl == 0 ? "(auto)" : "-");
                if (focus == i) { ui_fill_para(kx - 4, y - 4, 198, 48, 8, YEL); }
                ui_fill_para(kx, y + 2, 190, 36, 8, INK); ui_fill_para(kx + 3, y + 5, 184, 30, 8, g_capturing == i ? RED : PAPER);
                ui_text(UI_BODY, 20, kx + 95, y + 8, g_capturing == i ? PAPER : INK, shown, UI_CENTER);
            }
        }
        { int i = reg(560, 146 + 9 * 46 + 6, 380, 40, IK_BTN, BT_RESETKEYS, 0, NULL); ui_text(UI_TITLE, 26, 750, 146 + 9 * 46 + 8, focus == i ? RED : INK, T(S_RESET_KEYS), UI_CENTER); }
        break; }
    case 3: {
        int st = plat_wiimote_state(); char t[80]; snprintf(t, sizeof t, "%s", st == 0 ? T(S_WM_OFF) : st == 1 ? T(S_WM_SEARCH) : T(S_WM_OK));
        row_check(T(S_WM_ENABLE), OPT_WIIMOTE, fr() ? "Pointeur infrarouge (barre de capteur). Windows." : "Infrared pointer (sensor bar). Windows.");
        row_info(T(S_WM_STATE), t);
        row_slider(T(S_SMOOTHING), OPT_SMOOTH, fr() ? "Adoucit les tremblements du viseur (0 = brut)." : "Softens the aim jitter (0 = raw).");
        row_slider(T(S_DEADZONE), OPT_DEADZONE, fr() ? "Ignore les micro-mouvements." : "Ignores tiny movements.");
        row_button(T(S_PAIR), BT_PAIR, fr() ? "Appaire une Wiimote en Bluetooth (sans code PIN)." : "Pairs a Wiimote over Bluetooth.");
        row_button(T(S_CALIB), BT_CALIB, fr() ? "Calibrage à 2 points avec la barre infrarouge." : "2-point calibration with the sensor bar."); break; }
    case 4: {
        int n = g_ctx ? g_ctx->n : 0; int cur = !strcmp(g_opt_lang, "") || !strcmp(g_opt_lang, "original") ? -1 : -2;
        for (int k = 0; k < n; k++) if (!strcmp(g_ctx->code[k], g_opt_lang)) cur = k;
        static const uint32_t ACC[4] = { UI_RGB(120, 120, 140), UI_RGB(214, 28, 36), UI_RGB(20, 160, 140), UI_RGB(230, 140, 20) };
        for (int k = -1; k < n; k++) {
            int idx = k + 1, col = idx % 2, row = idx / 2; float x = 400 + col * 390, y = 150 + row * 200, w = 360, h = 170, sk = 20;
            const char *code = k < 0 ? "original" : g_ctx->code[k]; char big[48], small[120];
            if (k < 0) { snprintf(big, sizeof big, "%s", "ORIGINAL"); snprintf(small, sizeof small, "%s", fr() ? "Textes d'origine (japonais)" : "Original texts (Japanese)"); }
            else if (!strcmp(code, "fr")) { snprintf(big, sizeof big, "FRAN\xc3\x87" "AIS"); snprintf(small, sizeof small, "%s", "Textes du jeu en fran\xc3\xa7" "ais"); }
            else if (!strcmp(code, "en")) { snprintf(big, sizeof big, "ENGLISH"); snprintf(small, sizeof small, "%s", "Game texts in English"); }
            else { snprintf(big, sizeof big, "%s", g_ctx->name[k]); for (char *c = big; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32; snprintf(small, sizeof small, "%s", g_ctx->name[k]); }
            int i = reg(x - 10, y - 8, w + 20, h + 16, IK_LANG, k, 0, fr() ? "Le jeu redémarre quand la langue change." : "The game restarts when the language changes.");
            uint32_t acc = ACC[(k + 1) % 4]; int foc = focus == i, on = cur == k;
            if (foc) ui_fill_para(x - 7, y - 7, w + 14, h + 14, sk, YEL);
            ui_fill_para(x + 8, y + 8, w, h, sk, INK); if (foc) ui_fill_para(x - 3, y - 3, w + 6, h + 6, sk, INK);
            float X[4], Y[4]; ui_para(x, y, w, h, sk, X, Y); ui_fill_quad(X, Y, PAPER); ui_clip_quad(X, Y);
            ui_halftone(x, y, w, h, acc, 9, 30, 2, 0.0f, 0.6f); ui_fill_rect(x, y + 96, w, h - 96, PAPER); ui_fill_rect(x, y + 92, w, 5, INK);
            ui_clip_none(); ui_stroke_quad(X, Y, 4, INK);
            text_o(UI_TITLE, 46, x + 36, y + 22, PAPER, big, UI_LEFT, 3);
            ui_text(UI_BODY, 18, x + 26, y + 104, INK, small, UI_LEFT);
            if (on) {                                                                  /* "active" stamp */
                const char *a = fr() ? "ACTIF" : "ACTIVE"; int sw = ui_text_width(UI_TITLE, 26, a) + 22;
                ui_fill_para(x + w - sw - 30, y + h - 40, (float)sw, 32, 10, GREEN); ui_fill_para(x + w - sw - 26, y + h - 37, (float)sw - 8, 26, 10, PAPER);
                ui_text(UI_TITLE, 22, x + w - sw / 2.0f - 30, y + h - 38, GREEN, a, UI_CENTER);
            }
        }
        { int bi = reg(800, 498, 330, 44, IK_BTN, BT_DONE, 0, NULL); ui_fill_para(808, 505, 330, 44, 14, INK); ui_fill_para(800, 498, 330, 44, 14, focus == bi ? YEL : RED);
          ui_stroke_para(800, 498, 330, 44, 14, 4, INK); text_o(UI_TITLE, 30, 965, 502, focus == bi ? INK : PAPER, fr() ? "VALIDER" : "OK", UI_CENTER, focus == bi ? 0 : 2); }
        break; }
    case 5:
        row_check(T(S_START_LAST), OPT_START_LAST, fr() ? "Saute le choix du jeu au lancement." : "Skips the game selector at launch.");
        row_check(T(S_FREE), OPT_FREECREDIT, fr() ? "Le compteur de crédits du jeu reste au maximum : plus de pièces à insérer." : "The game's credit counter stays at maximum: no coins to insert.");
        row_check(T(S_TRIGSTART), OPT_TRIGSTART, fr() ? "Un tir vaut un appui sur Start : jouer avec la souris seule (tir + zoom)." : "A shot counts as a Start press: play with the mouse alone (fire + scope).");
        row_check(T(S_RECORD), OPT_RECORD, fr() ? "Un fichier .g13r par session (rejouable)." : "One .g13r file per session (replayable).");
        row_check(T(S_RETICLE), OPT_RETICLE, fr() ? "Croix de visée dessinée par le port (F3)." : "Crosshair drawn by the port (F3).");
        break;
    }
    if (focus >= 0 && focus < nit && it[focus].desc) bubble(400, 566, 780, 46, it[focus].desc, 440);
    const char *h[] = { "Up/Down", fr() ? "choisir" : "choose", "Left/Right", T(S_CHANGE), "PgUp/PgDn", T(S_TAB), "Esc", T(S_BACK) }; hint_bar(h, 4);
}

static void draw_pause(void)
{
    draw_bg();
    if (g_pause_img) {                                                     /* the frozen game, darkened, in its 4:3 frame */
        ui_image_t im = { g_pause_w, g_pause_h, (uint32_t *)g_pause_img }; ui_image_draw(&im, 160, 0, 960, 720, 0);
    }
    ui_tint(0, 0, UI_W, UI_H, UI_RGBA(0, 0, 20, 170)); ui_speed_lines(UI_W / 2.0f, UI_H / 2.0f, UI_RGBA(255, 255, 255, 22), 80, 5, 0.25f);
    panel(380, 120, 520, 470, 20, PAPER, 1);
    float X[4] = { 380, 900, 884, 364 }, Y[4] = { 120, 120, 212, 212 }; ui_fill_quad(X, Y, RED); ui_stroke_quad(X, Y, 5, INK);
    text_o(UI_TITLE, 58, 640, 138, PAPER, T(S_PAUSE), UI_CENTER, 3);
    int ids[5] = { BT_RESUME, BT_OPTIONS, BT_CHANGE, BT_RESET, BT_QUIT }, lab[5] = { S_RESUME, S_OPTIONS, S_CHANGE_GAME, S_RESET, S_QUIT_GAME };
    for (int i = 0; i < 5; i++) {
        float y = 240 + i * 66; int id = reg(420, y, 440, 54, IK_BTN, ids[i], 0, NULL);
        if (focus == id) { float X2[4] = { 436, 860, 846, 420 }, Y2[4] = { y, y, y + 54, y + 54 }; ui_fill_quad(X2, Y2, YEL); ui_stroke_quad(X2, Y2, 4, INK); }
        ui_text(UI_TITLE, 34, 640, y + 6, ids[i] == BT_QUIT ? RED : INK, T(lab[i]), UI_CENTER);
    }
    if (g_confirm_quit) {
        nit = 0; ui_tint(0, 0, UI_W, UI_H, UI_RGBA(0, 0, 0, 150)); panel(400, 250, 480, 230, 16, PAPER, 0);
        ui_text(UI_TITLE, 40, 640, 280, INK, T(S_SURE), UI_CENTER);
        int y = reg(450, 380, 170, 60, IK_BTN, BT_YES, 0, NULL), n = reg(660, 380, 170, 60, IK_BTN, BT_NO, 0, NULL);
        ui_fill_para(450, 380, 170, 60, 12, focus == y ? YEL : PAPER); ui_stroke_para(450, 380, 170, 60, 12, 4, INK); ui_text(UI_TITLE, 34, 535, 388, RED, T(S_YES), UI_CENTER);
        ui_fill_para(660, 380, 170, 60, 12, focus == n ? YEL : PAPER); ui_stroke_para(660, 380, 170, 60, 12, 4, INK); ui_text(UI_TITLE, 34, 745, 388, INK, T(S_NO), UI_CENTER);
    }
}

/* ---------------------------------------------------------------- input */
static void gather(void)
{
    plat_input_t in; plat_poll(&in); plat_ev_t e; g_nnav = g_nraw = 0; g_click = g_rclick = 0; g_quit |= in.quit;
    while (plat_ui_event(&e)) {
        if (e.type == 6 && g_nnav < 24) g_nav[g_nnav++] = e.code;
        else if (e.type == 1 && g_nraw < 8) g_raw[g_nraw++] = e.code;
        else if (e.type == 3) g_click = 1; else if (e.type == 4) g_rclick = 1;
    }
    int mx, my, inside = plat_ui_mouse(&mx, &my); g_moved = inside && (mx != g_mx || my != g_my); g_mx = mx; g_my = my; g_mouse_in = inside;
}
static int hit(void) { if (!g_mouse_in) return -1; for (int i = nit - 1; i >= 0; i--) if (it[i].kind != IK_INFO && g_mx >= it[i].x && g_mx < it[i].x + it[i].w && g_my >= it[i].y && g_my < it[i].y + it[i].h) return i; return -1; }
static int step_focus(int dir)
{
    if (nit == 0) return 0; int f = focus;
    for (int k = 0; k < nit; k++) { f = (f + dir + nit) % nit; if (it[f].kind != IK_INFO && it[f].kind != IK_TAB) break; }
    return f;
}
/* returns an item action code (>0 = BT_ id / card index + 1000) or 0 */
static int adjust(int i, int dir, int x)
{
    item_t *m = &it[i];
    switch (m->kind) {
    case IK_CHECK: options_set(m->a, !g_opt[m->a]); menu_apply_options(); if (m->a == OPT_WIIMOTE && g_opt[OPT_WIIMOTE] && plat_wiimote_state() == 0) plat_wiimote_enable(); break;
    case IK_CHOICE: options_set(m->a, (g_opt[m->a] + dir + m->b) % m->b); menu_apply_options(); break;
    case IK_SLIDER: {
        const opt_info_t *o = opt_info(m->a);
        if (x >= 0) { float t = (x - (m->x + 14)) / (m->w - 28); if (t < 0) t = 0; if (t > 1) t = 1; options_set(m->a, o->min + (int)(t * (o->max - o->min) + 0.5f)); }
        else { int st = (o->max - o->min) / 20; if (st < 1) st = 1; options_set(m->a, g_opt[m->a] + dir * st); }
        menu_apply_options(); break; }
    default: break;
    }
    return 0;
}
static int activate(int i, int mx)
{
    item_t *m = &it[i];
    switch (m->kind) {
    case IK_BTN: return m->a;
    case IK_CARD: return 1000 + m->a;
    case IK_TAB: g_tab = m->a; focus = NTABS; return 0;
    case IK_CHECK: adjust(i, 1, -1); return 0;
    case IK_CHOICE: adjust(i, (mx >= 0 && mx < m->x + m->w / 2) ? -1 : 1, -1); return 0;
    case IK_SLIDER: if (mx >= 0) adjust(i, 0, mx); return 0;
    case IK_KEY: g_capturing = i; return 0;
    case IK_LANG: if (g_ctx && g_ctx->apply_lang) { g_ctx->apply_lang(m->a < 0 ? "original" : g_ctx->code[m->a]); return 2000; } return 0;
    default: return 0;
    }
}
/* common input processing for the current page; horiz = left/right move the focus (selector / pause confirm) */
static int process(int horiz, int *back)
{
    int act = 0; *back = 0;
    if (g_capturing >= 0) {                                                /* key capture for the controls page */
        for (int k = 0; k < g_nraw; k++) {
            const char *nm = plat_key_name(g_raw[k]); item_t *m = &it[g_capturing < nit ? g_capturing : 0];
            if (!strcmp(nm, "Escape")) { g_capturing = -1; break; }
            if (!strcmp(nm, "Delete") || !strcmp(nm, "Backspace")) options_set_key(m->a, m->b, ""); else options_set_key(m->a, m->b, nm);
            menu_apply_options(); g_capturing = -1; break;
        }
        g_nnav = 0; return 0;
    }
    for (int k = 0; k < g_nnav; k++) {
        int n = g_nav[k];
        if (n == 1) { if (horiz) {} else focus = step_focus(-1); } else if (n == 2) { if (!horiz) focus = step_focus(1); }
        else if (n == 3) { if (horiz || (focus < nit && it[focus].kind == IK_LANG)) focus = step_focus(-1); else if (focus < nit && focus >= 0) adjust(focus, -1, -1); }
        else if (n == 4) { if (horiz || (focus < nit && it[focus].kind == IK_LANG)) focus = step_focus(1); else if (focus < nit && focus >= 0) adjust(focus, 1, -1); }
        else if (n == 5) { if (focus >= 0 && focus < nit) act = activate(focus, -1); }
        else if (n == 6) *back = 1;
        else if (n == 7 && g_tab > 0 && nit && it[0].kind == IK_TAB) { g_tab--; focus = NTABS; } else if (n == 8 && g_tab < 5 && nit && it[0].kind == IK_TAB) { g_tab++; focus = NTABS; }
    }
    if (g_moved) { int h = hit(); if (h >= 0 && it[h].kind != IK_TAB) focus = h; }
    if (g_click) { int h = hit(); if (h >= 0) { focus = h; act = activate(h, g_mx); } }
    if (g_rclick) *back = 1;
    if (focus >= nit) focus = nit ? nit - 1 : 0;
    if (nit && (it[focus].kind == IK_INFO || (it[focus].kind == IK_TAB && g_nnav + g_moved == 0))) focus = step_focus(1);
    return act;
}
static void draw_cursor(void)                 /* the aiming crosshair follows the mouse / Wiimote pointer */
{
    if (!g_mouse_in) return; float x = (float)g_mx, y = (float)g_my; ui_clip_none();
    for (int pass = 0; pass < 2; pass++) {
        float o = pass == 0 ? 2.5f : 0; uint32_t c = pass == 0 ? INK : UI_RGB(255, 40, 40);
        ui_fill_rect(x - 20 - o, y - 1.5f - o, 14 + 2 * o, 3 + 2 * o, c); ui_fill_rect(x + 6 - o, y - 1.5f - o, 14 + 2 * o, 3 + 2 * o, c);
        ui_fill_rect(x - 1.5f - o, y - 20 - o, 3 + 2 * o, 14 + 2 * o, c); ui_fill_rect(x - 1.5f - o, y + 6 - o, 3 + 2 * o, 14 + 2 * o, c);
        ui_fill_rect(x - 2 - o, y - 2 - o, 4 + 2 * o, 4 + 2 * o, c);
    }
}
static void present(void) { draw_cursor(); plat_present_ui((const uint8_t *)g_buf); options_flush(0); plat_sleep_us(12000); g_tick++; if (g_msg_t > 0) g_msg_t--; }

/* ---------------------------------------------------------------- modal loops */
static void options_loop(int tab)
{
    g_tab = tab; focus = NTABS; nit = 0; g_capturing = -1;
    for (;;) {
        gather(); if (g_quit) return;
        int back; int act = process(0, &back);
        if (act == BT_PAIR) { draw_options(); bubble(300, 330, 700, 60, T(S_PAIRING), 400); present(); plat_wiimote_pair(); }
        if (act == BT_CALIB) { plat_wiimote_recalibrate(); options_flush(1); return; }
        if (act == BT_RESETKEYS) { options_reset_keys(); menu_apply_options(); }
        if (act == 2000) options_flush(1);
        if (act == BT_DONE) { options_flush(1); return; }
        if (back || (g_nraw && 0)) { options_flush(1); return; }
        nit = 0; draw_options(); present();
    }
}
void menu_options(const menu_ctx_t *ctx, int tab) { g_ctx = ctx; static uint32_t buf[UI_W * UI_H]; g_buf = buf; options_loop(tab); }

int menu_select_game(const char *userroms, const menu_ctx_t *ctx)
{
    static uint32_t buf[UI_W * UI_H]; g_buf = buf; g_ctx = ctx; g_userroms = userroms; int n = games_count(); if (n > 3) n = 3;
    for (int i = 0; i < n; i++) { char p[1100]; g_roms[i] = game_find_roms(i, userroms, p, sizeof p); }
    focus = 0; nit = 0; g_msg[0] = 0; g_picked = -1;
    { int g = options_last_index(); if (g >= 0 && g < n) { focus = g; g_picked = g; } }
    for (;;) {
        gather(); if (g_quit) return -1;
        int back, act = process(1, &back);
        for (int k = 0; k < g_nraw; k++) { const char *nm = plat_key_name(g_raw[k]); if (!strcmp(nm, "F1")) act = BT_OPTIONS; if (!strcmp(nm, "Escape")) g_quit = 1; }
        if (g_quit) return -1;
        if (act == BT_OPTIONS) { options_loop(0); if (g_quit) return -1; nit = 0; continue; }
        if (act >= 1000 && act < 1100) {                          /* a card is picked: the PLAY button starts it */
            g_picked = act - 1000; g_msg[0] = 0;
            for (int k = 0; k < nit; k++) if (it[k].kind == IK_BTN && it[k].a == BT_PLAY) focus = k;
        }
        if (act == BT_PLAY) {
            int gi = g_picked >= 0 ? g_picked : (focus >= 0 && focus < nit && it[focus].kind == IK_CARD ? it[focus].a : 0); const game_info_t *g = game_info(gi);
            if (!g->supported) { snprintf(g_msg, sizeof g_msg, "%s", T(S_NOT_PLAYABLE)); g_msg_t = 180; }
            else if (!g_roms[gi]) { snprintf(g_msg, sizeof g_msg, "%s", T(S_ROMHELP)); g_msg_t = 240; }
            else { options_set_lastgame(g->id); options_flush(1); return gi; }
        }
        nit = 0; draw_select(); present();
    }
}

int menu_pause(const menu_ctx_t *ctx, const uint8_t *game, int gw, int gh)
{
    static uint32_t buf[UI_W * UI_H]; g_buf = buf; g_ctx = ctx; g_pause_img = game; g_pause_w = gw; g_pause_h = gh; focus = 0; nit = 0; g_confirm_quit = 0;
    for (;;) {
        gather(); if (g_quit) return MENU_QUIT;
        int back, act = process(g_confirm_quit, &back);
        if (g_confirm_quit) { if (act == BT_YES) return MENU_QUIT; if (act == BT_NO || back) { g_confirm_quit = 0; focus = 0; } }
        else {
            if (back || act == BT_RESUME) { g_pause_img = NULL; return MENU_RESUME; }
            if (act == BT_OPTIONS) { options_loop(0); if (g_quit) return MENU_QUIT; g_bg_ok = 1; nit = 0; continue; }
            if (act == BT_CHANGE) { g_pause_img = NULL; return MENU_CHANGE_GAME; }
            if (act == BT_RESET) { g_pause_img = NULL; return MENU_RESET; }
            if (act == BT_QUIT) { g_confirm_quit = 1; focus = 1; }
        }
        if (g_nnav == 0 && !g_moved) {}
        nit = 0; draw_pause(); present();
    }
}


/* ---------------------------------------------------------------- the NOTICE countdown gag */
static float ease(float t) { if (t < 0) t = 0; if (t > 1) t = 1; return 1 - (1 - t) * (1 - t) * (1 - t); }
static void ring(float cx, float cy, float r, float w, uint32_t c)
{
    for (int i = 0; i < 28; i++) {
        float a0 = i * 6.2831853f / 28, a1 = (i + 1) * 6.2831853f / 28;
        float X[4] = { cx + cosf(a0) * (r - w / 2), cx + cosf(a0) * (r + w / 2), cx + cosf(a1) * (r + w / 2), cx + cosf(a1) * (r - w / 2) };
        float Y[4] = { cy + sinf(a0) * (r - w / 2), cy + sinf(a0) * (r + w / 2), cy + sinf(a1) * (r + w / 2), cy + sinf(a1) * (r - w / 2) };
        ui_fill_quad(X, Y, c);
    }
}
static void burst(float cx, float cy, float r0, float r1, int n, uint32_t c, float rot)
{
    for (int i = 0; i < n * 2; i += 2) {
        float a = rot + i * 3.14159265f / n, b = a + 3.14159265f / n, d = a + 2 * 3.14159265f / n;
        float X[4] = { cx, cx + cosf(a) * r0, cx + cosf(b) * r1, cx + cosf(d) * r0 }, Y[4] = { cy, cy + sinf(a) * r0, cy + sinf(b) * r1, cy + sinf(d) * r0 };
        ui_fill_quad(X, Y, c);
    }
}
void menu_notice(const uint8_t *game, int gw, int gh, float p, unsigned ms)
{
    static uint32_t buf[UI_W * UI_H]; g_buf = buf; draw_bg();
    if (game) { ui_image_t im = { gw, gh, (uint32_t *)game }; ui_image_draw(&im, 160, 0, 960, 720, 0); }       /* the game's own NOTICE screen, untouched */
    const float DX = 640, DY = 672; const float SHOT = 0.55f;
    ui_fill_rect(DX - 60, DY - 22, 120, 44, UI_RGB(0, 0, 0));                                                   /* hides the game's own small digit */
    float slide = ease(p / 0.15f), px = UI_W - 20 - slide * 520 + (p < SHOT && p > 0.2f ? sinf(ms * 0.045f) * 2.0f : 0), py = 400;
    if (p > SHOT && p < SHOT + 0.04f) px += (SHOT + 0.04f - p) * 400;                                           /* recoil */
    if (slide > 0.01f) { float X[4], Y[4]; ui_para(px, py, 440, 170, 22, X, Y); ui_fill_para(px + 9, py + 9, 440, 170, 22, INK); ui_fill_para(px - 6, py - 6, 452, 182, 22, INK); ui_clip_quad(X, Y);
      if (g_notice) ui_image_draw(g_notice, px, py, 440, 170, 0); else ui_fill_rect(px, py, 440, 170, UI_RGB(30, 30, 40));
      ui_clip_none(); ui_stroke_quad(X, Y, 5, PAPER); }
    char num[8];
    if (p < SHOT) text_o(UI_TITLE, 58, DX, DY - 36, PAPER, "10", UI_CENTER, 3);
    else {
        float t = p - SHOT;
        if (t < 0.12f) for (int i = 0; i < 16; i++) {                                                           /* shards of the shot digit */
            float a = i * 0.7f + 0.3f, v = 200 + (i * 37 % 120), tt = t * 4.0f, x = DX + cosf(a) * v * tt, y = DY + sinf(a) * v * tt + 500 * tt * tt, sz = 8 + (i * 13 % 10), r = tt * (i % 2 ? 6 : -6);
            float X[4] = { x + cosf(r) * sz, x + cosf(r + 2.2f) * sz, x + cosf(r + 3.4f) * sz * 0.7f, x + cosf(r + 5.0f) * sz * 0.6f }, Y[4] = { y + sinf(r) * sz, y + sinf(r + 2.2f) * sz, y + sinf(r + 3.4f) * sz * 0.7f, y + sinf(r + 5.0f) * sz * 0.6f };
            ui_fill_quad(X, Y, PAPER);
        }
        if (t > 0.07f) {                                                                                         /* the counter drops to 0 */
            float q = (t - 0.07f) / 0.30f; if (q > 1) q = 1; int n = (int)(9.0f * (1.0f - q) + 0.5f); snprintf(num, sizeof num, "%d", n);
            text_o(UI_TITLE, 58, DX, DY - 36 + (1 - ease(q * 4.0f)) * -30, n == 0 ? RED : YEL, num, UI_CENTER, 3);
        }
    }
    if (p > 0.12f && p < SHOT + 0.06f) {                                                                          /* crosshair: wanders, then locks on the digit */
        float q = (p - 0.12f) / (SHOT - 0.12f), k = ease(q), jx = (1 - k) * (sinf(ms * 0.011f) * 150 + sinf(ms * 0.023f) * 60), jy = (1 - k) * (cosf(ms * 0.013f) * 90 + sinf(ms * 0.029f) * 40);
        float cx = 980 + (DX - 980) * k + jx, cy = 420 + (DY - 420) * k + jy; uint32_t col = p >= SHOT - 0.05f ? RED : UI_RGB(255, 60, 60);
        float rr = 50 - 20 * k; ring(cx, cy, rr, 5, INK); ring(cx, cy, rr, 3, col);
        ui_fill_rect(cx - rr - 22, cy - 2, 34, 4, col); ui_fill_rect(cx + rr - 12, cy - 2, 34, 4, col); ui_fill_rect(cx - 2, cy - rr - 22, 4, 34, col); ui_fill_rect(cx - 2, cy + rr - 12, 4, 34, col);
    }
    if (p >= SHOT && p < SHOT + 0.10f) {                                                                          /* the shot */
        float t = (p - SHOT) / 0.10f; burst(DX, DY, 40 + 40 * t, 110 + 90 * t, 12, UI_RGBA(255, 220, 40, (int)(255 * (1 - t))), 0.2f);
        text_o(UI_TITLE, 120, DX + 60, DY - 230, RED, "BANG !", UI_CENTER, 4); ui_tint(0, 0, UI_W, UI_H, UI_RGBA(255, 255, 255, (int)(150 * (1 - t))));
    }
    if (p > 0.95f) text_o(UI_TITLE, 46, DX + 110, DY - 40, YEL, fr() ? "C'EST PARTI !" : "HERE WE GO!", UI_LEFT, 3);
    plat_present_ui((const uint8_t *)g_buf);
}

/* ---------------------------------------------------------------- screenshots (no window) */
static void save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb"); if (!f) return; fprintf(f, "P6\n%d %d\n255\n", UI_W, UI_H);
    for (int i = 0; i < UI_W * UI_H; i++) fwrite((uint8_t *)&g_buf[i], 1, 3, f); fclose(f);
}
int menu_shots(const char *dir, const menu_ctx_t *ctx, const char *userroms)
{
    static uint32_t buf[UI_W * UI_H]; g_buf = buf; g_ctx = ctx; char p[1300]; int n = games_count(); if (n > 3) n = 3;
    for (int i = 0; i < n; i++) { char q[1100]; g_roms[i] = game_find_roms(i, userroms, q, sizeof q); }
    fs_mkdir_p(dir);
    focus = 1; nit = 0; draw_select(); nit = 0; draw_select(); snprintf(p, sizeof p, "%s/1_select.ppm", dir); save_ppm(p);
    for (int t = 0; t < 6; t++) { g_tab = t; focus = NTABS + (t == 3 ? 2 : 1); nit = 0; draw_options(); nit = 0; draw_options(); snprintf(p, sizeof p, "%s/2_options_%d.ppm", dir, t); save_ppm(p); }
    focus = 0; nit = 0; draw_pause(); nit = 0; draw_pause(); snprintf(p, sizeof p, "%s/3_pause.ppm", dir); save_ppm(p);
    for (int k = 0; k < 6; k++) { float pp[6] = { 0.1f, 0.35f, 0.54f, 0.58f, 0.7f, 0.97f }; menu_notice(NULL, 0, 0, pp[k], 20000 + k * 700); snprintf(p, sizeof p, "%s/4_notice_%d.ppm", dir, k); save_ppm(p); }
    return 0;
}
