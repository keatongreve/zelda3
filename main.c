
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <SDL.h>

#include "snes/snes.h"
#include "tracing.h"

#include "types.h"
#include "variables.h"

#include "zelda_rtl.h"
#include "zelda_cpu_infra.h"

extern struct Ppu *GetPpuForRendering();
extern struct Dsp *GetDspForRendering();
extern uint8 g_emulated_ram[0x20000];
bool g_run_without_emu = false;

int input1_current_state;

void setButtonState(int button, bool pressed) {
  // set key in constroller
  if (pressed) {
    input1_current_state |= 1 << button;
  } else {
    input1_current_state &= ~(1 << button);
  }
}

int gamepad_map[] = {SDLK_z, SDLK_a, SDLK_RSHIFT, SDLK_RETURN, // B Y Sel Sta
                     SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT,
                     SDLK_x, SDLK_s, SDLK_d, SDLK_c}; // A X L R
int joypad_map[] = {
  SDL_CONTROLLER_BUTTON_A, // B
  SDL_CONTROLLER_BUTTON_X, // Y
  SDL_CONTROLLER_BUTTON_BACK,
  SDL_CONTROLLER_BUTTON_START,
  SDL_CONTROLLER_BUTTON_DPAD_UP,
  SDL_CONTROLLER_BUTTON_DPAD_DOWN,
  SDL_CONTROLLER_BUTTON_DPAD_LEFT,
  SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
  SDL_CONTROLLER_BUTTON_B, // A
  SDL_CONTROLLER_BUTTON_Y, // X
  SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
  SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
};

