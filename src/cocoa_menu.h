#ifndef COCOA_MENU_H
#define COCOA_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

/* Build the macOS native menu bar. Safe to call once after SDL_Init();
   no-ops if there is no Cocoa main menu (e.g. headless selftest). */
void mac_menu_init(void);

/* Open the on-screen controller-setup overlay (the old SDL-rendered key
   redefinition UI). Implemented in gens_mac.c. */
void mac_action_open_controls(void);

/* High-level emulation actions, implemented in gens_mac.c and invoked from
   the native menu. Kept here so the Objective-C menu never reaches into the
   core directly. */
void mac_action_reset(void);
void mac_action_save_state(void);
void mac_action_load_state(void);
void mac_action_toggle_sound(void);
void mac_action_toggle_fullscreen(void);
void mac_action_toggle_turbo(void);
void mac_action_quit(void);

/* Refresh the menu's "N connected" gamepad readout (call after a hot-plug
   re-scan so the count stays current). Implemented in cocoa_menu.m. */
void mac_menu_sync_gamepads(void);

#ifdef __cplusplus
}
#endif

#endif /* COCOA_MENU_H */
