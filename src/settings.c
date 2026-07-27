#include "types.h"   /* uint8/uint16 before input.h/config.h */
#include "main.h"     /* MAX_INPUTS, sdl_input_update decl */
#include "settings.h"
#include "config.h"
#include "input.h"    /* DEVICE_*, SYSTEM_*, input, input_init */
#include "io_ctrl.h"  /* io_init (re-points I/O port handlers) */
#include "system.h"   /* system_hw (guards live re-apply) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

t_settings settings;

/* Menu port -> core input.pad[] slot. Recomputed by settings_apply() for the
   active multitap mode (Off keeps the classic 2-player split; Team Player mode
   collapses PORT 1..4 onto one console port's four sub-slots). Declared here as
   a mutable global so settings_apply() can rewrite it; sdl_input_update() and
   the core read it every frame. */
int port_to_pad[8] = { 0, 4, 1, 2, 3, 5, 6, 7 };

const char *button_name(int b)
{
  static const char *n[NUM_BUTTONS] = {
    "UP","DOWN","LEFT","RIGHT","A","B","C","X","Y","Z","START","MODE"
  };
  return (b >= 0 && b < NUM_BUTTONS) ? n[b] : "?";
}

const char *gpad_button_name(SDL_GameControllerButton b)
{
  if (b == GBTN_LTRIGGER) return "LT";
  if (b == GBTN_RTRIGGER) return "RT";
  switch (b) {
    case SDL_CONTROLLER_BUTTON_A:             return "A";
    case SDL_CONTROLLER_BUTTON_B:             return "B";
    case SDL_CONTROLLER_BUTTON_X:             return "X";
    case SDL_CONTROLLER_BUTTON_Y:             return "Y";
    case SDL_CONTROLLER_BUTTON_BACK:          return "BACK";
    case SDL_CONTROLLER_BUTTON_GUIDE:         return "GUIDE";
    case SDL_CONTROLLER_BUTTON_START:         return "START";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return "LSTICK";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return "RSTICK";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return "LB";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "RB";
    case SDL_CONTROLLER_BUTTON_DPAD_UP:       return "UP";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return "DOWN";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return "LEFT";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return "RIGHT";
    default:                                  return "-";
  }
}

const char *port_type_name(int t)
{
  switch (t) {
    case CT_NONE:     return "NONE";
    case CT_6BUTTON:  return "6-BUTTON";
    default:          return "?";
  }
}

const char *multitap_name(int m)
{
  switch (m) {
    case MULTITAP_OFF:  return "OFF";
    case MULTITAP_TP1:  return "TEAM PLAYER (PORT 1)";
    case MULTITAP_TP2:  return "TEAM PLAYER (PORT 2)";
    default:            return "?";
  }
}

const char *render_mode_name(int m)
{
  switch (m) {
    case RM_NORMAL:       return "NORMAL";
    case RM_INTERPOLATED: return "INTERPOLATED";
    case RM_SCALE2X:      return "SCALE2X";
    case RM_2XSAI:        return "2XSAI";
    case RM_HQ2X:         return "HQ2X";
    default:              return "?";
  }
}

const char *scanline_name(int s)
{
  switch (s) {
    case SC_OFF:  return "OFF";
    case SC_25:   return "25%";
    case SC_50:   return "50%";
    case SC_100:  return "100%";
    default:      return "?";
  }
}

/* one port's classic Gens player-1 keyboard layout */
static void set_keymap_defaults(int p)
{
  settings.keymap[p][BTN_UP]    = SDL_SCANCODE_UP;
  settings.keymap[p][BTN_DOWN]  = SDL_SCANCODE_DOWN;
  settings.keymap[p][BTN_LEFT]  = SDL_SCANCODE_LEFT;
  settings.keymap[p][BTN_RIGHT] = SDL_SCANCODE_RIGHT;
  settings.keymap[p][BTN_A]     = SDL_SCANCODE_A;
  settings.keymap[p][BTN_B]     = SDL_SCANCODE_S;
  settings.keymap[p][BTN_C]     = SDL_SCANCODE_D;
  settings.keymap[p][BTN_X]     = SDL_SCANCODE_Q;
  settings.keymap[p][BTN_Y]     = SDL_SCANCODE_W;
  settings.keymap[p][BTN_Z]     = SDL_SCANCODE_E;
  settings.keymap[p][BTN_START] = SDL_SCANCODE_RETURN;
  settings.keymap[p][BTN_MODE]  = SDL_SCANCODE_RSHIFT;
}

