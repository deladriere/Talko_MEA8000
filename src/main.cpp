#include <Arduino.h>
#include <cmath>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <hardware/sync.h>
#include "audio.h"
#include "banks.h"
#include "panel_logic.h"
#include "fx_controls.h"
#include "speech_hold.h"
#include "talko_startup.h"

namespace {
constexpr uint ErrorPin = 0, GatePin = 2, EncoderA = 3, EncoderB = 4;
constexpr uint SegmentA = 5, DecimalPoint = 12, SelectPin = 13;
constexpr uint ModeB = 14, ModeA = 15;
constexpr uint DisplayBrightness = 100;
constexpr uint IdleMs = 5 * 60 * 1000;
constexpr uint8_t FxGlyphs[] = {
    0x40, // - Off
    0x71, // F Formant
    0x5c, // o Ring
    0x7c, // b Bipolar bend
    0x74, // h Whisper
};
static_assert(sizeof(FxGlyphs) == unsigned(Effect::Count));
constexpr uint8_t LetterBankGlyphs[] = {
    0x39, // C: Citroën
    0x79, // E: Électronique
    0x7c, // b: Binaire
    0x54, // n: Oui / Non
};
static_assert(CitroenBank == sizeof(TalkoStartup::Digits));
static_assert(CitroenBank + sizeof(LetterBankGlyphs) == BankCount);

EncoderDecoder encoder;
volatile uint8_t bankCursor = 0, fxCursor = 0;
volatile bool fxPage = false;
volatile uint32_t clicks = 0;
uint32_t lastClicks = 0, lastActivity = 0, lastPots = 0;
PlaybackMode mode = PlaybackMode::Speech, rawMode = PlaybackMode::Speech;
uint32_t modeChangedAt = 0;
ModeCue modeCue;
bool gateRaw = false, gateHeld = false;
uint32_t gateChangedAt = 0;
uint8_t lastBank = 0;
bool droneLatched = false;

struct Button {
  uint pin;
  bool raw = false, held = false;
  uint32_t changedAt = 0;
  bool update(uint32_t now) {
    const bool next = !gpio_get(pin);
    if (next != raw) { raw = next; changedAt = now; }
    if (held == raw || now - changedAt < 5) return false;
    held = raw;
    return true;
  }
};
Button selectButton{SelectPin}, error{ErrorPin};
struct Pot {
  uint pin;
  int filtered = 0, previous = 0;
  void begin() { previous = analogRead(pin); filtered = previous * 8; }
  int value() const { return filtered / 8; }
  bool update() {
    filtered += analogRead(pin) - value();
    if (abs(value() - previous) < 32) return false;
    previous = value();
    return true;
  }
};
Pot sound{26}, fx{27}, speed{28}, pitch{29};

uint8_t encoderState() {
  const uint32_t pins = gpio_get_all();
  return (((pins >> EncoderA) & 1u) << 1) | ((pins >> EncoderB) & 1u);
}
void encoderInterrupt() {
  const int movement = encoder.edge(encoderState());
  if (!movement) return;
  ++clicks;
  // Limit each click at the interrupt boundary. Reversal at a limit is immediate.
  if (fxPage) fxCursor = boundedClick(fxCursor, movement, unsigned(Effect::Count));
  else bankCursor = boundedClick(bankCursor, movement, BankCount);
}
void drawDisplay(uint8_t mask, uint brightness = DisplayBrightness) {
  static uint16_t previous = 0xffff;
  const uint16_t state = (brightness << 8) | mask;
  if (state == previous) return;
  previous = state;
  for (uint bit = 0; bit < 8; ++bit)
    pwm_set_gpio_level(SegmentA + bit, (mask & (1u << bit)) ? brightness : 0);
}
void startupDisplay(uint8_t mask) { drawDisplay(mask); }
bool startupBusy() {
  serviceConsole();
  return audioBusy();
}

PlaybackMode readMode() {
  constexpr uint32_t mask = (1u << ModeA) | (1u << ModeB);
  const uint32_t pins = gpio_get_all() & mask;
  if (pins == mask) return PlaybackMode::Gate;
  if (pins == (1u << ModeA)) return PlaybackMode::Repeat;
  return PlaybackMode::Speech;
}
void updatePots(uint32_t now) {
  if (now - lastPots < 2) return;
  lastPots = now;
  const bool a = sound.update(), b = fx.update(), c = speed.update(), d = pitch.update();
  if (a || b || c || d) lastActivity = now;
}
void display(uint32_t now) {
  static uint32_t lastFlicker = 0;
  static unsigned level = DisplayBrightness;
  static bool wasError = false;
  if (error.held) {
    // Stock Talko FlickerMode: 20 Hz, 30% sharp dips and 5% blackouts.
    if (!wasError || now - lastFlicker >= 50) {
      lastFlicker = now;
      level = random(100) < 30 ? random(20, 70) : 80 + random(-10, 20);
      if (random(100) < 5) level = 0;
    }
  } else {
    level = DisplayBrightness;
  }
  wasError = error.held;
  if (now - lastActivity >= IdleMs) {
    constexpr uint8_t snake[] = {0,1,6,4,3,2,6,5};
    drawDisplay(1u << snake[(now / 170) % sizeof(snake)], 35);
  } else {
    const uint8_t bank = bankCursor;
    const uint8_t bankGlyph = bank < CitroenBank ? TalkoStartup::Digits[bank]
                                              : LetterBankGlyphs[bank - CitroenBank];
    uint8_t glyph = fxPage ? FxGlyphs[fxCursor] | 0x80 : bankGlyph;
    if (modeCue.active(now))
      glyph = modeGlyph(mode, bank == SynthBank) | (fxPage ? 0x80 : 0);
    drawDisplay(glyph, level);
  }
}
} // namespace

