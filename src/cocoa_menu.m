/*
 * cocoa_menu.m - native macOS menu bar for Gens for Mac
 * ----------------------------------------------------------------------------
 * Replaces the old in-app (SDL-rendered) settings overlay. The menu lives in
 * the macOS menu bar and is built from NSMenu / NSMenuItem. Each item's action
 * mutates the shared `settings` struct, persists it via settings_save(), and
 * re-applies device changes via settings_apply(). Video effects read `settings`
 * every frame, so most changes take effect immediately with no extra plumbing.
 *
 * The Controllers menu only *activates* the setup UI: its
 *   Controllers ▸ Configure Controls...
 * item opens the on-screen key-redefinition overlay (the old SDL-rendered
 * UI, implemented in ui.c). Per-button key capture happens there, not in the
 * menu, because a menu tree cannot capture a raw key press usefully.
 * ----------------------------------------------------------------------------
 */
#import <Cocoa/Cocoa.h>
#include "SDL.h"
#include "settings.h"
#include "cocoa_menu.h"

/* gens_mac.c */
extern int gens_pad_count(void);

/* ----- handler that owns every menu action ------------------------------- */
@interface MenuHandler : NSObject
@property (nonatomic, strong) NSMenuItem *stretchItem, *vsyncItem, *greyscaleItem;
@property (nonatomic, strong) NSMenuItem *brightnessItem, *contrastItem;
@property (nonatomic, strong) NSMutableArray<NSMenuItem *> *renderItems;
@property (nonatomic, strong) NSMutableArray<NSMenuItem *> *scanlineItems;
@property (nonatomic, strong) NSMutableArray<NSMenuItem *> *gamepadInfoItem; /* single item */
- (void)sync;
@end

static MenuHandler *g_handler = nil;

/* Called by gens_mac.c on gamepad hot-plug so the "N connected" readout stays
   current without the user reopening the menu. */
void mac_menu_sync_gamepads(void) { [g_handler sync]; }

@implementation MenuHandler

- (NSMenuItem *)add:(NSMenu *)menu title:(NSString *)t sel:(SEL)s key:(NSString *)k
{
  NSMenuItem *it = [menu addItemWithTitle:t action:s keyEquivalent:k];
  [it setTarget:g_handler];
  return it;
}

/* ----- video toggles ----- */
- (void)doStretch:(id)sender    { (void)sender; settings.stretch ^= 1; settings_save(); [self sync]; }
- (void)doVsync:(id)sender      { (void)sender; settings.vsync ^= 1; settings_save(); [self sync]; }
- (void)doGreyscale:(id)sender  { (void)sender; settings.greyscale ^= 1; settings_save(); [self sync]; }

- (void)doRender:(id)sender     { settings.render_mode = (int)[sender tag]; settings_save(); [self sync]; }
- (void)doScanline:(id)sender   { settings.scanline = (int)[sender tag]; settings_save(); [self sync]; }

- (void)doBrightnessUp:(id)sender   { (void)sender; settings.brightness = MIN(100, settings.brightness + 5); settings_save(); [self sync]; }
- (void)doBrightnessDown:(id)sender { (void)sender; settings.brightness = MAX(-100, settings.brightness - 5); settings_save(); [self sync]; }
- (void)doContrastUp:(id)sender     { (void)sender; settings.contrast = MIN(100, settings.contrast + 5); settings_save(); [self sync]; }
- (void)doContrastDown:(id)sender   { (void)sender; settings.contrast = MAX(-100, settings.contrast - 5); settings_save(); [self sync]; }

/* ----- controllers ----- */
- (void)doConfigureControls:(id)sender { (void)sender; mac_action_open_controls(); }

/* ----- emulation ----- */
- (void)doReset:(id)sender        { (void)sender; mac_action_reset(); }
- (void)doSaveState:(id)sender     { (void)sender; mac_action_save_state(); }
- (void)doLoadState:(id)sender     { (void)sender; mac_action_load_state(); }
- (void)doToggleSound:(id)sender   { (void)sender; mac_action_toggle_sound(); [self sync]; }
- (void)doToggleFullscreen:(id)sender { (void)sender; mac_action_toggle_fullscreen(); }
- (void)doToggleTurbo:(id)sender   { (void)sender; mac_action_toggle_turbo(); [self sync]; }

