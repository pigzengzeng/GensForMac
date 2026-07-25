#ifndef UI_H
#define UI_H

#include "SDL.h"

/* On-screen controller-setup overlay (SDL-rendered, no GTK).
   Opened from the native macOS menu Controllers > "Configure Controls...".
   While open the emulation is paused and keys drive the overlay; this is
   where per-button key bindings are captured the old on-screen way. */

void ui_init(void);
int  ui_is_open(void);
void ui_open(void);
void ui_close(void);

/* called from the main loop for each key / gamepad-button press while open */
void ui_handle_key(SDL_Keycode key);
void ui_handle_button(SDL_GameControllerButton button);

/* draw the menu overlay (emulation is paused by the caller) */
void ui_render(SDL_Renderer *r);

/* live gamepad diagnostics provided by gens_mac.c, shown in the overlay so
   the user can immediately see whether SDL detects the pad and its presses */
int gens_pad_count(void);
const char *gens_pad_label(int i);
int gens_pad_pressed(int i);

#endif /* UI_H */
