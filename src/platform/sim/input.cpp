#include "platform/sim/sim.h"

namespace pos::sim {

void Input::add(hal::TouchEvent::Type t, int x, int y) {
  hal::TouchEvent ev;
  ev.type = t;
  ev.x = static_cast<int16_t>(x);
  ev.y = static_cast<int16_t>(y);
  ev.ms = clock_.millis();
  q_.push_back(ev);
}

void Input::pump() {
  if (!SDL_WasInit(SDL_INIT_VIDEO)) return;  // headless: no window, no events
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) quit = true;
    if (e.type == SDL_KEYDOWN) {
      switch (e.key.keysym.sym) {
        case SDLK_q:
        case SDLK_ESCAPE: quit = true; break;
        case SDLK_s: shotRequested = true; break;
        case SDLK_g: ghostRequested = true; break;
        case SDLK_l: logRequested = true; break;
        default: break;
      }
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) add(hal::TouchEvent::Down, e.button.x, e.button.y);
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) add(hal::TouchEvent::Up, e.button.x, e.button.y);
    if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK)) add(hal::TouchEvent::Move, e.motion.x, e.motion.y);
  }
}

bool Input::poll(hal::TouchEvent& out) {
  if (q_.empty()) return false;
  out = q_.front();
  q_.pop_front();
  return true;
}

}  // namespace pos::sim
