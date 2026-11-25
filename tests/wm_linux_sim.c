/* Linux Wiimote driver test without a Wiimote: a pseudo-terminal stands in for the /dev/hidraw node (GOLGO13_WM_DEVICE).
 * The test plays the Wiimote: it sends 0x33 reports (buttons + two IR blobs) and checks what the driver reports.
 *   cc -O1 -Wall -Iruntime/platform tests/wm_linux_sim.c -lm -lpthread -o wm_sim && ./wm_sim */
#define _GNU_SOURCE
#include "../runtime/platform/wiimote.c"
#include <termios.h>
#include <sys/stat.h>

static int master = -1; static volatile int btns, blobs = 2, run = 1, sent_init;
static void *sim(void *a)
{
    (void)a; uint8_t in[64];
    while (run) {
        ssize_t n = read(master, in, sizeof in); if (n > 0) sent_init += (int)n;     /* what the driver writes to the "Wiimote" (init sequence) */
        uint8_t r[18]; memset(r, 0, sizeof r); r[0] = 0x33; r[1] = btns & 0xff; r[2] = (btns >> 8) & 0xff;
        for (int i = 0; i < 4; i++) { uint8_t *d = r + 6 + i * 3; d[0] = 0xff; d[1] = 0xff; d[2] = 0xff; }   /* 1023,1023 = not seen */
        if (blobs >= 2) {                                                          /* bar ends at (400,384) and (624,384): centre (512,384) */
            int xs[2] = { 400, 624 }, y = 384;
            for (int i = 0; i < 2; i++) { uint8_t *d = r + 6 + i * 3; d[0] = xs[i] & 255; d[1] = y & 255; d[2] = (uint8_t)(((y >> 8) << 6) | ((xs[i] >> 8) << 4) | 5); }
        }
        if (write(master, r, sizeof r) < 0) {}
        usleep(10000);
    }
    return NULL;
}
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
    master = posix_openpt(O_RDWR | O_NOCTTY); if (master < 0 || grantpt(master) || unlockpt(master)) { puts("no pty"); return 2; }
    int fl = fcntl(master, F_GETFL); fcntl(master, F_SETFL, fl | O_NONBLOCK);
    char *sl = ptsname(master); int keep = open(sl, O_RDWR | O_NOCTTY);
    struct termios t; tcgetattr(keep, &t); cfmakeraw(&t); tcsetattr(keep, TCSANOW, &t);
    setenv("GOLGO13_WM_DEVICE", sl, 1);
    FILE *cf = fopen("/tmp/wm_sim_wiimote.cal", "w"); fprintf(cf, "0.25 0.25 0.75 0.75\n"); fclose(cf);     /* calibration already done */
    pthread_t st; pthread_create(&st, NULL, sim, NULL);
    CHECK(wiimote_start("/tmp/wm_sim_"), "start");
    wiimote_state_t s; int ok = 0;
    for (int i = 0; i < 100 && !ok; i++) { usleep(100000); wiimote_poll(&s); ok = s.connected && s.aiming; }
    CHECK(ok, "connected + aiming (connected=%d aiming=%d dots=%d)", s.connected, s.aiming, s.dots);
    CHECK(sent_init >= 22 * 5, "init reports sent (%d bytes)", sent_init);
    usleep(400000); wiimote_poll(&s);
    CHECK(fabsf(s.x - 0.5f) < 0.02f && fabsf(s.y - 0.3f) < 0.02f, "pointer %.3f,%.3f (expected 0.5,0.3)", s.x, s.y);
    CHECK(!s.trigger, "trigger idle");
    btns = 0x0400 | 0x0010; usleep(200000); wiimote_poll(&s);                    /* B + plus */
    CHECK(s.trigger && s.start && !s.sensor, "B/+ (trigger=%d start=%d sensor=%d)", s.trigger, s.start, s.sensor);
    btns = 0x0800 | 0x0008; usleep(200000); wiimote_poll(&s);                    /* A + up */
    CHECK(s.sensor && s.up && !s.trigger, "A/up");
    btns = 0; blobs = 0; usleep(300000); wiimote_poll(&s);                      /* bar out of view */
    CHECK(s.connected && !s.aiming, "no blobs -> not aiming");
    blobs = 2; run = 0; pthread_join(st, NULL); close(master); master = -1;      /* the Wiimote goes away */
    ok = 0; for (int i = 0; i < 30 && !ok; i++) { usleep(100000); wiimote_poll(&s); ok = !s.connected; }
    CHECK(ok, "disconnect noticed");
    wiimote_stop(); remove("/tmp/wm_sim_wiimote.cal");
    puts(fails ? "wm_linux_sim: FAILED" : "wm_linux_sim: OK");
    return fails != 0;
}
