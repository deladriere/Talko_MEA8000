// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) Antoine Mine' 2006. Derived from MAME mea8000.cpp.
// Standalone streaming/control adaptation: Polaxis, 2026.
#include "mea8000.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr int Quant = 512;
constexpr unsigned TableSize = 3600;
// Shared coefficient tables. Initialized on the rendering core before first use.
int16_t cosTable[TableSize], expTable[TableSize], exp2Table[TableSize];
bool tablesReady = false;
constexpr int Fm1[] = {
  150,162,174,188,202,217,233,250,267,286,305,325,346,368,391,415,
  440,466,494,523,554,587,622,659,698,740,784,830,880,932,988,1047};
constexpr int Fm2[] = {
  440,466,494,523,554,587,622,659,698,740,784,830,880,932,988,1047,
  1100,1179,1254,1337,1428,1528,1639,1761,1897,2047,2214,2400,
  2609,2842,3105,3400};
constexpr int Fm3[] = {1179,1337,1528,1761,2047,2400,2842,3400};
constexpr int Bandwidth[] = {726,309,125,50};
constexpr int Amplitude[] = {0,8,11,16,22,31,44,62,88,125,177,250,354,500,707,1000};
constexpr int PitchIncrement[] = {
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,0,-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1};
}

void Mea8000::initTables() {
  if (tablesReady) return;
  constexpr double Pi = 3.14159265358979323846;
  for (unsigned i = 0; i < TableSize; ++i) {
    const double f = double(i) / SynthesisRate;
    cosTable[i] = int16_t(2.0 * std::cos(2.0 * Pi * f) * Quant);
    expTable[i] = int16_t(std::exp(-Pi * f) * Quant);
    exp2Table[i] = int16_t(std::exp(-2.0 * Pi * f) * Quant);
  }
  tablesReady = true;
}

bool Mea8000::start(const uint8_t *frames, size_t bytes, unsigned pitchHz) {
  stop();
  synth_ = false;
  holding_ = false;
  flutterPhase_ = holdMix_ = 0;
  flutterGain_ = 256;
  if (!frames || bytes < 4 || bytes % 4) return false;
  initTables();
  frames_ = frames;
  bytes_ = bytes;
  offset_ = 0;
  position_ = phase_ = outputPhase_ = 0;
  previousSample_ = nextSample_ = 0;
  random_ = 0x31b8a409;
  wordTicks_ = elapsedQ8_ = excitationPitch_ = 0;
  for (size_t i = 3; i < bytes; i += 4)
    wordTicks_ += 64u << ((frames[i] >> 5) & 3);
  for (auto &f : filters_) f = {};
  lastAmplitude_ = amplitude_ = 0;
  lastPitch_ = pitch_ = std::min(pitchHz, 510u);
  initialPitch_ = unsigned(pitch_);
  fading_ = false;
  // Preserve MAME's first-frame initialization (including its amplitude ramp).
  const int initialPitch = pitch_;
  decode();
  shift();
  lastPitch_ = initialPitch;
  amplitude_ = 0;
  running_ = true;
  return true;
}

void Mea8000::stop() { running_ = false; }

void Mea8000::synthControls(uint16_t vowel, uint16_t pitchHz, uint16_t motionMilliHz) {
  synthTarget_ = uint32_t(std::min<unsigned>(vowel, 4095)) << 16;
  synthPitchHz_ = std::clamp<unsigned>(pitchHz, 55, 880);
  const unsigned rate = std::min<unsigned>(motionMilliHz, 12000);
  synthMotionStep_ = uint32_t((uint64_t(rate) << 32) / (1000u * SynthesisRate));
  // Rise/Fall retain an audible attack even when motion is stopped.
  const unsigned bendMs = rate ? std::clamp(1000000u/rate, 50u, 1000u) : 250u;
  synthBendStep_ = std::max(1u, 65536u / (bendMs * 8));
  constexpr uint16_t notes[] = {
      55,58,62,65,69,73,78,82,87,92,98,104,110,117,123,131,139,
      147,156,165,175,185,196,208,220,233,247,262,277,294,311,330,
      349,370,392,415,440,466,494,523,554,587,622,659,698,740,784,831,880};
  synthQuantizedHz_ = notes[0];
  for (auto note : notes)
    if (std::abs(int(note)-synthPitchHz_) < std::abs(int(synthQuantizedHz_)-synthPitchHz_))
      synthQuantizedHz_ = note;
}

