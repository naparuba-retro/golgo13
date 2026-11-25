/* Wiimote driver: a paired Wiimote (Bluetooth HID) turns on its IR camera, and the pointing position is derived from the two sensor-bar blobs
 * (roll-compensated, then mapped by a 2-point calibration).
 * Reports used: 0x12 data mode, 0x13/0x1a IR enable, 0x16 register write, 0x11 LED; input 0x33 (buttons + accel + 12 IR bytes).
 * Windows: the Windows HID API (no external library).  Linux / Steam Deck: the kernel's hidraw node of the paired Wiimote (no external library).
 * Everything above the device access (IR maths, smoothing, calibration, buttons) is shared by the two backends. */
#include "wiimote.h"

#if !defined(_WIN32) && !defined(__linux__)
int  wiimote_pair(void) { return 0; }
int  wiimote_start(const char *cal_path) { (void)cal_path; return 0; }
void wiimote_stop(void) {}
void wiimote_recalibrate(void) {}
void wiimote_poll(wiimote_state_t *out) { out->connected = out->aiming = 0; out->msg = 0; out->mark = 0; out->trigger = out->sensor = out->start = out->coin = out->enter = out->test = out->up = out->down = 0; }
#else
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <bluetoothapis.h>
#include <setupapi.h>
#include <hidsdi.h>
#else
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <dirent.h>
#include <time.h>
#endif

#define REPORT_LEN 22

static volatile int stop_flag;
static volatile int connected;
static uint16_t btn;                         /* core buttons, as sent (byte1 | byte2 << 8) */
static int have_dots; static float raw_x, raw_y;   /* roll-compensated midpoint, units of 1024 */
static char cal_file[1024];
#ifdef __linux__
static volatile int g_perm;                  /* a Wiimote is there but its hidraw node cannot be opened (permissions) */
#endif

/* ---- platform primitives ---- */
#ifdef _WIN32
static CRITICAL_SECTION cs; static int cs_ready;
#define LOCK()   EnterCriticalSection(&cs)
#define UNLOCK() LeaveCriticalSection(&cs)
static float wm_dt(void)                      /* seconds since the previous report (clamped) */
{
    static LARGE_INTEGER t0, fq; LARGE_INTEGER t1; QueryPerformanceCounter(&t1); if (!fq.QuadPart) QueryPerformanceFrequency(&fq);
    float dt = t0.QuadPart ? (float)(t1.QuadPart - t0.QuadPart) / (float)fq.QuadPart : 0.01f; t0 = t1;
    if (dt < 0.002f) dt = 0.002f; if (dt > 0.1f) dt = 0.1f;
    return dt;
}
#else
static pthread_mutex_t cs = PTHREAD_MUTEX_INITIALIZER;
#define LOCK()   pthread_mutex_lock(&cs)
#define UNLOCK() pthread_mutex_unlock(&cs)
static void Sleep(int ms) { struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&t, NULL); }
static float wm_dt(void)
{
    static double t0; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    double t1 = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9; float dt = t0 > 0 ? (float)(t1 - t0) : 0.01f; t0 = t1;
    if (dt < 0.002f) dt = 0.002f; if (dt > 0.1f) dt = 0.1f;
    return dt;
}
#endif

static int wm_write(const uint8_t *buf);      /* one output report to the device (defined by the backend below) */

/* ---- protocol (shared) ---- */
static int wm_report(int id, int a, int b)
{
    uint8_t buf[REPORT_LEN]; memset(buf, 0, sizeof buf); buf[0] = (uint8_t)id; buf[1] = (uint8_t)a; buf[2] = (uint8_t)b;
    return wm_write(buf);
}
static int wm_reg(uint32_t addr, const uint8_t *d, int n)     /* write control-register space */
{
    uint8_t buf[REPORT_LEN]; memset(buf, 0, sizeof buf);
    buf[0] = 0x16; buf[1] = 0x04; buf[2] = (uint8_t)(addr >> 16); buf[3] = (uint8_t)(addr >> 8); buf[4] = (uint8_t)addr; buf[5] = (uint8_t)n;
    memcpy(buf + 6, d, n);
    return wm_write(buf);
}

