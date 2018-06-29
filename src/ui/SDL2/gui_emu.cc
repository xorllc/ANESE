#include "gui.h"

#include "common/debug.h"

void SDL_GUI::Emu::init() {
  // nes screen texture
  this->nes_texture = SDL_CreateTexture(
    this->gui.sdl.renderer,
    SDL_PIXELFORMAT_ARGB8888,
    SDL_TEXTUREACCESS_STREAMING,
    this->gui.RES_X, this->gui.RES_Y
  );

  // The rectangle that the nes screen texture is slapped onto
  const int screen_w = this->gui.RES_X * this->gui.SCREEN_SCALE;
  const int screen_h = this->gui.RES_Y * this->gui.SCREEN_SCALE;
  this->nes_screen.h = screen_h;
  this->nes_screen.w = screen_w;
  this->nes_screen.x = 0;
  this->nes_screen.y = 0;

  this->nes_sound_queue.init(SDL_GUI::SAMPLE_RATE);
}

SDL_GUI::Emu::~Emu() {
  SDL_DestroyTexture(this->nes_texture);
}

/**
 * @brief Handle SDL_Events corresponding to NES joypad/zapper input
 *
 * @param event SDL_Event
 */
void SDL_GUI::Emu::input(const SDL_Event& event) {
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
    const u8* screen = this->nes.getFramebuff();
    const uint offset = (256 * 4 * (event.motion.y / this->gui.SCREEN_SCALE))
                      + (event.motion.x / this->gui.SCREEN_SCALE) * 4;
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
        this->speedup = (event.type == SDL_KEYDOWN) ? 200 : 100;
        this->speed_counter = 0;
        this->nes.set_speed(this->speedup);
        break;
    }

    // Controller
    if (event.type == SDL_CONTROLLERBUTTONDOWN ||
        event.type == SDL_CONTROLLERBUTTONUP) {
      switch (event.cbutton.button) {
      case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
        this->speedup = (event.type == SDL_CONTROLLERBUTTONDOWN) ? 200 : 100;
        this->speed_counter = 0;
        this->nes.set_speed(this->speedup);
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
        this->nes.set_speed(this->speedup += 25);
        this->speed_counter = 0;
        break;
      case SDLK_MINUS:
        // Speed down
        if (this->speedup - 25 != 0) {
          this->nes.set_speed(this->speedup -= 25);
          this->speed_counter = 0;
        }
        break;
      case SDLK_c: {
        // Toggle CPU trace
        bool log = DEBUG_VARS::Get()->print_nestest ^= 1;
        fprintf(stderr, "NESTEST CPU logging: %s\n", log ? "ON" : "OFF");
      } break;
      default: break;
      }
    }
  }
}

/**
 * @brief Run NES for appropriate number of frames
 * @details Actual number of frames emulated depends on speed-multiplier
 */
void SDL_GUI::Emu::update() {
  // Calculate the number of frames to render
  // Speedup values that are not multiples of 100 cause every-other frame to
  // render 1 more/less frame than usual
  uint numframes = 0;
  this->speed_counter += this->speedup;
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
    // this->gui.sdl_running = true;
  }
}

/**
 * @brief Render NES Framebuffer
 */
void SDL_GUI::Emu::output() {
  // output audio!
  float* samples = nullptr;
  uint   count = 0;
  this->nes.getAudiobuff(samples, count);
  // SDL_QueueAudio(this->gui.sdl.nes_audiodev, samples, count * sizeof(float));
  if (count) this->nes_sound_queue.write(samples, count);

  // output video!
  const u8* framebuffer = this->nes.getFramebuff();
  SDL_UpdateTexture(this->nes_texture, nullptr, framebuffer, this->gui.RES_X * 4);
  SDL_RenderCopy(this->gui.sdl.renderer, this->nes_texture, nullptr, &this->nes_screen);
}
