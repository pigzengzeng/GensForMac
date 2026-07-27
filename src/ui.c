#include "ui.h"
#include "settings.h"
#include "font.h"
#include "SDL.h"
#include <stdio.h>
#include <string.h>

extern t_settings settings;   /* defined in settings.c */

/* Only the controller-setup screens live here now. Every other setting
   (video, emulation) is handled by the native macOS menu bar; this overlay
   is opened from Controllers > "Configure Controls..." and lets the user
   bind keys the old on-screen way.

   Model: each on-screen PORT (1..8) has its own SOURCE -- the keyboard, a
   specific connected gamepad, or NONE -- plus its own keyboard/gamepad button
   bindings. Two ports may pick the SAME gamepad, which is
   how "one pad controls two players" (一控二) is achieved. */

typedef enum { SCR_CTRL, SCR_REDEF } screen_t;

static screen_t screen = SCR_CTRL;
static int sel = 0;           /* row in current screen (port row / button row) */
static int redef_port = 0;   /* port being redefined */
static int capture_btn = -1; /* -1 = not capturing, else button index 0..11 */
static int open = 0;

void ui_init(void) { open = 0; screen = SCR_CTRL; sel = 0; capture_btn = -1; }
int  ui_is_open(void) { return open; }
void ui_open(void)  { open = 1; screen = SCR_CTRL; sel = 0; redef_port = 0; capture_btn = -1; }
void ui_close(void) { open = 0; }

/* ---------- source (per-port input device) helpers ---------- */
/* Option list for the source selector: [0]=KEYBOARD, [1..npads]=pad idx,
   [npads+1]=NONE. Cycling wraps through these. */
static int src_option_count(void) { return 1 + gens_pad_count() + 1; }

static int src_option_to_dev(int idx)
{
  int n = gens_pad_count();
  if (idx <= 0)      return PORT_DEV_KEYBOARD;
  if (idx <= n)      return idx - 1;          /* pad index */
  return PORT_DEV_NONE;
}

static int dev_to_src_option(int dev)
{
  int n = gens_pad_count();
  if (dev == PORT_DEV_KEYBOARD) return 0;
  if (dev == PORT_DEV_NONE)     return 1 + n;
  if (dev >= 0 && dev < n)      return dev + 1;
  return 0;                                     /* disconnected pad -> keyboard */
}

static const char *dev_label(int dev)
{
  if (dev == PORT_DEV_KEYBOARD) return "KEYBOARD";
  if (dev == PORT_DEV_NONE)     return "NONE";
  if (dev >= 0 && dev < gens_pad_count()) return gens_pad_label(dev);
  return "?";
}

/* ---------------------------- key handling ---------------------------- */
void ui_handle_key(SDL_Keycode key)
{
  /* capture mode: next key becomes the keyboard binding (Esc cancels) */
  if (capture_btn >= 0) {
    if (key == SDLK_ESCAPE) { capture_btn = -1; return; }
    settings.keymap[redef_port][capture_btn] = SDL_GetScancodeFromKey(key);
    capture_btn = -1;
    settings_save();
    return;
  }

  switch (screen) {
    case SCR_CTRL:
      if (key == SDLK_ESCAPE) { ui_close(); break; }
      if (key == SDLK_UP)        sel = (sel + 8) % 9;     /* 8 ports + DONE */
      else if (key == SDLK_DOWN) sel = (sel + 1) % 9;
      else if (key == SDLK_LEFT || key == SDLK_RIGHT) {
        if (sel < 8) {
          int d = (key == SDLK_RIGHT) ? 1 : -1;
          int idx = dev_to_src_option(settings.port_dev[sel]);
          int cnt = src_option_count();
          idx = (idx + d + cnt) % cnt;
          settings.port_dev[sel] = src_option_to_dev(idx);
          settings_apply();   /* re-derive USED/UNUSED + push to core */
          settings_save();
        }
      }
      else if (key == SDLK_RETURN || key == SDLK_SPACE) {
        if (sel == 8) { ui_close(); }              /* DONE */
        else { redef_port = sel; screen = SCR_REDEF; sel = 0; capture_btn = -1; }
      }
      break;

    case SCR_REDEF:
      if (key == SDLK_ESCAPE) { screen = SCR_CTRL; sel = redef_port; capture_btn = -1; break; }
      if (key == SDLK_UP)        sel = (sel + 13) % 14;   /* 12 buttons + BACK + RESET */
      else if (key == SDLK_DOWN) sel = (sel + 1) % 14;
      else if (key == SDLK_RETURN || key == SDLK_SPACE) {
        if (sel == 12) { screen = SCR_CTRL; sel = redef_port; }   /* BACK */
        else if (sel == 13) { settings_reset_port(redef_port); }   /* RESET */
        else { capture_btn = sel; }   /* button 0..11; next key/pad binds */
      }
      break;
  }
}