static int wm_init(void)
{
    static const uint8_t s1[9] = { 0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0xaa, 0x00, 0x64 }, s2[2] = { 0x63, 0x03 };   /* sensitivity level 3 */
    uint8_t v;
    if (!wm_report(0x11, 0x10, 0)) return 0;          /* LED 1 on: tells the user it is connected */
    wm_report(0x13, 0x04, 0); wm_report(0x1a, 0x04, 0);   /* IR camera pixel clock + logic */
    v = 0x08; wm_reg(0xb00030, &v, 1);
    wm_reg(0xb00000, s1, 9); wm_reg(0xb0001a, s2, 2);
    v = 0x03; wm_reg(0xb00033, &v, 1);                /* extended IR mode: 3 bytes per blob */
    v = 0x08; wm_reg(0xb00030, &v, 1);
    wm_report(0x12, 0x04, 0x33);                      /* continuous reports: buttons + accel + 12 IR bytes */
    return 1;
}

/* One Euro filter (Casiez et al.): strong smoothing when the pointer is nearly still (kills IR jitter), little when it moves fast (no lag).
 * Units: pointer in 1/1024 of the camera range, time in seconds. GOLGO13_WM_SMOOTH scales the resting cutoff (lower = smoother, default 1). */
typedef struct { int init; float x, dx; } oe_t;
static oe_t f_x, f_y;
static float oe_alpha(float cutoff, float dt) { float tau = 1.0f / (6.2831853f * cutoff); return 1.0f / (1.0f + tau / dt); }
static float oe_filter(oe_t *f, float v, float dt)
{
    static float k = -1;
    if (k < 0) { const char *e = getenv("GOLGO13_WM_SMOOTH"); k = e ? (float)atof(e) : 1.0f; if (k < 0.05f) k = 0.05f; }
    const float mincut = 1.2f * k, beta = 25.0f, dcut = 1.0f;
    if (!f->init) { f->init = 1; f->x = v; f->dx = 0; return v; }
    float d = (v - f->x) / dt;
    f->dx += oe_alpha(dcut, dt) * (d - f->dx);
    float cutoff = mincut + beta * fabsf(f->dx);
    f->x += oe_alpha(cutoff, dt) * (v - f->x);
    return f->x;
}

static void handle_report(const uint8_t *r)
{
    if (r[0] != 0x33) return;
    float dt = wm_dt();
    uint16_t b = (uint16_t)(r[1] | (r[2] << 8));
    float px[4], py[4]; int n = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *d = r + 6 + i * 3;
        int x = d[0] | ((d[2] >> 4) & 3) << 8, y = d[1] | ((d[2] >> 6) & 3) << 8;
        if (x == 1023 && y == 1023) continue;                          /* blob not seen */
        px[n] = (float)x; py[n] = (float)y; n++;
    }
    int ok = 0; float mx = 0, my = 0;
    if (n >= 2) {                                                      /* the two blobs farthest apart = the two ends of the bar */
        int a = 0, c = 1; float best = -1;
        for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {
            float dx = px[j] - px[i], dy = py[j] - py[i], q = dx * dx + dy * dy;
            if (q > best) { best = q; a = i; c = j; }
        }
        if (px[a] > px[c]) { int t = a; a = c; c = t; }
        float ang = atan2f(py[c] - py[a], px[c] - px[a]), ca = cosf(ang), sa = sinf(ang);
        float cx = (px[a] + px[c]) * 0.5f - 512.0f, cy = (py[a] + py[c]) * 0.5f - 384.0f;
        mx = (cx * ca + cy * sa + 512.0f) / 1024.0f;                    /* undo the roll of the Wiimote around its axis */
        my = (-cx * sa + cy * ca + 384.0f) / 1024.0f;
        ok = 1;
    }
    LOCK();
    btn = b;
    if (ok) { if (!have_dots) { f_x.init = f_y.init = 0; } raw_x = oe_filter(&f_x, mx, dt); raw_y = oe_filter(&f_y, my, dt); have_dots = 1; }
    else have_dots = 0;
    UNLOCK();
}

#ifdef _WIN32
/* ---- Windows backend ---- */
static HANDLE thread;
static HANDLE dev = INVALID_HANDLE_VALUE;

