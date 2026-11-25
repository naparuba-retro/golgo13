/* SDL3 platform layer (Windows / Linux / macOS): window, keyboard, mouse (per-device ids), gamepad, frame pacing.
 * The pointing device is the "light gun": any absolute pointer works (mouse, Sinden, Aimtrak, Gun4IR ... show up as mice;
 * SDL3 reports a distinct SDL_MouseID per device so a gun can be selected later). */
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "wiimote.h"
static int wm_on; static wiimote_state_t wm;

#define LW 640
#define LH 480
static SDL_Window *win; static SDL_Renderer *ren; static SDL_Texture *tex; static int tex_w, tex_h;
static int quit_flag, lmb, rmb, fullscreen;
static float mx = LW / 2, my = LH / 2;            /* pointer in logical (4:3) coordinates */
static SDL_MouseID gun_mouse;                      /* 0 = any mouse */
static uint8_t key[SDL_SCANCODE_COUNT];
static SDL_AudioStream *astream;
static int show_reticle = 1;
static char pending[1024]; static volatile int pending_ok;
static int ui_zip_ev, ui_dir_ev, menu_ev, pause_ev;
static plat_ev_t evq[64]; static int evq_n;                          /* UI events (menu) queued by plat_poll */
static void ev_push(int type, int code) { if (evq_n < 64) { evq[evq_n].type = type; evq[evq_n].code = code; evq_n++; } }
static void nav_key(int sc, int repeat)
{
    int n = 0;
    switch (sc) {
    case SDL_SCANCODE_UP: n = 1; break; case SDL_SCANCODE_DOWN: n = 2; break; case SDL_SCANCODE_LEFT: n = 3; break; case SDL_SCANCODE_RIGHT: n = 4; break;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: n = repeat ? 0 : 5; break;
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: n = repeat ? 0 : 6; break;
    case SDL_SCANCODE_PAGEUP: n = 7; break; case SDL_SCANCODE_PAGEDOWN: case SDL_SCANCODE_TAB: n = 8; break;
    default: break;
    }
    if (n) ev_push(6, n);
}
static void nav_pad(int b)
{
    int n = 0;
    switch (b) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: n = 1; break; case SDL_GAMEPAD_BUTTON_DPAD_DOWN: n = 2; break; case SDL_GAMEPAD_BUTTON_DPAD_LEFT: n = 3; break; case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: n = 4; break;
    case SDL_GAMEPAD_BUTTON_SOUTH: n = 5; break; case SDL_GAMEPAD_BUTTON_EAST: n = 6; break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: n = 7; break; case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: n = 8; break;
    default: break;
    }
    if (n) ev_push(6, n);
}
static float mwx, mwy;                                               /* last pointer position in window pixels */
static SDL_Texture *ui_tex; static plat_video_t vid = { 0, 0, 0, 1, 0, 0, 1, 1 };
static int audio_vol = 100, audio_mute, wm_smooth, wm_dz; static float wsx, wsy; static int wsv;
static const int bind_def[9][2] = { { SDL_SCANCODE_LCTRL, SDL_SCANCODE_SPACE }, { SDL_SCANCODE_LSHIFT, 0 }, { SDL_SCANCODE_RETURN, 0 }, { SDL_SCANCODE_UP, 0 }, { SDL_SCANCODE_DOWN, 0 },
                          { SDL_SCANCODE_1, 0 }, { SDL_SCANCODE_5, 0 }, { SDL_SCANCODE_F2, 0 }, { SDL_SCANCODE_F1, 0 } };
static int bind[9][2] = { { SDL_SCANCODE_LCTRL, SDL_SCANCODE_SPACE }, { SDL_SCANCODE_LSHIFT, 0 }, { SDL_SCANCODE_RETURN, 0 }, { SDL_SCANCODE_UP, 0 }, { SDL_SCANCODE_DOWN, 0 },
                          { SDL_SCANCODE_1, 0 }, { SDL_SCANCODE_5, 0 }, { SDL_SCANCODE_F2, 0 }, { SDL_SCANCODE_F1, 0 } };
