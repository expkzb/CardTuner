#include <Arduino.h>
#include <M5Cardputer.h>
#include <Preferences.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "note.h"
#include "pitch_detector.h"

namespace {
constexpr uint32_t kSampleRate = 16000;
constexpr std::size_t kBlockSize = 512;
constexpr uint32_t kSignalTimeoutMs = 300;
constexpr float kStableNoteHysteresisCents = 58.0f;
constexpr uint8_t kSensitivityLevelCount = 5;
constexpr uint8_t kDefaultSensitivityLevel = 3;
constexpr float kSensitivityThresholds[kSensitivityLevelCount] = {
    500.0f, 300.0f, 180.0f, 110.0f, 60.0f,
};
constexpr uint16_t kBackground = 0x18E3;
constexpr uint16_t kPanel = 0x2945;
constexpr uint16_t kMuted = 0x8410;
constexpr uint16_t kAccent = TFT_GREEN;
constexpr uint16_t kWarning = 0xFD20;

PitchDetector g_detector(static_cast<float>(kSampleRate));
Preferences g_preferences;
int16_t g_capture_blocks[3][kBlockSize] = {};
int16_t g_window[PitchDetector::kWindowSize] = {};
std::size_t g_window_fill = 0;
uint8_t g_submit_block = 0;
uint8_t g_process_block = 0;
uint32_t g_last_valid_ms = 0;
float g_recent_frequencies[3] = {};
uint8_t g_recent_count = 0;
uint8_t g_recent_index = 0;
int g_display_midi = -1;
uint8_t g_sensitivity_level = kDefaultSensitivityLevel;

struct DisplayState {
  bool valid = false;
  bool low_signal = false;
  int midi = -1;
  int cents = 999;
  int frequency_tenths = -1;
};

DisplayState g_drawn;

bool hasHidKey(const Keyboard_Class::KeysState& keys, uint8_t key) {
  return std::find(keys.hid_keys.begin(), keys.hid_keys.end(), key) !=
         keys.hid_keys.end();
}

bool hasWordKey(const Keyboard_Class::KeysState& keys, char first, char second) {
  return std::find(keys.word.begin(), keys.word.end(), first) != keys.word.end() ||
         std::find(keys.word.begin(), keys.word.end(), second) != keys.word.end();
}

bool sensitivityUp(const Keyboard_Class::KeysState& keys) {
  return hasHidKey(keys, 0x52) || hasWordKey(keys, ';', ':');
}

bool sensitivityDown(const Keyboard_Class::KeysState& keys) {
  return hasHidKey(keys, 0x51) || hasWordKey(keys, '.', '>');
}

float medianFrequency(float frequency) {
  g_recent_frequencies[g_recent_index] = frequency;
  g_recent_index = (g_recent_index + 1) % 3;
  if (g_recent_count < 3) {
    ++g_recent_count;
  }

  float sorted[3] = {};
  for (uint8_t i = 0; i < g_recent_count; ++i) {
    sorted[i] = g_recent_frequencies[i];
  }
  std::sort(sorted, sorted + g_recent_count);
  return sorted[g_recent_count / 2];
}

NoteInfo stableNote(float frequency) {
  NoteInfo note = noteFromFrequency(frequency);
  if (!note.valid) {
    return note;
  }

  if (g_display_midi >= 0 && note.midi != g_display_midi) {
    const float current_note_hz =
        440.0f * std::pow(2.0f, (static_cast<float>(g_display_midi) - 69.0f) / 12.0f);
    const float offset =
        1200.0f * std::log2(frequency / current_note_hz);
    if (std::fabs(offset) < kStableNoteHysteresisCents) {
      note.midi = g_display_midi;
      note.name = noteFromFrequency(current_note_hz).name;
      note.octave = g_display_midi / 12 - 1;
      note.cents = offset;
    }
  }
  g_display_midi = note.midi;
  return note;
}

void drawStaticUi() {
  auto& display = M5Cardputer.Display;
  display.fillScreen(kBackground);
  display.setTextWrap(false);
  display.setTextDatum(top_left);
  display.setFont(&fonts::Font2);
  display.setTextColor(kMuted, kBackground);
  display.drawString("CARDTUNER", 8, 5);
}

void drawSensitivity() {
  auto& display = M5Cardputer.Display;
  char label[16];
  std::snprintf(label, sizeof(label), "SENS %u", g_sensitivity_level);
  display.fillRect(160, 0, 80, 23, kBackground);
  display.setFont(&fonts::Font2);
  display.setTextColor(kMuted, kBackground);
  display.setTextDatum(top_right);
  display.drawString(label, 232, 5);
}

void drawTunerBody() {
  auto& display = M5Cardputer.Display;
  display.fillRoundRect(8, 24, 224, 72, 7, kPanel);

  display.drawFastHLine(20, 108, 200, kMuted);
  for (int i = 0; i <= 10; ++i) {
    const int x = 20 + i * 20;
    const int height = (i == 5) ? 9 : ((i % 5 == 0) ? 6 : 3);
    display.drawFastVLine(x, 108 - height / 2, height, i == 5 ? kAccent : kMuted);
  }
}

void applySensitivity(bool persist) {
  g_detector.setRmsThreshold(kSensitivityThresholds[g_sensitivity_level - 1]);
  if (persist) {
    g_preferences.putUChar("sensitivity", g_sensitivity_level);
  }
  drawSensitivity();
}

void handleSensitivityKeys() {
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
    return;
  }