static HANDLE find_wiimote(void)
{
    GUID g; HidD_GetHidGuid(&g);
    HDEVINFO set = SetupDiGetClassDevsA(&g, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    HANDLE found = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA id; id.cbSize = sizeof id;
    for (DWORD i = 0; found == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(set, NULL, &g, i, &id); i++) {
        DWORD need = 0; SetupDiGetDeviceInterfaceDetailA(set, &id, NULL, 0, &need, NULL);
        if (!need) continue;
        SP_DEVICE_INTERFACE_DETAIL_DATA_A *d = (SP_DEVICE_INTERFACE_DETAIL_DATA_A *)malloc(need);
        if (!d) continue;
        d->cbSize = sizeof *d;
        if (SetupDiGetDeviceInterfaceDetailA(set, &id, d, need, NULL, NULL)) {
            HANDLE h = CreateFileA(d->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES at; at.Size = sizeof at;
                if (HidD_GetAttributes(h, &at) && at.VendorID == 0x057E && (at.ProductID == 0x0306 || at.ProductID == 0x0330)) found = h;
                else CloseHandle(h);
            }
        }
        free(d);
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

static int wm_write(const uint8_t *buf)
{
    OVERLAPPED ov; memset(&ov, 0, sizeof ov); ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD w = 0; BOOL ok = WriteFile(dev, buf, REPORT_LEN, &w, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 500) == WAIT_OBJECT_0) ok = GetOverlappedResult(dev, &ov, &w, FALSE);
        else { CancelIo(dev); ok = FALSE; }
    }
    CloseHandle(ov.hEvent);
    if (!ok) ok = HidD_SetOutputReport(dev, (PVOID)buf, REPORT_LEN);   /* some Bluetooth stacks only accept this */
    Sleep(40);
    return ok;
}

static DWORD WINAPI worker(LPVOID arg)
{
    (void)arg;
    while (!stop_flag) {
        dev = find_wiimote();
        if (dev == INVALID_HANDLE_VALUE) { for (int i = 0; i < 10 && !stop_flag; i++) Sleep(100); continue; }
        if (!wm_init()) { CloseHandle(dev); dev = INVALID_HANDLE_VALUE; Sleep(1000); continue; }
        connected = 1;
        OVERLAPPED ov; memset(&ov, 0, sizeof ov); ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        uint8_t buf[REPORT_LEN];
        while (!stop_flag) {
            DWORD got = 0; ResetEvent(ov.hEvent);
            BOOL ok = ReadFile(dev, buf, sizeof buf, &got, &ov);
            if (!ok && GetLastError() == ERROR_IO_PENDING) {
                DWORD w;
                while ((w = WaitForSingleObject(ov.hEvent, 200)) == WAIT_TIMEOUT && !stop_flag) {}
                if (w == WAIT_OBJECT_0) ok = GetOverlappedResult(dev, &ov, &got, FALSE);
                else { CancelIo(dev); break; }
            }
            if (!ok) break;                                             /* unplugged / powered off */
            if (got >= 6) handle_report(buf);
        }
        CloseHandle(ov.hEvent);
        connected = 0; LOCK(); btn = 0; have_dots = 0; UNLOCK();
        CloseHandle(dev); dev = INVALID_HANDLE_VALUE;
    }
    return 0;
}

/* ---- pairing without the Windows dialog (which asks for a PIN it cannot take) ----
 * The Wiimote PIN is binary: the 6 address bytes of the host radio (1+2 method) or of the Wiimote itself (SYNC method). */
static BYTE g_pin[6];
static BOOL CALLBACK pin_cb(LPVOID ctx, PBLUETOOTH_AUTHENTICATION_CALLBACK_PARAMS p)
{
    (void)ctx;
    BLUETOOTH_AUTHENTICATE_RESPONSE r; memset(&r, 0, sizeof r);
    r.bthAddressRemote = p->deviceInfo.Address; r.authMethod = p->authenticationMethod;
    memcpy(r.pinInfo.pin, g_pin, 6); r.pinInfo.pinLength = 6; r.negativeResponse = FALSE;
    DWORD e = BluetoothSendAuthenticationResponseEx(NULL, &r);
    printf("  PIN requested -> answered (%lu)\n", (unsigned long)e); fflush(stdout);
    return TRUE;
}

int wiimote_pair(void)
{
    static const GUID hid_uuid = { 0x00001124, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };
    BLUETOOTH_FIND_RADIO_PARAMS rp; rp.dwSize = sizeof rp; HANDLE hr = NULL;
    HBLUETOOTH_RADIO_FIND rf = BluetoothFindFirstRadio(&rp, &hr);
    if (!rf) { printf("No Bluetooth adapter found.\n"); return 0; }
    BluetoothFindRadioClose(rf);
    BLUETOOTH_RADIO_INFO ri; memset(&ri, 0, sizeof ri); ri.dwSize = sizeof ri; BluetoothGetRadioInfo(hr, &ri);
    printf("Hold the red SYNC button (under the battery cover), or 1+2, until pairing ends...\n"); fflush(stdout);
    for (int attempt = 0; attempt < 12; attempt++) {
        BLUETOOTH_DEVICE_SEARCH_PARAMS sp; memset(&sp, 0, sizeof sp); sp.dwSize = sizeof sp;
        sp.fReturnAuthenticated = sp.fReturnRemembered = sp.fReturnUnknown = sp.fReturnConnected = TRUE; sp.fIssueInquiry = TRUE;
        sp.cTimeoutMultiplier = 3; sp.hRadio = hr;
        BLUETOOTH_DEVICE_INFO di; memset(&di, 0, sizeof di); di.dwSize = sizeof di;
        HBLUETOOTH_DEVICE_FIND df = BluetoothFindFirstDevice(&sp, &di);
        int found = 0;
        for (int ok = df != NULL; ok; ok = BluetoothFindNextDevice(df, &di)) {
            if (wcsncmp(di.szName, L"Nintendo RVL-CNT", 16) != 0) continue;
            found = 1; printf("Found %ls, pairing...\n", di.szName); fflush(stdout);
            BluetoothRemoveDevice(&di.Address);                              /* drop any half-paired entry */
            const BYTE *pins[2] = { ri.address.rgBytes, di.Address.rgBytes };
            static int turn = 1;                                                 /* one method per sighting: a failed try drops the link */
            for (int k = 0; k < 1; k++) {
                int m = (turn++ & 1) ? 0 : 1;                                /* SYNC first, then 1+2, alternating */
                BLUETOOTH_DEVICE_INFO d2 = di;
                memcpy(g_pin, pins[m], 6);
                HBLUETOOTH_AUTHENTICATION_REGISTRATION reg = NULL;
                BluetoothRegisterForAuthenticationEx(&d2, &reg, pin_cb, NULL);
                DWORD r = BluetoothAuthenticateDeviceEx(NULL, hr, &d2, NULL, MITMProtectionNotRequired);
                if (reg) BluetoothUnregisterAuthentication(reg);
                if (r != ERROR_SUCCESS) r = BluetoothAuthenticateDevice(NULL, hr, &d2, (PWSTR)pins[m], 6);
                if (r == ERROR_SUCCESS) {
                    DWORD s = BluetoothSetServiceState(hr, &d2, &hid_uuid, BLUETOOTH_SERVICE_ENABLE);
                    printf("Paired (%s method). HID service: %s. You can now start the game with --wiimote.\n", m ? "SYNC" : "1+2", s == ERROR_SUCCESS ? "enabled" : "enable failed");
                    BluetoothFindDeviceClose(df); CloseHandle(hr); return 1;
                }
                printf("  PIN method %d: error %lu\n", m + 1, (unsigned long)r);
            }
            break;
        }
        if (df) BluetoothFindDeviceClose(df);
        if (!found) { printf("  no Wiimote seen yet (%d/12) - keep SYNC pressed\n", attempt + 1); fflush(stdout); }
    }
    CloseHandle(hr);
    printf("Pairing failed.\n");
    return 0;
}

static int backend_running(void) { return thread != NULL; }
static int backend_start(void)
{
    InitializeCriticalSection(&cs); cs_ready = 1;
    stop_flag = 0;
    thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    return thread != NULL;
}
static void backend_stop(void)
{
    if (!thread) return;
    stop_flag = 1; WaitForSingleObject(thread, 2000); CloseHandle(thread); thread = NULL;
}
#else
/* ---- Linux backend: the /dev/hidraw node of the paired Wiimote ----
 * The kernel's hid-wiimote driver keeps the device bound and exposes it through hidraw, which carries the raw reports in both directions:
 * the same protocol as on Windows. Needs read/write access to the node (the udev rule that tags hidraw devices with uaccess, as Steam installs). */
static pthread_t th; static int th_on; static int fd = -1;

static int is_wiimote_line(const char *line)             /* a line of the hidraw device's uevent file */
{
    unsigned bus, vid, pid;
    if (!strncmp(line, "HID_NAME=Nintendo RVL-CNT", 25)) return 1;
    if (sscanf(line, "HID_ID=%x:%x:%x", &bus, &vid, &pid) != 3) return 0;
    return vid == 0x057E && (pid == 0x0306 || pid == 0x0330);
}
static int find_wiimote(void)                             /* opens the device into fd; 1 if found */
{
    const char *force = getenv("GOLGO13_WM_DEVICE");     /* a given node (test tool) instead of the scan */
    char path[200]; int perm = 0;
    if (force && *force) { fd = open(force, O_RDWR | O_NONBLOCK); g_perm = fd < 0 && errno == EACCES; return fd >= 0; }
    DIR *d = opendir("/sys/class/hidraw"); if (!d) return 0;
    struct dirent *e;
    while (fd < 0 && (e = readdir(d))) {
        if (strncmp(e->d_name, "hidraw", 6)) continue;
        snprintf(path, sizeof path, "/sys/class/hidraw/%s/device/uevent", e->d_name);
        FILE *f = fopen(path, "r"); if (!f) continue;
        char ln[256]; int ok = 0;
        while (!ok && fgets(ln, sizeof ln, f)) ok = is_wiimote_line(ln);
        fclose(f); if (!ok) continue;
        snprintf(path, sizeof path, "/dev/%s", e->d_name);
        fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0 && errno == EACCES) perm = 1;
    }
    closedir(d); g_perm = fd < 0 && perm;
    return fd >= 0;
}

