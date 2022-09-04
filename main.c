#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif
#include <math.h>
#include "snes/snes.h"
#include "tracing.h"

#include "types.h"
#include "variables.h"
#include "win_volume_control/win_volume_control.h"

#include "zelda_rtl.h"
#include "config.h"

extern Ppu *GetPpuForRendering();
extern Dsp *GetDspForRendering();
extern Snes *g_snes;
extern uint8 g_emulated_ram[0x20000];
bool g_run_without_emu = false;

static int g_input1_state;

void PatchRom(uint8 *rom);
void SetSnes(Snes *snes);
void RunAudioPlayer();
void CopyStateAfterSnapshotRestore(bool is_reset);
void SaveLoadSlot(int cmd, int which);
void PatchCommand(char cmd);
bool RunOneFrame(Snes *snes, int input_state, bool turbo);

static bool LoadRom(const char *name, Snes *snes);
static void PlayAudio(Snes *snes, SDL_AudioDeviceID device, SDL_AudioFormat format, int16 *audioBuffer);
static void RenderScreen(SDL_Window *window, SDL_Renderer *renderer, SDL_Texture *texture, bool fullscreen);
static void HandleInput(int keyCode, int modCode, bool pressed);
static void HandleGamepadInput(int button, bool pressed);
static void HandleGamepadAxisInput(int gamepad_id, int axis, int value);
static void OpenOneGamepad(int i);

static inline int IntMin(int a, int b) { return a < b ? a : b; }
static inline int IntMax(int a, int b) { return a > b ? a : b; }
static inline int Clamp(int value, int a, int b) { return (value < a) ? a : (value > b ? b : value);  }

enum {
  kRenderWidth = 512,
  kRenderHeight = 480,
  kDefaultZoom = 2,
  kMaxUserVolumeLevel = 100,
  kVolumeIncrementInterval = 5,
};


static uint32 g_win_flags = SDL_WINDOW_RESIZABLE;
static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static uint8 g_paused, g_turbo = true;
static int current_zoom = kDefaultZoom;
static int g_current_user_volume_level = kMaxUserVolumeLevel;
static uint8 g_gamepad_buttons;

void NORETURN Die(const char *error) {
  fprintf(stderr, "Error: %s\n", error);
  exit(1);
}

void SetButtonState(int button, bool pressed) {
  // set key in constroller
  if (pressed) {
    g_input1_state |= 1 << button;
  } else {
    g_input1_state &= ~(1 << button);
  }
}


void DoZoom(int zoom_step) {
  int screen = SDL_GetWindowDisplayIndex(g_window);
  if (screen < 0) screen = 0;
  int max_zoom = kDefaultZoom * 5;
  SDL_Rect bounds;
  int bt = -1, bl, bb, br;
  // note this takes into effect Windows display scaling, i.e., resolution is divided by scale factor
  if (SDL_GetDisplayUsableBounds(screen, &bounds) == 0) {
    // this call may take a while before it is reported by Windows (or not at all in my testing)
    if (SDL_GetWindowBordersSize(g_window, &bt, &bl, &bb, &br) != 0) {
      // guess based on Windows 10/11 defaults
      bl = br = bb = 1;
      bt = 31;
    }
    // Allow a zoom level slightly above the max that fits on screen
    int mw = (bounds.w - bl - br + (kRenderWidth / kDefaultZoom) / 4) / (kRenderWidth / kDefaultZoom);
    int mh = (bounds.h - bt - bb + (kRenderHeight / kDefaultZoom) / 4) / (kRenderHeight / kDefaultZoom);
    max_zoom = IntMin(mw, mh);
  }
  int new_zoom = IntMax(IntMin(current_zoom + zoom_step, max_zoom), 1);
  current_zoom = new_zoom;
  int w = new_zoom * (kRenderWidth / kDefaultZoom);
  int h = new_zoom * (kRenderHeight / kDefaultZoom);
  
  //SDL_RenderSetLogicalSize(g_renderer, w, h);
  SDL_SetWindowSize(g_window, w, h);
  if (bt >= 0) {
    // Center the window on top of the mouse
    int mx, my;
    SDL_GetGlobalMouseState(&mx, &my);
    int wx = IntMax(IntMin(mx - w / 2, bounds.x + bounds.w - bl - br - w), bounds.x + bl);
    int wy = IntMax(IntMin(my - h / 2, bounds.y + bounds.h - bt - bb - h), bounds.y + bt);
    SDL_SetWindowPosition(g_window, wx, wy);
  } else {
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }
}

