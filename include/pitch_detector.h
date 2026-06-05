#pragma once

#include <cstddef>
#include <cstdint>

struct PitchResult {
  float frequency_hz = 0.0f;
  float confidence = 0.0f;
  float rms = 0.0f;
  bool valid = false;
};

class PitchDetector {
 public:
  static constexpr std::size_t kWindowSize = 2048;

  explicit PitchDetector(float sample_rate_hz = 16000.0f);
  void setRmsThreshold(float threshold);
  PitchResult process(const int16_t* samples, std::size_t count);

 private:
  static constexpr std::size_t kMaxLag = 490;

  float sample_rate_hz_;
  float rms_threshold_;
  float centered_[kWindowSize];
  float yin_[kMaxLag + 1];
};
