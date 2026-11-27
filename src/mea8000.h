// SPDX-License-Identifier: BSD-3-Clause
// Derived from MAME's MEA8000 emulation, Copyright (C) Antoine Mine' 2006.
// Standalone streaming/control adaptation: Polaxis, 2026.
#pragma once
#include <cstddef>
#include <cstdint>

enum class VoiceEffect : uint8_t { Natural, Robot, Rise, Fall, Whisper };

class Mea8000 {
public:
  static constexpr unsigned SynthesisRate = 8000;
  static constexpr unsigned SampleRate = 32000;
  static constexpr unsigned Upsample = SampleRate / SynthesisRate;

  static constexpr uint16_t MaxSpeedQ8 = 8 * 256;

  // frames contains only four-byte frames; file headers are handled by banks.cpp.
  bool start(const uint8_t *frames, size_t bytes, unsigned pitchHz);
  void startSynth(uint16_t vowel, uint16_t pitchHz, uint16_t motionMilliHz);
  void synthControls(uint16_t vowel, uint16_t pitchHz, uint16_t motionMilliHz);
  void releaseSynth();
  void stop();
  int16_t sample();
  bool active() const { return running_; }
  void controls(uint16_t speedQ8, uint16_t pitchQ8, uint16_t formantQ8 = 256,
                bool freeze = false);
  void voiceEffect(VoiceEffect effect, uint16_t amount);
  unsigned framesRead() const { return unsigned(offset_ / 4); }
  uint32_t framePosition() const { return position_; }
  unsigned pitchHz() const { return unsigned(pitch_); }
  uint32_t excitationPitchQ8() const { return excitationPitch_; }
  uint16_t vowelPosition() const { return uint16_t(synthPosition_ >> 16); }

private:
  struct Filter {
    int lastFrequency = 0, frequency = 0;
    int lastBandwidth = 0, bandwidth = 0;
    int32_t output = 0, previous = 0;
  };
  static void initTables();
  void decode();
  void shift();
  void advance();
  int interpolate(int from, int to) const;
  int32_t filter(unsigned index, int32_t input);
  int16_t compute();
  int noiseSample();
  void updateHold(int amplitude);
  uint32_t voicePitch(int naturalPitch) const;
  void updateSynthFormants();

  const uint8_t *frames_ = nullptr;
  size_t bytes_ = 0, offset_ = 0;
  Filter filters_[4]{};
  uint32_t position_ = 0, phase_ = 0, random_ = 0x31b8a409;
  uint32_t wordTicks_ = 0, elapsedQ8_ = 0, excitationPitch_ = 0;
  unsigned initialPitch_ = 0;
  VoiceEffect voiceEffect_ = VoiceEffect::Natural;
  uint16_t voiceAmount_ = 0;
  uint32_t synthPosition_ = 0, synthTarget_ = 0, synthCentre_ = 0;
  uint32_t synthMotionPhase_ = 0, synthMotionStep_ = 0;
  uint32_t flutterPhase_ = 0, flutterStep_ = 0;
  int flutterDepth_ = 0, flutterGain_ = 256, holdMix_ = 0;
  bool holding_ = false;
  uint32_t synthBend_ = 0, synthBendStep_ = 1, synthEnvelope_ = 0;
  uint16_t synthPitchHz_ = 220, synthQuantizedHz_ = 220;
  uint16_t synthGain_ = 256;
  bool synth_ = false, synthReleased_ = false;
  uint16_t speed_ = 256, pitchScale_ = 256, formantScale_ = 256;
  unsigned durationLog_ = 6, outputPhase_ = 0;
  int lastAmplitude_ = 0, amplitude_ = 0, lastPitch_ = 0, pitch_ = 0;
  int16_t previousSample_ = 0, nextSample_ = 0;
  bool noise_ = false, running_ = false, fading_ = false, freeze_ = false;
};