void AdjustMasterVolume(int volume_adjustment) {
  g_current_user_volume_level = Clamp(g_current_user_volume_level + volume_adjustment, 0, kMaxUserVolumeLevel);
  SetApplicationVolume(g_current_user_volume_level);
}

static SDL_HitTestResult HitTestCallback(SDL_Window *win, const SDL_Point *area, void *data) {
  uint32 flags = SDL_GetWindowFlags(win);
  return (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0 && 
         (SDL_GetModState() & KMOD_CTRL) != 0 ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
}

#undef main
int main(int argc, char** argv) {
  ParseConfigFile();
  AfterConfigParse();

  // set up SDL
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Failed to init SDL: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* window = SDL_CreateWindow("Zelda3", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, kRenderWidth, kRenderHeight, g_win_flags);
  if(window == NULL) {
    printf("Failed to create window: %s\n", SDL_GetError());
    return 1;
  }
  g_window = window;
  SDL_SetWindowHitTest(window, HitTestCallback, NULL);
  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if(renderer == NULL) {
    printf("Failed to create renderer: %s\n", SDL_GetError());
    return 1;
  }
  g_renderer = renderer;
  SDL_RenderSetLogicalSize(renderer, 512, 480); 
  SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBX8888, SDL_TEXTUREACCESS_STREAMING, kRenderWidth, kRenderHeight);
  if(texture == NULL) {
    printf("Failed to create texture: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

  SDL_AudioSpec want, have;
  SDL_AudioDeviceID device;
  SDL_memset(&want, 0, sizeof(want));
  want.freq = 44100;
  want.format = AUDIO_S16;
  want.channels = 2;
  want.samples = 2048;
  want.callback = NULL; // use queue
  device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if(device == 0) {
    printf("Failed to open audio device: %s\n", SDL_GetError());
    return 1;
  }
  int16 *audioBuffer = (int16 *)malloc(735 * 4); // *2 for stereo, *2 for sizeof(int16)
  SDL_PauseAudioDevice(device, 0);

  Snes *snes = snes_init(g_emulated_ram), *snes_run = NULL;
  if (argc >= 2 && !g_run_without_emu) {
    // init snes, load rom
    bool loaded = LoadRom(argv[1], snes);
    if (!loaded) {
      puts("No rom loaded");
      return 1;
    }
    snes_run = snes;
  } else {
    snes_reset(snes, true);
  }

#if defined(_WIN32)
  _mkdir("saves");
#else
  mkdir("saves", 755);
#endif

  SetSnes(snes);
  ZeldaInitialize();
  ZeldaReadSram(snes);

  for (int i = 0; i < SDL_NumJoysticks(); i++)
    OpenOneGamepad(i);

  bool running = true;
  SDL_Event event;
  uint32 lastTick = SDL_GetTicks();
  uint32 curTick = 0;
  uint32 frameCtr = 0;

  while(running) {
    while(SDL_PollEvent(&event)) {
      switch(event.type) {
      case SDL_CONTROLLERDEVICEADDED:
        OpenOneGamepad(event.cdevice.which);
        break;
      case SDL_CONTROLLERAXISMOTION:
        HandleGamepadAxisInput(event.caxis.which, event.caxis.axis, event.caxis.value);
        break;
      case SDL_CONTROLLERBUTTONDOWN:
        HandleGamepadInput(event.cbutton.button, event.cbutton.state == SDL_PRESSED);
        break;
      case SDL_CONTROLLERBUTTONUP:
        HandleGamepadInput(event.cbutton.button, event.cbutton.state == SDL_PRESSED);
        break;
      case SDL_MOUSEWHEEL:
        if ((g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0 && event.wheel.y != 0 && SDL_GetModState() & KMOD_CTRL)
          DoZoom(event.wheel.y > 0 ? 1 : -1);
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT && event.button.state == SDL_PRESSED && event.button.clicks == 2) {
          if ((g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0 && SDL_GetModState() & KMOD_SHIFT) {
            g_win_flags ^= SDL_WINDOW_BORDERLESS;
            SDL_SetWindowBordered(g_window, (g_win_flags & SDL_WINDOW_BORDERLESS) == 0);
          }
        }
        break;
      case SDL_KEYDOWN:
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, true);
        break;
      case SDL_KEYUP:
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, false);
        break;
      case SDL_QUIT:
        running = false;
        break;
      }
    }

    if (g_paused) {
      SDL_Delay(16);
      continue;
    }

    // Clear gamepad inputs when joypad directional inputs to avoid wonkiness
    int inputs = g_input1_state;
    if (g_input1_state & 0xf0)
      g_gamepad_buttons = 0;

    bool is_turbo = RunOneFrame(snes_run, g_input1_state | g_gamepad_buttons, (frameCtr++ & 0x7f) != 0 && g_turbo);

    if (is_turbo)
      continue;

    ZeldaDrawPpuFrame();

    PlayAudio(snes_run, device, have.format, audioBuffer);
    RenderScreen(window, renderer, texture, (g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0);

    SDL_RenderPresent(renderer); // vsyncs to 60 FPS
    // if vsync isn't working, delay manually
    curTick = SDL_GetTicks();

    static const uint8 delays[3] = { 17, 17, 16 }; // 60 fps
#if 1
    lastTick += delays[frameCtr % 3];

    if (lastTick > curTick) {
      uint32 delta = lastTick - curTick;
      if (delta > 500) {
        lastTick = curTick - 500;
        delta = 500;
      }
      SDL_Delay(delta);
    } else if (curTick - lastTick > 500) {
      lastTick = curTick;
    }
#endif
  }
  // clean snes
  snes_free(snes);
  // clean sdl
  SDL_PauseAudioDevice(device, 1);
  SDL_CloseAudioDevice(device);
  free(audioBuffer);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

static void PlayAudio(Snes *snes, SDL_AudioDeviceID device, SDL_AudioFormat format, int16 *audioBuffer) {
  // generate enough samples
  if (!kIsOrigEmu && snes) {
    while (snes->apu->dsp->sampleOffset < 534)
      apu_cycle(snes->apu);
    snes->apu->dsp->sampleOffset = 0;
  }

  SDL_memset(audioBuffer, 0, 735 * 4);
  dsp_getSamples(GetDspForRendering(), audioBuffer, 735);
  // don't queue audio if buffer is still filled
  if (SDL_GetQueuedAudioSize(device) <= 735 * 4 * 6) {
    int16 *volumeAdjustedAudioBuffer = (int16 *)malloc(735 * 4);
    SDL_memset(volumeAdjustedAudioBuffer, 0, 735 * 4);

    int16 volume_div_shift = 0x8;
#if defined(__WIN32__)
    int32 volume_shifted = 100 << volume_div_shift;
#else
    int32 volume_shifted = (g_current_user_volume_level << volume_div_shift) / kMaxUserVolumeLevel * SDL_MIX_MAXVOLUME;
#endif

    SDL_MixAudioFormat((Uint8 *)volumeAdjustedAudioBuffer, (Uint8 *)audioBuffer, format, 735 * 4, volume_shifted >> volume_div_shift);

    SDL_QueueAudio(device, volumeAdjustedAudioBuffer, 735 * 4);
    free(volumeAdjustedAudioBuffer);
  } else {
    printf("Skipping audio!\n");
  }
}

static void RenderScreen(SDL_Window *window, SDL_Renderer *renderer, SDL_Texture *texture, bool fullscreen) {
  void* pixels = NULL;
  int pitch = 0;
  if(SDL_LockTexture(texture, NULL, &pixels, &pitch) != 0) {
    printf("Failed to lock texture: %s\n", SDL_GetError());
    return;
  }
  ppu_putPixels(GetPpuForRendering(), (uint8_t*) pixels);
  SDL_UnlockTexture(texture);
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
}

static void HandleCommand(uint32 j, bool pressed) {
  if (j <= kKeys_Controls_Last) {
    static const uint8 kKbdRemap[12] = { 4, 5, 6, 7, 2, 3, 8, 0, 9, 1, 10, 11 };
    SetButtonState(kKbdRemap[j], pressed);
    return;
  }
  if (!pressed)
    return;
  if (j <= kKeys_Load_Last) {
    SaveLoadSlot(kSaveLoad_Load, j - kKeys_Load);
  } else if (j <= kKeys_Save_Last) {
    SaveLoadSlot(kSaveLoad_Save, j - kKeys_Save);
  } else if (j <= kKeys_Replay_Last) {
    SaveLoadSlot(kSaveLoad_Replay, j - kKeys_Replay);
  } else if (j <= kKeys_LoadRef_Last) {
    SaveLoadSlot(kSaveLoad_Load, 256 + j - kKeys_LoadRef);
  } else if (j <= kKeys_ReplayRef_Last) {
    SaveLoadSlot(kSaveLoad_Replay, 256 + j - kKeys_ReplayRef);
  } else {
    switch (j) {
    case kKeys_CheatLife: PatchCommand('w'); break;
    case kKeys_CheatKeys: PatchCommand('o'); break;
    case kKeys_ClearKeyLog: PatchCommand('k'); break;
    case kKeys_StopReplay: PatchCommand('l'); break;
    case kKeys_Fullscreen:
      g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
      SDL_SetWindowFullscreen(g_window, g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
      break;
    case kKeys_Reset:
      snes_reset(g_snes, true);
      ZeldaReadSram(g_snes);
      CopyStateAfterSnapshotRestore(true);
      break;
    case kKeys_Pause: g_paused = !g_paused; break;
    case kKeys_PauseDimmed: 
      g_paused = !g_paused;
      if (g_paused) {
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 159);
        SDL_RenderFillRect(g_renderer, NULL);
        SDL_RenderPresent(g_renderer);
      }
      break;
    case kKeys_Turbo: g_turbo = !g_turbo; break;
    case kKeys_ZoomIn: DoZoom(1); break;
    case kKeys_ZoomOut: DoZoom(-1); break;
    case kKeys_MasterVolumeDown:
      AdjustMasterVolume(-kVolumeIncrementInterval);
      break;
    case kKeys_MasterVolumeUp:
      AdjustMasterVolume(kVolumeIncrementInterval);
      break;
    default: assert(0);
    }
  }
}

static void HandleInput(int keyCode, int keyMod, bool pressed) {
  int j = FindCmdForSdlKey(keyCode, keyMod);
  if (j >= 0)
    HandleCommand(j, pressed);
}

static void OpenOneGamepad(int i) {
  if (SDL_IsGameController(i)) {
    SDL_GameController *controller = SDL_GameControllerOpen(i);
    if (!controller)
      fprintf(stderr, "Could not open gamepad %d: %s\n", i, SDL_GetError());
  }
}

static void HandleGamepadInput(int button, bool pressed) {
  switch (button) {
  case SDL_CONTROLLER_BUTTON_A: SetButtonState(0, pressed); break;
  case SDL_CONTROLLER_BUTTON_X: SetButtonState(1, pressed); break;
  case SDL_CONTROLLER_BUTTON_BACK: SetButtonState(2, pressed); break;
  case SDL_CONTROLLER_BUTTON_START: SetButtonState(3, pressed); break;
  case SDL_CONTROLLER_BUTTON_DPAD_UP: SetButtonState(4, pressed); break;
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN: SetButtonState(5, pressed); break;
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT: SetButtonState(6, pressed); break;
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: SetButtonState(7, pressed); break;
  case SDL_CONTROLLER_BUTTON_B: SetButtonState(8, pressed); break;
  case SDL_CONTROLLER_BUTTON_Y: SetButtonState(9, pressed); break;
  case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: SetButtonState(10, pressed); break;
  case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: SetButtonState(11, pressed); break;
  }
}

static void HandleGamepadAxisInput(int gamepad_id, int axis, int value) {
  static int last_gamepad_id, last_x, last_y;
  if (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_LEFTY) {
    // try to be smart if there's more than one gamepad
    if (last_gamepad_id != gamepad_id && (value < -10000 || value > 10000)) {
      last_gamepad_id = gamepad_id;
      last_x = last_y = 0;
    }
    *(axis == SDL_CONTROLLER_AXIS_LEFTX ? &last_x : &last_y) = value;
    int buttons = 0;
    if (last_x * last_x + last_y * last_y >= 10000 * 10000) {
      // in the non deadzone part, divide the circle into eight 45 degree
      // segments rotated by 22.5 degrees that control which direction to move.
      // todo: do this without floats?
      static const uint8 kSegmentToButtons[8] = {
        1 << 4,           // 0 = up
        1 << 4 | 1 << 7,  // 1 = up, right
        1 << 7,           // 2 = right
        1 << 7 | 1 << 5,  // 3 = right, down
        1 << 5,           // 4 = down
        1 << 5 | 1 << 6,  // 5 = down, left
        1 << 6,           // 6 = left
        1 << 6 | 1 << 4,  // 7 = left, up
      };
      int angle = (int)(atan2f(last_y, last_x) * (float)(128 / M_PI) + 0.5f);
      buttons = kSegmentToButtons[(uint8)(angle + 16 + 64) >> 5];
    }
    g_gamepad_buttons = buttons;
  }
}

static bool LoadRom(const char *name, Snes *snes) {
  size_t length = 0;
  uint8 *file = ReadFile(name, &length);
  if(!file) Die("Failed to read file");

  PatchRom(file);

  bool result = snes_loadRom(snes, file, length);
  free(file);
  return result;
}


