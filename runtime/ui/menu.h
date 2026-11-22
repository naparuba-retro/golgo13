/* Launcher menu: game selector, options (tabs), pause. Comic-book look drawn by ui.c. */
#ifndef GOLGO_MENU_H
#define GOLGO_MENU_H
#include <stdint.h>

typedef struct {                       /* what main.c tells the menu about languages and the running game */
    int n; const char *code[16], *name[16];       /* translations found (without "original") */
    void (*apply_lang)(const char *code);         /* called when the user picks a language ("original" or a code) */
    int in_game;                                  /* 1 when a game is running (pause / options opened from the game) */
} menu_ctx_t;

enum { MENU_RESUME = 0, MENU_CHANGE_GAME, MENU_RESET, MENU_QUIT, MENU_LANG };

void menu_init(const char *exe_dir, const char *pref_dir);     /* loads fonts + card art */
int  menu_available(void);                                     /* fonts present: the new menu can be used */
void menu_apply_options(void);                                 /* pushes options to the platform (video, audio, keys, Wiimote) */
int  menu_select_game(const char *userroms, const menu_ctx_t *ctx);     /* game index, -1 = quit */
int  menu_pause(const menu_ctx_t *ctx, const uint8_t *game_rgba, int gw, int gh);   /* MENU_* */
void menu_options(const menu_ctx_t *ctx, int tab);             /* options only; tab 0..5 (4 = language) */
int  menu_shots(const char *dir, const menu_ctx_t *ctx, const char *userroms);      /* writes one PPM per page, no window */
void menu_notice(const uint8_t *game_rgba, int gw, int gh, float progress, unsigned ms);                  /* the 10-to-0 gag shown while the NOTICE screen runs unpaced; progress 0..1 */
void menu_set_roms_dir(const char *abs_path);                    /* the folder where the user must put the ROMs (shown by the first-run page) */
void menu_set_exe_path(const char *exe);                        /* this program's own path (the build screen replaces it with the new build) */
void menu_tick_flush(void);
void menu_set_ui_lang(const char *code);                       /* language of the menu texts ("fr" or anything else = English) */
#endif
