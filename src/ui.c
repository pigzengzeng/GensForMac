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
   redefine keys the old on-screen way. */
typedef enum { SCR_CTRL, SCR_REDEF } screen_t;

static screen_t screen = SCR_CTRL;
static int sel = 0;           /* row in current screen (port row / button row) */
static int redef_port = 0;   /* port being redefined */
static int capture_btn = -1; /* -1 = not capturing, else button index */
static int open = 0;

void ui_init(void) { open = 0; screen = SCR_CTRL; sel = 0; capture_btn = -1; }
int  ui_is_open(void) { return open; }
void ui_open(void)  { open = 1; screen = SCR_CTRL; sel = 0; redef_port = 0; capture_btn = -1; }
void ui_close(void) { open = 0; }

/* ---------------------------- key handling ---------------------------- */
void ui_handle_key(SDL_Keycode key)
{
  /* capture mode: next key becomes the keyboard binding (Esc cancels) */
  if (capture_btn >= 0) {
    if (key == SDLK_ESCAPE) { capture_btn = -1; return; }
    settings.keymap[redef_port][capture_btn] =
        SDL_GetScancodeFromKey(key);
    capture_btn = -1;
    settings_save();
    return;
  }

  switch (screen) {
    case SCR_CTRL:
      if (key == SDLK_ESCAPE) { ui_close(); break; }
      if (key == SDLK_UP)        sel = (sel + 8) % 9;
      else if (key == SDLK_DOWN) sel = (sel + 1) % 9;
      else if (key == SDLK_LEFT || key == SDLK_RIGHT) {
        if (sel < 8) {
          int d = (key == SDLK_RIGHT) ? 1 : -1;
          settings.port_type[sel] = (settings.port_type[sel] + d + 3) % 3;
          settings_apply();
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
      if (key == SDLK_UP)        sel = (sel + 13) % 14;
      else if (key == SDLK_DOWN) sel = (sel + 1) % 14;
      else if (key == SDLK_RETURN || key == SDLK_SPACE) {
        if (sel == 12) { screen = SCR_CTRL; sel = redef_port; }   /* BACK */
        else if (sel == 13) { settings_reset_port(redef_port); }  /* RESET DEFAULTS */
        else { capture_btn = sel; }   /* begin capture; next key OR pad button binds */
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
    put(r, cx - font_width("CONTROL PAD SETUP", scale)/2, 36,
        "CONTROL PAD SETUP", scale, title);
    int y = 110;
    for (int i = 0; i < 8; i++) {
      char lbl[48];
      snprintf(lbl, sizeof(lbl), "PORT %d%s", i + 1,
               (i == settings.keyboard_port) ? " (KEYBOARD)" : "");
      put(r, lx, y, lbl, scale, i == sel ? hi : (i == settings.keyboard_port ? kbd : norm));
      const char *tname = port_type_name(settings.port_type[i]);
      put(r, rx - font_width(tname, scale), y, tname, scale, i == sel ? hi : norm);
      y += 34;
    }
    put(r, lx, y, "DONE", scale, 8 == sel ? hi : norm);
    put(r, lx, y + 36, "LEFT/RIGHT: TYPE   ENTER: KEYS   ESC: DONE", 1, dim);

    /* ---- live gamepad diagnostics: is the pad seen? does it send input? --- */
    {
      int dy = y + 60;
      int n = gens_pad_count();
      char buf[96];
      if (n == 0) {
        put(r, lx, dy, "GAMEPADS: NONE DETECTED BY SDL", 1,
            (SDL_Color){255, 120, 120, 255});
      } else {
        snprintf(buf, sizeof(buf), "GAMEPADS: %d DETECTED", n);
        put(r, lx, dy, buf, 1, pad);
        /* pad 0 shares the keyboard's port (player 1); later pads take the
           remaining menu ports in order -- mirror of sdl_input_update() */
        int order[8], no = 0;
        order[no++] = settings.keyboard_port;
        for (int mp = 0; mp < 8; mp++)
          if (mp != settings.keyboard_port) order[no++] = mp;
        for (int i = 0; i < n && i < 4; i++) {
          dy += 18;
          int on = gens_pad_pressed(i);
          snprintf(buf, sizeof(buf), "%d: %s -> PORT %d %s", i + 1,
                   gens_pad_label(i), order[i] + 1, on ? "[INPUT!]" : "");
          put(r, lx, dy, buf, 1, on ? hi : dim);
        }
      }
    }
  }
  else if (screen == SCR_REDEF) {
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "PORT %d - REDEFINE%s", redef_port + 1,
             (redef_port == settings.keyboard_port) ? " (KEYBOARD)" : " (GAMEPAD)");
    put(r, cx - font_width(hdr, scale)/2, 32, hdr, scale, title);
    int kx = cx - 170;        /* KEY column: left-aligned, clear of button name */
    int px = cx + 30;         /* PAD column: left-aligned, clear of KEY */
    /* column headers */
    put(r, kx, 70, "KEY", 1, kbd);
    put(r, px, 70, "PAD", 1, pad);
    int y = 84;
    for (int b = 0; b < 12; b++) {
      const char *kn = SDL_GetScancodeName(settings.keymap[redef_port][b]);
      if (!kn || kn[0] == 0) kn = "-";
      const char *pn = gpad_button_name(settings.gpadmap[redef_port][b]);
      put(r, lx, y, (char *)button_name(b), scale, b == sel ? hi : norm);
      put(r, kx, y, kn, scale, b == sel ? kbd : norm);
      put(r, px, y, pn, scale, b == sel ? pad : norm);
      y += 27;
    }
    put(r, lx, y, "BACK", scale, 12 == sel ? hi : norm);
    put(r, lx, y + 26, "RESET DEFAULTS", scale, 13 == sel ? hi :
        (SDL_Color){255, 150, 120, 255});
    if (capture_btn >= 0)
      put(r, lx, y + 56, "PRESS KEY OR GAMEPAD BUTTON (ESC CANCEL)", 1, hi);
    else
      put(r, lx, y + 56, "ENTER: SET   ESC: BACK   RESET: RESTORE THIS PORT", 1, dim);
  }

  SDL_RenderPresent(r);
}