/* Default gamepad mapping. ALL six Mega Drive face buttons (A B C X Y Z) plus
   START and MODE are bound, so every button works out of the box on a modern
   pad -- previously X and Z were left unbound, which is why "only four face
   buttons worked". Layout: bottom row A/B/C -> SDL X/A/B, top row X/Y/Z ->
   SDL LB/Y/RB. dpad -> dpad. Users can rebind any of these on the screen. */
static void set_gpadmap_defaults(int p)
{
  settings.gpadmap[p][BTN_UP]    = SDL_CONTROLLER_BUTTON_DPAD_UP;
  settings.gpadmap[p][BTN_DOWN]  = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
  settings.gpadmap[p][BTN_LEFT]  = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
  settings.gpadmap[p][BTN_RIGHT] = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
  settings.gpadmap[p][BTN_A]     = SDL_CONTROLLER_BUTTON_X;
  settings.gpadmap[p][BTN_B]     = SDL_CONTROLLER_BUTTON_A;
  settings.gpadmap[p][BTN_C]     = SDL_CONTROLLER_BUTTON_B;
  settings.gpadmap[p][BTN_X]     = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
  settings.gpadmap[p][BTN_Y]     = SDL_CONTROLLER_BUTTON_Y;
  settings.gpadmap[p][BTN_Z]     = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
  settings.gpadmap[p][BTN_START] = SDL_CONTROLLER_BUTTON_START;
  settings.gpadmap[p][BTN_MODE]  = SDL_CONTROLLER_BUTTON_BACK;
}

void settings_init_defaults(void)
{
  settings.stretch     = 0;
  settings.vsync       = 1;
  settings.render_mode = RM_NORMAL;
  settings.scanline    = SC_OFF;
  settings.greyscale   = 0;
  settings.brightness  = 0;
  settings.contrast    = 0;
  settings.multitap    = MULTITAP_OFF;

  for (int p = 0; p < NUM_PORTS; p++) {
    /* AUTO grabs the first unclaimed connected pad on every (re)enumeration
       (see gamepad_reconcile()), so a freshly plugged controller "just works"
       without manual setup. */
    settings.port_dev[p]  = PORT_DEV_AUTO;
    /* Every wired port is a 6-button pad (compatible with 3-button games too),
       so the type is fixed; settings_apply() derives USED vs UNUSED from the
       source. */
    settings.port_type[p] = CT_6BUTTON;
    set_keymap_defaults(p);   /* every port gets a keyboard layout so any port
                                 can be switched to KEYBOARD instantly */
    set_gpadmap_defaults(p);
  }
}

/* Restore ONE port's controller bindings + type to the factory defaults.
   Backs the on-screen "RESET DEFAULTS" item, so an accidental mis-binding
   (e.g. a stray key press captured onto the wrong function) can be undone
   without editing ~/.gensmacrc by hand. */
void settings_reset_port(int p)
{
  if (p < 0 || p >= NUM_PORTS) return;
  settings.port_type[p] = CT_6BUTTON;
  set_gpadmap_defaults(p);
  set_keymap_defaults(p);   /* any port may be switched to KEYBOARD, so every
                               port keeps a usable keyboard layout */
  settings_apply();
  settings_save();
}

static void cfg_path(char *buf, size_t n)
{
  const char *home = getenv("HOME");
  if (!home) home = ".";
  snprintf(buf, n, "%s/.gensmacrc", home);
}