#define KEYB(a) (key[bind[a][0]] || (bind[a][1] && key[bind[a][1]]))
static uint64_t fps_t0; static int fps_n, fps_val;
static SDL_Gamepad *pad; static float pad_ax, pad_ay;   /* aim stick accumulation */

int plat_init(const char *title, int w, int h)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) return 0;
    win = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE);
    if (!win) return 0;
    ren = SDL_CreateRenderer(win, NULL);
    if (!ren) return 0;
    SDL_SetRenderLogicalPresentation(ren, LW, LH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_HideCursor();                                // the game draws its own reticle
    return 1;
}

int plat_audio_init(int rate)
{
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, rate };
    astream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!astream) { SDL_Log("audio: %s", SDL_GetError()); return 0; }
    SDL_ResumeAudioStreamDevice(astream);
    return 1;
}
void plat_audio_push(const int16_t *stereo, size_t frames)
{
    if (!astream) return;
    /* keep latency bounded (~100 ms): drop if the device is not consuming */
    if (SDL_GetAudioStreamQueued(astream) > (int)(88200 * 4 / 10)) return;
    if (audio_mute || audio_vol < 100) {
        static int16_t tmp[8192 * 2]; size_t n = frames > 8192 ? 8192 : frames; int g = audio_mute ? 0 : audio_vol * audio_vol * 256 / 10000;   /* squared curve, 8.8 fixed */
        for (size_t i = 0; i < n * 2; i++) tmp[i] = (int16_t)((stereo[i] * g) >> 8);
        SDL_PutAudioStreamData(astream, tmp, (int)(n * 4)); return;
    }
    SDL_PutAudioStreamData(astream, stereo, (int)(frames * 4));
}

void plat_shutdown(void)
{
    if (wm_on) wiimote_stop();
    if (astream) SDL_DestroyAudioStream(astream);
    if (pad) SDL_CloseGamepad(pad);
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}

static void to_logical(float wx, float wy)
{
    float lx = wx, ly = wy;
    SDL_RenderCoordinatesFromWindow(ren, wx, wy, &lx, &ly);
    mx = lx; my = ly;
}

