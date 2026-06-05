#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <random>

#include "note.h"
#include "pitch_detector.h"

namespace {
constexpr float kSampleRate = 16000.0f;
constexpr float kPi = 3.14159265358979323846f;

void makeTone(int16_t* samples, float frequency, float amplitude = 9000.0f,
              float harmonic = 0.0f) {
  for (std::size_t i = 0; i < PitchDetector::kWindowSize; ++i) {
    const float phase = 2.0f * kPi * frequency * static_cast<float>(i) / kSampleRate;
    const float value = amplitude * std::sin(phase) +
                        amplitude * harmonic * std::sin(2.0f * phase);
    samples[i] = static_cast<int16_t>(std::clamp(value, -32767.0f, 32767.0f));
  }
}

void assertPitch(float expected_hz, float harmonic = 0.0f) {
  int16_t samples[PitchDetector::kWindowSize];
  makeTone(samples, expected_hz, 9000.0f, harmonic);
  PitchDetector detector(kSampleRate);
  const PitchResult result = detector.process(samples, PitchDetector::kWindowSize);
  if (!result.valid) {
    std::printf("Rejected reference tone %.3f Hz (detected %.3f Hz, confidence %.3f, RMS %.1f)\n",
                expected_hz, result.frequency_hz, result.confidence, result.rms);
  }
  TEST_ASSERT_TRUE(result.valid);
  const float cents = 1200.0f * std::log2(result.frequency_hz / expected_hz);
  TEST_ASSERT_FLOAT_WITHIN(5.0f, 0.0f, cents);
}

void test_reference_notes() {
  assertPitch(32.703f);
  assertPitch(65.406f);
  assertPitch(110.0f);
  assertPitch(440.0f);
  assertPitch(622.254f);
  assertPitch(2093.005f);
}

void test_sensitivity_threshold() {
  int16_t samples[PitchDetector::kWindowSize];
  makeTone(samples, 220.0f, 120.0f);
  PitchDetector detector(kSampleRate);
  detector.setRmsThreshold(100.0f);
  TEST_ASSERT_FALSE(detector.process(samples, PitchDetector::kWindowSize).valid);
  detector.setRmsThreshold(60.0f);
  TEST_ASSERT_TRUE(detector.process(samples, PitchDetector::kWindowSize).valid);
}

void test_harmonic_tone_tracks_fundamental() {
  assertPitch(196.0f, 0.65f);
}

void test_silence_is_rejected() {
  int16_t samples[PitchDetector::kWindowSize] = {};
  PitchDetector detector(kSampleRate);
  TEST_ASSERT_FALSE(detector.process(samples, PitchDetector::kWindowSize).valid);
}

void test_noise_is_rejected() {
  int16_t samples[PitchDetector::kWindowSize];
  std::mt19937 generator(12345);
  std::uniform_int_distribution<int> distribution(-3000, 3000);
  for (auto& sample : samples) {
    sample = static_cast<int16_t>(distribution(generator));
  }
  PitchDetector detector(kSampleRate);
  TEST_ASSERT_FALSE(detector.process(samples, PitchDetector::kWindowSize).valid);
}

void test_flat_note_names_and_cents() {
  const NoteInfo eb = noteFromFrequency(311.127f);
  TEST_ASSERT_TRUE(eb.valid);
  TEST_ASSERT_EQUAL_STRING("Eb", eb.name);
  TEST_ASSERT_EQUAL(4, eb.octave);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, eb.cents);

  const NoteInfo sharp = noteFromFrequency(445.0f);
  TEST_ASSERT_EQUAL_STRING("A", sharp.name);
  TEST_ASSERT_TRUE(sharp.cents > 0.0f);

  const NoteInfo flat = noteFromFrequency(435.0f);
  TEST_ASSERT_EQUAL_STRING("A", flat.name);
  TEST_ASSERT_TRUE(flat.cents < 0.0f);
}
}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_reference_notes);
  RUN_TEST(test_harmonic_tone_tracks_fundamental);
  RUN_TEST(test_sensitivity_threshold);
  RUN_TEST(test_silence_is_rejected);
  RUN_TEST(test_noise_is_rejected);
  RUN_TEST(test_flat_note_names_and_cents);
  return UNITY_END();
}