  const auto& keys = M5Cardputer.Keyboard.keysState();
  uint8_t next = g_sensitivity_level;
  if (sensitivityUp(keys) && next < kSensitivityLevelCount) {
    ++next;
  } else if (sensitivityDown(keys) && next > 1) {
    --next;
  }
  if (next == g_sensitivity_level) {
    return;
  }

  g_sensitivity_level = next;
  applySensitivity(true);
}

void drawMainPanel(const DisplayState& state, const NoteInfo& note) {
  auto& display = M5Cardputer.Display;
  display.fillRoundRect(8, 24, 224, 72, 7, kPanel);
  display.setTextDatum(middle_center);

  if (!state.valid) {
    display.setFont(&fonts::Font4);
    display.setTextColor(state.low_signal ? kWarning : kMuted, kPanel);
    display.drawString(state.low_signal ? "LOW SIGNAL" : "LISTENING", 120, 60);
    return;
  }

  char octave[8];
  std::snprintf(octave, sizeof(octave), "%d", note.octave);
  display.setFont(&fonts::FreeSansBold24pt7b);
  display.setTextColor(TFT_WHITE, kPanel);
  display.drawString(note.name, 108, 59);
  display.setFont(&fonts::Font4);
  display.setTextColor(kMuted, kPanel);
  display.drawString(octave, 154, 67);
}

void drawMeter(const DisplayState& state) {
  auto& display = M5Cardputer.Display;
  display.fillRect(16, 99, 208, 19, kBackground);
  display.drawFastHLine(20, 108, 200, kMuted);
  for (int i = 0; i <= 10; ++i) {
    const int x = 20 + i * 20;
    const int height = (i == 5) ? 9 : ((i % 5 == 0) ? 6 : 3);
    display.drawFastVLine(x, 108 - height / 2, height, i == 5 ? kAccent : kMuted);
  }
  if (!state.valid) {
    return;
  }

  const int clamped_cents = std::max(-50, std::min(50, state.cents));
  const int x = 120 + clamped_cents * 2;
  const uint16_t color = std::abs(state.cents) <= 5 ? kAccent : TFT_WHITE;
  display.fillTriangle(x, 99, x - 5, 106, x + 5, 106, color);
}

void drawFooter(const DisplayState& state) {
  auto& display = M5Cardputer.Display;
  display.fillRect(0, 119, 240, 16, kBackground);
  if (!state.valid) {
    return;
  }

  char frequency[24];
  char cents[20];
  std::snprintf(frequency, sizeof(frequency), "%.1f Hz",
                static_cast<float>(state.frequency_tenths) / 10.0f);
  std::snprintf(cents, sizeof(cents), "%+d cents", state.cents);
  display.setFont(&fonts::Font2);
  display.setTextColor(TFT_WHITE, kBackground);
  display.setTextDatum(top_left);
  display.drawString(frequency, 16, 119);
  display.setTextDatum(top_right);
  display.drawString(cents, 224, 119);
}

