/* GPU rasteriser unit tests: every pixel inside a mesh of adjacent triangles must be covered exactly once (additive blend counts coverage). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu.h"
static void tri(int x0, int y0, int x1, int y1, int x2, int y2)
{
    gpu_gp0_write(0x22080000u);        /* flat triangle, semi-transparent, colour (0,0,8) -> b5 = 1 */
    gpu_gp0_write((uint32_t)((y0 & 0xffff) << 16) | (uint32_t)(x0 & 0xffff));
    gpu_gp0_write((uint32_t)((y1 & 0xffff) << 16) | (uint32_t)(x1 & 0xffff));
    gpu_gp0_write((uint32_t)((y2 & 0xffff) << 16) | (uint32_t)(x2 & 0xffff));
}
static void reset_env(void)
{
    gpu_reset(); memset(gpu_vram(), 0, 2 * 1024 * 1024);
    gpu_gp0_write(0xe1000020u);                     /* abr = 1 (B+F) */
    gpu_gp0_write(0xe3000000u); gpu_gp0_write(0xe4000000u | (1023) | (1023 << 10));
    gpu_gp0_write(0xe5000000u);
}
static int count_bad(int x0, int y0, int x1, int y1, int want_inside_test, const char *name)
{
    int dbl = 0, cov = 0; (void)want_inside_test;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) { int v = (gpu_vram()[y * 1024 + x] >> 10) & 31; if (v > 1) dbl++; if (v >= 1) cov++; }
    printf("%-34s covered %6d  double-drawn %d\n", name, cov, dbl);
    return dbl;
}
int main(void)
{
    int bad = 0;
    reset_env(); tri(10, 10, 110, 10, 10, 90); tri(110, 10, 10, 90, 110, 90);
    bad += count_bad(0, 0, 200, 200, 0, "quad (2 tris) 100x80");
    { int cov = 0, minx = 999, maxx = -1, miny = 999, maxy = -1;
      for (int y = 0; y < 200; y++) for (int x = 0; x < 200; x++) if (gpu_vram()[y * 1024 + x]) { cov++; if (x < minx) minx = x; if (x > maxx) maxx = x; if (y < miny) miny = y; if (y > maxy) maxy = y; }
      printf("   area %d (want 8000); bbox x %d..%d y %d..%d (want x 10..109, y 10..89: top/left inclusive, bottom/right exclusive)\n", cov, minx, maxx, miny, maxy);
      if (cov != 8000 || minx != 10 || maxx != 109 || miny != 10 || maxy != 89) bad++; }
    reset_env(); tri(20, 20, 220, 40, 100, 150); tri(220, 40, 100, 150, 260, 160); tri(20, 20, 100, 150, 5, 120);
    bad += count_bad(0, 0, 300, 300, 0, "mesh of 3 skewed triangles");
    reset_env();   /* fan around (100,100) with 8 spokes */
    { int px[8] = { 100, 160, 200, 160, 100, 40, 0, 40 }, py[8] = { 0, 40, 100, 160, 200, 160, 100, 40 };
      for (int i = 0; i < 8; i++) tri(100, 100, px[i], py[i], px[(i + 1) & 7], py[(i + 1) & 7]); }
    bad += count_bad(0, 0, 300, 300, 0, "8-triangle fan");
    reset_env();   /* vertical / horizontal shared edges, odd coordinates */
    tri(0, 0, 33, 0, 0, 77); tri(33, 0, 0, 77, 33, 77); tri(33, 0, 91, 0, 33, 77); tri(91, 0, 33, 77, 91, 77);
    bad += count_bad(0, 0, 200, 200, 0, "4 triangles, axis-aligned edges");
    printf(bad ? "GPU TEST FAILED\n" : "GPU TEST OK\n");
    return bad ? 1 : 0;
}