/* ----- app ----- */
- (void)doAbout:(id)sender {
  (void)sender;
  NSAlert *a = [[NSAlert alloc] init];
  [a setMessageText:@"Gens for Mac"];
  [a setInformativeText:@"Sega Mega Drive / Genesis emulator (Intel x86_64)\nA 64-bit-clean C core (Genesis Plus GX lineage) with an SDL2 frontend."];
  [a runModal];
}
- (void)doQuit:(id)sender { (void)sender; mac_action_quit(); }

/* ----- refresh all checkmarks / values ----- */
- (void)sync
{
  _stretchItem.state   = settings.stretch   ? NSOnState : NSOffState;
  _vsyncItem.state     = settings.vsync     ? NSOnState : NSOffState;
  _greyscaleItem.state = settings.greyscale ? NSOnState : NSOffState;

  for (int i = 0; i < (int)_renderItems.count; i++)
    _renderItems[i].state = (settings.render_mode == i) ? NSOnState : NSOffState;
  for (int i = 0; i < (int)_scanlineItems.count; i++)
    _scanlineItems[i].state = (settings.scanline == i) ? NSOnState : NSOffState;

  [_brightnessItem setTitle:[NSString stringWithFormat:@"Brightness: %+d%%", settings.brightness]];
  [_contrastItem   setTitle:[NSString stringWithFormat:@"Contrast: %+d%%", settings.contrast]];

  int pads = gens_pad_count();   /* counts every opened pad (Game Controller
                                     AND raw joystick), unlike SDL_IsGameController
                                     which skips raw joysticks like GreenAsia */
  NSString *info = [NSString stringWithFormat:@"Gamepads: %d connected", pads];
  for (NSMenuItem *it in _gamepadInfoItem) [it setTitle:info];
}

@end

/* ----- menu construction ------------------------------------------------- */
static void apply_targets(NSMenu *menu)
{
  for (NSMenuItem *it in [menu itemArray]) {
    if ([it action] != nil) it.target = g_handler;
    if ([it hasSubmenu]) apply_targets([it submenu]);
  }
}