void setup() {
  TalkoStartup::prepareLed();
  Serial.begin(115200);
  for (uint pin : {ErrorPin, GatePin, EncoderA, EncoderB, SelectPin, ModeA, ModeB})
    pinMode(pin, INPUT_PULLUP);
  for (uint pin = SegmentA; pin <= DecimalPoint; ++pin) {
    const uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_wrap(slice, 255);
    pwm_set_gpio_level(pin, 0);
    gpio_set_function(pin, GPIO_FUNC_PWM);
    pwm_set_enabled(slice, true);
  }
  analogReadResolution(12);
  sound.begin(); fx.begin(); speed.begin(); pitch.begin();
  setupAudio();
  TalkoStartup::showVersion(VersionMajor, VersionMinor, startupDisplay, sayStartupPart, startupBusy);
  // Inputs have had ample settling time; sample the actual state before IRQs.
  encoder.begin(encoderState());
  attachInterrupt(digitalPinToInterrupt(EncoderA), encoderInterrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(EncoderB), encoderInterrupt, CHANGE);
  mode = rawMode = readMode();
  gateRaw = !gpio_get(GatePin);
  gateChangedAt = micros();
  lastActivity = millis();
}
void loop() {
  const uint32_t now = millis();
  updatePots(now);
  const bool selectChanged = selectButton.update(now);
  const bool errorChanged = error.update(now);
  if (selectChanged && selectButton.held) {
    const uint32_t irq = save_and_disable_interrupts();
    fxPage = !fxPage;
    restore_interrupts(irq);
  }
  if (selectChanged || errorChanged || clicks != lastClicks) {
    lastClicks = clicks;
    lastActivity = now;
  }
  const PlaybackMode nextMode = readMode();
  if (nextMode != rawMode) { rawMode = nextMode; modeChangedAt = now; }
  if (mode != rawMode && now - modeChangedAt >= 10) {
    if (!audioBenchActive()) stopAudio();
    droneLatched = false;
    mode = rawMode;
    modeCue.show(now);
    lastActivity = now;
  }
  const bool nextGate = !gpio_get(GatePin);
  const uint32_t gateNow = micros();
  if (nextGate != gateRaw) { gateRaw = nextGate; gateChangedAt = gateNow; }
  bool rising = false, falling = false;
  if (gateHeld != gateRaw && gateNow - gateChangedAt >= 1000) {
    gateHeld = gateRaw;
    rising = gateHeld;
    falling = !gateHeld;
    lastActivity = now;
  }
  setGateActive(gateHeld);
  const uint8_t bank = bankCursor;
  const bool synth = banks[bank].kind == BankKind::Synth;
  if (bank != lastBank) {
    if (lastBank == SynthBank && !audioBenchActive()) stopAudio();
    droneLatched = false;
    lastBank = bank;
  }
  if (falling && !audioBenchActive()) {
    if (synth && mode == PlaybackMode::Gate) releaseSynth();
    else if (!synth && stopSpeechOnRelease(mode, gateHeld, error.held)) stopAudio();
  }
  if (errorChanged && !error.held && !synth && !audioBenchActive() &&
      stopSpeechOnRelease(mode, gateHeld, false)) stopAudio();
  const Effect effect = Effect(fxCursor);
  const uint amount = effectAmount(effect, fx.value());
  setAudioControls(panelRatioQ8(speed.value(), 3), panelRatioQ8(pitch.value(), 1), effect, amount, error.held);
  const uint16_t synthPitch = uint16_t(55.0f * exp2f(float(pitch.value())*4.0f/4095.0f)+0.5f);
  const uint16_t motion = speed.value() <= 80 ? 0 : uint16_t(
      150.0f * exp2f(float(speed.value()-80)*6.321928f/4015.0f)+0.5f);
  setSynthControls(uint(constrain(sound.value(),0,int(SoundMax)))*4095/SoundMax,synthPitch,motion);
  if (rising && !audioBenchActive()) {
    if (synth && mode == PlaybackMode::Repeat && droneLatched && audioBusy()) {
      releaseSynth();
      droneLatched = false;
    } else {
      const uint item = soundItem(sound.value(), banks[bank].count);
      triggerAudio(bank, item, mode);
      if (synth && mode == PlaybackMode::Repeat) droneLatched = true;
    }
  }
  if (gateHeld || error.held || audioBusy()) lastActivity = now;
  display(now);
  serviceConsole();
}
