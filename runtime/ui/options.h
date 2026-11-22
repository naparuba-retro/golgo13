/* User options (menu) and settings.ini: INI file with sections; only values that differ from the default are written,
 * unknown lines are kept, writes are atomic (temp file + rename). */
#ifndef GOLGO_OPTIONS_H
#define GOLGO_OPTIONS_H

enum { OPT_FULLSCREEN, OPT_INTEGER, OPT_ASPECT, OPT_FILTER, OPT_SCANLINES, OPT_SHOWFPS, OPT_VSYNC,
       OPT_VOLUME, OPT_MUTE, OPT_WIIMOTE, OPT_SMOOTH, OPT_DEADZONE, OPT_RETICLE, OPT_START_LAST, OPT_RECORD, OPT_FREECREDIT, OPT_TRIGSTART, OPT_COUNT };
enum { ACT_FIRE, ACT_SENSOR, ACT_ENTER, ACT_UP, ACT_DOWN, ACT_START, ACT_COIN, ACT_SERVICE, ACT_TEST, ACT_COUNT };

typedef struct { const char *key, *section; int def, min, max; } opt_info_t;

extern int g_opt[OPT_COUNT];
extern char g_opt_lang[32];                    /* "original" or a translation code */
extern char g_opt_lastgame[40];
extern char g_opt_key[ACT_COUNT][2][32];       /* key names (SDL scancode names); "" = default binding */

const opt_info_t *opt_info(int id);
const char *opt_action_name(int act);          /* ini key prefix */
void options_init(const char *ini_path);       /* sets defaults, reads the file if any */
void options_set(int id, int value);           /* clamps, marks dirty */
void options_set_lang(const char *code);
void options_set_lastgame(const char *id);
void options_set_key(int act, int slot, const char *name);
void options_reset_keys(void);
void options_flush(int force);                 /* writes the file when dirty (delayed ~0.5 s unless force) */
#endif