static int wm_write(const uint8_t *buf)
{
    if (fd < 0) return 0;
    ssize_t w = -1;
    for (int tries = 0; tries < 50; tries++) {
        w = write(fd, buf, REPORT_LEN);
        if (w >= 0 || (errno != EINTR && errno != EAGAIN)) break;
        Sleep(2);
    }
    Sleep(40);
    return w > 0;
}

static void *worker(void *arg)
{
    (void)arg; uint8_t buf[64];
    while (!stop_flag) {
        if (!find_wiimote()) { for (int i = 0; i < 10 && !stop_flag; i++) Sleep(100); continue; }
        if (!wm_init()) { close(fd); fd = -1; Sleep(1000); continue; }
        connected = 1; int foreign = 0;
        struct pollfd p; p.fd = fd; p.events = POLLIN; p.revents = 0;
        while (!stop_flag) {
            int r = poll(&p, 1, 200);
            if (r < 0) { if (errno == EINTR) continue; break; }
            if (r == 0) continue;
            if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) break;           /* powered off / unpaired */
            ssize_t got = read(fd, buf, sizeof buf);
            if (got < 0) { if (errno == EAGAIN || errno == EINTR) continue; break; }
            if (got >= 6 && buf[0] == 0x33) { handle_report(buf); foreign = 0; }
            else if (got >= 1 && buf[0] >= 0x30 && buf[0] <= 0x3f && ++foreign >= 60) { wm_init(); foreign = 0; }   /* the kernel driver changed the data mode: take it back */
        }
        connected = 0; LOCK(); btn = 0; have_dots = 0; UNLOCK();
        close(fd); fd = -1;
    }
    return NULL;
}

