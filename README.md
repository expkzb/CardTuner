# CardTuner

CardTuner is a standalone real-time monophonic pitch detector for the M5Stack
Cardputer-Adv. It uses the built-in microphone and shows a large note name,
tuning offset, and measured frequency on the 240x135 display.

## Behavior

- Detection range: C1-C7 (about 32.7-2093 Hz)
- Reference tuning: A4 = 440 Hz
- Flat note names: `C Db D Eb E F Gb G Ab A Bb B`
- Monophonic input only
- Weak or unreliable input is shown as `LISTENING` or `LOW SIGNAL`
- Up/down changes sensitivity from level 1 to 5 and remembers it after restart.

On keyboards that do not report arrow HID keys, `;` increases sensitivity and
`.` decreases it.

The detector uses 16 kHz mono input, a 2048-sample sliding window, YIN pitch
detection, parabolic period interpolation, and short median smoothing.

## Build

From this directory:

```powershell
python -m platformio run
```

Flash to a connected Cardputer-Adv:

```powershell
python -m platformio run --target upload --upload-port COM6
```

Replace `COM6` with the device port.

## Tests

The native tests generate reference tones and verify pitch accuracy, harmonic
handling, note naming, cents direction, silence rejection, and noise rejection:

```powershell
python -m platformio test -e native
```

## Limitations

This first version targets stable sustained notes. Speech, chords, multiple
simultaneous instruments, heavy background noise, and strong transients are not
expected to produce reliable results.
