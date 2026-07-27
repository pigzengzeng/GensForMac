/*
 * Gens for Mac - SDL2 frontend
 * ----------------------------------------------------------------------------
 * A macOS (Intel x86_64) port of the Gens Sega Mega Drive / Genesis emulator.
 *
 * The original Gens engine (68k / Z80 / VDP / sound / blitters) was written
 * entirely in 32-bit x86 assembly and cannot run on 64-bit-only macOS.
 * This port keeps the Gens spirit and UX but drives a mature, 64-bit-clean
 * C emulation core (Genesis Plus GX lineage) instead of the legacy asm engine.
 *
 * Video : core renders RGB565 into `bitmap.data`, blitted via SDL_Texture.
 * Audio : SDL2 audio callback pulls resampled S16 stereo from audio_update().
 * Input : keyboard + first connected game controller.
 * ----------------------------------------------------------------------------
 */

#include "SDL.h"

/* sdl2-compat / SDL3-based headers dropped the SDL1.2 keypad aliases */
#ifndef SDLK_KP9
#define SDLK_KP9  SDLK_KP_9
#define SDLK_KP8  SDLK_KP_8
#define SDLK_KP7  SDLK_KP_7
#define SDLK_KP6  SDLK_KP_6
#define SDLK_KP5  SDLK_KP_5
#define SDLK_KP4  SDLK_KP_4
#define SDLK_KP3  SDLK_KP_3
#define SDLK_KP2  SDLK_KP_2
#define SDLK_KP1  SDLK_KP_1
#endif

#include "shared.h"
#include "sms_ntsc.h"
#include "md_ntsc.h"
#include "settings.h"
#include "video_fx.h"
#include "ui.h"
#include "font.h"
#include "cocoa_menu.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define APP_NAME          "Gens for Mac"
#define SOUND_FREQUENCY   48000
#define SOUND_SAMPLES     2048

/* Full internal framebuffer the core may render into */
#define FB_WIDTH   720
#define FB_HEIGHT  576

/* Default logical Mega Drive resolution & window scale */
#define BASE_W     320
#define BASE_H     240
#define DEFAULT_SCALE 2

/* texture is allocated at 2x to hold Scale2x / 2xSAI / Hq2x output */
#define TEX_W   (FB_WIDTH * 2)
#define TEX_H   (FB_HEIGHT * 2)
static int last_vsync = -1;

md_ntsc_t *md_ntsc;
sms_ntsc_t *sms_ntsc;

/* referenced by the core / osd layer (declared in main.h) */
int debug_on  = 0;
int log_error = 0;

static int use_sound   = 1;
static int turbo_mode  = 0;
static int running     = 1;
static int fullscreen  = 0;

static char window_title[1280] = APP_NAME;  /* current window title */

static char rom_path[1024] = {0};
static char sram_path[1088] = {0};
static char state_path[1088] = {0};

/* ------------------------------------------------------------------ video */
static struct {
  SDL_Window   *window;
  SDL_Renderer *renderer;
  SDL_Texture  *texture;
  int frames_rendered;
} vid;

/* ------------------------------------------------------------------ audio */
static struct {
  SDL_AudioDeviceID dev;
  Uint8 *buffer;
  int   size;        /* ring buffer size in bytes */
  volatile int head; /* write pos (bytes) */
  volatile int tail; /* read pos (bytes)  */
} au;

static short soundframe[SOUND_SAMPLES * 2];

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
  (void)userdata;
  int avail = au.head - au.tail;
  if (avail < 0) avail += au.size;

  if (avail < len) {
    /* underrun: output what we have + silence */
    int first = au.size - au.tail;
    if (first > avail) first = avail;
    memcpy(stream, au.buffer + au.tail, first);
    if (avail - first > 0)
      memcpy(stream + first, au.buffer, avail - first);
    memset(stream + avail, 0, len - avail);
    au.tail = (au.tail + avail) % au.size;
    return;
  }

  int first = au.size - au.tail;
  if (first > len) first = len;
  memcpy(stream, au.buffer + au.tail, first);
  if (len - first > 0)
    memcpy(stream + first, au.buffer, len - first);
  au.tail = (au.tail + len) % au.size;
}

static int audio_open(void)
{
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq     = SOUND_FREQUENCY;
  want.format   = AUDIO_S16SYS;
  want.channels = 2;
  want.samples  = SOUND_SAMPLES;
  want.callback = audio_callback;

  au.dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (!au.dev) {
    fprintf(stderr, "[audio] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    return 0;
  }
  au.size   = SOUND_FREQUENCY * 2 * sizeof(short); /* ~1s ring */
  au.buffer = (Uint8 *)calloc(1, au.size);
  au.head   = au.tail = 0;
  SDL_PauseAudioDevice(au.dev, 0);
  return 1;
}

static void audio_push(void)
{
  int frames = audio_update(soundframe);       /* stereo sample pairs */
  int bytes  = frames * 2 * sizeof(short);
  Uint8 *src = (Uint8 *)soundframe;

  SDL_LockAudioDevice(au.dev);
  int space = au.tail - au.head - 1;
  if (space < 0) space += au.size;
  if (bytes > space) bytes = space;            /* drop on overflow */
  int first = au.size - au.head;
  if (first > bytes) first = bytes;
  memcpy(au.buffer + au.head, src, first);
  if (bytes - first > 0)
    memcpy(au.buffer, src + first, bytes - first);
  au.head = (au.head + bytes) % au.size;
  SDL_UnlockAudioDevice(au.dev);
}

static void audio_close(void)
{
  if (au.dev) SDL_CloseAudioDevice(au.dev);
  free(au.buffer);
  au.buffer = NULL;
}

/* ------------------------------------------------------------------ video */
static void video_recreate(int vsync);  /* defined below; used by video_init */

static int video_init(void)
{
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "[video] init failed: %s\n", SDL_GetError());
    return 0;
  }
  vid.window = SDL_CreateWindow(
      APP_NAME,
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      BASE_W * DEFAULT_SCALE, BASE_H * DEFAULT_SCALE,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!vid.window) { fprintf(stderr, "[video] window: %s\n", SDL_GetError()); return 0; }

  video_recreate(settings.vsync);
  last_vsync = settings.vsync;
  return 1;
}