int wiimote_pair(void)
{
    printf("Linux / Steam Deck: pair the Wiimote with the system Bluetooth settings (desktop mode > Bluetooth), no PIN needed:\n"
           "press the red SYNC button (under the battery cover), or 1+2, then choose \"Nintendo RVL-CNT-01\".\nWaiting for it...\n"); fflush(stdout);
    for (int i = 0; i < 240; i++) {
        if (find_wiimote()) { close(fd); fd = -1; printf("Wiimote found. You can now start the game with --wiimote.\n"); return 1; }
        if (g_perm && i % 10 == 0) { printf("  a Wiimote is there but access to its /dev/hidraw node is denied (udev rule needed).\n"); fflush(stdout); }
        Sleep(500);
    }
    printf("No Wiimote found.\n");
    return 0;
}

static int backend_running(void) { return th_on; }
static int backend_start(void) { stop_flag = 0; th_on = pthread_create(&th, NULL, worker, NULL) == 0; return th_on; }
static void backend_stop(void) { if (!th_on) return; stop_flag = 1; pthread_join(th, NULL); th_on = 0; }
#endif

/* ---- main-thread side: calibration and mapping (shared) ---- */
static int cal_ok, cal_step = -1;
void wiimote_recalibrate(void) { cal_step = 0; }            /* cal_step: -1 idle, 0 = top-left target, 1 = bottom-right target */
static float c0x, c0y, c1x, c1y;
static uint16_t prev_btn;
#define MARGIN 0.10f

static void cal_load(void)
{
    FILE *f = fopen(cal_file, "r");
    if (f) { if (fscanf(f, "%f %f %f %f", &c0x, &c0y, &c1x, &c1y) == 4 && c1x != c0x && c1y != c0y) cal_ok = 1; fclose(f); }
}
static void cal_save(void)
{
    FILE *f = fopen(cal_file, "w");
    if (f) { fprintf(f, "%f %f %f %f\n", c0x, c0y, c1x, c1y); fclose(f); }
}

