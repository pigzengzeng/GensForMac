#ifndef SETTINGS_H
#define SETTINGS_H

#include "SDL.h"
#include "types.h"   /* uint8/uint16 needed by the settings struct */

#define NUM_PORTS    8
#define NUM_BUTTONS 12

/* button order used everywhere (keymap, menu) */
#define BTN_UP    0
#define BTN_DOWN  1
#define BTN_LEFT  2
#define BTN_RIGHT 3
#define BTN_A     4
#define BTN_B     5
#define BTN_C     6
#define BTN_X     7
#define BTN_Y     8
#define BTN_Z     9
#define BTN_START 10
#define BTN_MODE  11

/* render mode indices (Video > Render) */
#define RM_NORMAL      0
#define RM_INTERPOLATED 1
#define RM_SCALE2X     2
#define RM_2XSAI       3
#define RM_HQ2X        4

/* scanline intensity (Video > Scanline) */
#define SC_OFF 0
#define SC_25  1
#define SC_50  2
#define SC_100 3

/* controller type. The emulator always presents a 6-button pad (which is
   backward-compatible with 3-button games), so there is no longer a user
   choice between 3- and 6-button -- a port is either WIRED (6-button) or
   UNUSED. The active/inactive state is derived from the port's SOURCE. */
#define CT_NONE     0
#define CT_6BUTTON  2

/* Per-port input SOURCE. Each on-screen port is driven by exactly one source:
     PORT_DEV_KEYBOARD (-1) -> the keyboard (its own per-port keymap)
     PORT_DEV_NONE     (-2) -> nothing (port is unused)
     PORT_DEV_AUTO     (-3) -> resolved once at startup / on hot-plug into the
                               next free gamepad, else keyboard (port 0) / none
     0 .. n-1               -> that specific connected gamepad (pads[] index)
   Because two ports can pick the SAME gamepad, "one pad controls two players"
   (一控二) falls out for free: both ports read the same physical device. */
#define PORT_DEV_KEYBOARD (-1)
#define PORT_DEV_NONE     (-2)
#define PORT_DEV_AUTO     (-3)

/* Pseudo "buttons" for the analog triggers. SDL exposes LT/RT as AXES
   (SDL_CONTROLLER_AXIS_TRIGGERLEFT/RIGHT), not buttons, so they never emit
   SDL_CONTROLLERBUTTONDOWN and cannot appear in gpadmap with real button
   codes. We extend the button code space past SDL_CONTROLLER_BUTTON_MAX so
   triggers become bindable like any other button; pad_button_down() and the
   capture path in the main loop translate them back to axis reads. */
#define GBTN_LTRIGGER ((SDL_GameControllerButton)(SDL_CONTROLLER_BUTTON_MAX))
#define GBTN_RTRIGGER ((SDL_GameControllerButton)(SDL_CONTROLLER_BUTTON_MAX + 1))

typedef struct {
  /* video */
  int stretch;       /* 0 = keep 4:3, 1 = stretch to fill window */
  int vsync;         /* 0/1 */
  int render_mode;   /* RM_* */
  int scanline;      /* SC_* intensity */
  int greyscale;     /* 0/1 */
  int brightness;    /* -100 .. +100 (percent offset) */
  int contrast;      /* -100 .. +100 */

  /* controllers */
  int port_dev[NUM_PORTS];       /* source per port: KEYBOARD / pad idx / NONE */
  int port_type[NUM_PORTS];      /* CT_* */
  SDL_Scancode keymap[NUM_PORTS][NUM_BUTTONS];          /* keyboard binding */
  SDL_GameControllerButton gpadmap[NUM_PORTS][NUM_BUTTONS]; /* gamepad binding */
} t_settings;

extern t_settings settings;

/* Menu port (0..7, i.e. PORT 1..8) -> core input.pad[] index.
   The Genesis has two physical ports: port 0 reads input.pad[0] (player 1)
   and port 1 reads input.pad[4] (player 2). Slots 1-3 / 5-7 are only read by
   games that support the 4-Way Play / Team Player adapter, so a plain 2-player
   game only ever uses input.pad[0] and input.pad[4]. This table keeps the menu
   layout physically correct: PORT 1 = player 1 (pad 0), PORT 2 = player 2
   (pad 4), the rest are Team Player sub-slots. */
extern const int port_to_pad[8];

void settings_init_defaults(void);
void settings_load(void);     /* from ~/.gensmacrc, falls back to defaults */
void settings_save(void);     /* to ~/.gensmacrc */
void settings_apply(void);    /* push port types into the core's input map */
void settings_reset_port(int port); /* restore one port to factory defaults */
void settings_resolve_auto(void); /* turn PORT_DEV_AUTO into a concrete source */

const char *button_name(int b);
const char *gpad_button_name(SDL_GameControllerButton b);
const char *port_type_name(int t);
const char *render_mode_name(int m);
const char *scanline_name(int s);

#endif /* SETTINGS_H */
