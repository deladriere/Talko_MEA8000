#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include "audio.h"

constexpr unsigned SoundMax = 4000;
inline unsigned soundItem(int reading, unsigned count) {
  const unsigned position = std::clamp(reading, 0, int(SoundMax));
  return position * count / (SoundMax + 1);
}

// Both controls retain the original lower octave and a stable noon position.
inline uint16_t panelRatioQ8(int value, int upperOctaves) {
  constexpr int centre = 2048, deadband = 60;
  if (value < centre - deadband)
    return uint16_t(256.0f * std::exp2(float(value - centre + deadband) /
                                      (centre - deadband)) + 0.5f);
  if (value <= centre + deadband) return 256;
  return uint16_t(256.0f * std::exp2(float((value - centre - deadband) * upperOctaves) /
                                    (4095 - centre - deadband)) + 0.5f);
}

// A nonblocking mode reminder, with wrap-safe timing and no effect on selection.
class ModeCue {
public:
  void show(uint32_t now) { shownAt_ = now; visible_ = true; }
  bool active(uint32_t now) {
    if (visible_ && uint32_t(now - shownAt_) >= 550) visible_ = false;
    return visible_;
  }
private:
  uint32_t shownAt_ = 0;
  bool visible_ = false;
};
inline uint8_t modeGlyph(PlaybackMode mode, bool synth) {
  if (mode == PlaybackMode::Gate) return 0x3d; // G: hold/release gate
  if (mode == PlaybackMode::Repeat) return synth ? 0x5e : 0x50; // d drone / r repeat
  return synth ? 0x06 : 0x6d; // 1 one-shot / S whole speech
}
// Kept free of hardware calls so complete click sequences can be checked on host.
inline uint8_t boundedClick(uint8_t value, int direction, unsigned count) {
  const int next = int(value) + direction;
  return uint8_t(next < 0 ? 0 : unsigned(next) >= count ? count - 1 : next);
}
class EncoderDecoder {
public:
  void begin(uint8_t state) { previous_ = state; steps_ = 0; }
  int edge(uint8_t state) {
    constexpr int8_t transitions[] = {
      0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
    const uint8_t previous = previous_;
    previous_ = state;
    if ((previous ^ state) == 3) { steps_ = 0; return 0; }
    steps_ += transitions[(previous << 2) | state];
    if (steps_ != 4 && steps_ != -4) return 0;
    const int result = steps_ > 0 ? 1 : -1;
    steps_ = 0;
    return result;
  }
private:
  uint8_t previous_ = 0;
  int8_t steps_ = 0;
};