static int test_mx = -1, test_my = -1;                                   /* tooling: token m<x>,<y>: a fake mouse position over the menu */
static void test_events(void)          /* tooling: GOLGO13_UIEVENTS=\"F1,n8,n3,kG,...\" injects one UI event every 4th poll (n<k> navigation, k<name> raw key, c click, m<x>;<y> mouse position) */
{
    static const char *env; static int init, n, wait; if (!init) { init = 1; env = SDL_getenv("GOLGO13_UIEVENTS"); } if (!env) return;
    if (++wait < 4) return; wait = 0;
    const char *p = env; for (int i = 0; i < n && p; i++) { p = SDL_strchr(p, ','); if (p) p++; } if (!p || !*p) return; n++;
    char tok[40]; size_t l = 0; while (p[l] && p[l] != ',' && l < 39) { tok[l] = p[l]; l++; } tok[l] = 0;
    if (tok[0] == 'm') { char *q = SDL_strchr(tok, ';'); test_mx = SDL_atoi(tok + 1); test_my = q ? SDL_atoi(q + 1) : 0; }
    else if (tok[0] == 'n') ev_push(6, SDL_atoi(tok + 1)); else if (tok[0] == 'c') ev_push(3, 0);
    else if (tok[0] == 'k') { SDL_Scancode c = SDL_GetScancodeFromName(tok + 1); if (c != SDL_SCANCODE_UNKNOWN) ev_push(1, (int)c); }
    else { SDL_Scancode c = SDL_GetScancodeFromName(tok); if (c != SDL_SCANCODE_UNKNOWN) ev_push(1, (int)c); }
}
void plat_poll(plat_input_t *in)
{
    SDL_Event e; test_events();
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT: quit_flag = 1; break;
        case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
            if (e.type == SDL_EVENT_KEY_DOWN) nav_key((int)e.key.scancode, e.key.repeat);
            if (e.key.scancode < SDL_SCANCODE_COUNT) key[e.key.scancode] = e.type == SDL_EVENT_KEY_DOWN;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                ev_push(1, (int)e.key.scancode);
                if (e.key.scancode == SDL_SCANCODE_ESCAPE) pause_ev = 1;
                if (e.key.scancode == SDL_SCANCODE_F3) show_reticle = !show_reticle;
                if (e.key.scancode == SDL_SCANCODE_F4) menu_ev = 1;
                if (e.key.scancode == SDL_SCANCODE_RETURN) ui_zip_ev = 1;
                if (e.key.scancode == SDL_SCANCODE_TAB) ui_dir_ev = 1;
                if (e.key.scancode == SDL_SCANCODE_F11) { fullscreen = !fullscreen; SDL_SetWindowFullscreen(win, fullscreen != 0); }
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            mwx = e.motion.x; mwy = e.motion.y;
            if (!gun_mouse || e.motion.which == gun_mouse) to_logical(e.motion.x, e.motion.y);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
            if (!gun_mouse || e.button.which == gun_mouse) {
                int d = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                if (d && e.button.button == SDL_BUTTON_LEFT) { ui_zip_ev = 1; ev_push(3, 0); }
                if (d && e.button.button == SDL_BUTTON_RIGHT) ev_push(4, 0);
                if (e.button.button == SDL_BUTTON_LEFT) lmb = d; else if (e.button.button == SDL_BUTTON_RIGHT) rmb = d;
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL: ev_push(5, e.wheel.y > 0 ? 1 : -1); break;
        case SDL_EVENT_DROP_FILE: if (e.drop.data) { SDL_strlcpy(pending, e.drop.data, sizeof pending); pending_ok = 1; } break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (pad && ((e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) ||
                        (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)))) menu_ev = 1;
            ev_push(2, (int)e.gbutton.button); nav_pad((int)e.gbutton.button);
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) ui_zip_ev = 1; else if (e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) ui_dir_ev = 1;
            break;
        case SDL_EVENT_GAMEPAD_ADDED: if (!pad) pad = SDL_OpenGamepad(e.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_REMOVED: if (pad && SDL_GetGamepadID(pad) == e.gdevice.which) { SDL_CloseGamepad(pad); pad = NULL; } break;
        default: break;
        }
    }
    int pt = 0, ps = 0, pen = 0, pu = 0, pd = 0, pst = 0, pco = 0;
    if (pad) {                                       /* right stick moves the reticle; RT/A fire, B/LT sensor, Start/Back */
        float ax = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX) / 32768.0f, ay = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) / 32768.0f;
        if (ax > -0.15f && ax < 0.15f) ax = 0; if (ay > -0.15f && ay < 0.15f) ay = 0;
        pad_ax += ax * 9.0f; pad_ay += ay * 9.0f;
        if (ax != 0 || ay != 0) { mx += ax * 9.0f; my += ay * 9.0f; }
        pt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH);
        ps = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST);
        pen = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST);
        pu = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP); pd = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        pst = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START); pco = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK);
    }
    if (wm_on) {                                     /* Wiimote + sensor bar: pointer overrides the mouse while the bar is in view */
        { static int pt; wiimote_poll(&wm); if (wm.trigger && !pt && wm.aiming) ev_push(3, 0); pt = wm.trigger; if (wm.home) pause_ev = 1; }
        if (wm.aiming) {
            float tx = wm.x * (LW - 1), ty = wm.y * (LH - 1);
            if (wm_smooth > 0 || wm_dz > 0) {                         /* exponential smoothing + dead zone (pixels) */
                if (!wsv) { wsx = tx; wsy = ty; wsv = 1; }
                float dx = tx - wsx, dy = ty - wsy, dist = SDL_sqrtf(dx * dx + dy * dy), dz = wm_dz * 1.5f;
                if (dist > dz) { float a = 1.0f - 0.93f * wm_smooth / 100.0f; wsx += dx * a; wsy += dy * a; }
                tx = wsx; ty = wsy;
            } else { wsx = tx; wsy = ty; wsv = 1; }
            mx = tx; my = ty;
        } else wsv = 0;
        pt |= wm.trigger; ps |= wm.sensor; pen |= wm.enter; pu |= wm.up; pd |= wm.down; pst |= wm.start; pco |= wm.coin;
    }
    if (mx < 0) mx = 0; if (mx > LW - 1) mx = LW - 1; if (my < 0) my = 0; if (my > LH - 1) my = LH - 1;
    /* Calibration measured on the game (tools: replay + impact marker): the game's own aim point is stretched compared with the picture,
     * impact_x = 1.107 * x_picture - 29.8 and impact_y = 1.020 * y_picture - 2.3 (x_picture = gun_x * 1.0004, y_picture = gun_y * 479 / 511).
     * We invert it so the impact lands under the pointer. */
    {
        float px = mx * 512.0f / LW, py = my;                                   /* pointer position on the 512x480 picture */
        float gx = px, gy = py * 511.0f / 479.0f;   /* picture pixels -> 0..511 gun space (the game's own calibration does the rest) */
    in->gun_x = gx < 0 ? 0 : gx > 511 ? 511 : (int)gx; in->gun_y = gy < 0 ? 0 : gy > 511 ? 511 : (int)gy;
    }
    in->trigger = lmb || pt || KEYB(0);
    in->sensor = rmb || ps || KEYB(1);
    in->enter = pen || KEYB(2);
    in->up = pu || KEYB(3); in->down = pd || KEYB(4);
    in->start = pst || KEYB(5); in->coin = pco || KEYB(6);
    in->service = KEYB(7); in->test = KEYB(8) || (wm_on && wm.test);
    in->quit = quit_flag;
    in->ui_zip = ui_zip_ev; in->ui_dir = ui_dir_ev; in->menu = menu_ev; in->pause = pause_ev; ui_zip_ev = ui_dir_ev = menu_ev = pause_ev = 0;
    in->drop_path[0] = 0;
    if (pending_ok) { SDL_strlcpy(in->drop_path, pending, sizeof in->drop_path); pending_ok = 0; }
}

