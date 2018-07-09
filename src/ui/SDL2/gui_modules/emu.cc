#include "emu.h"

#include <cstdio>

#include "../fs/load.h"
#include "../fs/util.h"

EmuModule::EmuModule(const SDLCommon& sdl_common, Config& config)
: GUIModule(sdl_common, config)
, params { this->sdl.SAMPLE_RATE, 100, false, false }
, nes { this->params }
{
  /*-------------------------------  SDL init  -------------------------------*/

  fprintf(stderr, "[SDL2] Initializing ANESE core GUI\n");

  this->sdl.window = SDL_CreateWindow(
    "anese",
    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
    this->sdl.RES_X * this->config.window_scale,
    this->sdl.RES_Y * this->config.window_scale,
    SDL_WINDOW_RESIZABLE
  );

  this->sdl.renderer = SDL_CreateRenderer(
    this->sdl.window,
    -1,
    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
  );

  // Letterbox the screen in the window
  SDL_RenderSetLogicalSize(this->sdl.renderer,
    this->sdl.RES_X * this->sdl_common.SCREEN_SCALE,
    this->sdl.RES_Y * this->sdl_common.SCREEN_SCALE);

  // Allow opacity
  SDL_SetRenderDrawBlendMode(this->sdl.renderer, SDL_BLENDMODE_BLEND);

  // nes screen texture
  this->sdl.screen_texture = SDL_CreateTexture(
    this->sdl.renderer,
    SDL_PIXELFORMAT_ARGB8888,
    SDL_TEXTUREACCESS_STREAMING,
    this->sdl.RES_X, this->sdl.RES_Y
  );

  // The rectangle that the nes screen texture is slapped onto
  const int screen_w = this->sdl.RES_X * this->sdl_common.SCREEN_SCALE;
  const int screen_h = this->sdl.RES_Y * this->sdl_common.SCREEN_SCALE;
  this->sdl.screen.h = screen_h;
  this->sdl.screen.w = screen_w;
  this->sdl.screen.x = 0;
  this->sdl.screen.y = 0;

  // SDL_AudioSpec as, have;
  // as.freq = SDL_GUI::SAMPLE_RATE;
  // as.format = AUDIO_F32SYS;
  // as.channels = 1;
  // as.samples = 4096;
  // as.callback = nullptr; // use SDL_QueueAudio
  // this->sdl_common.nes_audiodev = SDL_OpenAudioDevice(NULL, 0, &as, &have, 0);
  // SDL_PauseAudioDevice(this->sdl_common.nes_audiodev, 0);

  this->sdl.sound_queue.init(this->sdl.SAMPLE_RATE);

  // ---------------------------- Movie Support ----------------------------- //

  if (this->config.cli.replay_fm2_path != "") {
    bool did_load = this->fm2_replay.init(this->config.cli.replay_fm2_path.c_str());
    if (!did_load)
      fprintf(stderr, "[Replay][fm2] Movie loading failed!\n");
    fprintf(stderr, "[Replay][fm2] Movie successfully loaded!\n");
  }

  if (this->config.cli.record_fm2_path != "") {
    bool did_load = this->fm2_record.init(this->config.cli.record_fm2_path.c_str());
    if (!did_load)
      fprintf(stderr, "[Record][fm2] Failed to setup Movie recording!\n");
    fprintf(stderr, "[Record][fm2] Movie recording is setup!\n");
  }

  // -------------------------- NES Initialization -------------------------- //

  // pass controllers to this->fm2_record
  this->fm2_record.set_joy(0, FM2_Controller::SI_GAMEPAD, &this->joy_1);
  this->fm2_record.set_joy(1, FM2_Controller::SI_GAMEPAD, &this->joy_2);

  // Check if there is fm2 to replay
  if (this->fm2_replay.is_enabled()) {
    // plug in fm2 controllers
    this->nes.attach_joy(0, this->fm2_replay.get_joy(0));
    this->nes.attach_joy(1, this->fm2_replay.get_joy(1));
  } else {
    // plug in physical nes controllers
    this->nes.attach_joy(0, &this->joy_1);
    this->nes.attach_joy(1, &this->zap_2);
  }

  // Finally, flip some parameters (if needed)
  if (this->config.cli.log_cpu)         { this->params.log_cpu         = true; }
  if (this->config.cli.ppu_timing_hack) { this->params.ppu_timing_hack = true; }
  this->nes.updated_params();
}