/* (re)create the renderer + texture honouring the current vsync setting */
static void video_recreate(int vsync)
{
  if (vid.texture)  SDL_DestroyTexture(vid.texture);
  if (vid.renderer) SDL_DestroyRenderer(vid.renderer);

  vid.renderer = SDL_CreateRenderer(vid.window, -1,
      SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
  if (!vid.renderer)
    vid.renderer = SDL_CreateRenderer(vid.window, -1, 0);
  if (!vid.renderer) { fprintf(stderr, "[video] renderer: %s\n", SDL_GetError()); return; }

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
              settings.render_mode == RM_INTERPOLATED ? "linear" : "nearest");
  vid.texture = SDL_CreateTexture(vid.renderer,
      SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
      TEX_W, TEX_H);
  if (!vid.texture) { fprintf(stderr, "[video] texture: %s\n", SDL_GetError()); return; }
}

static void video_render(void)
{
  /* run one emulated frame for the active system */
  if (system_hw == SYSTEM_MCD)
    system_frame_scd(0);
  else if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
    system_frame_gen(0);
  else
    system_frame_sms(0);

  int vw = bitmap.viewport.w + 2 * bitmap.viewport.x;
  int vh = bitmap.viewport.h + 2 * bitmap.viewport.y;
  if (vw <= 0) vw = BASE_W;
  if (vh <= 0) vh = BASE_H;

  /* recreate renderer if the vsync setting changed at runtime */
  if (settings.vsync != last_vsync) {
    video_recreate(settings.vsync);
    last_vsync = settings.vsync;
  }

  int ow, oh;
  uint16 *out = video_fx_process((const uint16 *)bitmap.data, bitmap.pitch,
                                  vw, vh, settings.render_mode, settings.scanline,
                                  settings.greyscale, settings.brightness, settings.contrast,
                                  &ow, &oh);
  if (!out || ow <= 0 || oh <= 0) return;

  SDL_SetTextureScaleMode(vid.texture,
      settings.render_mode == RM_INTERPOLATED ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);

  SDL_Rect src = { 0, 0, ow, oh };
  SDL_UpdateTexture(vid.texture, &src, out, ow * 2);

  /* present with aspect preservation (or stretched to fill) */
  int rw, rh;
  SDL_GetRendererOutputSize(vid.renderer, &rw, &rh);
  SDL_Rect dst;
  if (settings.stretch) {
    dst.x = 0; dst.y = 0; dst.w = rw; dst.h = rh;
  } else {
    double ar = (double)ow / (double)oh;
    int dw = rw, dh = (int)(rw / ar);
    if (dh > rh) { dh = rh; dw = (int)(rh * ar); }
    dst.x = (rw - dw) / 2; dst.y = (rh - dh) / 2; dst.w = dw; dst.h = dh;
  }

  SDL_RenderClear(vid.renderer);
  SDL_RenderCopy(vid.renderer, vid.texture, &src, &dst);
  SDL_RenderPresent(vid.renderer);
  vid.frames_rendered++;
}

static void video_close(void)
{
  if (vid.texture)  SDL_DestroyTexture(vid.texture);
  if (vid.renderer) SDL_DestroyRenderer(vid.renderer);
  if (vid.window)   SDL_DestroyWindow(vid.window);
}

/* ------------------------------------------------------------------ paths */
static void derive_paths(const char *rom)
{
  strncpy(rom_path, rom, sizeof(rom_path) - 1);
  snprintf(sram_path, sizeof(sram_path), "%s.srm", rom_path);
  snprintf(state_path, sizeof(state_path), "%s.gs0", rom_path);
}

static void sram_load(void)
{
  if (!sram.on) return;
  FILE *f = fopen(sram_path, "rb");
  if (f) { if (fread(sram.sram, 1, 0x10000, f)) {} fclose(f); }
}

static void sram_save(void)
{
  if (!sram.on) return;
  FILE *f = fopen(sram_path, "wb");
  if (f) { fwrite(sram.sram, 1, 0x10000, f); fclose(f); }
}

static void state_save_file(void)
{
  uint8 *buf = (uint8 *)malloc(STATE_SIZE);
  if (!buf) return;
  int len = state_save(buf);
  FILE *f = fopen(state_path, "wb");
  if (f) { fwrite(buf, 1, len, f); fclose(f); fprintf(stderr, "[state] saved: %s\n", state_path); }
  free(buf);
}

static void state_load_file(void)
{
  FILE *f = fopen(state_path, "rb");
  if (!f) return;
  uint8 *buf = (uint8 *)malloc(STATE_SIZE);
  if (buf) {
    size_t n = fread(buf, 1, STATE_SIZE, f);
    if (n) { state_load(buf); fprintf(stderr, "[state] loaded: %s\n", state_path); }
    free(buf);
  }
  fclose(f);
}

/* ------------------------------------------------------------------ input
 * Called by the emulation core once per frame via the osd_input_update()
 * hook (see osd.h). Reads keyboard + first game controller into input.pad. */
/* ------------------------------------------------------------------ gamepads
 * Connected SDL game controllers are enumerated once (and re-enumerated on
 * hot-plug) and cached here so we don't re-open them every frame. Up to 8
 * pads are tracked. Devices that SDL recognises as Game Controllers use the
 * high-level SDL_GameController API; everything else (many third-party /
 * Bluetooth pads, especially under sdl2-compat on macOS) is opened as a raw
 * SDL_Joystick with a best-effort button mapping so it is not silently dropped. */
typedef struct {
  SDL_GameController *gc;   /* non-NULL when opened as a Game Controller */
  SDL_Joystick       *joy;  /* non-NULL when opened as a raw joystick */
  char label[64];           /* "GC name" / "JOY name" for on-screen diagnostics */
  Sint16 raw_idle[8];      /* sampled resting value of each raw axis, used to
                              skip axes that rest away from centre (so a d-pad
                              mapped to such an axis can't jam a direction on) */
} pad_t;
static pad_t pads[8];
static int  npads = 0;

/* Load community + device-specific game-controller mappings so third-party
   pads (notably BETOP C3, which SDL2 2.32.8's bundled DB doesn't know) are
   recognised as real Game Controllers with the correct button layout rather
   than falling back to a raw joystick whose hard-coded button order scrambles
   the face buttons. MUST run before gamepads_enumerate(). */
