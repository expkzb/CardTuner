#include "pitch_detector.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kMinFrequencyHz = 32.703f;
constexpr float kMaxFrequencyHz = 2093.005f;
constexpr float kDefaultRmsThreshold = 180.0f;
constexpr float kYinThreshold = 0.18f;
constexpr float kMinimumConfidence = 0.72f;
}  // namespace

PitchDetector::PitchDetector(float sample_rate_hz)
    : sample_rate_hz_(sample_rate_hz),
      rms_threshold_(kDefaultRmsThreshold),
      centered_{},
      yin_{} {}

void PitchDetector::setRmsThreshold(float threshold) {
  rms_threshold_ = std::max(0.0f, threshold);
}

PitchResult PitchDetector::process(const int16_t* samples, std::size_t count) {
  PitchResult result;
  if (samples == nullptr || count != kWindowSize) {
    return result;
  }

  float mean = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    mean += samples[i];
  }
  mean /= static_cast<float>(count);

  float energy = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    centered_[i] = static_cast<float>(samples[i]) - mean;
    energy += centered_[i] * centered_[i];
  }
  result.rms = std::sqrt(energy / static_cast<float>(count));
  if (result.rms < rms_threshold_) {
    return result;
  }

  const std::size_t min_lag = std::max<std::size_t>(
      2, static_cast<std::size_t>(sample_rate_hz_ / kMaxFrequencyHz));
  const std::size_t max_lag = std::min<std::size_t>(
      kMaxLag, static_cast<std::size_t>(std::ceil(sample_rate_hz_ / kMinFrequencyHz)));

  yin_[0] = 1.0f;
  float running_sum = 0.0f;
  for (std::size_t lag = 1; lag <= max_lag; ++lag) {
    float difference = 0.0f;
    const std::size_t limit = count - lag;
    for (std::size_t i = 0; i < limit; ++i) {
      const float delta = centered_[i] - centered_[i + lag];
      difference += delta * delta;
    }
    running_sum += difference;
    yin_[lag] = running_sum > 0.0f
                    ? difference * static_cast<float>(lag) / running_sum
                    : 1.0f;
  }

  std::size_t best_lag = 0;
  for (std::size_t lag = min_lag; lag <= max_lag; ++lag) {
    if (yin_[lag] < kYinThreshold) {
      while (lag + 1 <= max_lag && yin_[lag + 1] < yin_[lag]) {
        ++lag;
      }
      best_lag = lag;
      break;
    }
  }

  if (best_lag == 0) {
    best_lag = min_lag;
    for (std::size_t lag = min_lag + 1; lag <= max_lag; ++lag) {
      if (yin_[lag] < yin_[best_lag]) {
        best_lag = lag;
      }
    }
  }

  result.confidence = std::max(0.0f, std::min(1.0f, 1.0f - yin_[best_lag]));
  if (result.confidence < kMinimumConfidence) {
    return result;
  }

  float refined_lag = static_cast<float>(best_lag);
  if (best_lag > min_lag && best_lag < max_lag) {
    const float left = yin_[best_lag - 1];
    const float center = yin_[best_lag];
    const float right = yin_[best_lag + 1];
    const float denominator = left - 2.0f * center + right;
    if (std::fabs(denominator) > 1e-9f) {
      refined_lag += 0.5f * (left - right) / denominator;
    }
  }

  result.frequency_hz = sample_rate_hz_ / refined_lag;
  constexpr float kBoundaryTolerance = 0.01f;
  result.valid =
      result.frequency_hz >= kMinFrequencyHz * (1.0f - kBoundaryTolerance) &&
      result.frequency_hz <= kMaxFrequencyHz * (1.0f + kBoundaryTolerance);
  return result;
}