static void wm_calib_screen(void)                  /* the 2-point Wiimote calibration, drawn on the LWxLH logical picture (game and menu) */
{
    SDL_SetRenderDrawColor(ren, 8, 10, 24, 255); SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, 40, 50, 90, 255);
    for (int i = 1; i < 8; i++) { SDL_RenderLine(ren, i * LW / 8.0f, 0, i * LW / 8.0f, LH); SDL_RenderLine(ren, 0, i * LH / 8.0f, LW, i * LH / 8.0f); }
    SDL_SetRenderDrawColor(ren, 120, 140, 220, 255); SDL_FRect fr = { 6, 6, LW - 12, LH - 12 }; SDL_RenderRect(ren, &fr);
    #define CTEXT(Y, SC, R, G, B, T) do { const char *t_ = (T); float sc_ = (SC); SDL_SetRenderScale(ren, sc_, sc_); SDL_SetRenderDrawColor(ren, R, G, B, 255); \
        SDL_RenderDebugText(ren, (LW - 8.0f * sc_ * (float)strlen(t_)) / 2 / sc_, (Y) / sc_, t_); SDL_SetRenderScale(ren, 1, 1); } while (0)
    CTEXT(150, 2.0f, 255, 220, 80, "CALIBRATION WIIMOTE");
    CTEXT(200, 1.0f, 235, 235, 235, wm.text[0]);
    CTEXT(216, 1.0f, 235, 235, 235, wm.text[1]);
    if (wm.text[2][0]) CTEXT(232, 1.0f, 255, 120, 80, wm.text[2]);
    char st[96];
    if (wm.dots) { snprintf(st, sizeof st, "Barre infrarouge : DETECTEE   (x=%4d  y=%4d)", (int)(wm.raw_x * 1024), (int)(wm.raw_y * 1024)); CTEXT(300, 1.0f, 100, 255, 120, st); }
    else CTEXT(300, 1.0f, 255, 90, 90, "Barre infrarouge : NON DETECTEE - allume la barre et pointe vers elle");
    if (wm.mark) {
        float x = wm.mark_x * LW, y = wm.mark_y * LH; SDL_SetRenderDrawColor(ren, 40, 255, 80, 255);
        SDL_FRect r[2] = { { x - 22, y - 2, 44, 4 }, { x - 2, y - 22, 4, 44 } }; SDL_RenderFillRects(ren, r, 2);
        SDL_FRect ring = { x - 10, y - 10, 20, 20 }; SDL_RenderRect(ren, &ring);
    } else if (wm.aiming) {                      /* verification: live crosshair */
        float x = wm.x * (LW - 1), y = wm.y * (LH - 1); SDL_SetRenderDrawColor(ren, 255, 60, 60, 255);
        SDL_FRect r[2] = { { x - 16, y - 1.5f, 32, 3 }, { x - 1.5f, y - 16, 3, 32 } }; SDL_RenderFillRects(ren, r, 2);
    }
}
void plat_present(const uint8_t *rgba, int w, int h)
{
    if (!tex || tex_w != w || tex_h != h) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        tex_w = w; tex_h = h;
    }
    SDL_SetTextureScaleMode(tex, vid.filter ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    SDL_UpdateTexture(tex, NULL, rgba, w * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_FRect dst = { 0, 0, LW, LH };
    SDL_RenderTexture(ren, tex, NULL, &dst);
    if (vid.scanlines > 0) {                         /* CRT scanlines: dark line on every other picture row */
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND); SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)(vid.scanlines * 150 / 100));
        for (int y = 1; y < LH; y += 2) { SDL_FRect r = { 0, (float)y, LW, 1 }; SDL_RenderFillRect(ren, &r); }
    }
    if (vid.show_fps) {
        uint64_t now = SDL_GetTicksNS(); fps_n++; if (now - fps_t0 > 1000000000ull) { fps_val = fps_n; fps_n = 0; fps_t0 = now; }
        char t[24]; snprintf(t, sizeof t, "%d FPS", fps_val); SDL_SetRenderDrawColor(ren, 255, 255, 0, 255); SDL_RenderDebugText(ren, 8, LH - 16, t);
    }
    if (show_reticle && vid.reticle) {          /* the arcade used a real gun with no on-screen cursor: draw our own crosshair at the aim point */
        float x = mx, y = my; const float g = 5, l = 16, t = 2;
        for (int pass = 0; pass < 2; pass++) {
            float o = pass == 0 ? 1.5f : 0;    /* pass 0: dark outline, pass 1: bright core */
            if (pass == 0) SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); else SDL_SetRenderDrawColor(ren, 255, 40, 40, 255);
            SDL_FRect r[5] = { { x - l - o, y - t / 2 - o, l - g + 2 * o, t + 2 * o }, { x + g - o, y - t / 2 - o, l - g + 2 * o, t + 2 * o },
                               { x - t / 2 - o, y - l - o, t + 2 * o, l - g + 2 * o }, { x - t / 2 - o, y + g - o, t + 2 * o, l - g + 2 * o },
                               { x - t / 2 - o, y - t / 2 - o, t + 2 * o, t + 2 * o } };
            SDL_RenderFillRects(ren, r, 5);
        }
    }
    if (wm_on && wm.calib) wm_calib_screen();                          /* full-screen Wiimote calibration */
    else if (wm_on && wm.msg) { SDL_SetRenderDrawColor(ren, 255, 255, 255, 255); SDL_RenderDebugText(ren, 12, 12, wm.msg); }
    SDL_RenderPresent(ren);
}