static void load_gamecontroller_mappings(void)
{
  /* Community SDL_GameControllerDB (best-effort; ignore if the file is absent
     or unreadable). Helps every other pad work out of the box too. */
  const char *cands[] = { "gamecontrollerdb.txt", "src/gamecontrollerdb.txt", NULL };
  char path[1024];
  const char *base = SDL_GetBasePath();
  if (base) {
    snprintf(path, sizeof(path), "%sgamecontrollerdb.txt", base);
    SDL_GameControllerAddMappingsFromFile(path);                 /* next to exe */
    snprintf(path, sizeof(path), "%s../Resources/gamecontrollerdb.txt", base);
    SDL_GameControllerAddMappingsFromFile(path);                 /* inside .app */
    SDL_free((void *)base);
  }
  for (int i = 0; cands[i]; i++)
    SDL_GameControllerAddMappingsFromFile(cands[i]);

  /* BETOP C3 (vendor 0x20bc / product 0x0de0) on macOS -- exact GUID seen on
     this machine. Added LAST so it overrides any community entry. The face /
     shoulder / trigger / stick buttons are mapped here (these button mappings
     are reliable). The d-pad is deliberately LEFT OUT of the mapping: in this
     SDL2 2.32.8 macOS build the hat->DPAD-button translation does not fire, and
     referencing the hat makes SDL "consume" it so the underlying
     SDL_JoystickGetHat() returns centred. By leaving dpup/dpdown/... out, the
     d-pad stays readable as a raw hat 0, which pad_button_down() reads directly
     (see the DPAD case in pad_button_down). Triggers map to the trigger AXES
     (lefttrigger:b6/righttrigger:b7) so the existing axis-based LT/RT handling
     keeps working unchanged. */
  static const char betop_c3[] =
    "03004c4ebc200000e00d000014010000,BETOP C3,"
    "a:b2,b:b1,back:b8,"
    "leftshoulder:b4,leftstick:b10,lefttrigger:b6,leftx:a0,lefty:a1,"
    "rightshoulder:b5,rightstick:b11,righttrigger:b7,rightx:a2,righty:a3,"
    "start:b9,x:b3,y:b0,";
  SDL_GameControllerAddMapping(betop_c3);
}

static void gamepads_enumerate(void)
{
  for (int i = 0; i < npads; i++) {
    if (pads[i].gc)  SDL_GameControllerClose(pads[i].gc);
    if (pads[i].joy) SDL_JoystickClose(pads[i].joy);
    pads[i].gc = NULL; pads[i].joy = NULL;
  }
  npads = 0;
  int n = SDL_NumJoysticks();
  fprintf(stderr, "[pads] SDL_NumJoysticks=%d\n", n);
  for (int j = 0; j < n && npads < 8; j++) {
    if (SDL_IsGameController(j)) {
      SDL_GameController *gc = SDL_GameControllerOpen(j);
      if (gc) {
        pads[npads].gc = gc; pads[npads].joy = NULL;
        const char *nm = SDL_GameControllerName(gc);
        snprintf(pads[npads].label, sizeof(pads[npads].label), "GC %s", nm ? nm : "?");
        fprintf(stderr, "[pads] #%d opened as GameController: %s\n", npads, nm ? nm : "?");
        npads++;
      } else {
        fprintf(stderr, "[pads] device %d GC open FAILED: %s\n", j, SDL_GetError());
      }
    } else {
      SDL_Joystick *joy = SDL_JoystickOpen(j);
      if (joy) {
        pads[npads].gc = NULL; pads[npads].joy = joy;
        const char *nm = SDL_JoystickName(joy);
        snprintf(pads[npads].label, sizeof(pads[npads].label), "JOY %s", nm ? nm : "?");
        /* sample each axis' resting value so the d-pad reader can skip axes
           that rest away from centre (a digital axis that idles at an extreme
           would otherwise jam a direction on permanently). */
        SDL_JoystickUpdate();
        int na = SDL_JoystickNumAxes(joy);
        for (int a = 0; a < na && a < 8; a++) {
          Sint16 s = 0;
          for (int t = 0; t < 3; t++) s += SDL_JoystickGetAxis(joy, a);
          pads[npads].raw_idle[a] = (Sint16)(s / 3);
        }
        fprintf(stderr, "[pads] #%d opened as raw Joystick: %s (buttons=%d hats=%d axes=%d)\n",
                npads, nm ? nm : "?", SDL_JoystickNumButtons(joy),
                SDL_JoystickNumHats(joy), SDL_JoystickNumAxes(joy));
        npads++;
      } else {
        fprintf(stderr, "[pads] device %d JOY open FAILED: %s\n", j, SDL_GetError());
      }
    }
  }

  /* turn PORT_DEV_AUTO into a concrete source now that we know the pads:
     port mp binds to gamepad mp if it exists, else port 0 falls back to the
     keyboard and the rest stay NONE (unused). Already-explicit choices are
     left untouched, so manual configuration survives hot-plug re-scans. */
  settings_resolve_auto();
}

/* Resolve every PORT_DEV_AUTO source (see settings.h). Called from
   gamepads_enumerate() after the pad list is rebuilt, and once at startup. */
void settings_resolve_auto(void)
{
  for (int mp = 0; mp < NUM_PORTS; mp++) {
    if (settings.port_dev[mp] != PORT_DEV_AUTO) continue;
    if (mp < npads)            settings.port_dev[mp] = mp;          /* pad mp */
    else if (mp == 0)          settings.port_dev[mp] = PORT_DEV_KEYBOARD;
    else                       settings.port_dev[mp] = PORT_DEV_NONE;
  }
}

/* find the pad that owns a given joystick instance id and is opened RAW
   (game controllers get their own SDL_CONTROLLER* events; without this guard
   a GC would deliver every button twice via the duplicate joystick events). */
static pad_t *raw_pad_from_instance(SDL_JoystickID id)
{
  for (int i = 0; i < npads; i++) {
    if (pads[i].joy && !pads[i].gc &&
        SDL_JoystickInstanceID(pads[i].joy) == id)
      return &pads[i];
  }
  return NULL;
}

