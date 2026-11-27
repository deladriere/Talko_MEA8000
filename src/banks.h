#pragma once
#include <cstddef>
#include <cstdint>
struct Entry {
  const char *label;
  const uint8_t *frames;
  uint16_t bytes;
  uint16_t pitchHz;
};
enum class BankKind : uint8_t { Speech, Synth };
struct Bank {
  const char *label;
  const Entry *entries;
  uint8_t count;
  BankKind kind = BankKind::Speech;
};
constexpr uint8_t CitroenBank = 10;
constexpr uint8_t ElectronicsBank = CitroenBank + 1;
constexpr uint8_t BinaryBank = ElectronicsBank + 1;
constexpr uint8_t AnswersBank = BinaryBank + 1;
constexpr uint8_t BankCount = AnswersBank + 1;
constexpr uint8_t SynthBank = 9;
extern const Bank banks[BankCount];
constexpr uint8_t StartupNumberCount = 12; // ZERO through ONZE
constexpr uint8_t StartupPointToken = 0xff;
extern const Entry startupNumbers[StartupNumberCount];
extern const Entry startupPoint;
