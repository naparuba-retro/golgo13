/* Wii Remote (Wiimote) as a light gun: Bluetooth HID + the IR camera looking at a sensor bar. Windows (HID API) and Linux (hidraw); other platforms: stubs. */
#ifndef GOLGO_WIIMOTE_H
#define GOLGO_WIIMOTE_H

typedef struct {
    int connected;                  /* a Wiimote is paired and talking to us */
    int aiming;                     /* pointer valid (the bar is in view); x,y are 0..1 over the game picture */
    float x, y;
    int trigger, sensor, start, coin, enter, test, up, down;   /* B, A, +, -, 1, 2, d-pad up/down */
    const char *msg;                /* on-screen prompt (calibration / hints) or NULL */
    int mark; float mark_x, mark_y; /* calibration target to draw, 0..1 */
    int home;                       /* Home button pressed this frame (edge): opens the pause menu */
    int calib, step;                /* full-screen calibration in progress (step 0..2) */
    int dots; float raw_x, raw_y;   /* IR bar seen? raw pointer (1/1024 units) for the live readout */
    char text[3][96];               /* calibration instructions */
} wiimote_state_t;

int  wiimote_pair(void);                    /* Windows: pair a Wiimote from code (console output); returns 1 on success */
int  wiimote_start(const char *cal_path);   /* start the background reader; returns 0 if unsupported */
void wiimote_stop(void);
void wiimote_recalibrate(void);            /* restart the 2-point calibration (like the Home button) */
void wiimote_poll(wiimote_state_t *out);    /* latest state; call once per frame */

#endif