/* counterpart of raw_pad_from_instance(): find the pad whose Game Controller
   owns the joystick with the given instance id, so a Game Controller whose
   d-pad is reported on a hat still feeds the d-pad bind in the UI. */
static pad_t *gc_pad_from_instance(SDL_JoystickID id)
{
  for (int i = 0; i < npads; i++) {
    if (pads[i].gc &&
        SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i].gc)) == id)
      return &pads[i];
  }
  return NULL;
}

/* reverse of joy_button_index(): raw joystick button -> GameController enum,
   used so buttons captured from a raw joystick land in the same gpadmap. */
static SDL_GameControllerButton joy_index_to_button(int bi)
{
  switch (bi) {
    case 0:  return SDL_CONTROLLER_BUTTON_A;
    case 1:  return SDL_CONTROLLER_BUTTON_B;
    case 2:  return SDL_CONTROLLER_BUTTON_X;
    case 3:  return SDL_CONTROLLER_BUTTON_Y;
    case 4:  return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    case 5:  return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    case 6:  return SDL_CONTROLLER_BUTTON_BACK;
    case 7:  return SDL_CONTROLLER_BUTTON_START;
    case 8:  return SDL_CONTROLLER_BUTTON_GUIDE;
    case 9:  return SDL_CONTROLLER_BUTTON_LEFTSTICK;
    case 10: return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    case 12: return SDL_CONTROLLER_BUTTON_DPAD_UP;
    case 13: return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    case 14: return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    case 15: return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    default: return SDL_CONTROLLER_BUTTON_INVALID;
  }
}

/* d-pad / trigger deflection thresholds, shared by in-game reading and the
   settings capture. Defined here (above pad_button_down) so the helpers that
   use it compile. */
#define AXIS_ON 16000
#define TRIG_ON 8000   /* trigger axes rest at 0, full pull = 32767 */

/* Many cheap pads that SDL opens as a RAW joystick (e.g. GreenAsia USB
   Joystick) report their d-pad on a pair of analog axes (often axes 2/3, not
   the left-stick axes 0/1). Read every axis whose resting value is near centre
   and treat a large deflection as a d-pad direction. Axes that idle away from
   centre (digital axes that rest at an extreme, or trigger axes) are skipped so
   they can't jam a direction on. Even axis index => horizontal (LEFT/RIGHT),
   odd index => vertical (UP/DOWN); sign gives the direction. */
static int raw_dpad_axis(const pad_t *pd, SDL_GameControllerButton gb)
{
  int na = SDL_JoystickNumAxes(pd->joy);
  for (int a = 0; a < na && a < 8; a++) {
    if (abs(pd->raw_idle[a]) > 8000) continue;       /* skip off-centre axes */
    Sint16 v = SDL_JoystickGetAxis(pd->joy, a);
    if (abs(v) <= AXIS_ON) continue;
    int horiz = (a % 2 == 0);
    if (horiz) {
      if (v < 0 && gb == SDL_CONTROLLER_BUTTON_DPAD_LEFT)  return 1;
      if (v > 0 && gb == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) return 1;
    } else {
      if (v < 0 && gb == SDL_CONTROLLER_BUTTON_DPAD_UP)    return 1;
      if (v > 0 && gb == SDL_CONTROLLER_BUTTON_DPAD_DOWN)  return 1;
    }
  }
  return 0;
}

/* ---- on-screen diagnostics used by ui.c (SCR_CTRL footer) --------------- */
int gens_pad_count(void) { return npads; }

const char *gens_pad_label(int i)
{
  return (i >= 0 && i < npads) ? pads[i].label : "";
}

/* non-zero when the pad currently reports ANY pressed button / hat / big axis
   deflection -- lets the user see immediately whether SDL receives input. */
