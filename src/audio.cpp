#include "audio.h"
#include "banks.h"
#include "mea8000.h"
#include "fx_controls.h"
#include "speech_hold.h"
#include <Arduino.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <hardware/clocks.h>
#include <hardware/pwm.h>
#include <hardware/sync.h>
#include <pico/util/queue.h>

namespace {
constexpr uint AudioPin = 1;
constexpr uint CarrierCycles = 2;
constexpr uint RingSize = 1024, RingMask = RingSize - 1;
constexpr uint PrefillSamples = AudioBlockSamples;
constexpr uint QueueAheadSamples = AudioBlockSamples * 2;
constexpr uint8_t StartupBank = BankCount;
static_assert(AudioSampleRate == Mea8000::SampleRate);
struct Request {
  uint32_t epoch;
  uint8_t bank, item;
  PlaybackMode mode;
};
enum class Event : uint8_t { Play, Done, Cut, Error };
struct Report {
  Request request;
  Event event;
  uint32_t blocks = 0, totalUs = 0, peakUs = 0, lateBlocks = 0;
  uint32_t underrunEvents = 0, underrunSamples = 0;
};
queue_t requests, reports, playbackLabels;
constexpr uint16_t NoLabel = 0xffff;
std::atomic<uint16_t> audibleLabel{NoLabel};
uint16_t streamLabel = NoLabel;
std::atomic<bool> ready{false}, playing{false}, requestActive{false}, renderDone{false};
std::atomic<bool> gateActive{false};
std::atomic<uint32_t> epoch{0}, readIndex{0}, writeIndex{0};
std::atomic<uint32_t> underrunEvents{0}, underrunSamples{0};
std::atomic<uint32_t> controls{(256u << 16) | 256u}, effects{0};
std::atomic<uint32_t> synthVowel{2048}, synthSettings{(1500u << 16) | 220u};
std::atomic<bool> synthHeld{false};
uint32_t streamEpoch;
bool inUnderrun = false;
uint16_t duties[RingSize];
bool labelStarts[RingSize];
uint audioSlice, audioChannel, pwmPeriod, carrierPhase = 0;
Mea8000 engine;
int16_t sine[256];
uint32_t ringPhase = 0;
// Serial bench overrides are written/read only on core 0; rendering sees the
// same atomic controls as panel use. 'c panel' returns control to the knobs.
bool serialOverride = false, haveLastReport = false, requestLastReport = false;
bool requestIdentity = false;
bool debugEnabled = false;
PlaybackMode benchMode = PlaybackMode::Speech;
uint8_t benchBank = 0;
Report lastReport{};

void labelAtPlayback(uint16_t label) {
  audibleLabel.store(label, std::memory_order_release);
  // Only a two-byte nonblocking event is queued in the ISR. USB formatting
  // stays on core 0; a stalled host must never stall the audio clock.
  queue_try_add(&playbackLabels, &label);
}
uint nextDuty() {
  if (!playing.load(std::memory_order_acquire)) return pwmPeriod / 2;
  if (streamEpoch != epoch.load(std::memory_order_relaxed)) {
    audibleLabel.store(NoLabel, std::memory_order_release);
    playing.store(false, std::memory_order_release);
    return pwmPeriod / 2;
  }
  const uint32_t read = readIndex.load(std::memory_order_relaxed);
  const bool done = renderDone.load(std::memory_order_acquire);
  if (read != writeIndex.load(std::memory_order_acquire)) {
    if (labelStarts[read & RingMask]) labelAtPlayback(streamLabel);
    const uint duty = duties[read & RingMask];
    readIndex.store(read + 1, std::memory_order_release);
    inUnderrun = false;
    return duty;
  }
  if (done) {
    audibleLabel.store(NoLabel, std::memory_order_release);
    playing.store(false, std::memory_order_release);
  } else {
    underrunSamples.store(underrunSamples.load(std::memory_order_relaxed) + 1,
                          std::memory_order_relaxed);
    if (!inUnderrun) {
      underrunEvents.store(underrunEvents.load(std::memory_order_relaxed) + 1,
                           std::memory_order_relaxed);
      inUnderrun = true;
    }
  }
  return pwmPeriod / 2;
}

void __isr __not_in_flash_func(audioInterrupt)() {
  pwm_clear_irq(audioSlice);
  if (++carrierPhase == CarrierCycles) {
    carrierPhase = 0;
    pwm_set_chan_level(audioSlice, audioChannel, nextDuty());
  }
}

bool cancelled(const Request &request) {
  return request.epoch != epoch.load(std::memory_order_acquire);
}
Entry entryFor(const Request &request) {
  if (request.bank == StartupBank)
    return request.item == StartupPointToken ? startupPoint : startupNumbers[request.item];
  return banks[request.bank].entries[request.item];
}
void render(const Request &request) {
  const Entry entry = entryFor(request);
  const bool synth = request.bank < BankCount && banks[request.bank].kind == BankKind::Synth;
  Report report{request, Event::Play};
  if (synth) {
    const uint32_t settings = synthSettings.load(std::memory_order_relaxed);
    engine.startSynth(synthVowel.load(std::memory_order_relaxed),settings & 0xffff,settings >> 16);
  } else if (!engine.start(entry.frames, entry.bytes, entry.pitchHz)) {
    report.event = Event::Error;
    queue_try_add(&reports, &report);
    return;
  }
  if (cancelled(request)) return;
  ringPhase = 0;
  readIndex.store(0, std::memory_order_relaxed);
  writeIndex.store(0, std::memory_order_relaxed);
  renderDone.store(false, std::memory_order_relaxed);
  underrunEvents.store(0, std::memory_order_relaxed);
  underrunSamples.store(0, std::memory_order_relaxed);
  streamEpoch = request.epoch;
  streamLabel = (uint16_t(request.bank) << 8) | request.item;
  inUnderrun = false;
  queue_try_add(&reports, &report);
  uint32_t write = 0;
  bool started = false, more = true;
  bool labelStart = true;
  while (more && !cancelled(request)) {
    if (write - readIndex.load(std::memory_order_acquire) >
        QueueAheadSamples - AudioBlockSamples) {
      tight_loop_contents();
      continue;
    }
    const uint32_t startedAt = micros();
    const uint32_t control = request.bank == StartupBank
                                 ? (256u << 16) | 256u
                                 : controls.load(std::memory_order_relaxed);
    const uint32_t fx = request.bank == StartupBank ? 0 : effects.load(std::memory_order_relaxed);
    const auto effect = Effect(fx & 0xff);
    const uint amount = (fx >> 8) & 4095;
    const bool freeze = (fx >> 31) != 0;
    const uint formant = effect == Effect::Formant ? 256 + amount * 192 / 4095 : 256;
    engine.controls(control & 0xffff, synth ? 256 : control >> 16, formant, freeze);
    if (synth) {
      const uint32_t settings = synthSettings.load(std::memory_order_relaxed);
      engine.synthControls(synthVowel.load(std::memory_order_relaxed),settings & 0xffff,settings >> 16);
    }
    const auto voice = voiceControl(effect, amount);
    engine.voiceEffect(voice.effect, voice.amount);
    const uint32_t ringIncrement =
        uint32_t((uint64_t(30 + amount * 970 / 4095) << 32) / AudioSampleRate);
    unsigned count = 0;
    while (count < AudioBlockSamples) {
      if (synth && ((request.mode == PlaybackMode::Speech &&
                     write+count >= AudioSampleRate/2) ||
                    (request.mode != PlaybackMode::Speech &&
                     !synthHeld.load(std::memory_order_relaxed))))
        engine.releaseSynth();
      if (!engine.active()) {
        if (!synth && restartSpeech(request.mode,
            gateActive.load(std::memory_order_relaxed), freeze)) {
          labelStart = true;
          engine.start(entry.frames, entry.bytes, entry.pitchHz);
          engine.controls(control & 0xffff, control >> 16, formant, freeze);
        } else {
          more = false;
          break;
        }
      }
      int32_t sample = engine.sample();
      if (effect == Effect::Ring && amount) {
        const int32_t wet = int32_t((int64_t(sample) * sine[ringPhase >> 24]) / 32768);
        sample += ((wet - sample) * int32_t(amount)) / 4095;
        ringPhase += ringIncrement;
      }
      labelStarts[(write + count) & RingMask] = labelStart;
      labelStart = false;
      duties[(write + count) & RingMask] =
          uint16_t((uint32_t(sample + 32768) * pwmPeriod) >> 16);
      ++count;
    }
    const uint32_t elapsed = micros() - startedAt;
    ++report.blocks;
    report.totalUs += elapsed;
    report.peakUs = max(report.peakUs, elapsed);
    // A final partial block has a proportionally smaller deadline.
    report.lateBlocks += count && elapsed > (count * 1000000u) / AudioSampleRate;
    if (cancelled(request)) break;
    write += count;
    writeIndex.store(write, std::memory_order_release);
    if (!more) renderDone.store(true, std::memory_order_release);
    if (!started && (write >= PrefillSamples || !more)) {
      playing.store(true, std::memory_order_release);
      started = true;
    }
  }
  while (playing.load(std::memory_order_acquire)) tight_loop_contents();
  engine.stop();
  report.event = cancelled(request) ? Event::Cut : Event::Done;
  report.underrunEvents = underrunEvents.load(std::memory_order_relaxed);
  report.underrunSamples = underrunSamples.load(std::memory_order_relaxed);
  queue_try_add(&reports, &report);
}

int formatReport(char *line, size_t size, const Report &report) {
  const Request &r = report.request;
  const Entry entry = entryFor(r);
  const char *mode = r.mode == PlaybackMode::Speech ? "speech" :
                     r.mode == PlaybackMode::Gate ? "gate" : "repeat";
  if (report.event == Event::Play)
    return snprintf(line, size, "PLAY bank=%u item=%u label=\"%s\" mode=%s\r\n",
                    r.bank, unsigned(r.item) + 1, entry.label, mode);
  const char *status = report.event == Event::Done ? "done" :
                       report.event == Event::Cut ? "cut" : "error";
  return snprintf(line, size,
      "RESULT label=\"%s\" status=%s blocks=%lu avg_us=%lu peak_us=%lu "
      "budget_us=%u late_blocks=%lu underrun_events=%lu underrun_samples=%lu\r\n",
      entry.label, status, (unsigned long)report.blocks,
      (unsigned long)(report.blocks ? report.totalUs / report.blocks : 0),
      (unsigned long)report.peakUs, AudioBlockBudgetUs, (unsigned long)report.lateBlocks,
      (unsigned long)report.underrunEvents, (unsigned long)report.underrunSamples);
}

int formatLabel(char *line, size_t size, uint16_t label) {
  const Request request{0, uint8_t(label >> 8), uint8_t(label), PlaybackMode::Speech};
  return snprintf(line, size, "%s\r\n", entryFor(request).label);
}

void readCommands() {
  static char input[40];
  static unsigned used = 0;
  static bool overflow = false;
  for (unsigned count = 0; count < 32 && Serial.available(); ++count) {
    const int byte = Serial.read();
    if (byte == '\n' || byte == '\r') {
      if (!overflow && used) {
        input[used] = 0;
        unsigned bank, item, speed, pitch, effect, amount, freeze, vowel, rate, held, requestedMode = 0;
        if (!strcmp(input, "s")) stopAudio();
        else if (!strcmp(input, "?")) requestIdentity = true;
        else if (!strcmp(input, "t")) requestLastReport = true;
        else if (!strcmp(input, "debug 1")) debugEnabled = true;
        else if (!strcmp(input, "debug 0")) debugEnabled = false;
        else if (!strcmp(input, "c panel")) serialOverride = false;
        else if (sscanf(input, "g %u", &held) == 1 && held <= 1) {
          serialOverride = true;
          gateActive.store(held, std::memory_order_relaxed);
          if (!held && benchBank == SynthBank) releaseSynth();
          else if (stopSpeechOnRelease(benchMode, held,
                   effects.load(std::memory_order_relaxed) >> 31)) stopAudio();
        }
        else if (sscanf(input,"v %u %u %u",&vowel,&pitch,&rate)==3 &&
                 vowel<=4095 && pitch>=55 && pitch<=880 && rate<=12000) {
          serialOverride = false;
          setSynthControls(vowel,pitch,rate);
          serialOverride = true;
        }
        else if (sscanf(input,"n %u",&held)==1 && held<=2) {
          if (held) {
            benchBank = SynthBank;
            benchMode = held==2 ? PlaybackMode::Repeat : PlaybackMode::Gate;
            triggerAudio(SynthBank,0,benchMode);
          }
          else releaseSynth();
        }
        else if (sscanf(input, "c %u %u %u %u %u", &speed, &pitch, &effect,
                        &amount, &freeze) == 5 && speed >= 64 && speed <= Mea8000::MaxSpeedQ8 &&
                 pitch >= 64 && pitch <= 1024 && effect < unsigned(Effect::Count) &&
                 amount <= 4095 && freeze <= 1) {
          serialOverride = false;
          setAudioControls(speed, pitch, Effect(effect), amount, freeze);
          serialOverride = true;
          if (benchBank != SynthBank &&
              stopSpeechOnRelease(benchMode, gateActive.load(std::memory_order_relaxed), freeze))
            stopAudio();
        }
        else if (sscanf(input, "p %u %u %u", &bank, &item, &requestedMode) >= 2 &&
                 bank < BankCount && item > 0 && item <= banks[bank].count && requestedMode <= 2) {
          benchBank = bank; benchMode = PlaybackMode(requestedMode);
          triggerAudio(uint8_t(bank), uint8_t(item - 1), benchMode);
        }
      }
      used = 0;
      overflow = false;
    } else if (used + 1 < sizeof(input) && !overflow) {
      input[used++] = char(byte);
    } else {
      overflow = true;
    }
  }
}
} // namespace

