#pragma once

struct NoteInfo {
  const char* name = "--";
  int midi = 0;
  int octave = 0;
  float cents = 0.0f;
  bool valid = false;
};

NoteInfo noteFromFrequency(float frequency_hz, float concert_a_hz = 440.0f);