static uint8_t* readFile(char* name, size_t* length) {
  FILE* f = fopen(name, "rb");
  if(f == NULL) {
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  int size = ftell(f);
  rewind(f);
  uint8_t* buffer = (uint8_t *)malloc(size);
  fread(buffer, size, 1, f);
  fclose(f);
  *length = size;
  return buffer;
}

void parseKeyconf(char* buf) {
  char* names[] = {
    "\nB:", "\nY:", "\nSelect:", "\nStart:",
    "\nUp:", "\nDown:", "\nLeft:", "\nRight:",
    "\nA:", "\nX:", "\nL:", "\nR:", NULL
  };
  for (int i = 0; names[i]; i++) {
    char* found = strstr(buf, names[i]);
    if (!found) {
      //printf("'%s' not found in key mapping\n");
      continue; // keep default value
    }
    gamepad_map[i] = strtol(found + strlen(names[i]), NULL, 0);
    //printf("Map %s%i\n",names[i]+1, gamepad_map[i]);
  }
}

static bool loadRom(char* name, Snes* snes) {
  // zip library from https://github.com/kuba--/zip
  size_t length = 0;
  uint8_t* file = NULL;
  file = readFile(name, &length);
  if(file == NULL) {
    puts("Failed to read file");
    return false;
  }

  PatchRom(file);

  bool result = snes_loadRom(snes, file, length);
  free(file);
  return result;
}

static void handleInputPad(SDL_GameControllerButton button, bool pressed) {
  for (int i = 0; i <= 11; i++) {
    if (button == joypad_map[i]) {
      setButtonState(i, pressed);
      return;
    }
  }
}

static void handleInput(int keyCode, int keyMod, bool pressed) {
  // lookup if each 0..11 SNES button has it corresponding SDL_<key> pressed
  for (int i = 0; i <= 11; i++) {
    if (keyCode == gamepad_map[i]) {
      setButtonState(i, pressed);
      return;
    }
  }
  // handle F1..10
  if (SDLK_F1 <= keyCode && keyCode <= SDLK_F10 && pressed) {
    SaveLoadSlot((keyMod & KMOD_CTRL) ? kSaveLoad_Replay : 
                 (keyMod & KMOD_SHIFT) ? kSaveLoad_Save :
                 kSaveLoad_Load, keyCode - SDLK_F1);
  }
}

static void playAudio(Snes *snes, SDL_AudioDeviceID device, int16_t* audioBuffer) {
  // generate enough samples
  if (!kIsOrigEmu && snes) {
    while (snes->apu->dsp->sampleOffset < 534)
      apu_cycle(snes->apu);
    snes->apu->dsp->sampleOffset = 0;
  }

  dsp_getSamples(GetDspForRendering(), audioBuffer, 735);
  if(SDL_GetQueuedAudioSize(device) <= 735 * 4 * 6) {
    // don't queue audio if buffer is still filled
    SDL_QueueAudio(device, audioBuffer, 735 * 4);
  } else {
    printf("Skipping audio!\n");
  }
}

static void renderScreen(SDL_Renderer* renderer, SDL_Texture* texture) {
  void* pixels = NULL;
  int pitch = 0;
  if(SDL_LockTexture(texture, NULL, &pixels, &pitch) != 0) {
    printf("Failed to lock texture: %s\n", SDL_GetError());
    return;
  }

  ppu_putPixels(GetPpuForRendering(), (uint8_t*) pixels);
  SDL_UnlockTexture(texture);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
}

#undef main
int main(int argc, char** argv) {
  // set up SDL
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Failed to init SDL: %s\n", SDL_GetError());
    return 1;
  }
  uint32_t win_flags = SDL_WINDOWPOS_UNDEFINED;
  SDL_Window* window = SDL_CreateWindow("Zelda3", SDL_WINDOWPOS_UNDEFINED, win_flags, 512, 480, 0);
  if(window == NULL) {
    printf("Failed to create window: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if(renderer == NULL) {
    printf("Failed to create renderer: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBX8888, SDL_TEXTUREACCESS_STREAMING, 512, 480);
  if(texture == NULL) {
    printf("Failed to create texture: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GameController* handle = NULL;
  for (int i = 0; i < SDL_NumJoysticks(); ++i) {
    if (SDL_IsGameController(i))
      handle = SDL_GameControllerOpen(i);
  }

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
  int16_t* audioBuffer = (int16_t * )malloc(735 * sizeof(int16) * 2); // stereo
  SDL_PauseAudioDevice(device, 0);

  size_t keyconf_length;
  char* keyconf_file = (char*)readFile("keyconf.yaml", &keyconf_length);
  if (keyconf_file) {
    printf("using keyconf\n");
    keyconf_file = realloc(keyconf_file, keyconf_length + 1); // add ending NUL
    keyconf_file[keyconf_length] = '\0';
    parseKeyconf(keyconf_file);
  }

  Snes *snes = snes_init(g_emulated_ram), *snes_run = NULL;
  if (argc >= 2 && !g_run_without_emu) {
    // init snes, load rom
    bool loaded = loadRom(argv[1], snes);
    if (!loaded) {
      puts("No rom loaded");
      return 1;
    }
    snes_run = snes;
  } else {
    snes_reset(snes, true);
  }
  SetSnes(snes);
  ZeldaInitialize();
  bool hooks = true;
  // sdl loop
  bool running = true;
  SDL_Event event;
  uint32_t lastTick = SDL_GetTicks();
  uint32_t curTick = 0;
  uint32_t delta = 0;
  int numFrames = 0;
  bool cpuNext = false;
  bool spcNext = false;
  int counter = 0;
  bool paused = false;
  bool turbo = true;
  uint32_t frameCtr = 0;

  printf("%d\n", *(int *)snes->cart->ram);

  while(running) {
    while(SDL_PollEvent(&event)) {
      switch(event.type) {
        case SDL_KEYDOWN: {
          SDL_Keycode key = event.key.keysym.sym;
          bool ctrl = event.key.keysym.mod & KMOD_CTRL;
          if (key == SDLK_F11) {
            SDL_SetWindowFullscreen(window, win_flags ^= SDL_WINDOW_FULLSCREEN);
          } else if (key == SDLK_ESCAPE) {
            running = false;
          } else if (ctrl && key == SDLK_e && snes) {
            snes_reset(snes, event.key.keysym.sym == SDLK_e);
            CopyStateAfterSnapshotRestore(true);
          } else if (ctrl && key == SDLK_p) {
            paused = !paused;
          } else if (ctrl && key == SDLK_t) {
            turbo = !turbo;
          } else if (ctrl && key == SDLK_w) {
              PatchCommand('w');
          } else if (ctrl && key == SDLK_o) {
              PatchCommand('o');
          } else if (ctrl && key == SDLK_t) {
              PatchCommand('k');
          } else {
            handleInput(event.key.keysym.sym, event.key.keysym.mod, true);
          }
          break;
        }
        case SDL_KEYUP:
          handleInput(event.key.keysym.sym, event.key.keysym.mod, false);
          break;
        case SDL_CONTROLLERDEVICEADDED: {
          if (handle) {
            SDL_GameControllerClose(handle);
          }
          handle = SDL_GameControllerOpen(event.cdevice.which);
          break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
          handleInputPad(event.cbutton.button, event.cbutton.state == SDL_PRESSED);
          break;
        case SDL_CONTROLLERBUTTONUP:
          handleInputPad(event.cbutton.button, event.cbutton.state == SDL_PRESSED);
          break;
        case SDL_QUIT: {
          running = false;
          break;
        }
      }
    }

    if (paused) {
      SDL_Delay(16);
      continue;
    }

    bool is_turbo = RunOneFrame(snes_run, input1_current_state, (counter++ & 0x7f) != 0 && turbo);

    if (is_turbo)
      continue;

    ZeldaDrawPpuFrame();

    playAudio(snes_run, device, audioBuffer);
    renderScreen(renderer, texture);

    SDL_RenderPresent(renderer); // vsyncs to 60 FPS
    // if vsync isn't working, delay manually
    curTick = SDL_GetTicks();

    static const uint8 delays[3] = { 17, 17, 16 }; // 60 fps
#if 1
    lastTick += delays[frameCtr++ % 3];

    if (lastTick > curTick) {
      delta = lastTick - curTick;
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