EmuModule::~EmuModule() {
  /*------------------------------  SDL Cleanup  -----------------------------*/

  SDL_DestroyTexture(this->sdl.screen_texture);

  SDL_DestroyRenderer(this->sdl.renderer);
  SDL_DestroyWindow(this->sdl.window);

  /*------------------------------  NES Cleanup  -----------------------------*/
  this->unload_rom(this->cart);
  delete this->cart;
}

/*----------  Utils  ----------*/

int EmuModule::load_rom(const char* rompath) {
  delete this->cart;
  for (uint i = 0; i < 4; i++) {
    delete this->savestate[i];
    this->savestate[i] = nullptr;
  }

  fprintf(stderr, "[Load] Loading '%s'\n", rompath);
  Cartridge* cart = new Cartridge (ANESE_fs::load::load_rom_file(rompath));

  switch (cart->status()) {
  case Cartridge::Status::CART_BAD_DATA:
    fprintf(stderr, "[Cart] ROM file could not be parsed!\n");
    delete cart;
    return 1;
  case Cartridge::Status::CART_BAD_MAPPER:
    fprintf(stderr, "[Cart] Mapper %u has not been implemented yet!\n",
      cart->get_rom_file()->meta.mapper);
    delete cart;
    return 1;
  case Cartridge::Status::CART_NO_ERROR:
    fprintf(stderr, "[Cart] ROM file loaded successfully!\n");
    strcpy(this->current_rom_file, rompath);
    this->cart = cart;
    break;
  }

  // Try to load battery-backed save

  if (!this->config.cli.no_sav) {
    u8* data = nullptr;
    uint len = 0;
    ANESE_fs::load::load_file((std::string(rompath) + ".sav").c_str(), data, len);

    if (!data) fprintf(stderr, "[Savegame][Load] No save data found.\n");
    else {
      fprintf(stderr, "[Savegame][Load] Found save data.\n");
      const Serializable::Chunk* sav = Serializable::Chunk::parse(data, len);
      this->cart->get_mapper()->setBatterySave(sav);
    }

    delete data;
  }

  // Try to load savestate
  // kinda jank lol
  if (!this->config.cli.no_sav) {
    u8* data = nullptr;
    uint len = 0;
    ANESE_fs::load::load_file((std::string(rompath) + ".state").c_str(), data, len);

    u8* og_data = data;

    if (!data) fprintf(stderr, "[Savegame][Load] No savestate data found.\n");
    else {
      fprintf(stderr, "[Savegame][Load] Found savestate data.\n");
      for (const Serializable::Chunk*& savestate : this->savestate) {
        uint sav_len = ((uint*)data)[0];
        data += sizeof(uint);
        if (!sav_len) savestate = nullptr;
        else {
          savestate = Serializable::Chunk::parse(data, sav_len);
          data += sav_len;
        }
      }
    }

    delete og_data;
  }

  // Slap a cartridge in!
  this->nes.loadCartridge(this->cart->get_mapper());

  // Power-cycle the NES
  this->nes.power_cycle();

  return 0;
}

int EmuModule::unload_rom(Cartridge* cart) {
  if (!cart) return 0;
  fprintf(stderr, "[UnLoad] Unloading cart...\n");

  // Save Battey-Backed RAM
  if (cart != nullptr && !this->config.cli.no_sav) {
    const Serializable::Chunk* sav = cart->get_mapper()->getBatterySave();
    if (sav) {
      const u8* data;
      uint len;
      Serializable::Chunk::collate(data, len, sav);

      char buf [256];
      sprintf(buf, "%s.sav", this->current_rom_file);

      FILE* sav_file = fopen(buf, "wb");
      if (!sav_file) {
        fprintf(stderr, "[Savegame][Save] Failed to open save file!\n");
        return 1;
      }

      fwrite(data, 1, len, sav_file);
      fclose(sav_file);
      fprintf(stderr, "[Savegame][Save] Game saved to '%s'!\n", buf);

      delete sav;
    }
  }

  // Save Savestates
  if (cart != nullptr && !this->config.cli.no_sav) {
    char buf [256];
    sprintf(buf, "%s.state", this->current_rom_file);
    FILE* state_file = fopen(buf, "wb");
    if (!state_file) {
      fprintf(stderr, "[Savegame][Save] Failed to open savestate file!\n");
      return 1;
    }

    // kinda jank lol
    for (const Serializable::Chunk* savestate : this->savestate) {
      const u8* data;
      uint len;
      Serializable::Chunk::collate(data, len, savestate);

      fwrite(&len, sizeof(uint), 1, state_file);
      if (data) fwrite(data, 1, len, state_file);
    }

    fclose(state_file);
    fprintf(stderr, "[Savegame][Save] Savestates saved to '%s'!\n", buf);
  }

  this->nes.removeCartridge();

  return 0;
}

