# Velour: dynamic resonance suppressor

Velour finds harsh, ringing frequency regions as they happen and turns them down only while they're there. The rest of the signal passes through untouched. It builds as an **Audio Unit** for Logic Pro, plus VST3 and a standalone app.

---

## Build it (macOS, one-time setup)

1. **Install Apple's compiler.** Open Terminal and run:
   ```
   xcode-select --install
   ```
   (If you already have Xcode installed, skip this step.)

2. **Install CMake.** The easiest way is with [Homebrew](https://brew.sh):
   ```
   brew install cmake
   ```

3. **Build.** In Terminal, `cd` into this folder and run:
   ```
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release -j 8
   ```
   The first run downloads JUCE (about 1–2 minutes). The finished plugin is
   copied automatically to `~/Library/Audio/Plug-Ins/Components/Velour.component`.

4. **Open Logic.** The plugin shows up under **Audio FX → Velour Audio → Velour**.
   If it doesn't appear, go to **Logic Pro → Settings → Plug-in Manager**, select
   Velour and click **Reset & Rescan Selection**.

### Troubleshooting
- Check that the AU passes Apple's validator: `auval -v aufx Vlr1 Vlra`
- If Logic still doesn't see it, run `killall -9 AudioComponentRegistrar` and then reopen Logic.
- After you change the code, run the `cmake --build` command again. It only takes a few seconds after the first build.

---

## Controls

| Control | What it does |
|---|---|
| **Depth** | How hard it cuts. It's also the maximum cut in dB. Start around 4–8. |
| **Sharpness** | How narrow the detection is. Low values tame broad harsh regions; high values go after single ringing tones or whistles. |
| **Selectivity** | How far a region has to stick out before it gets touched. Raise it so only the worst peaks are cut. |
| **Attack / Release** | How fast the cuts appear and recover. Fast attack catches sibilance; a longer release sounds smoother. |
| **Focus Low / High** | The frequency range it works on, with soft edges. Anything outside is left alone (shaded in the display). |
| **Mix** | Blends the processed and dry signals. Both stay perfectly phase-aligned. |
| **Output** | Makeup gain. |
| **Delta** | Plays *only* what's being removed. Use it to check that you're cutting harshness and not tone. |
| **Stereo Link** | On: both channels get the same cuts, which keeps the stereo image stable. Off: each side is processed on its own. |

The display shows the input spectrum, with the live reduction curve (rose colour) hanging from the top.

### Starting points
- **Harsh vocal:** Depth 6, Sharpness 4, Selectivity 4, Attack 3 ms, Release 60 ms, Focus 1.5k–12k
- **De-essing:** Depth 10, Sharpness 3, Selectivity 3, Attack 1 ms, Release 40 ms, Focus 4k–12k
- **Bright acoustic / strings:** Depth 5, Sharpness 5, Selectivity 5, Focus 1k–8k
- **Cymbals / overheads:** Depth 6, Sharpness 6, Release 100 ms, Focus 3k–16k
- **Mix bus (gentle):** Depth 3, Sharpness 3, Selectivity 6, Mix 60%

### Latency
Velour looks ahead with a 2048-sample analysis window, which is about 43 ms at 48 kHz. Logic compensates for this automatically when you're mixing. Avoid it on a track you're recording through live.

---

## How it works

Everything happens in `Source/dsp/ResonanceSuppressor.h`, which doesn't depend on JUCE:

1. Short-time FFT (2048-point Hann window, 87.5% overlap).
2. **Detail spectrum.** Local energy is measured over a narrow band whose width is set by Sharpness. This merges the individual harmonics of a note, so whole resonant *regions* stand out rather than every partial.
3. **Baseline.** A broad, smooth average of the spectrum, about 1.5 octaves wide.
4. Where the detail spectrum rises above the baseline by more than the Selectivity threshold, the excess is scaled by Depth to give a cut (capped at Depth). The Focus range then shapes it.
5. Each frequency bin's cut is smoothed in time with Attack and Release.
6. The gains are applied, then the signal goes through an inverse FFT and overlap-add. Mix and Delta are done before resynthesis, so wet, dry and delta always null perfectly.

### Tests
`tests/` contains an offline harness that compiles without JUCE:
```
g++ -O2 -std=c++17 -o tests/harness tests/harness.cpp
python3 tests/test_dsp.py      # needs numpy + scipy
```
It checks transparency, latency, the delta/mix nulls, and how much a resonance gets cut compared with untouched broadband content.

---

## Notes
- This is an original implementation of a general technique (dynamic resonance suppression). Don't use another company's product name, logo or UI artwork if you share or sell it.
- JUCE is free for personal use and for small commercial use under its starter licence. Check juce.com for the current terms before selling.
