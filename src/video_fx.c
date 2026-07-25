#include "video_fx.h"
#include <string.h>

#define MAXW 720
#define MAXH 576

/* internal scratch buffers (max 2x output) */
static uint8  src888[MAXW * MAXH * 3];
static uint8  out888[(MAXW * 2) * (MAXH * 2) * 3];
static uint16 out565[(MAXW * 2) * (MAXH * 2)];

/* ---- 565 <-> 888 helpers ---- */
static void c565(uint16 v, int *r, int *g, int *b)
{
  *r = ((v >> 11) & 0x1f) * 255 / 31;
  *g = ((v >> 5)  & 0x3f) * 255 / 63;
  *b = ( v        & 0x1f) * 255 / 31;
}
static uint16 p565(int r, int g, int b)
{
  if (r < 0) r = 0; else if (r > 255) r = 255;
  if (g < 0) g = 0; else if (g > 255) g = 255;
  if (b < 0) b = 0; else if (b > 255) b = 255;
  return (uint16)(((r * 31 / 255) << 11) | ((g * 63 / 255) << 5) | (b * 31 / 255));
}

/* ---- 888 colour helpers ---- */
static inline int same(uint32 a, uint32 b) { return a == b; }

static inline uint32 avg(uint32 a, uint32 b)
{
  uint8 ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
  uint8 br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
  return (((ar + br) / 2) << 16) | (((ag + bg) / 2) << 8) | ((ab + bb) / 2);
}
static inline uint32 avg4(uint32 a, uint32 b, uint32 c, uint32 d)
{
  return avg(avg(a, b), avg(c, d));
}

static inline uint32 getc(const uint8 *buf, int w, int h, int x, int y)
{
  if (x < 0) x = 0; else if (x >= w) x = w - 1;
  if (y < 0) y = 0; else if (y >= h) y = h - 1;
  const uint8 *p = buf + (y * w + x) * 3;
  return ((uint32)p[0] << 16) | ((uint32)p[1] << 8) | p[2];
}
static inline void putc(uint8 *buf, int w, int x, int y, uint32 c)
{
  uint8 *p = buf + (y * w + x) * 3;
  p[0] = (c >> 16) & 255; p[1] = (c >> 8) & 255; p[2] = c & 255;
}

/* ---- 2x upscalers (operate on RGB888 src w*h -> dst 2w*2h) ---- */
static void upscale2x(uint8 *src, int w, int h, uint8 *dst, int mode)
{
  int W = 2 * w;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint32 P = getc(src, w, h, x, y);
      uint32 A = getc(src, w, h, x, y - 1);
      uint32 B = getc(src, w, h, x - 1, y);
      uint32 C = getc(src, w, h, x + 1, y);
      uint32 D = getc(src, w, h, x, y + 1);
      uint32 E, F, G, H;

      int sE = (same(A, B) && !same(A, C) && !same(B, D));
      int sF = (same(A, C) && !same(A, B) && !same(C, D));
      int sG = (same(B, D) && !same(B, A) && !same(D, C));
      int sH = (same(C, D) && !same(C, A) && !same(D, B));

      if (mode == 2) {                 /* Scale2x: strict, sharp */
        E = sE ? A : P;
        F = sF ? C : P;
        G = sG ? D : P;
        H = sH ? D : P;
      } else if (mode == 3) {          /* 2xSAI: smooth non-edge pixels */
        uint32 n = avg4(A, B, C, D);
        E = sE ? A : n;
        F = sF ? C : n;
        G = sG ? D : n;
        H = sH ? D : n;
      } else {                         /* Hq2x: blend even edge pixels */
        E = sE ? avg(A, P) : avg4(A, B, C, D);
        F = sF ? avg(C, P) : avg4(A, B, C, D);
        G = sG ? avg(D, P) : avg4(A, B, C, D);
        H = sH ? avg(D, P) : avg4(A, B, C, D);
      }
      putc(dst, W, 2 * x,     2 * y,     E);
      putc(dst, W, 2 * x + 1, 2 * y,     F);
      putc(dst, W, 2 * x,     2 * y + 1, G);
      putc(dst, W, 2 * x + 1, 2 * y + 1, H);
    }
  }
}

/* ---- per-pixel colour adjust + scanline on 888 buffer ---- */
static void apply_effects(uint8 *buf, int w, int h,
                          int scanline, int greyscale,
                          int brightness, int contrast)
{
  int f = 100 + contrast;
  int bo = brightness * 255 / 100;
  float scanf = 1.0f;
  if (scanline == 1) scanf = 0.82f;      /* 25% */
  else if (scanline == 2) scanf = 0.62f; /* 50% */
  else if (scanline == 3) scanf = 0.40f; /* 100% */

  for (int y = 0; y < h; y++) {
    int scan_dark = (scanline != 0 && (y & 1)) ? scanf : 1.0f;
    for (int x = 0; x < w; x++) {
      uint8 *p = buf + (y * w + x) * 3;
      int r = p[0], g = p[1], b = p[2];
      r += bo; g += bo; b += bo;
      r = (r - 128) * f / 100 + 128;
      g = (g - 128) * f / 100 + 128;
      b = (b - 128) * f / 100 + 128;
      if (greyscale) {
        int l = (r * 299 + g * 587 + b * 114) / 1000;
        r = g = b = l;
      }
      if (scan_dark != 1.0f) { r = (int)(r * scan_dark); g = (int)(g * scan_dark); b = (int)(b * scan_dark); }
      if (r < 0) r = 0; else if (r > 255) r = 255;
      if (g < 0) g = 0; else if (g > 255) g = 255;
      if (b < 0) b = 0; else if (b > 255) b = 255;
      p[0] = (uint8)r; p[1] = (uint8)g; p[2] = (uint8)b;
    }
  }
}

uint16 *video_fx_process(const uint16 *src, int pitch, int vw, int vh,
                         int render_mode, int scanline, int greyscale,
                         int brightness, int contrast,
                         int *out_w, int *out_h)
{
  if (vw <= 0 || vh <= 0) { *out_w = 0; *out_h = 0; return NULL; }
  if (vw > MAXW) vw = MAXW;
  if (vh > MAXH) vh = MAXH;
  int sp = pitch / 2;   /* uint16 pitch */

  /* 1) source 565 -> 888 */
  for (int y = 0; y < vh; y++) {
    for (int x = 0; x < vw; x++) {
      int r, g, b;
      c565(src[y * sp + x], &r, &g, &b);
      uint8 *p = src888 + (y * vw + x) * 3;
      p[0] = (uint8)r; p[1] = (uint8)g; p[2] = (uint8)b;
    }
  }

  /* 2) upscale or pass-through */
  int ow, oh;
  if (render_mode >= 2 && render_mode <= 4) {
    upscale2x(src888, vw, vh, out888, render_mode);
    ow = vw * 2; oh = vh * 2;
  } else {
    memcpy(out888, src888, (size_t)vw * vh * 3);
    ow = vw; oh = vh;
  }

  /* 3) colour adjust + scanline */
  apply_effects(out888, ow, oh, scanline, greyscale, brightness, contrast);

  /* 4) 888 -> 565 output */
  for (int i = 0; i < ow * oh; i++) {
    uint8 *p = out888 + i * 3;
    out565[i] = p565(p[0], p[1], p[2]);
  }

  *out_w = ow; *out_h = oh;
  return out565;
}