void Mea8000::startSynth(uint16_t vowel, uint16_t pitchHz, uint16_t motionMilliHz) {
  static constexpr uint8_t seed[] = {0xaa,0xb3,0x84,0x20};
  start(seed,sizeof(seed),110); // Initialize the same MEA filter/oscillator state.
  synth_ = true;
  synthReleased_ = false;
  synthBend_ = synthEnvelope_ = 0;
  synthMotionPhase_ = 0;
  synthControls(vowel,pitchHz,motionMilliHz);
  synthCentre_ = synthPosition_ = synthTarget_;
  updateSynthFormants();
}

void Mea8000::releaseSynth() { if (synth_) synthReleased_ = true; }

void Mea8000::updateSynthFormants() {
  // Oo -> Oh -> Ah -> Eh -> Ee. These drive the existing four-filter model.
  constexpr int vowels[][3] = {
      {300,870,2240},{450,800,2600},{800,1150,2900},
      {500,1900,2480},{270,2300,3010}};
  const uint32_t scaled = synthPosition_ * 4;
  const unsigned segment = std::min(3u, unsigned(scaled / (4095u << 16)));
  const int fraction = int((scaled - segment * (4095u << 16)) >> 16);
  // Balance broad vowel level changes without a time-varying AGC.
  constexpr int gains[] = {180,270,720,1080,720};
  synthGain_ = uint16_t(gains[segment] +
      ((gains[segment+1]-gains[segment])*fraction)/4095);
  for (unsigned i=0;i<3;++i)
    filters_[i].frequency = vowels[segment][i] +
        ((vowels[segment+1][i]-vowels[segment][i])*fraction)/4095;
  filters_[3].frequency = 3500;
  constexpr int widths[] = {125,125,170,250};
  for (unsigned i=0;i<4;++i) filters_[i].bandwidth = widths[i];
}

void Mea8000::voiceEffect(VoiceEffect effect, uint16_t amount) {
  voiceEffect_ = effect;
  voiceAmount_ = std::min<unsigned>(amount, 4095);
}

void Mea8000::controls(uint16_t speedQ8, uint16_t pitchQ8, uint16_t formantQ8, bool freeze) {
  speed_ = std::clamp<unsigned>(speedQ8, 64, MaxSpeedQ8);
  const unsigned aboveNoon = speed_ > 256 ? speed_ - 256 : 0;
  flutterDepth_ = int(160 * aboveNoon / (MaxSpeedQ8 - 256));
  // Phase increments are in turns per 8 kHz tick. No division in the audio loop.
  constexpr uint32_t oneHz = uint32_t((1ull << 32) / SynthesisRate);
  flutterStep_ = oneHz * 6 + uint32_t(uint64_t(oneHz * 30) * aboveNoon / (MaxSpeedQ8 - 256));
  pitchScale_ = std::clamp<unsigned>(pitchQ8, 64, 1024);
  formantScale_ = std::clamp<unsigned>(formantQ8, 128, 512);
  freeze_ = freeze;
}

void Mea8000::shift() {
  lastPitch_ = pitch_;
  lastAmplitude_ = amplitude_;
  for (auto &f : filters_) {
    f.lastFrequency = f.frequency;
    f.lastBandwidth = f.bandwidth;
  }
}

void Mea8000::decode() {
  const uint8_t *b = frames_ + offset_;
  offset_ += 4;
  const unsigned fd = (b[3] >> 5) & 3;
  noise_ = (b[3] & 31) == 16;
  pitch_ = std::clamp(lastPitch_ + PitchIncrement[b[3] & 31] * (1 << fd), 0, 510);
  for (unsigned i = 0; i < 4; ++i)
    filters_[i].bandwidth = Bandwidth[(b[0] >> (6 - 2 * i)) & 3];
  filters_[0].frequency = Fm1[b[2] >> 3];
  filters_[1].frequency = Fm2[b[1] & 31];
  filters_[2].frequency = Fm3[b[1] >> 5];
  filters_[3].frequency = 3500;
  amplitude_ = Amplitude[((b[2] & 7) << 1) | (b[3] >> 7)];
  durationLog_ = fd + 6;
}