int wiimote_start(const char *cal_path)
{
    if (backend_running()) return 1;
    snprintf(cal_file, sizeof cal_file, "%s%s", cal_path ? cal_path : "", "wiimote.cal");
    cal_load();
    return backend_start();
}
void wiimote_stop(void) { backend_stop(); }

void wiimote_poll(wiimote_state_t *o)
{
    static char msg[128]; static int wait_release;
    memset(o, 0, sizeof *o);
    if (!backend_running()) return;
    uint16_t b; int dots; float rx, ry;
    LOCK(); b = btn; dots = have_dots; rx = raw_x; ry = raw_y; UNLOCK();
    o->connected = connected;
    if (!o->connected) { cal_step = -1; prev_btn = 0; o->msg = "WIIMOTE: appuie sur un bouton (ou 1+2) pour la connecter";
#ifdef __linux__
        if (g_perm) o->msg = "WIIMOTE: acces refuse a /dev/hidraw (regle udev)";
#endif
        return; }
    uint16_t edge = (uint16_t)(b & ~prev_btn); prev_btn = b;
    /* core button bits: byte1 = d-pad L 1, R 2, D 4, U 8, + 0x10 ; byte2 (<<8) = 2 0x01, 1 0x02, B 0x04, A 0x08, - 0x10, Home 0x80 */
    const uint16_t UP = 0x0008, DOWN = 0x0004, PLUS = 0x0010, TWO = 0x0100, ONE = 0x0200, BB = 0x0400, AA = 0x0800, MINUS = 0x1000, HOME = 0x8000;
    if (wait_release) { if (b & AA) b &= (uint16_t)~AA; else wait_release = 0; }   /* the A that closed the calibration must not fire */
    if (!cal_ok && cal_step < 0) cal_step = 0;                       /* first use: calibration screen */
    if (edge & HOME) o->home = 1;                                    /* opens the pause menu; recalibrate from Options > Wiimote */
    o->dots = dots; o->raw_x = rx; o->raw_y = ry;
    float mxn = 0, myn = 0;
    if (cal_ok && (cal_step < 0 || cal_step == 2)) {                 /* mapped pointer (also used for the verification step) */
        mxn = MARGIN + (1 - 2 * MARGIN) * (rx - c0x) / (c1x - c0x); myn = MARGIN + (1 - 2 * MARGIN) * (ry - c0y) / (c1y - c0y);
        mxn = mxn < 0 ? 0 : mxn > 1 ? 1 : mxn; myn = myn < 0 ? 0 : myn > 1 ? 1 : myn;
    }
    if (cal_step >= 0) {
        o->calib = 1; o->step = cal_step;
        if (cal_step < 2) {
            o->mark = 1; o->mark_x = o->mark_y = cal_step == 0 ? MARGIN : 1 - MARGIN;
            snprintf(o->text[0], sizeof o->text[0], "ETAPE %d/3 : vise la croix verte %s", cal_step + 1, cal_step == 0 ? "en HAUT A GAUCHE" : "en BAS A DROITE");
            snprintf(o->text[1], sizeof o->text[1], "puis appuie sur A  (la manette reste pointee vers l'ecran)");
            if ((edge & AA) && dots) {
                if (cal_step == 0) { c0x = rx; c0y = ry; cal_step = 1; }
                else if (fabsf(rx - c0x) > 0.02f && fabsf(ry - c0y) > 0.02f) { c1x = rx; c1y = ry; cal_ok = 1; cal_step = 2; }
                else snprintf(o->text[2], sizeof o->text[2], "points trop proches : recommence cette etape");
            }
        } else {
            o->aiming = dots; o->x = mxn; o->y = myn;
            snprintf(o->text[0], sizeof o->text[0], "ETAPE 3/3 : verification - bouge la manette, la croix doit suivre");
            snprintf(o->text[1], sizeof o->text[1], "A = valider    Home = recommencer");
            if ((edge & AA)) { cal_save(); cal_step = -1; wait_release = 1; }
        }
        return;
    }
    if (!dots) { o->msg = "WIIMOTE: vise la barre infrarouge"; }
    else { o->x = mxn; o->y = myn; o->aiming = 1; }
    o->trigger = !!(b & BB); o->sensor = !!(b & AA); o->start = !!(b & PLUS); o->coin = !!(b & MINUS);
    o->enter = !!(b & ONE); o->test = !!(b & TWO); o->up = !!(b & UP); o->down = !!(b & DOWN);
}
#endif