void settings_load(void)
{
  settings_init_defaults();
  char path[1024];
  cfg_path(path, sizeof(path));
  FILE *f = fopen(path, "r");
  if (!f) return;

  char line[256];
  while (fgets(line, sizeof(line), f)) {
    char *nl = strchr(line, '\n'); if (nl) *nl = 0;
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    const char *key = line;
    int val = atoi(eq + 1);

    if      (!strcmp(key, "stretch"))      settings.stretch = val;
    else if (!strcmp(key, "vsync"))        settings.vsync = val;
    else if (!strcmp(key, "render_mode"))  settings.render_mode = val;
    else if (!strcmp(key, "scanline"))     settings.scanline = val;
    else if (!strcmp(key, "greyscale"))    settings.greyscale = val;
    else if (!strcmp(key, "brightness"))   settings.brightness = val;
    else if (!strcmp(key, "contrast"))     settings.contrast = val;
    else if (!strcmp(key, "multitap"))     settings.multitap = val;
    else if (!strcmp(key, "keyboard_port"))settings.port_dev[0] = PORT_DEV_KEYBOARD; /* legacy rc */
    else if (!strncmp(key, "port_dev", 8)) {
      int p = atoi(key + 8);
      if (p >= 0 && p < NUM_PORTS) settings.port_dev[p] = val;
    }
    else if (!strncmp(key, "port_type", 9)) {
      int p = atoi(key + 9);
      if (p >= 0 && p < NUM_PORTS) settings.port_type[p] = val;
    }
    else if (!strncmp(key, "keymap", 6)) {
      /* keymapP_B */
      const char *rest = key + 6;
      char *us = strchr((char *)rest, '_');
      if (us) {
        int p = atoi(rest);
        int b = atoi(us + 1);
        if (p >= 0 && p < NUM_PORTS && b >= 0 && b < NUM_BUTTONS)
          settings.keymap[p][b] = (SDL_Scancode)val;
      }
    }
    else if (!strncmp(key, "gpadmap", 7)) {
      /* gpadmapP_B */
      const char *rest = key + 7;
      char *us = strchr((char *)rest, '_');
      if (us) {
        int p = atoi(rest);
        int b = atoi(us + 1);
        if (p >= 0 && p < NUM_PORTS && b >= 0 && b < NUM_BUTTONS)
          settings.gpadmap[p][b] = (SDL_GameControllerButton)val;
      }
    }
  }
  fclose(f);

  /* clamp any out-of-range source to AUTO so it gets re-resolved below */
  for (int p = 0; p < NUM_PORTS; p++)
    if (settings.port_dev[p] < PORT_DEV_NONE)   /* below NONE(-2) is invalid */
      settings.port_dev[p] = PORT_DEV_AUTO;
}

void settings_save(void)
{
  char path[1024];
  cfg_path(path, sizeof(path));
  FILE *f = fopen(path, "w");
  if (!f) return;

  fprintf(f, "# Gens for Mac settings\n");
  fprintf(f, "stretch=%d\n",      settings.stretch);
  fprintf(f, "vsync=%d\n",        settings.vsync);
  fprintf(f, "render_mode=%d\n",  settings.render_mode);
  fprintf(f, "scanline=%d\n",     settings.scanline);
  fprintf(f, "greyscale=%d\n",    settings.greyscale);
  fprintf(f, "brightness=%d\n",   settings.brightness);
  fprintf(f, "contrast=%d\n",     settings.contrast);
  fprintf(f, "multitap=%d\n",     settings.multitap);
  for (int p = 0; p < NUM_PORTS; p++)
    fprintf(f, "port_dev%d=%d\n", p, settings.port_dev[p]);
  for (int p = 0; p < NUM_PORTS; p++)
    fprintf(f, "port_type%d=%d\n", p, settings.port_type[p]);
  for (int p = 0; p < NUM_PORTS; p++)
    for (int b = 0; b < NUM_BUTTONS; b++)
      fprintf(f, "keymap%d_%d=%d\n", p, b, (int)settings.keymap[p][b]);
  for (int p = 0; p < NUM_PORTS; p++)
    for (int b = 0; b < NUM_BUTTONS; b++)
      fprintf(f, "gpadmap%d_%d=%d\n", p, b, (int)settings.gpadmap[p][b]);
  fclose(f);
}

