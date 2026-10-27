# Parametric Fuzz

A circuit-level model of a Maestro MPF-style parametric distortion filter, built as an
Audio Unit / VST3 plugin for macOS.

## Signal path (modeled stage by stage)

- **Input**: 1M / 33n coupling into a ~3.5 V bias (9 V supply through a 1N5817).
- **Clean**: TL072 buffer, gain 1 + 6.2k/10k.
- **Dirty**: TL072 gain stage (1x–38x, A100k), CD4007 P-channel source follower,
  CD4007 P-channel common-source stage with 4M7 feedback, 1N4148 pair into 2u2,
  22k/5k6 output divider. Solved per sample with Newton's method at 4x oversampling.
- **Parametric**: Moog cut/boost (2k2 / B10k / 2k2) feeding a 5-step bandwidth
  switch and a dual-op-amp resonator tuned by a dual C100k pot. Solved exactly as a
  linear circuit (modified nodal analysis, trapezoidal integration).
- **Output**: 4k7/33n low-pass, 68k/33k divider, TL072 makeup stage whose gain is set
  by the second gang of the Height pot, then 10k / 10u / 100k.
- TL072 output swing limits are modeled on every op-amp stage.

Levels: a sample value of 1.0 equals 1 volt at the jacks. Output Trim is outside the circuit.

## Building

Pushing to `main` builds a universal (Apple Silicon + Intel) AU, VST3 and Standalone
on GitHub Actions, validates the AU with `auval`, and publishes them on the
[latest release](../../releases/tag/latest).

## Tests

`tests/test_response.cpp` measures the model's small-signal response for comparison
against an independent AC circuit simulation; `tests/render_demo.cpp` renders demo audio.

```
g++ -O2 -std=c++17 -o test_response tests/test_response.cpp
./test_response 1.0 0.5 3 100 760 2000
```