void updateDisplay(const PitchResult& pitch, uint32_t now) {
  DisplayState next;
  NoteInfo note;
  if (pitch.valid) {
    const float frequency = medianFrequency(pitch.frequency_hz);
    note = stableNote(frequency);
    next.valid = note.valid;
    next.midi = note.midi;
    next.cents = static_cast<int>(std::lround(note.cents));
    next.frequency_tenths = static_cast<int>(std::lround(frequency * 10.0f));
    g_last_valid_ms = now;
  } else {
    next.valid = now - g_last_valid_ms < kSignalTimeoutMs && g_drawn.valid;
    if (next.valid) {
      return;
    }
    next.low_signal = pitch.rms > 0.0f;
    g_recent_count = 0;
    g_recent_index = 0;
    g_display_midi = -1;
  }

  const bool panel_changed = next.valid != g_drawn.valid ||
                             next.low_signal != g_drawn.low_signal ||
                             next.midi != g_drawn.midi;
  const bool meter_changed = panel_changed || next.cents != g_drawn.cents;
  const bool footer_changed =
      meter_changed || next.frequency_tenths != g_drawn.frequency_tenths;

  if (panel_changed) {
    drawMainPanel(next, note);
  }
  if (meter_changed) {
    drawMeter(next);
  }
  if (footer_changed) {
    drawFooter(next);
  }
  g_drawn = next;
}

void appendSamples(const int16_t* block) {
  if (g_window_fill < PitchDetector::kWindowSize) {
    const std::size_t copy_count =
        std::min(kBlockSize, PitchDetector::kWindowSize - g_window_fill);
    std::memcpy(g_window + g_window_fill, block, copy_count * sizeof(int16_t));
    g_window_fill += copy_count;
    return;
  }

  std::memmove(g_window, g_window + kBlockSize,
               (PitchDetector::kWindowSize - kBlockSize) * sizeof(int16_t));
  std::memcpy(g_window + PitchDetector::kWindowSize - kBlockSize, block,
              kBlockSize * sizeof(int16_t));
}
}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.internal_mic = true;
  cfg.internal_spk = false;
  M5Cardputer.begin(cfg, true);
  Serial.begin(115200);

  M5Cardputer.Display.setRotation(1);
  g_preferences.begin("cardtuner", false);
  g_sensitivity_level =
      g_preferences.getUChar("sensitivity", kDefaultSensitivityLevel);
  if (g_sensitivity_level < 1 || g_sensitivity_level > kSensitivityLevelCount) {
    g_sensitivity_level = kDefaultSensitivityLevel;
  }
  applySensitivity(false);
  drawStaticUi();
  drawSensitivity();
  drawTunerBody();
  DisplayState initial;
  drawMainPanel(initial, {});
  drawMeter(initial);

  M5Cardputer.Speaker.end();
  auto mic_cfg = M5Cardputer.Mic.config();
  mic_cfg.sample_rate = kSampleRate;
  mic_cfg.noise_filter_level = 0;
  mic_cfg.dma_buf_len = 256;
  M5Cardputer.Mic.config(mic_cfg);
  if (!M5Cardputer.Mic.begin()) {
    M5Cardputer.Display.fillRoundRect(8, 24, 224, 72, 7, kPanel);
    M5Cardputer.Display.setTextDatum(middle_center);
    M5Cardputer.Display.setFont(&fonts::Font4);
    M5Cardputer.Display.setTextColor(TFT_RED, kPanel);
    M5Cardputer.Display.drawString("MIC ERROR", 120, 60);
    return;
  }

  M5Cardputer.Mic.record(g_capture_blocks[0], kBlockSize, kSampleRate);
  M5Cardputer.Mic.record(g_capture_blocks[1], kBlockSize, kSampleRate);
  g_submit_block = 2;
  g_process_block = 0;
}

void loop() {
  M5Cardputer.update();
  handleSensitivityKeys();
  if (!M5Cardputer.Mic.isRunning()) {
    delay(20);
    return;
  }

  if (M5Cardputer.Mic.isRecording() < 2) {
    appendSamples(g_capture_blocks[g_process_block]);
    g_process_block = (g_process_block + 1) % 3;

    if (g_window_fill == PitchDetector::kWindowSize) {
      updateDisplay(g_detector.process(g_window, PitchDetector::kWindowSize), millis());
    }

    if (M5Cardputer.Mic.record(g_capture_blocks[g_submit_block], kBlockSize, kSampleRate)) {
      g_submit_block = (g_submit_block + 1) % 3;
    }
  }
  delay(1);
}