/* gamepad-button capture: while capturing, the next pad button becomes the
   gamepad binding for the selected emulator function (Esc cancels). */
void ui_handle_button(SDL_GameControllerButton button)
{
  if (capture_btn >= 0) {
    settings.gpadmap[redef_port][capture_btn] = button;
    capture_btn = -1;
    settings_save();
  }
}

/* ---------------------------- rendering ---------------------------- */
static void put(SDL_Renderer *r, int x, int y, const char *s, int sc, SDL_Color c)
{
  font_draw(r, x, y, s, sc, c);
}

/* Draw `s` right-aligned so its right edge sits at x_right, but never let it
   extend left past (x_right - max_w). Long gamepad names are truncated with
   an ellipsis so they cannot overlap the PORT/type labels on the left. */
static void put_right_clip(SDL_Renderer *r, int x_right, int y,
                           const char *s, int sc, SDL_Color c, int max_w)
{
  int w = font_width(s, sc);
  if (w <= max_w) { put(r, x_right - w, y, s, sc, c); return; }
  int ell  = font_width("...", sc);
  int avail = max_w - ell;
  char buf[64];
  strncpy(buf, s, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  while (font_width(buf, sc) > avail && strlen(buf) > 1)
    buf[strlen(buf) - 1] = 0;
  if (strlen(buf) + 3 < sizeof(buf)) {
    buf[strlen(buf)] = '.';
    buf[strlen(buf)] = '.';
    buf[strlen(buf)] = '.';
    buf[strlen(buf)] = 0;
  }
  put(r, x_right - font_width(buf, sc), y, buf, sc, c);
}

void ui_render(SDL_Renderer *r)
{
  int W, H;
  SDL_GetRendererOutputSize(r, &W, &H);

  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r, 16, 16, 24, 255);
  SDL_RenderClear(r);

  int scale = 2;
  SDL_Color title = {255, 230, 120, 255};
  SDL_Color norm  = {210, 210, 220, 255};
  SDL_Color hi    = {255, 230, 80, 255};
  SDL_Color dim   = {150, 150, 160, 255};
  SDL_Color kbd   = {120, 200, 255, 255};
  SDL_Color pad   = {140, 230, 150, 255};

  int cx = W / 2;
  int lx = cx - 280;           /* left margin for labels */
  int rx = cx + 270;           /* right margin for values */

  if (screen == SCR_CTRL) {
    put(r, cx - font_width("CONTROL PAD SETUP", scale)/2, 30,
        "CONTROL PAD SETUP", scale, title);
    int y = 96;
    for (int i = 0; i < 8; i++) {
      char lbl[48];
      snprintf(lbl, sizeof(lbl), "PORT %d", i + 1);
      put(r, lx, y, lbl, scale, (i == sel) ? hi : norm);

      /* source on the right; clip long gamepad names so they never overlap
         the PORT label on the left */
      const char *sn = dev_label(settings.port_dev[i]);
      SDL_Color scol = (settings.port_dev[i] == PORT_DEV_KEYBOARD) ? kbd
                     : (settings.port_dev[i] == PORT_DEV_NONE)     ? dim : pad;
      int maxw = rx - (lx + 200);   /* keep clear of the PORT label */
      if (maxw < 40) maxw = 40;
      put_right_clip(r, rx, y, sn, scale, (i == sel) ? hi : scol, maxw);
      y += 32;
    }
    put(r, lx, y, "DONE", scale, 8 == sel ? hi : norm);
    put(r, lx, y + 34, "LEFT/RIGHT: SOURCE   ENTER: KEYS   ESC: DONE", 1, dim);

    /* ---- live gamepad diagnostics: which port(s) does each pad drive? ---- */
    {
      int dy = y + 58;
      int n = gens_pad_count();
      char buf[160];
      if (n == 0) {
        put(r, lx, dy, "GAMEPADS: NONE DETECTED BY SDL", 1,
            (SDL_Color){255, 120, 120, 255});
      } else {
        snprintf(buf, sizeof(buf), "GAMEPADS: %d DETECTED", n);
        put(r, lx, dy, buf, 1, pad);
        for (int i = 0; i < n; i++) {
          dy += 18;
          /* collect the ports that source this pad */
          char ports[64] = "";
          int cnt = 0;
          for (int mp = 0; mp < NUM_PORTS; mp++) {
            if (settings.port_dev[mp] == i) {
              char tmp[10];
              snprintf(tmp, sizeof(tmp), "%sPORT %d", cnt ? ", " : "", mp + 1);
              strncat(ports, tmp, sizeof(ports) - strlen(ports) - 1);
              cnt++;
            }
          }
          int on = gens_pad_pressed(i);
          snprintf(buf, sizeof(buf), "%s -> %s%s", gens_pad_label(i),
                   ports[0] ? ports : "(unmapped)",
                   on ? "  [INPUT!]" : "");
          put(r, lx, dy, buf, 1, on ? hi : dim);
        }
        /* keyboard-sourced ports */
        char kbuf[64] = ""; int kc = 0;
        for (int mp = 0; mp < NUM_PORTS; mp++)
          if (settings.port_dev[mp] == PORT_DEV_KEYBOARD) {
            char tmp[10];
            snprintf(tmp, sizeof(tmp), "%sPORT %d", kc ? ", " : "", mp + 1);
            strncat(kbuf, tmp, sizeof(kbuf) - strlen(kbuf) - 1);
            kc++;
          }
        if (kc) {
          dy += 18;
          snprintf(buf, sizeof(buf), "KEYBOARD -> %s", kbuf);
          put(r, lx, dy, buf, 1, kbd);
        }
      }
    }
  }
  else if (screen == SCR_REDEF) {
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "PORT %d - REDEFINE  (SRC: %s)", redef_port + 1,
             dev_label(settings.port_dev[redef_port]));
    put(r, cx - font_width(hdr, scale)/2, 28, hdr, scale, title);

    int dev = settings.port_dev[redef_port];
    int is_kbd = (dev == PORT_DEV_KEYBOARD);
    int is_pad = (dev >= 0 && dev < gens_pad_count());
    /* A port with a real source owns exactly ONE binding column: keyboard
       ports show the KEY column, pad ports show the PAD column. They never
       coexist (the old UI drew both even for a keyboard-only port). */

    int y = 74;
    if (!is_kbd && !is_pad) {
      /* unassigned (NONE) port: nothing meaningful to bind here */
      put(r, lx, y, "THIS PORT HAS NO DEVICE ASSIGNED", 1, dim);
      put(r, lx, y + 24, "SET A SOURCE IN CONTROL PAD SETUP FIRST", 1, dim);
      y += 70;
    } else {
      int col_x = cx + 30;     /* the single visible binding column */
      put(r, col_x, 56, is_kbd ? "KEY" : "PAD", 1, is_kbd ? kbd : pad);
      for (int b = 0; b < 12; b++) {
        int row = b;           /* row index in SEL space (0..11) */
        const char *vn = is_kbd
          ? SDL_GetScancodeName(settings.keymap[redef_port][b])
          : gpad_button_name(settings.gpadmap[redef_port][b]);
        if (is_kbd && (!vn || vn[0] == 0)) vn = "-";
        char rowlbl[24];
        snprintf(rowlbl, sizeof(rowlbl), "%s%s", (row == sel) ? "> " : "  ",
                 button_name(b));
        put(r, lx, y, rowlbl, scale, (row == sel) ? hi : norm);
        put(r, col_x, y, vn, scale, (row == sel) ? (is_kbd ? kbd : pad) : norm);
        y += 24;
      }
    }
    put(r, lx, y, (12 == sel) ? "> BACK" : "  BACK", scale, 12 == sel ? hi : norm);
    put(r, lx, y + 24, (13 == sel) ? "> RESET DEFAULTS" : "  RESET DEFAULTS",
        scale, 13 == sel ? hi : (SDL_Color){255, 150, 120, 255});
    if (capture_btn >= 0)
      put(r, lx, y + 54, "PRESS KEY OR GAMEPAD BUTTON (ESC CANCEL)", 1, hi);
    else if (!is_kbd && !is_pad)
      put(r, lx, y + 54, "ESC: BACK", 1, dim);
    else
      put(r, lx, y + 54, is_kbd ? "ENTER: SET KEY   ESC: BACK"
                                : "ENTER: SET PAD   ESC: BACK", 1, dim);
  }

  SDL_RenderPresent(r);
}
