#pragma once
#include <cstdint>
#include "audio.h"

inline bool stopSpeechOnRelease(PlaybackMode mode, bool gate, bool error) {
  return mode != PlaybackMode::Speech && !gate && !error;
}
inline bool restartSpeech(PlaybackMode mode, bool gate, bool error) {
  return error || (mode == PlaybackMode::Repeat && gate);
}
