#pragma once
#include "audio.h"
#include "mea8000.h"

// Bend is deliberately bipolar at the user's request. Other FX retain their
// original fully-left dry zone, including the unchanged Whisper mapping.
inline uint16_t effectAmount(Effect effect, uint16_t pot) {
  if (effect == Effect::Bend) return pot;
  return pot <= 80 ? 0 : uint32_t(pot - 80) * 4095 / (4095 - 80);
}
struct VoiceControl {
  VoiceEffect effect;
  uint16_t amount;
};
inline VoiceControl voiceControl(Effect effect, uint16_t amount) {
  if (effect == Effect::Whisper) return {VoiceEffect::Whisper, amount};
  if (effect == Effect::Bend) {
    constexpr unsigned lower = 2048 - 80, upper = 2048 + 80;
    if (amount < lower)
      return {VoiceEffect::Fall, uint16_t(uint32_t(lower - amount) * 4095 / lower)};
    if (amount > upper)
      return {VoiceEffect::Rise, uint16_t(uint32_t(amount - upper) * 4095 / (4095 - upper))};
    return {VoiceEffect::Natural, 0};
  }
  return {VoiceEffect::Natural, amount};
}
