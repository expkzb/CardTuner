#include "note.h"

#include <cmath>

namespace {
constexpr const char* kFlatNoteNames[12] = {
    "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B",
};
}

NoteInfo noteFromFrequency(float frequency_hz, float concert_a_hz) {
  NoteInfo result;
  if (frequency_hz <= 0.0f || concert_a_hz <= 0.0f) {
    return result;
  }

  const float exact_midi =
      69.0f + 12.0f * std::log2(frequency_hz / concert_a_hz);
  const int midi = static_cast<int>(std::lround(exact_midi));
  if (midi < 0 || midi > 127) {
    return result;
  }

  const int note_index = (midi % 12 + 12) % 12;
  result.name = kFlatNoteNames[note_index];
  result.midi = midi;
  result.octave = midi / 12 - 1;
  result.cents = (exact_midi - static_cast<float>(midi)) * 100.0f;
  result.valid = true;
  return result;
}