bool core1_separate_stack = true;
void setupAudio() {
  queue_init(&requests, sizeof(Request), 1);
  queue_init(&reports, sizeof(Report), 16);
  queue_init(&playbackLabels, sizeof(uint16_t), 32);
  audioSlice = pwm_gpio_to_slice_num(AudioPin);
  audioChannel = pwm_gpio_to_channel(AudioPin);
  constexpr uint carrierRate = AudioSampleRate * CarrierCycles;
  pwmPeriod = (clock_get_hz(clk_sys) + carrierRate / 2) / carrierRate;
  pwm_config config = pwm_get_default_config();
  pwm_config_set_wrap(&config, pwmPeriod - 1);
  pwm_init(audioSlice, &config, false);
  pwm_set_chan_level(audioSlice, audioChannel, pwmPeriod / 2);
  gpio_set_function(AudioPin, GPIO_FUNC_PWM);
  irq_set_exclusive_handler(PWM_IRQ_WRAP, audioInterrupt);
  pwm_clear_irq(audioSlice);
  pwm_set_irq_enabled(audioSlice, true);
  irq_set_enabled(PWM_IRQ_WRAP, true);
  pwm_set_enabled(audioSlice, true);
  ready.store(true, std::memory_order_release);
}
void setup1() {
  while (!ready.load(std::memory_order_acquire)) tight_loop_contents();
  for (unsigned i = 0; i < 256; ++i)
    sine[i] = int16_t(std::sin(6.283185307179586 * i / 256.0) * 32767);
}
void loop1() {
  Request request;
  queue_peek_blocking(&requests, &request);
  requestActive.store(true, std::memory_order_release);
  // A producer can replace the one pending request between peek and removal.
  // Always use the item actually removed.
  if (queue_try_remove(&requests, &request) && !cancelled(request)) render(request);
  requestActive.store(false, std::memory_order_release);
}
void triggerAudio(uint8_t bank, uint8_t item, PlaybackMode mode) {
  if (bank >= BankCount || item >= banks[bank].count) return;
  if (mode == PlaybackMode::Speech && audioBusy()) return;
  stopAudio();
  if (banks[bank].kind == BankKind::Synth)
    synthHeld.store(true,std::memory_order_relaxed);
  const Request request{epoch.load(std::memory_order_relaxed), bank, item, mode};
  Request old;
  queue_try_remove(&requests, &old);
  queue_try_add(&requests, &request);
}
void stopAudio() {
  synthHeld.store(false,std::memory_order_relaxed);
  // Core 0 is the sole writer; no atomic read/modify/write lock in the ISR.
  epoch.store(epoch.load(std::memory_order_relaxed) + 1, std::memory_order_release);
}
void setGateActive(bool held) {
  if (!serialOverride) gateActive.store(held, std::memory_order_relaxed);
}
void releaseSynth() { synthHeld.store(false,std::memory_order_relaxed); }
void setSynthControls(uint16_t vowel, uint16_t pitchHz, uint16_t motionMilliHz) {
  if (serialOverride) return;
  synthVowel.store(vowel,std::memory_order_relaxed);
  synthSettings.store((uint32_t(motionMilliHz)<<16)|pitchHz,std::memory_order_relaxed);
}
void setAudioControls(uint16_t speedQ8, uint16_t pitchQ8,
                      Effect effect, uint16_t amount, bool freeze) {
  if (serialOverride) return;
  controls.store((uint32_t(pitchQ8) << 16) | speedQ8, std::memory_order_relaxed);
  effects.store(uint32_t(effect) | (uint32_t(amount) << 8) | (uint32_t(freeze) << 31),
                std::memory_order_relaxed);
}
void sayStartupPart(uint8_t part) {
  if (part != StartupPointToken && part >= StartupNumberCount) return;
  const Request request{epoch.load(std::memory_order_relaxed), StartupBank, part,
                        PlaybackMode::Speech};
  queue_try_add(&requests, &request);
}
bool audioBusy() {
  // Inspect the queue before busy: core 1 publishes busy before removing a
  // request, so an empty queue cannot hide an in-flight dequeue here.
  return !queue_is_empty(&requests) || requestActive.load(std::memory_order_acquire);
}
bool audioBenchActive() { return serialOverride; }
void serviceConsole() {
  static char line[256];
  static size_t sent = 0, length = 0;
  static bool connected = false;
  static uint16_t connectionLabel = NoLabel;
  Report report;
  uint16_t label;
  if (!Serial) {
    connected = false;
    debugEnabled = false;
    sent = length;
    while (queue_try_remove(&playbackLabels, &label)) {}
    while (queue_try_remove(&reports, &report)) {
      if (report.event != Event::Play) { lastReport = report; haveLastReport = true; }
    }
    return;
  }
  if (!connected) {
    while (queue_try_remove(&playbackLabels, &label)) {}
    // The PWM interrupt publishes a start to the queue and to audibleLabel.
    // Read both together so a start concurrent with connect is sent once.
    const uint32_t irq = save_and_disable_interrupts();
    connectionLabel = queue_is_empty(&playbackLabels) ? audibleLabel.load(std::memory_order_acquire) : NoLabel;
    restore_interrupts(irq);
    connected = true;
  }
  readCommands();
  if (!debugEnabled) {
    while (queue_try_remove(&reports, &report)) {
      if (report.event != Event::Play) { lastReport = report; haveLastReport = true; }
    }
  }
  const int available = Serial.availableForWrite();
  if (available <= 0) return;
  if (sent == length) {
    int size;
    if (connectionLabel != NoLabel) {
      size = formatLabel(line, sizeof(line), connectionLabel);
      connectionLabel = NoLabel;
    } else if (queue_try_remove(&playbackLabels, &label)) {
      size = formatLabel(line, sizeof(line), label);
    } else if (requestIdentity) {
      size = snprintf(line, sizeof(line),
          "Talko 2 MEA8000 v%u.%u sample_hz=%u block_samples=%u budget_us=%u "
          "commands: p <bank 0-%u> <item 1-N>, v <vowel pitch rate>, n <0-2>, s=stop\r\n",
          VersionMajor, VersionMinor, AudioSampleRate, AudioBlockSamples, AudioBlockBudgetUs, unsigned(BankCount - 1));
      requestIdentity = false;
    } else if (requestLastReport && haveLastReport) {
      size = formatReport(line, sizeof(line), lastReport);
      requestLastReport = false;
    } else if (debugEnabled && queue_try_remove(&reports, &report)) {
      size = formatReport(line, sizeof(line), report);
      if (report.event != Event::Play) { lastReport = report; haveLastReport = true; }
    } else return;
    length = size > 0 ? min(size_t(size), sizeof(line) - 1) : 0;
    sent = 0;
  }
  sent += Serial.write(reinterpret_cast<const uint8_t *>(line) + sent,
                       min(length - sent, size_t(available)));
}