int plat_wiimote_pair(void) { return wiimote_pair(); }

void plat_wiimote_enable(void)
{
    char p[1100]; snprintf(p, sizeof p, "%s", plat_pref_dir());
    wm_on = wiimote_start(p);
}

uint64_t plat_time_us(void) { return SDL_GetTicksNS() / 1000; }
void plat_sleep_us(uint64_t us) { if (us > 1500) SDL_DelayNS((us - 1000) * 1000); }

static void SDLCALL dialog_cb(void *ud, const char *const *list, int filter)
{
    (void)ud; (void)filter;
    if (list && list[0]) { SDL_strlcpy(pending, list[0], sizeof pending); pending_ok = 1; }
}
void plat_choose(int folder)
{
    static const SDL_DialogFileFilter flt[] = { { "ROM zip", "zip" } };
    if (folder) SDL_ShowOpenFolderDialog(dialog_cb, NULL, win, NULL, 0);
    else SDL_ShowOpenFileDialog(dialog_cb, NULL, win, flt, 1, NULL, 0);
}
void plat_show_cursor(int show) { if (show) SDL_ShowCursor(); else SDL_HideCursor(); }
const char *plat_pref_dir(void)
{
    static char buf[1024]; static int done;
    if (!done) { done = 1; char *p = SDL_GetPrefPath("golgo13-port", "golgo13"); if (p) { SDL_strlcpy(buf, p, sizeof buf); SDL_free(p); } }
    return buf;
}
void plat_present_text(const char *const *lines, int n)
{
    SDL_SetRenderDrawColor(ren, 12, 12, 24, 255); SDL_RenderClear(ren);
    float y = 24;
    for (int i = 0; i < n; i++) {
        const char *t = lines[i]; float sc = 1.0f; Uint8 r = 200, g = 200, b = 200;
        if (*t == '#') { sc = 2.0f; r = 255; g = 220; b = 80; t++; }
        else if (*t == '!') { r = 255; g = 90; b = 90; t++; }
        else if (*t == '+') { r = 100; g = 255; b = 120; t++; }
        SDL_SetRenderScale(ren, sc, sc); SDL_SetRenderDrawColor(ren, r, g, b, 255);
        SDL_RenderDebugText(ren, 24 / sc, y / sc, t);
        y += 12 * sc + (sc > 1 ? 6 : 2);
    }
    SDL_SetRenderScale(ren, 1, 1);
    { const char *shot = SDL_getenv("GOLGO13_UISHOT");                 /* test hook: dump the text screen as PPM */
      if (shot) { SDL_Surface *sf = SDL_RenderReadPixels(ren, NULL);
        if (sf) { SDL_Surface *c = SDL_ConvertSurface(sf, SDL_PIXELFORMAT_RGB24); FILE *f = fopen(shot, "wb");
          if (c && f) { fprintf(f, "P6\n%d %d\n255\n", c->w, c->h); for (int y = 0; y < c->h; y++) fwrite((Uint8 *)c->pixels + y * c->pitch, 3, c->w, f); }
          if (f) fclose(f); SDL_DestroySurface(c); SDL_DestroySurface(sf); } } }
    SDL_RenderPresent(ren);
}