void mac_menu_init(void)
{
  if (![NSApp mainMenu]) return;   /* headless / no Cocoa: do nothing */

  g_handler = [[MenuHandler alloc] init];

  NSMenu *main = [NSApp mainMenu];
  [main removeAllItems];

  /* --- Application menu --- */
  NSMenuItem *appItem = [main addItemWithTitle:@"Gens for Mac" action:nil keyEquivalent:@""];
  NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"Gens for Mac"];
  [appItem setSubmenu:appMenu];
  [appMenu addItemWithTitle:@"About Gens for Mac" action:@selector(doAbout:) keyEquivalent:@""];
  [appMenu addItem:[NSMenuItem separatorItem]];
  [[appMenu addItemWithTitle:@"Quit Gens for Mac" action:@selector(doQuit:) keyEquivalent:@"q"] setTarget:g_handler];

  /* --- Video menu --- */
  NSMenuItem *videoItem = [main addItemWithTitle:@"Video" action:nil keyEquivalent:@""];
  NSMenu *videoMenu = [[NSMenu alloc] initWithTitle:@"Video"];
  [videoItem setSubmenu:videoMenu];

  g_handler.stretchItem   = [videoMenu addItemWithTitle:@"Stretch to Fill" action:@selector(doStretch:) keyEquivalent:@""];
  g_handler.vsyncItem     = [videoMenu addItemWithTitle:@"VSync" action:@selector(doVsync:) keyEquivalent:@""];
  [videoMenu addItem:[NSMenuItem separatorItem]];

  /* Render submenu */
  NSMenu *renderMenu = [[NSMenu alloc] initWithTitle:@"Render"];
  NSMenuItem *renderItem = [videoMenu addItemWithTitle:@"Render" action:nil keyEquivalent:@""];
  [renderItem setSubmenu:renderMenu];
  g_handler.renderItems = [NSMutableArray array];
  const char *rm[5] = {"Normal", "Interpolated", "Scale2x", "2xSAI", "Hq2x"};
  for (int i = 0; i < 5; i++) {
    NSMenuItem *it = [renderMenu addItemWithTitle:[NSString stringWithUTF8String:rm[i]]
                                            action:@selector(doRender:) keyEquivalent:@""];
    it.tag = i; it.target = g_handler;
    [g_handler.renderItems addObject:it];
  }

  /* Scanline submenu */
  NSMenu *scanMenu = [[NSMenu alloc] initWithTitle:@"Scanline"];
  NSMenuItem *scanItem = [videoMenu addItemWithTitle:@"Scanline" action:nil keyEquivalent:@""];
  [scanItem setSubmenu:scanMenu];
  g_handler.scanlineItems = [NSMutableArray array];
  const char *scn[4] = {"Off", "25%", "50%", "100%"};
  for (int i = 0; i < 4; i++) {
    NSMenuItem *it = [scanMenu addItemWithTitle:[NSString stringWithUTF8String:scn[i]]
                                         action:@selector(doScanline:) keyEquivalent:@""];
    it.tag = i; it.target = g_handler;
    [g_handler.scanlineItems addObject:it];
  }

  [videoMenu addItem:[NSMenuItem separatorItem]];
  g_handler.greyscaleItem = [videoMenu addItemWithTitle:@"Greyscale" action:@selector(doGreyscale:) keyEquivalent:@""];

  /* Brightness submenu */
  NSMenu *brightMenu = [[NSMenu alloc] initWithTitle:@"Brightness"];
  NSMenuItem *brightItem = [videoMenu addItemWithTitle:@"Brightness" action:nil keyEquivalent:@""];
  [brightItem setSubmenu:brightMenu];
  [brightMenu addItemWithTitle:@"Decrease" action:@selector(doBrightnessDown:) keyEquivalent:@""];
  [brightMenu addItemWithTitle:@"Increase" action:@selector(doBrightnessUp:) keyEquivalent:@""];
  for (NSMenuItem *it in [brightMenu itemArray]) it.target = g_handler;
  g_handler.brightnessItem = brightItem;

  /* Contrast submenu */
  NSMenu *contrastMenu = [[NSMenu alloc] initWithTitle:@"Contrast"];
  NSMenuItem *contrastItem = [videoMenu addItemWithTitle:@"Contrast" action:nil keyEquivalent:@""];
  [contrastItem setSubmenu:contrastMenu];
  [contrastMenu addItemWithTitle:@"Decrease" action:@selector(doContrastDown:) keyEquivalent:@""];
  [contrastMenu addItemWithTitle:@"Increase" action:@selector(doContrastUp:) keyEquivalent:@""];
  for (NSMenuItem *it in [contrastMenu itemArray]) it.target = g_handler;
  g_handler.contrastItem = contrastItem;

  /* --- Controllers menu --- */
  NSMenuItem *ctrlItem = [main addItemWithTitle:@"Controllers" action:nil keyEquivalent:@""];
  NSMenu *ctrlMenu = [[NSMenu alloc] initWithTitle:@"Controllers"];
  [ctrlItem setSubmenu:ctrlMenu];

  /* The on-screen key-redefinition UI (old SDL-rendered method). Each port's
     input SOURCE (keyboard / a specific gamepad / none) and its per-button
     bindings are configured there; the native menu only activates it. */
  [[ctrlMenu addItemWithTitle:@"Configure Controls..." action:@selector(doConfigureControls:) keyEquivalent:@""]
      setTarget:g_handler];

  [ctrlMenu addItem:[NSMenuItem separatorItem]];
  NSMenuItem *gp = [ctrlMenu addItemWithTitle:@"Gamepads: 0 connected"
                                   action:nil keyEquivalent:@""];
  gp.enabled = NO;
  g_handler.gamepadInfoItem = [NSMutableArray arrayWithObject:gp];

  /* --- Emulation menu --- */
  NSMenuItem *emuItem = [main addItemWithTitle:@"Emulation" action:nil keyEquivalent:@""];
  NSMenu *emuMenu = [[NSMenu alloc] initWithTitle:@"Emulation"];
  [emuItem setSubmenu:emuMenu];
  [[emuMenu addItemWithTitle:@"Reset" action:@selector(doReset:) keyEquivalent:@""] setTarget:g_handler];
  [[emuMenu addItemWithTitle:@"Save State" action:@selector(doSaveState:) keyEquivalent:@""] setTarget:g_handler];
  [[emuMenu addItemWithTitle:@"Load State" action:@selector(doLoadState:) keyEquivalent:@""] setTarget:g_handler];
  [emuMenu addItem:[NSMenuItem separatorItem]];
  [[emuMenu addItemWithTitle:@"Toggle Sound" action:@selector(doToggleSound:) keyEquivalent:@""] setTarget:g_handler];
  [[emuMenu addItemWithTitle:@"Toggle Fullscreen" action:@selector(doToggleFullscreen:) keyEquivalent:@""] setTarget:g_handler];
  [[emuMenu addItemWithTitle:@"Toggle Turbo" action:@selector(doToggleTurbo:) keyEquivalent:@""] setTarget:g_handler];

  apply_targets(main);   /* ensure every actionable item routes to the handler */
  [g_handler sync];
}
