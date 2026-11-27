#pragma once
#include <cstdint>
constexpr uint8_t VersionMajor = 1;
constexpr uint8_t VersionMinor = 0;
constexpr unsigned AudioSampleRate = 32000;
constexpr unsigned AudioBlockSamples = 128;
constexpr unsigned AudioBlockBudgetUs = 4000;
enum class PlaybackMode : uint8_t { Speech, Gate, Repeat };
enum class Effect : uint8_t { Off, Formant, Ring, Bend, Whisper, Count };
void setupAudio();
void triggerAudio(uint8_t bank, uint8_t item, PlaybackMode mode);
void stopAudio();
void setGateActive(bool held);
void setSynthControls(uint16_t vowel, uint16_t pitchHz, uint16_t motionMilliHz);
void releaseSynth();
void setAudioControls(uint16_t speedQ8, uint16_t pitchQ8,
                      Effect effect, uint16_t amount, bool freeze);
void sayStartupPart(uint8_t part);
bool audioBusy();
bool audioBenchActive();
void serviceConsole();