int Mea8000::interpolate(int from, int to) const {
  // Explicit floor division preserves MAME's signed arithmetic right shift.
  // All parameter differences times position fit in int32 at the allowed limits.
  const int32_t delta = (to - from) * int32_t(position_);
  const unsigned shift = durationLog_ + 8;
  const uint32_t unit = 1u << shift;
  return from + (delta >= 0 ? int(uint32_t(delta) >> shift)
                            : -int((uint32_t(-delta) + unit - 1) >> shift));
}

int32_t Mea8000::filter(unsigned index, int32_t input) {
  auto &f = filters_[index];
  const int frequency = std::clamp(
      ((synth_ ? f.frequency : interpolate(f.lastFrequency, f.frequency)) *
       formantScale_) / 256, 0, 3500);
  const int bandwidth = synth_ ? f.bandwidth : interpolate(f.lastBandwidth, f.bandwidth);
  const int b = (int(cosTable[frequency]) * expTable[bandwidth]) / Quant;
  const int c = exp2Table[bandwidth];
  // Widen feedback arithmetic: malformed/shifted resonances cannot overflow int32.
  const int64_t next = int64_t(input) +
      (int64_t(b) * f.output - int64_t(c) * f.previous) / Quant;
  f.previous = f.output;
  f.output = int32_t(std::clamp<int64_t>(next, -(1 << 26), 1 << 26));
  return f.output;
}

int Mea8000::noiseSample() {
  random_ ^= random_ << 13;
  random_ ^= random_ >> 17;
  random_ ^= random_ << 5;
  return int(random_ & 1023) - Quant;
}

uint32_t Mea8000::voicePitch(int naturalPitch) const {
  int32_t pitchQ8 = std::max(0, naturalPitch) * 256;
  // Zero depth uses exactly the original pitch and preserves the noise sequence.
  if (voiceAmount_ && voiceEffect_ != VoiceEffect::Natural &&
      voiceEffect_ != VoiceEffect::Whisper) {
    int32_t targetQ8 = int32_t(synth_ ? synthPitchHz_ : initialPitch_) * 256;
    if (synth_ && voiceEffect_ == VoiceEffect::Robot)
      targetQ8 = int32_t(synthQuantizedHz_) * 256;
    if (voiceEffect_ == VoiceEffect::Rise || voiceEffect_ == VoiceEffect::Fall) {
      // Progress follows speech speed and pauses with ERROR. Fade-out holds its
      // end pitch. Full Rise reaches one octave up; Fall reaches one octave down.
      const uint32_t progress = synth_ ? synthBend_ : wordTicks_ ?
          uint32_t(std::min<uint64_t>((uint64_t(elapsedQ8_) << 16) /
          (uint64_t(wordTicks_) * 256), 65536)) : 0;
      const int32_t movement = int32_t((int64_t(targetQ8) * progress) >> 16);
      targetQ8 += voiceEffect_ == VoiceEffect::Rise ? movement : -movement / 2;
    }
    pitchQ8 += int32_t((int64_t(targetQ8 - pitchQ8) * voiceAmount_) / 4095);
  }
  // Leave headroom below the synthesis Nyquist frequency at extreme bench pitch.
  return std::min<uint32_t>(uint32_t((uint64_t(pitchQ8) * pitchScale_) / 256),
                            3500u * 256);
}

void Mea8000::updateHold(int amplitude) {
  // Seek through a silent onset at synthesis-tick resolution, even at 8x speed.
  holding_ = freeze_ && (synth_ || (!fading_ && amplitude >= 32));
  const int mixTarget = holding_ ? 256 : 0;
  holdMix_ += std::clamp(mixTarget - holdMix_, -4, 4);
  if (holding_ && flutterDepth_) flutterPhase_ += flutterStep_;
  else flutterPhase_ = 0;
  // A quarter-cycle dip keeps the note audible even at the strongest setting.
  const int target = flutterPhase_ >= 0xc0000000u ? 256 - flutterDepth_ : 256;
  flutterGain_ += std::clamp(target - flutterGain_, -12, 12);
}