int gens_pad_pressed(int i)
{
  if (i < 0 || i >= npads) return 0;
  pad_t *pd = &pads[i];
  if (pd->gc) {
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++)
      if (SDL_GameControllerGetButton(pd->gc, (SDL_GameControllerButton)b)) return 1;
    if (abs(SDL_GameControllerGetAxis(pd->gc, SDL_CONTROLLER_AXIS_LEFTX)) > 16000) return 1;
    if (abs(SDL_GameControllerGetAxis(pd->gc, SDL_CONTROLLER_AXIS_LEFTY)) > 16000) return 1;
    if (SDL_GameControllerGetAxis(pd->gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > 8000) return 1;
    if (SDL_GameControllerGetAxis(pd->gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000) return 1;
    /* also catch a d-pad that lives on the underlying joystick's hat
       (see pad_button_down) so pressing directions lights the diagnostic */
    SDL_Joystick *j = SDL_GameControllerGetJoystick(pd->gc);
    if (j) {
      int nh = SDL_JoystickNumHats(j);
      for (int h = 0; h < nh; h++)
        if (SDL_JoystickGetHat(j, h) != SDL_HAT_CENTERED) return 1;
    }
  } else if (pd->joy) {
    int nb = SDL_JoystickNumButtons(pd->joy);
    for (int b = 0; b < nb; b++)
      if (SDL_JoystickGetButton(pd->joy, b)) return 1;
    int nh = SDL_JoystickNumHats(pd->joy);
    for (int h = 0; h < nh; h++)
      if (SDL_JoystickGetHat(pd->joy, h) != SDL_HAT_CENTERED) return 1;
    int na = SDL_JoystickNumAxes(pd->joy);
    for (int a = 0; a < na && a < 2; a++)
      if (abs(SDL_JoystickGetAxis(pd->joy, a)) > 16000) return 1;
  }
  return 0;
}

/* best-effort mapping from a Game Controller button enum to a raw joystick
   button index; returns -1 if unmapped, -2 to indicate "use hat 0 for dpad". */
static int joy_button_index(SDL_GameControllerButton gb)
{
  switch (gb) {
    case SDL_CONTROLLER_BUTTON_A:             return 0;
    case SDL_CONTROLLER_BUTTON_B:             return 1;
    case SDL_CONTROLLER_BUTTON_X:             return 2;
    case SDL_CONTROLLER_BUTTON_Y:             return 3;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return 4;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return 5;
    case SDL_CONTROLLER_BUTTON_BACK:          return 6;
    case SDL_CONTROLLER_BUTTON_START:         return 7;
    case SDL_CONTROLLER_BUTTON_GUIDE:         return 8;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return 9;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return 10;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return -2;  /* read hat 0 */
    default:                                  return -1;
  }
}

/* return non-zero if the given pad device currently reports `gb` pressed.
   For the four dpad directions the LEFT ANALOG STICK is also honoured (axis
   0/1, ~50% deflection) -- many pads map their primary stick there and users
   expect it to steer the game just like the dpad. */
static int pad_button_down(const pad_t *pd, SDL_GameControllerButton gb)
{
  /* LT / RT are ANALOG AXES in SDL, not buttons; they are stored in gpadmap
     as pseudo button codes (GBTN_LTRIGGER/RTRIGGER, see settings.h) and read
     here as axis pulls. Raw joysticks have no reliable trigger convention
     (an idle extra axis may rest at an extreme), so triggers are GC-only. */
  if (gb == GBTN_LTRIGGER)
    return pd->gc &&
      SDL_GameControllerGetAxis(pd->gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > TRIG_ON;
  if (gb == GBTN_RTRIGGER)
    return pd->gc &&
      SDL_GameControllerGetAxis(pd->gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > TRIG_ON;

  if (pd->gc) {
    if (SDL_GameControllerGetButton(pd->gc, gb)) return 1;
    switch (gb) {
      case SDL_CONTROLLER_BUTTON_DPAD_UP:
      case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
      case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
      case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: {
        /* Many cheap pads (e.g. HJC/BETOP C3) are recognised as Game
           Controllers whose SDL mapping puts the d-pad on hat 0
           (dpup:h0.1 ...), but sdl2-compat's hat->DPAD-button translation is
           unreliable, so the GetButton() above reads 0 and the d-pad appears
           dead. Read the underlying joystick's hat 0 directly (authoritative
           for this pad) and also honour the left analog stick. We deliberately
           do NOT guess dpad-on-axis (axes 6/7): an idle extra axis can rest at
           an extreme value and would jam a direction on permanently. */
        int lx = SDL_GameControllerGetAxis(pd->gc, SDL_CONTROLLER_AXIS_LEFTX);
        int ly = SDL_GameControllerGetAxis(pd->gc, SDL_CONTROLLER_AXIS_LEFTY);
        SDL_Joystick *j = SDL_GameControllerGetJoystick(pd->gc);
        Uint8 hat = SDL_HAT_CENTERED;
        if (j) {
          int nh = SDL_JoystickNumHats(j);
          for (int h = 0; h < nh; h++)
            hat |= SDL_JoystickGetHat(j, h);
        }
        /* HJC/BETOP C3 (and many cheap pads) report the d-pad on the FIRST TWO
           RAW axes (the left-stick axes), not as DPAD buttons or a hat -- so
           read those underlying raw axes directly too. This is belt-and-
           -suspenders: it works whether or not the mapping assigns the d-pad to
           the SDL left-stick axes (leftx:a0/lefty:a1). We deliberately only
           ever read axes 0/1 here (the primary stick), never high-numbered
           "extra" axes, because an idle extra axis can rest at an extreme and
           would jam a direction on permanently. */
        int rax0 = (j && SDL_JoystickNumAxes(j) > 1) ? SDL_JoystickGetAxis(j, 0) : 0;
        int rax1 = (j && SDL_JoystickNumAxes(j) > 1) ? SDL_JoystickGetAxis(j, 1) : 0;
        switch (gb) {
          case SDL_CONTROLLER_BUTTON_DPAD_UP:
            return (ly < -AXIS_ON) || (rax1 < -AXIS_ON) || (hat & SDL_HAT_UP);
          case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            return (ly >  AXIS_ON) || (rax1 >  AXIS_ON) || (hat & SDL_HAT_DOWN);
          case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            return (lx < -AXIS_ON) || (rax0 < -AXIS_ON) || (hat & SDL_HAT_LEFT);
          case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            return (lx >  AXIS_ON) || (rax0 >  AXIS_ON) || (hat & SDL_HAT_RIGHT);
          default: return 0;
        }
      }
      default: return 0;
    }
  }
  if (pd->joy) {
    int bi = joy_button_index(gb);
    if (bi >= 0)
      return SDL_JoystickGetButton(pd->joy, bi);
    if (bi == -2) {          /* dpad: hat 0, fallback buttons 12-15, axes 0/1 */
      Uint8 h = SDL_JoystickGetHat(pd->joy, 0);
      switch (gb) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
          return (h & SDL_HAT_UP)    || SDL_JoystickGetButton(pd->joy, 12) ||
                 SDL_JoystickGetAxis(pd->joy, 1) < -AXIS_ON ||
                 raw_dpad_axis(pd, gb);
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
          return (h & SDL_HAT_DOWN)  || SDL_JoystickGetButton(pd->joy, 13) ||
                 SDL_JoystickGetAxis(pd->joy, 1) >  AXIS_ON ||
                 raw_dpad_axis(pd, gb);
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
          return (h & SDL_HAT_LEFT)  || SDL_JoystickGetButton(pd->joy, 14) ||
                 SDL_JoystickGetAxis(pd->joy, 0) < -AXIS_ON ||
                 raw_dpad_axis(pd, gb);
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
          return (h & SDL_HAT_RIGHT) || SDL_JoystickGetButton(pd->joy, 15) ||
                 SDL_JoystickGetAxis(pd->joy, 0) >  AXIS_ON ||
                 raw_dpad_axis(pd, gb);
        default: break;
      }
    }
  }
  return 0;
}

/* Called by the emulation core once per frame via the osd_input_update()
 * hook (see osd.h). Reads each on-screen port's chosen SOURCE into input.pad.
 *
 * Every port (PORT 1..8 in the UI) is independently bound to a source stored in
 * settings.port_dev[]: the keyboard, a specific connected gamepad (a pads[]
 * index), or nothing. The binding is fixed and explicit -- no "most-recently
 * used" magic -- so the on-screen setup and the live input always agree.
 *
 * A plain 2-player game only reads input.pad[0] (player 1, console port 0) and
 * input.pad[4] (player 2, console port 1). Slots 1-3 / 5-7 are only read by
 * Team-Player / 4-Way Play games. port_to_pad[] translates each menu port into
 * its real core slot, so player 2 reliably lands on input.pad[4].
 *
 * "One pad controls two players" (一控二) is automatic: if PORT 1 and PORT 2
 * are both bound to the SAME gamepad, both core slots read that one device, so
 * a single controller drives both players -- useful for some shmups / brawlers. */
int sdl_input_update(void)
{
  static const int mask[12] = {
    INPUT_UP, INPUT_DOWN, INPUT_LEFT, INPUT_RIGHT,
    INPUT_A, INPUT_B, INPUT_C, INPUT_X, INPUT_Y, INPUT_Z,
    INPUT_START, INPUT_MODE
  };

  /* start from a clean slate so unconfigured slots report "no buttons" */
  for (int i = 0; i < MAX_DEVICES; i++) input.pad[i] = 0;

  const Uint8 *k = SDL_GetKeyboardState(NULL);

  for (int mp = 0; mp < NUM_PORTS; mp++) {
    int core = port_to_pad[mp];
    int src  = settings.port_dev[mp];

    if (src == PORT_DEV_KEYBOARD) {
      for (int b = 0; b < 12; b++) {
        SDL_Scancode sc = settings.keymap[mp][b];
        if (sc != SDL_SCANCODE_UNKNOWN && k[sc])
          input.pad[core] |= mask[b];
      }
    } else if (src >= 0 && src < npads) {
      pad_t *pd = &pads[src];
      for (int b = 0; b < 12; b++) {
        SDL_GameControllerButton gb = settings.gpadmap[mp][b];
        if (gb >= 0 && pad_button_down(pd, gb))
          input.pad[core] |= mask[b];
      }
    }
    /* PORT_DEV_NONE (or a stale/disconnected index) contributes nothing */
  }
  return 1;
}

/* ------------------------------------------------------------------ rom load / boot */
static int boot_rom_load(void);

static int start_game(const char *path)
{
  derive_paths(path);

  if (!load_rom((char *)path)) {
    char msg[1200];
    snprintf(msg, sizeof(msg), "Failed to load ROM:\n%s", path);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, APP_NAME, msg, vid.window);
    return 0;
  }

  audio_init(SOUND_FREQUENCY, 0);
  system_init();
  sram_load();
  system_reset();

  const char *name = (rominfo.international[0] != 0x20 && rominfo.international[0])
                     ? rominfo.international : rominfo.domestic;
  snprintf(window_title, sizeof(window_title), "%s - %s", APP_NAME, name);
  SDL_SetWindowTitle(vid.window, window_title);
  return 1;
}

static void handle_key(SDL_Keycode key)
{
  switch (key) {
    case SDLK_TAB:    system_reset(); break;
    case SDLK_ESCAPE: running = 0;    break;
    case SDLK_F2:
      fullscreen = fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP;
      SDL_SetWindowFullscreen(vid.window, fullscreen);
      break;
    case SDLK_F4:  use_sound ^= 1; SDL_PauseAudioDevice(au.dev, !use_sound); break;
    case SDLK_F5:  state_save_file(); break;
    case SDLK_F7:  state_load_file(); break;
    case SDLK_F6:  turbo_mode ^= 1;  break;
    default: break;
  }
}

/* ------------------------------------------------------------------ native
 * menu hooks (invoked from cocoa_menu.m action methods) */

/* Open the on-screen controller-setup overlay (the old SDL-rendered key
   redefinition UI). Called from Controllers > "Configure Controls...". */
void mac_action_open_controls(void)
{
  ui_open();
}

void mac_action_reset(void)        { system_reset(); }
void mac_action_save_state(void)   { state_save_file(); }
void mac_action_load_state(void)   { state_load_file(); }
void mac_action_toggle_sound(void)
{
  use_sound ^= 1;
  SDL_PauseAudioDevice(au.dev, !use_sound);
}
void mac_action_toggle_fullscreen(void)
{
  fullscreen = fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP;
  SDL_SetWindowFullscreen(vid.window, fullscreen);
}
void mac_action_toggle_turbo(void) { turbo_mode ^= 1; }
void mac_action_quit(void)         { running = 0; }

/* ------------------------------------------------------------------ main */
int main(int argc, char **argv)
{
  fprintf(stderr, "%s (Sega Mega Drive / Genesis, Intel x86_64)\n", APP_NAME);

  error_init();
  set_config_defaults();
  settings_load();
  settings_apply();
  ui_init();
  system_bios = 0;

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER |
               SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) < 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  load_gamecontroller_mappings();
  gamepads_enumerate();   /* open any connected game controllers */

  if (!video_init()) { SDL_Quit(); return 1; }
  audio_open();
  boot_rom_load();

  /* set up the shared framebuffer the core renders into */
  memset(&bitmap, 0, sizeof(t_bitmap));
  bitmap.width  = FB_WIDTH;
  bitmap.height = FB_HEIGHT;
  bitmap.pitch  = FB_WIDTH * 2;             /* RGB565 => 2 bytes/pixel */
  bitmap.data   = (uint8 *)calloc(1, FB_WIDTH * FB_HEIGHT * 2);
  bitmap.viewport.changed = 3;

  /* hidden self-test: `gens_mac --selftest rom.bin` runs N frames headless,
     prints framebuffer statistics, and exits (used to verify the port). */
  if (argc >= 3 && strcmp(argv[1], "--selftest") == 0) {
    if (!start_game(argv[2])) { SDL_Quit(); return 2; }
    int frames = (argc >= 4) ? atoi(argv[3]) : 600;
    long peak_nonzero = 0;
    for (int f = 0; f < frames; f++) {
      if (system_hw == SYSTEM_MCD) system_frame_scd(0);
      else if ((system_hw & SYSTEM_PBC) == SYSTEM_MD) system_frame_gen(0);
      else system_frame_sms(0);
      audio_update(soundframe);
      /* track peak filled pixels across whole run */
      uint16 *p = (uint16 *)bitmap.data;
      long nz = 0;
      for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) if (p[i]) nz++;
      if (nz > peak_nonzero) peak_nonzero = nz;
    }
    /* analyze final framebuffer (full buffer) */
    uint16 *px = (uint16 *)bitmap.data;
    int vw = bitmap.viewport.w, vh = bitmap.viewport.h;
    long nonzero = 0, distinct_seen = 0;
    unsigned short seen[16]; int ns = 0;
    for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
      uint16 c = px[i];
      if (c) nonzero++;
      int found = 0;
      for (int j = 0; j < ns; j++) if (seen[j] == c) { found = 1; break; }
      if (!found && ns < 16) { seen[ns++] = c; distinct_seen++; }
    }
    fprintf(stderr, "[selftest] frames=%d system_hw=%d viewport=%dx%d final_nonzero=%ld peak_nonzero=%ld distinct(sample)=%ld\n",
            frames, system_hw, vw, vh, nonzero, peak_nonzero, distinct_seen);
    if (peak_nonzero > nonzero) nonzero = peak_nonzero;
    fprintf(stderr, "[selftest] ROM=\"%s\"\n",
            (rominfo.international[0] > 0x20) ? rominfo.international : rominfo.domestic);
    fprintf(stderr, "[selftest] %s\n", nonzero > 0 ? "PASS: emulation produced video output" : "FAIL: blank framebuffer");

    /* (Video settings live in the native macOS menu bar. The on-screen
       controller-setup overlay is opened from Controllers > "Configure
       Controls..."; its core video/input pipeline is exercised below.) */

    /* native menu construction smoke test (safe no-op if no Cocoa main menu) */
    mac_menu_init();
    fprintf(stderr, "[selftest] MENU: built native menu bar OK\n");

    /* on-screen controller overlay smoke test: open it, render both screens,
       cycle a port source and capture one key + one pad button, then close it.
       (selftest runs with a temp HOME so settings_save() writes there, not the
       user's rc) */
    {
      int cok = 1;
      mac_action_open_controls();                 /* ui_open() */
      if (!ui_is_open()) cok = 0;
      ui_render(vid.renderer);                     /* SCR_CTRL render */

      /* cycle PORT 1's source: LEFT must move to a different source, RIGHT
         must move it back. Robust to the number of gamepads present (AUTO
         resolution may have started PORT 1 on a gamepad, not the keyboard). */
      int src_before = settings.port_dev[0];
      ui_handle_key(SDLK_LEFT);
      int src_afterL = settings.port_dev[0];
      if (src_afterL == src_before) cok = 0;
      ui_handle_key(SDLK_RIGHT);
      if (settings.port_dev[0] != src_before) cok = 0;

      /* PORT 2 (sel -> 1): enter REDEF, bind keyboard key to UP (button 0).
         After entering SCR_REDEF sel=0 is already the UP row (no TYPE row). */
      ui_handle_key(SDLK_DOWN);                    /* sel 0 -> 1 (PORT 2) */
      ui_handle_key(SDLK_RETURN);                  /* enter SCR_REDEF redef_port=1 */
      ui_handle_key(SDLK_RETURN);                  /* begin capture on btn 0 (UP) */
      ui_handle_key(SDLK_KP1);                     /* bind KP_1 */
      ui_handle_key(SDLK_ESCAPE);                  /* back to SCR_CTRL (sel=1) */

      /* PORT 3 (sel -> 2): bind gamepad A to UP (btn 0), RT to DOWN (btn 1) */
      ui_handle_key(SDLK_DOWN);                    /* sel 1 -> 2 (PORT 3) */
      ui_handle_key(SDLK_RETURN);                  /* enter SCR_REDEF redef_port=2 */
      ui_handle_key(SDLK_RETURN);                  /* capture btn 0 (UP) */
      ui_handle_button(SDL_CONTROLLER_BUTTON_A);   /* bind gamepad A */
      ui_handle_key(SDLK_DOWN);                    /* sel 0 -> 1 (DOWN row) */
      ui_handle_key(SDLK_RETURN);                  /* capture btn 1 */
      ui_handle_button(GBTN_RTRIGGER);             /* bind right trigger */
      ui_handle_key(SDLK_ESCAPE);                  /* back to SCR_CTRL (sel=2) */
      ui_handle_key(SDLK_ESCAPE);                  /* close overlay */
      if (ui_is_open()) cok = 0;

      /* verify the bindings actually landed in the settings table */
      if (settings.keymap[1][0] != SDL_SCANCODE_KP_1) cok = 0;
      if (settings.gpadmap[2][0] != SDL_CONTROLLER_BUTTON_A) cok = 0;
      if (settings.gpadmap[2][1] != GBTN_RTRIGGER) cok = 0;
      /* verify RESET restores factory bindings (X/Z become bound again) */
      settings.gpadmap[1][BTN_X] = SDL_CONTROLLER_BUTTON_INVALID;
      settings_reset_port(1);
      if (settings.gpadmap[1][BTN_X] != SDL_CONTROLLER_BUTTON_LEFTSHOULDER) cok = 0;
      if (settings.gpadmap[1][BTN_Z] != SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) cok = 0;
      fprintf(stderr, "[selftest] CONTROLS: %s\n",
              cok ? "PASS: overlay + source cycle + key/pad capture + reset OK"
                  : "FAIL: overlay/source/capture/reset");
    }

    audio_shutdown();
    SDL_Quit();
    return nonzero > 0 ? 0 : 1;
  }

  mac_menu_init();         /* native macOS menu bar (interactive mode only) */

  int have_rom = 0;
  if (argc >= 2)
    have_rom = start_game(argv[1]);

  if (!have_rom) {
    /* idle window until user drops a ROM file */
    snprintf(window_title, sizeof(window_title), "%s - drop a .bin/.md/.gen/.smd ROM to start", APP_NAME);
    SDL_SetWindowTitle(vid.window, window_title);
    SDL_RenderClear(vid.renderer);
    SDL_RenderPresent(vid.renderer);
  }

  Uint32 last = SDL_GetTicks();
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      switch (ev.type) {
        case SDL_QUIT:    running = 0; break;
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
        case SDL_JOYDEVICEADDED:        /* raw joysticks (non-GC pads) hot-plug */
        case SDL_JOYDEVICEREMOVED:
          gamepads_enumerate();   /* re-scan pads on hot-plug */
          break;
        case SDL_KEYDOWN: {
          SDL_Keycode key = ev.key.keysym.sym;
          if (ui_is_open()) {
            ui_handle_key(key);     /* drive the on-screen control overlay */
          } else {
            handle_key(key);
          }
          break;
        }
        case SDL_DROPFILE: {
          char *dropped = ev.drop.file;
          if (dropped) {
            if (have_rom) sram_save();
            have_rom = start_game(dropped);
            SDL_free(dropped);
          }
          break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
          if (ui_is_open()) ui_handle_button(ev.cbutton.button);
          break;
        case SDL_CONTROLLERAXISMOTION:
          if (ui_is_open()) {
            SDL_GameControllerButton db = SDL_CONTROLLER_BUTTON_INVALID;
            /* triggers are axes, not buttons: pulling LT/RT past the threshold
               while capturing binds the corresponding pseudo button.
               ui_handle_button() is a no-op outside capture, and capture ends
               after the first bind, so repeated motion events are harmless. */
            if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT && ev.caxis.value > TRIG_ON)
              db = GBTN_LTRIGGER;
            else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT && ev.caxis.value > TRIG_ON)
              db = GBTN_RTRIGGER;
            /* Many cheap pads (BETOP C3, etc.) report the d-pad on the LEFT
               STICK axes (0/1), not as DPAD buttons or a hat. Deflection past
               the d-pad threshold while capturing binds the matching direction
               so the d-pad is remappable too. */
            else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
              if (ev.caxis.value < -AXIS_ON)      db = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
              else if (ev.caxis.value >  AXIS_ON) db = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
            } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
              if (ev.caxis.value < -AXIS_ON)      db = SDL_CONTROLLER_BUTTON_DPAD_UP;
              else if (ev.caxis.value >  AXIS_ON) db = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
            }
            if (db != SDL_CONTROLLER_BUTTON_INVALID) ui_handle_button(db);
          }
          break;
        case SDL_JOYBUTTONDOWN:
          /* raw-joystick pads must also be able to bind buttons in the UI.
             Skip devices opened as GameControllers (they already delivered
             this press via SDL_CONTROLLERBUTTONDOWN). */
          if (ui_is_open() && raw_pad_from_instance(ev.jbutton.which)) {
            SDL_GameControllerButton gb = joy_index_to_button(ev.jbutton.button);
            if (gb != SDL_CONTROLLER_BUTTON_INVALID) ui_handle_button(gb);
          }
          break;
        case SDL_JOYHATMOTION:
          /* d-pad capture in the UI. Covers both raw-joystick pads and Game
             Controllers whose d-pad is reported on a hat. */
          if (ui_is_open()) {
            pad_t *pd = raw_pad_from_instance(ev.jhat.which);
            if (!pd) pd = gc_pad_from_instance(ev.jhat.which);
            if (pd) {
              Uint8 v = ev.jhat.value;
              if (v & SDL_HAT_UP)         ui_handle_button(SDL_CONTROLLER_BUTTON_DPAD_UP);
              else if (v & SDL_HAT_DOWN)  ui_handle_button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
              else if (v & SDL_HAT_LEFT)  ui_handle_button(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
              else if (v & SDL_HAT_RIGHT) ui_handle_button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
            }
          }
          break;
        case SDL_JOYAXISMOTION:
          /* raw-joystick d-pad capture: cheap pads (e.g. GreenAsia USB
             Joystick) report the d-pad on a pair of analog axes, not as
             buttons/hat. Bind the matching direction when an on-centre axis
             deflects past threshold. Off-centre-idle axes are skipped so a
             resting-extreme axis can't jam a bind. */
          if (ui_is_open()) {
            pad_t *pd = raw_pad_from_instance(ev.jaxis.which);
            if (pd && ev.jaxis.axis >= 0 && ev.jaxis.axis < 8 &&
                abs(pd->raw_idle[ev.jaxis.axis]) <= 8000 &&
                abs(ev.jaxis.value) > AXIS_ON) {
              int horiz = (ev.jaxis.axis % 2 == 0);
              if (horiz) {
                if (ev.jaxis.value < 0) ui_handle_button(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
                else                     ui_handle_button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
              } else {
                if (ev.jaxis.value < 0) ui_handle_button(SDL_CONTROLLER_BUTTON_DPAD_UP);
                else                     ui_handle_button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
              }
            }
          }
          break;
        default: break;
      }
    }

    if (ui_is_open()) {
      /* controller-setup overlay is shown: pause emulation and draw it */
      ui_render(vid.renderer);
    } else if (have_rom) {
      /* input is polled by the core via osd_input_update() each frame */
      video_render();
      if (use_sound) audio_push();

      if (!turbo_mode) {
        /* frame pacing: ~60 Hz (NTSC) / 50 Hz (PAL) */
        double target = vdp_pal ? (1000.0 / 50.0) : (1000.0 / 60.0);
        Uint32 now = SDL_GetTicks();
        double elapsed = now - last;
        if (elapsed < target)
          SDL_Delay((Uint32)(target - elapsed));
        last = SDL_GetTicks();
      }
    } else {
      SDL_Delay(16);
    }
  }

  if (have_rom) sram_save();

  audio_shutdown();
  error_shutdown();
  free(bitmap.data);
  video_close();
  audio_close();
  SDL_Quit();
  return 0;
}

/* ---- optional Genesis boot ROM (rom.bin next to the binary) ------------- */
static int boot_rom_load(void)
{
  memset(boot_rom, 0xFF, 0x800);
  FILE *fp = fopen("rom.bin", "rb");
  if (!fp) return 0;
  size_t n = fread(boot_rom, 1, 0x800, fp);
  fclose(fp);
  if (n && !memcmp((char *)(boot_rom + 0x120), "GENESIS OS", 10)) {
    system_bios = SYSTEM_MD;
    for (int i = 0; i < 0x800; i += 2) {
      uint8 t = boot_rom[i];
      boot_rom[i] = boot_rom[i + 1];
      boot_rom[i + 1] = t;
    }
    for (int i = 0x800; i < 0x10000; i++)
      boot_rom[i] = boot_rom[i & 0x7ff];
  }
  return 1;
}