/* ---------------- menu / options support ---------------- */
int plat_ui_event(plat_ev_t *e) { if (!evq_n) return 0; *e = evq[0]; memmove(evq, evq + 1, sizeof(plat_ev_t) * (size_t)(--evq_n)); return 1; }
const char *plat_key_name(int sc) { const char *n = SDL_GetScancodeName((SDL_Scancode)sc); return n && *n ? n : "?"; }
int plat_key_from_name(const char *n) { SDL_Scancode c = SDL_GetScancodeFromName(n); return c == SDL_SCANCODE_UNKNOWN ? -1 : (int)c; }
static void apply_logical(void)
{
    SDL_RendererLogicalPresentation m = vid.integer ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE : vid.aspect == 1 ? SDL_LOGICAL_PRESENTATION_STRETCH : SDL_LOGICAL_PRESENTATION_LETTERBOX;
    SDL_SetRenderLogicalPresentation(ren, LW, LH, m);
}
void plat_video_set(const plat_video_t *v)
{
    int fs_changed = v->fullscreen != vid.fullscreen; vid = *v;
    if (!win) return;
    if (fs_changed) { fullscreen = vid.fullscreen; SDL_SetWindowFullscreen(win, fullscreen != 0); }
    SDL_SetRenderVSync(ren, vid.vsync ? 1 : 0); apply_logical();
}
int plat_ui_mouse(int *x, int *y)
{
    if (wm_on && wm.aiming) { *x = (int)(wm.x * 1279); *y = (int)(wm.y * 719); return 1; }
    if (test_mx >= 0) { *x = test_mx; *y = test_my; return 1; }
    float lx = 0, ly = 0; SDL_SetRenderLogicalPresentation(ren, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_RenderCoordinatesFromWindow(ren, mwx, mwy, &lx, &ly); apply_logical();
    *x = (int)lx; *y = (int)ly; return lx >= 0 && ly >= 0 && lx < 1280 && ly < 720;
}
void plat_present_ui(const uint8_t *rgba)
{
    { const char *shot = SDL_getenv("GOLGO13_UISHOT");                 /* test hook: dump the menu picture as PPM */
      if (shot) { char tmp[1100]; SDL_snprintf(tmp, sizeof tmp, "%s.tmp", shot); FILE *f = fopen(tmp, "wb");
        if (f) { fprintf(f, "P6\n1280 720\n255\n"); for (int i = 0; i < 1280 * 720; i++) fwrite(rgba + i * 4, 1, 3, f); fclose(f); remove(shot); rename(tmp, shot); } } }
    if (!ren) return;
    if (!ui_tex) { ui_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 1280, 720); SDL_SetTextureScaleMode(ui_tex, SDL_SCALEMODE_LINEAR); }
    SDL_SetRenderLogicalPresentation(ren, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_UpdateTexture(ui_tex, NULL, rgba, 1280 * 4);
    if (wm_on && wm.calib) { SDL_SetRenderLogicalPresentation(ren, LW, LH, SDL_LOGICAL_PRESENTATION_LETTERBOX); SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren); wm_calib_screen(); SDL_RenderPresent(ren); apply_logical(); return; }   /* started from the options page: no game running yet */
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren); SDL_RenderTexture(ren, ui_tex, NULL, NULL);
    SDL_RenderPresent(ren); apply_logical();
}
void plat_bind(int a, int slot, const char *name)
{
    if (a < 0 || a >= 9 || slot < 0 || slot > 1) return;
    int sc = (name && *name) ? plat_key_from_name(name) : -1;
    bind[a][slot] = sc >= 0 ? sc : bind_def[a][slot];
}
void plat_audio_set(int volume, int mute) { audio_vol = volume < 0 ? 0 : volume > 100 ? 100 : volume; audio_mute = mute; }
void plat_wiimote_params(int smooth, int dz) { wm_smooth = smooth; wm_dz = dz; }
int plat_wiimote_state(void) { return !wm_on ? 0 : wm.connected ? 2 : 1; }
void plat_wiimote_recalibrate(void) { if (wm_on) wiimote_recalibrate(); }
void plat_open_folder(const char *path)
{
    char url[1300]; size_t k = 0; k += (size_t)SDL_snprintf(url, sizeof url, "file:///");
    for (const char *p = path; *p && k + 4 < sizeof url; p++) { if (*p == 92) url[k++] = 47; else if (*p == 32) { url[k++] = 37; url[k++] = 50; url[k++] = 48; } else url[k++] = *p; }
    url[k] = 0; SDL_OpenURL(url);
}