void EmuModule::input(const SDL_Event& event) {
  // Update from Controllers
  if (event.type == SDL_CONTROLLERBUTTONDOWN ||
      event.type == SDL_CONTROLLERBUTTONUP) {
    bool new_state = (event.type == SDL_CONTROLLERBUTTONDOWN) ? true : false;
    using namespace JOY_Standard_Button;

    // Player 1
    switch (event.cbutton.button) {
    case SDL_CONTROLLER_BUTTON_A:          this->joy_1.set_button(A,      new_state); break;
    case SDL_CONTROLLER_BUTTON_X:          this->joy_1.set_button(B,      new_state); break;
    case SDL_CONTROLLER_BUTTON_START:      this->joy_1.set_button(Start,  new_state); break;
    case SDL_CONTROLLER_BUTTON_BACK:       this->joy_1.set_button(Select, new_state); break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:    this->joy_1.set_button(Up,     new_state); break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  this->joy_1.set_button(Down,   new_state); break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  this->joy_1.set_button(Left,   new_state); break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: this->joy_1.set_button(Right,  new_state); break;
    }

    // Player 2
    // switch (event.cbutton.button) {
    // case SDL_CONTROLLER_BUTTON_A:          this->joy_2.set_button(A,      new_state); break;
    // case SDL_CONTROLLER_BUTTON_X:          this->joy_2.set_button(B,      new_state); break;
    // case SDL_CONTROLLER_BUTTON_START:      this->joy_2.set_button(Start,  new_state); break;
    // case SDL_CONTROLLER_BUTTON_BACK:       this->joy_2.set_button(Select, new_state); break;
    // case SDL_CONTROLLER_BUTTON_DPAD_UP:    this->joy_2.set_button(Up,     new_state); break;
    // case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  this->joy_2.set_button(Down,   new_state); break;
    // case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  this->joy_2.set_button(Left,   new_state); break;
    // case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: this->joy_2.set_button(Right,  new_state); break;
    // }
  }

  // Update from Keyboard
  if (event.type == SDL_KEYDOWN ||
      event.type == SDL_KEYUP) {
    // ------ Keyboard controls ------ //
    bool new_state = (event.type == SDL_KEYDOWN) ? true : false;
    using namespace JOY_Standard_Button;

    // Player 1
    switch (event.key.keysym.sym) {
    case SDLK_z:      this->joy_1.set_button(A,      new_state); break;
    case SDLK_x:      this->joy_1.set_button(B,      new_state); break;
    case SDLK_RETURN: this->joy_1.set_button(Start,  new_state); break;
    case SDLK_RSHIFT: this->joy_1.set_button(Select, new_state); break;
    case SDLK_UP:     this->joy_1.set_button(Up,     new_state); break;
    case SDLK_DOWN:   this->joy_1.set_button(Down,   new_state); break;
    case SDLK_LEFT:   this->joy_1.set_button(Left,   new_state); break;
    case SDLK_RIGHT:  this->joy_1.set_button(Right,  new_state); break;
    }

    // Player 2
    // switch (event.key.keysym.sym) {
    // case SDLK_z:      this->joy_2.set_button(A,      new_state); break;
    // case SDLK_x:      this->joy_2.set_button(B,      new_state); break;
    // case SDLK_RETURN: this->joy_2.set_button(Start,  new_state); break;
    // case SDLK_RSHIFT: this->joy_2.set_button(Select, new_state); break;
    // case SDLK_UP:     this->joy_2.set_button(Up,     new_state); break;
    // case SDLK_DOWN:   this->joy_2.set_button(Down,   new_state); break;
    // case SDLK_LEFT:   this->joy_2.set_button(Left,   new_state); break;
    // case SDLK_RIGHT:  this->joy_2.set_button(Right,  new_state); break;
    // }
  }

  // Update from Mouse
  if (event.type == SDL_MOUSEMOTION) {
    // getting the light from the screen is a bit trickier...
    const u8* screen;
    this->nes.getFramebuff(screen);
    const uint offset = (256 * 4 * (event.motion.y / this->sdl_common.SCREEN_SCALE))
                      + (event.motion.x / this->sdl_common.SCREEN_SCALE) * 4;
    const bool new_light = screen[offset+ 0]  // R
                         | screen[offset+ 1]  // G
                         | screen[offset+ 2]; // B
    this->zap_2.set_light(new_light);
  }

  if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
    bool new_state = (event.type == SDL_MOUSEBUTTONDOWN) ? true : false;
    this->zap_2.set_trigger(new_state);
  }

  // Handle Key-events
  if (event.type == SDL_KEYDOWN ||
      event.type == SDL_KEYUP) {
    bool mod_shift = event.key.keysym.mod & KMOD_SHIFT;
    // Use CMD on macOS, and CTRL on windows / linux
    bool mod_ctrl = strcmp(SDL_GetPlatform(), "Mac OS X") == 0
      ? event.key.keysym.mod & (KMOD_LGUI | KMOD_RGUI)
      : mod_ctrl = event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL);

    // Regular 'ol keys
    switch (event.key.keysym.sym) {
      case SDLK_SPACE:
        // Fast-Forward
        this->speed_counter = 0;
        this->params.speed = (event.type == SDL_KEYDOWN) ? 200 : 100;
        this->nes.updated_params();
        break;
    }

    // Controller
    if (event.type == SDL_CONTROLLERBUTTONDOWN ||
        event.type == SDL_CONTROLLERBUTTONUP) {
      switch (event.cbutton.button) {
      case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
        this->speed_counter = 0;
        this->params.speed = (event.type == SDL_CONTROLLERBUTTONDOWN) ? 200 : 100;
        this->nes.updated_params();
        break;
      }
    }

    // Meta Modified keys
    if (event.type == SDL_KEYDOWN && mod_ctrl) {
      #define SAVESTATE(i) do {                               \
        if (mod_shift) {                                      \
          delete this->savestate[i];                          \
          this->savestate[i] = this->nes.serialize();     \
        } else this->nes.deserialize(this->savestate[i]); \
      } while(0);

      switch (event.key.keysym.sym) {
      case SDLK_1: SAVESTATE(0); break; // Savestate Slot 1
      case SDLK_2: SAVESTATE(1); break; // Savestate Slot 2
      case SDLK_3: SAVESTATE(2); break; // Savestate Slot 3
      case SDLK_4: SAVESTATE(3); break; // Savestate Slot 4
      case SDLK_r: this->nes.reset();       break; // Reset
      case SDLK_p: this->nes.power_cycle(); break; // Power-Cycle
      case SDLK_EQUALS:
        // Speed up
        this->speed_counter = 0;
        this->params.speed += 25;
        this->nes.updated_params();
        break;
      case SDLK_MINUS:
        // Speed down
        if (this->params.speed - 25 != 0) {
          this->speed_counter = 0;
          this->params.speed -= 25;
          this->nes.updated_params();
        }
        break;
      case SDLK_c: {
        // Toggle CPU trace
        bool log = this->params.log_cpu = !this->params.log_cpu;
        this->nes.updated_params();
        fprintf(stderr, "NESTEST CPU logging: %s\n", log ? "ON" : "OFF");
      } break;
      default: break;
      }
    }
  }
}