/* push controller types into the core's input device map.
   The core derives input.dev[] from input.system[0..1] + config.input[].padtype
   inside input_init()/io_init(), so we must NOT write input.dev directly.
   Each on-screen menu port is translated through port_to_pad[] into the real
   core input.pad[] index before touching config.input[].padtype. */
void settings_apply(void)
{
  const uint8 auto_mask = DEVICE_PAD2B | DEVICE_PAD3B | DEVICE_PAD6B;

  /* 1) Rewrite the menu-port -> core-pad mapping for the active multitap mode.
       Off          : PORT1=pad0, PORT2=pad4 -> plain 2-player (each on its own
                      console port); the rest stay on their console port.
       Team Player 1: PORT1..4 -> pads 0..3, i.e. the four sub-slots of console
                      port 1, so PORT1=1P .. PORT4=4P on that one tap.
       Team Player 2: PORT1..4 -> pads 4..7, the four sub-slots of console port
                      2 (the layout most 4-player MD games expect). */
  static const int map_off[8] = { 0, 4, 1, 2, 3, 5, 6, 7 };
  static const int map_tp1[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
  static const int map_tp2[8] = { 4, 5, 6, 7, 0, 1, 2, 3 };
  const int *base = map_off;
  if (settings.multitap == MULTITAP_TP1)      base = map_tp1;
  else if (settings.multitap == MULTITAP_TP2) base = map_tp2;
  for (int mp = 0; mp < NUM_PORTS; mp++) port_to_pad[mp] = base[mp];

  /* 2) Mark which core pad slots are actually in use and tag every slot as a
       6-button pad. We set ALL eight slots to DEVICE_PAD6B (not just the used
       ones): the core's Team-Player reader pulls config.input[player].padtype
       by PLAYER counter, which for port 2 is config.input[1..4] -- a different
       index than the core pad slot -- so leaving the unused slots at their
       default would starve the tap's later sub-slots. 6B is backward-compatible
       with 3-button games, so this is harmless for every connected device. */
  int used[8] = {0};
  for (int mp = 0; mp < NUM_PORTS; mp++) {
    int pad = port_to_pad[mp];
    int t = (settings.port_dev[mp] == PORT_DEV_NONE) ? CT_NONE : CT_6BUTTON;
    settings.port_type[mp] = t;
    if (t != CT_NONE) used[pad] = 1;
    config.input[pad].padtype = DEVICE_PAD6B;
  }
  (void)auto_mask;

  /* 3) Configure the two console ports. In an explicit Team Player mode the
       chosen port is forced to SYSTEM_TEAMPLAYER (the game then reads 1P..4P
       from its four sub-slots); the other port stays a plain GAMEPAD so a pad
       bound to PORT5..8 still drives a separate player there. In Off mode we
       keep the old auto rule: a console port becomes TEAMPLAYER only when more
       than one device sits on it (so count-select games keep working), else
       plain GAMEPAD. */
  if (settings.multitap == MULTITAP_TP1) {
    input.system[0] = SYSTEM_TEAMPLAYER;
    input.system[1] = SYSTEM_GAMEPAD;
  } else if (settings.multitap == MULTITAP_TP2) {
    input.system[0] = SYSTEM_GAMEPAD;
    input.system[1] = SYSTEM_TEAMPLAYER;
  } else {
    for (int cp = 0; cp < 2; cp++) {
      int n = 0;
      for (int i = 0; i < 4; i++) n += used[cp * 4 + i];
      input.system[cp] = n ? (n > 1 ? SYSTEM_TEAMPLAYER : SYSTEM_GAMEPAD)
                            : SYSTEM_GAMEPAD;
    }
  }

  /* if a ROM is already running, re-apply the mapping immediately */
  if (system_hw != 0) {
    input_init();
    io_init();
  }
}
