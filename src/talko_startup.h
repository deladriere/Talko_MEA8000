#pragma once

#include <Arduino.h>
#include <cstdio>
#include "banks.h"
#include "ws2812.pio.h"

namespace TalkoStartup {

constexpr uint LedPin = 16;
constexpr uint LedStateMachine = 0;
constexpr uint8_t Dot = 0x80;
constexpr uint8_t Digits[] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
};

inline void prepareLed() {
  pinMode(LedPin, OUTPUT);
  digitalWrite(LedPin, LOW);
}

inline void showVersion(uint8_t major, uint8_t minor, void (*show)(uint8_t),
                        void (*say)(uint8_t) = nullptr, bool (*busy)() = nullptr) {
  pio_sm_claim(pio0, LedStateMachine);
  const uint offset = pio_add_program(pio0, &ws2812_program);
  ws2812_program_init(pio0, LedStateMachine, offset, LedPin, 800000, false);
  pio_sm_put_blocking(pio0, LedStateMachine, 0);
  delay(5);

  auto part = [&](uint8_t glyph, uint8_t spoken, uint32_t grb, uint8_t flashes, bool announce) {
    show(glyph);
    if (say && announce) {
      say(spoken);
    }
    if (flashes == 0) {
      delay(450);
    }
    for (uint8_t i = 0; i < flashes; ++i) {
      pio_sm_put_blocking(pio0, LedStateMachine, grb << 8);
      delay(150);
      pio_sm_put_blocking(pio0, LedStateMachine, 0);
      delay(300);
    }
    while (busy && busy()) {
      delay(1);
    }
  };

  auto number = [&](uint8_t value, uint32_t color) {
    char digits[4]; // A uint8_t has at most three decimal digits.
    const int count = std::snprintf(digits, sizeof(digits), "%u", unsigned(value));
    for (int i = 0; i < count; ++i) {
      if (i > 0 && digits[i] == digits[i - 1]) {
        show(0);
        delay(100);
      }
      const uint8_t digit = digits[i] - '0';
      // Spread the component's flashes across its displayed digits.
      const unsigned flashes = value * (i + 1) / count - value * i / count;
      const bool wholeWord = value < StartupNumberCount;
      part(Digits[digit], wholeWord ? value : digit, color, flashes, !wholeWord || i == 0);
    }
  };
  number(major, 100u << 8);
  part(Dot, StartupPointToken, 255, 1, true);
  number(minor, 100u << 16);
}

} // namespace TalkoStartup
