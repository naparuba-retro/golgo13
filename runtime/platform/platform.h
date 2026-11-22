/* Platform layer: window, keyboard/mouse (absolute pointer = light gun), frame pacing.
 * Implementations: platform_win32.c (no external dependency). A SDL2 implementation (Linux / Steam Deck) uses the same API. */
#ifndef GOLGO_PLATFORM_H
#define GOLGO_PLATFORM_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    int gun_x, gun_y;       /* absolute pointer inside the 4:3 game area, 0..511 each (like the arcade gun axes) */
    int trigger, sensor, enter, up, down, start, coin, service, test;
    int quit, fullscreen_toggle;
    int menu;               /* F4 / pad L1+R1: open the language menu (edge event) */
    int pause;              /* Esc (edge event): open the pause menu */
    int ui_zip, ui_dir;     /* menu edge events: choose a zip (Enter / click / pad A), choose a folder (Tab / pad Y) */
    char drop_path[1024];   /* file or folder dropped on the window, or picked in a file dialog (empty if none) */
} plat_input_t;

int      plat_init(const char *title, int w, int h);
void     plat_shutdown(void);
void     plat_poll(plat_input_t *in);                       /* pump events, fill input state */
void     plat_present(const uint8_t *rgba, int w, int h);   /* show one frame (letterboxed to 4:3) */
int      plat_audio_init(int rate);                         /* interleaved stereo int16 */
void     plat_audio_push(const int16_t *stereo, size_t frames);
void     plat_present_text(const char *const *lines, int n);   /* text screen; line prefix: # title, ! error (red), + ok (green) */
void     plat_show_cursor(int show);
void     plat_choose(int folder);                           /* open a native file/folder dialog; result arrives in drop_path */
const char *plat_pref_dir(void);                           /* per-user writable application data folder (with trailing separator) */
void     plat_wiimote_enable(void);                         /* use a paired Wiimote + sensor bar as the gun (Windows) */
int      plat_wiimote_pair(void);                           /* pair a Wiimote over Bluetooth without the OS dialog (Windows) */
uint64_t plat_time_us(void);
void     plat_sleep_us(uint64_t us);

/* ---- menu / options support ---- */
typedef struct { int type, code; } plat_ev_t;       /* type: 1 key down (code = SDL scancode), 2 pad button (SDL gamepad button), 3 left click, 4 right click, 5 wheel (code +-1), 6 navigation (code 1 up 2 down 3 left 4 right 5 confirm 6 back 7 tab prev 8 tab next; from keyboard and pad) */
int  plat_ui_event(plat_ev_t *e);                   /* pops the next queued UI event; 0 when empty (call after plat_poll) */
int  plat_ui_mouse(int *x, int *y);                 /* pointer in 1280x720 menu coordinates; 0 if outside the picture */
const char *plat_key_name(int scancode);
int  plat_key_from_name(const char *name);          /* -1 unknown */
void plat_present_ui(const uint8_t *rgba);          /* 1280x720 RGBA menu picture (letterboxed 16:9) */
void plat_bind(int action, int slot, const char *name);   /* action = ACT_* of options.h; "" = default key */
typedef struct { int fullscreen, integer, aspect, filter, scanlines, show_fps, vsync, reticle; } plat_video_t;
void plat_video_set(const plat_video_t *v);
void plat_audio_set(int volume, int mute);
void plat_wiimote_params(int smooth, int deadzone);
int  plat_wiimote_state(void);                      /* 0 off, 1 searching, 2 connected */
void plat_wiimote_recalibrate(void);
#endif