/* ---------------- child processes (the in-app build screen) ---------------- */
struct plat_proc { SDL_Process *p; SDL_Thread *th; SDL_Mutex *mu; char buf[1 << 16]; int len, pos; volatile int eof; };
static int proc_reader(void *u)
{
    struct plat_proc *q = (struct plat_proc *)u; SDL_IOStream *io = SDL_GetProcessOutput(q->p); char t[512];
    while (io) {
        size_t n = SDL_ReadIO(io, t, sizeof t);
        if (n == 0) { if (SDL_GetIOStatus(io) == SDL_IO_STATUS_NOT_READY) { SDL_Delay(20); continue; } break; }   /* no data yet is not the end */
        SDL_LockMutex(q->mu);
        if (q->len + (int)n > (int)sizeof q->buf) { int drop = q->pos; memmove(q->buf, q->buf + drop, (size_t)(q->len - drop)); q->len -= drop; q->pos = 0; if (q->len + (int)n > (int)sizeof q->buf) q->len = 0; }
        memcpy(q->buf + q->len, t, n); q->len += (int)n; SDL_UnlockMutex(q->mu);
    }
    q->eof = 1; return 0;
}
plat_proc_t *plat_proc_start(const char *const *args, const char *workdir)
{
    SDL_PropertiesID pr = SDL_CreateProperties(); SDL_Environment *env = SDL_CreateEnvironment(true);
    if (env) SDL_SetEnvironmentVariable(env, "PYTHONUNBUFFERED", "1", true);
    SDL_SetPointerProperty(pr, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetNumberProperty(pr, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(pr, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    if (env) SDL_SetPointerProperty(pr, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    if (workdir) SDL_SetStringProperty(pr, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, workdir);
    SDL_Process *p = SDL_CreateProcessWithProperties(pr); SDL_DestroyProperties(pr); if (env) SDL_DestroyEnvironment(env);
    if (!p) return NULL;
    struct plat_proc *q = (struct plat_proc *)SDL_calloc(1, sizeof *q); q->p = p; q->mu = SDL_CreateMutex(); q->th = SDL_CreateThread(proc_reader, "proc_out", q); return q;
}
int plat_proc_read(plat_proc_t *q, char *buf, int n)
{
    SDL_LockMutex(q->mu); int k = q->len - q->pos; if (k > n) k = n; if (k > 0) { memcpy(buf, q->buf + q->pos, (size_t)k); q->pos += k; } else k = 0; SDL_UnlockMutex(q->mu); return k;
}
int plat_proc_done(plat_proc_t *q, int *code)
{
    int c = 0; if (!SDL_WaitProcess(q->p, false, &c)) return 0;
    SDL_LockMutex(q->mu); int pending = q->len - q->pos; SDL_UnlockMutex(q->mu);
    if (!q->eof && pending == 0) { SDL_Delay(5); }
    if (code) *code = c; return 1;
}
void plat_proc_kill(plat_proc_t *q) { if (q && q->p) SDL_KillProcess(q->p, true); }
void plat_proc_free(plat_proc_t *q) { if (!q) return; if (q->th) SDL_WaitThread(q->th, NULL); if (q->p) SDL_DestroyProcess(q->p); if (q->mu) SDL_DestroyMutex(q->mu); SDL_free(q); }
int plat_run_wait(const char *const *args)
{
    SDL_Process *p = SDL_CreateProcess(args, false); if (!p) return -1; int c = -1; SDL_WaitProcess(p, true, &c); SDL_DestroyProcess(p); return c;
}
void plat_launch(const char *exe) { const char *a[2] = { exe, NULL }; SDL_Process *p = SDL_CreateProcess(a, false); if (p) SDL_DestroyProcess(p); }
void plat_open_url(const char *url) { SDL_OpenURL(url); }