int16_t Mea8000::compute() {
  if (synth_) updateSynthFormants();
  int amplitude = synth_ ? 0 : interpolate(lastAmplitude_, amplitude_);
  updateHold(amplitude);
  int pitch = synth_ ? synthPitchHz_ : interpolate(lastPitch_, pitch_);
  if (holdMix_ && !synth_) {
    // Lift a quiet captured onset into a playable note over 8 ms.
    amplitude += std::max(0, 177 - amplitude) * holdMix_ / 256;
    if (!pitch) pitch = int(initialPitch_);
  }
  excitationPitch_ = voicePitch(pitch);
  int32_t source;
  if (noise_ && !holdMix_) {
    source = noiseSample();
  } else {
    phase_ = (phase_ + excitationPitch_) % (SynthesisRate * 256u);
    source = int((phase_ * (Quant * 2u)) / (SynthesisRate * 256u)) - Quant;
    if (noise_) {
      // Keep some consonant hiss, crossfading in pitch through the captured formants.
      int toneMix = holdMix_ * 3 / 4;
      if (voiceEffect_ == VoiceEffect::Whisper)
        toneMix -= toneMix * voiceAmount_ / (2 * 4095);
      const int hiss = noiseSample();
      source = hiss + (source - hiss) * toneMix / 256;
    } else if (voiceEffect_ == VoiceEffect::Whisper && voiceAmount_) {
      // The held Whisper keeps breath and enough tone to follow PITCH.
      const int breath = voiceAmount_ - int(voiceAmount_) * holdMix_ / 512;
      source += ((noiseSample() - source) * breath) / 4095;
    }
  }
  int32_t out = source * (synth_ ? 5 : amplitude / 32);
  for (unsigned i = 0; i < 4; ++i) out = filter(i, out);
  if (synth_) out = int32_t((int64_t(out)*synthGain_)/256);
  out = std::clamp<int32_t>(out, -32767, 32767);
  if (synth_) out = (out * int32_t(synthEnvelope_)) / (177 * 256);
  out = out * flutterGain_ / 256;
  return int16_t(out);
}

void Mea8000::advance() {
  if (synth_) {
    constexpr uint32_t maximum = 177 * 256;
    if (synthReleased_) {
      synthEnvelope_ = synthEnvelope_ > 236 ? synthEnvelope_-236 : 0;
      if (!synthEnvelope_) running_ = false;
    } else {
      synthEnvelope_ = std::min(maximum,synthEnvelope_+708);
    }
    if (!freeze_) {
      constexpr uint32_t centreStep = (4095u << 16) / (40 * 8);
      if (synthCentre_ < synthTarget_)
        synthCentre_ += std::min(centreStep,synthTarget_-synthCentre_);
      else
        synthCentre_ -= std::min(centreStep,synthCentre_-synthTarget_);
      if (synthMotionStep_) synthMotionPhase_ += synthMotionStep_;
      else synthMotionPhase_ = 0;
      const int32_t phase = int32_t(synthMotionPhase_ >> 16);
      const int32_t triangle = phase < 16384 ? phase*4 :
          phase < 49152 ? 131072-phase*4 : phase*4-262144;
      int32_t position = int32_t(synthCentre_) + triangle*1024;
      constexpr int32_t end = 4095u << 16;
      if (position < 0) position = -position;
      if (position > end) position = 2*end-position;
      synthPosition_ = uint32_t(position);
      synthBend_ = std::min<uint32_t>(65536u,synthBend_+synthBendStep_);
    }
    return;
  }
  if (holding_) return;
  if (!fading_)
    elapsedQ8_ = uint32_t(std::min<uint64_t>(uint64_t(elapsedQ8_) + speed_,
                                           uint64_t(wordTicks_) * 256));
  position_ += speed_;
  const uint32_t length = 1u << (durationLog_ + 8);
  if (position_ < length) return;
  position_ -= length;
  shift();
  if (offset_ < bytes_) {
    decode();
  } else if (!fading_) {
    // Like MAME, use one extra frame to ramp the last amplitude to zero.
    amplitude_ = 0;
    fading_ = true;
  } else {
    running_ = false;
  }
}

int16_t Mea8000::sample() {
  if (!running_) return 0;
  if (outputPhase_ == 0) {
    previousSample_ = nextSample_;
    nextSample_ = compute();
  }
  const int value = previousSample_ +
      (int(outputPhase_) * (int(nextSample_) - previousSample_)) / int(Upsample);
  if (++outputPhase_ == Upsample) {
    outputPhase_ = 0;
    advance();
  }
  return int16_t(value);
}