void EmuModule::update() {
  // Calculate the number of frames to render
  // Speedup values that are not multiples of 100 cause every-other frame to
  // render 1 more/less frame than usual
  uint numframes = 0;
  this->speed_counter += this->params.speed;
  while (this->speed_counter > 0) {
    this->speed_counter -= 100;
    numframes++;
  }

  // Run ANESE core for some number of frames
  for (uint i = 0; i < numframes; i++) {
    // log frame to fm2
    if (this->fm2_record.is_enabled())
      this->fm2_record.step_frame();

    // set input from fm2
    if (this->fm2_replay.is_enabled())
      this->fm2_replay.step_frame();

    // run the NES for a frame
    this->nes.step_frame();
  }

  if (this->nes.isRunning() == false) {
    // this->sdl.sdl_running = true;
  }
}

void EmuModule::output() {
  // output audio!
  float* samples = nullptr;
  uint   count = 0;
  this->nes.getAudiobuff(samples, count);
  // SDL_QueueAudio(this->gui.sdl.nes_audiodev, samples, count * sizeof(float));
  if (count) this->sdl.sound_queue.write(samples, count);

  // output video!
  const u8* framebuffer;
  this->nes.getFramebuff(framebuffer);
  SDL_UpdateTexture(this->sdl.screen_texture, nullptr, framebuffer, this->sdl.RES_X * 4);
  SDL_RenderCopy(this->sdl.renderer, this->sdl.screen_texture, nullptr, &this->sdl.screen);
}
