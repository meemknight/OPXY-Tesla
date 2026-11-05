# Tesla

An efficient, clean-room Open-XY recreation of the behavior of the original
OP-1 Tesla synth engine. It is not affiliated with or endorsed by Teenage
Engineering. It was designed from public manuals, demonstrations and user
analysis, not from proprietary implementation code.

Tesla combines two lookup-table oscillators with a fixed-memory AM path:

- **Ampere** moves the primary oscillator from sine, through saw, to square while increasing a modest AM edge.
- **Ground** moves the secondary oscillator through the same sine → saw → square path. With both at the saw midpoint and a small Detune, they form a compact, supersaw-like pair.
- **Filter** is divided into three continuous regions: 0–33% opens a low-pass filter; 33–66% is raw and noise-free; 66–100% progressively adds noise while leaving the oscillator harmonics unfiltered. The default, 50%, is in the raw, noise-free region.
- **Detune** sweeps the second oscillator smoothly from unison down one octave.
- **Crush** (parameter 5, directly below Ampere on the second encoder page; default 0) adds rate reduction and bit-depth loss for coarse, rain-like textures.
- **Spread** (parameter 6) is a short, slowly moving additive ensemble delay. It broadens a mono sound without adding another stable detune, retains the dry level, and grows to a stronger one-third wet layer at 100%; use the track mixer for actual stereo placement.
- **LFO Rate** and **LFO Amt** (parameters 7–8) currently move Ampere at block rate; Amount 0 is inert, smaller turns are intentionally more audible, and the curve lifts further near the top for a 25% deeper maximum sweep. The cubic rate curve is very slow near 0 and reaches roughly 12 Hz at 100%. A local disabled compile-time constant in `engine.cpp` retains the clean Filter sweep for future experimentation; it never changes Filter noise.

The display uses only OP-XY’s stock grayscale steps: Ampere is the dark left current (`#646464`), Ground is the light right current (`#CDCDCD`), and both visibly morph from sine through saw to square while joining at the centre as one continuous electrical arc. The left coil uses a near-black `#000000` body with solid `#646464` crown pieces; the right uses a dark `#646464` body with solid `#CDCDCD` crown pieces. A dim secondary Ground current runs beneath them: Detune drops it lower and slows its visual cycle toward octave-down. Spread widens only the main current ribbons by increasing the gap between their two trace edges. Crush makes the animated current visibly stepped. Filter sets the whole towers’ shared spacing; LFO Rate fans out only the five light left windings, and LFO Amount does the same for the right windings.

The audio callback uses no allocation, I/O, floating point or platform-specific
code. Each voice is 2,084 bytes, including a fixed 512-sample ensemble buffer, and the sine lookup table is shared read-only data.

## Develop

Keep the [SDK](https://github.com/etcetc000/open-xy) beside this repository as
`../open-xy`, on `staging`; adjust the commands if yours is elsewhere. The
[first-machine guide](https://github.com/etcetc000/open-xy/blob/staging/docs/first-machine.md)
covers prerequisites. Read the
[core machines](https://github.com/etcetc000/open-xy-core-machines) as worked
examples and the [machine API](https://github.com/etcetc000/open-xy/blob/staging/docs/api.md)
for the supported interfaces. Follow the
[UX conventions](https://github.com/etcetc000/open-xy/blob/staging/docs/ux.md);
Macro Osc is the two-page reference. Synth starters with five to eight controls
use the [shared header](https://github.com/etcetc000/open-xy/blob/staging/docs/display.md#two-page-header):
keep its SVG block and update the labels/value captions with your controls.
Columns without alternates repeat the same saved parameter on both rows.
Agent instructions are in [AGENTS.md](AGENTS.md).

Build and audition the synth in the browser:

```sh
python ../open-xy/sdk.py preview --machine . --out ../previews/tesla-001
python ../open-xy/tools/serve_harness.py --preview ../previews/tesla-001
```

```sh
python ../open-xy/sdk.py test --machine . --cxx clang++ --host-cxx g++
```

Extend `tests/` as the machine's behavior grows. The starter is a minimal
implementation, not a finished instrument. Keep generated output outside this
repository. The generated workflow checks source, sound tests and synth previews
on Linux, and compiles the native module on Windows; it never uses a device.
Its `OPEN_XY_REF` starts at `staging`; pin a compatible SDK commit for a release.

## Hardware and publication

Follow the SDK's [firmware build guide](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md)
and [hardware profiling workflow](https://github.com/etcetc000/open-xy/blob/staging/docs/profiling.md).
Keep firmware and stock content outside every checkout. Commit the machine,
build with profiling enabled, install when authorized and collect three runs.
The host estimate printed by `test` cannot substitute for those measurements.

### Hardware CPU measurement

On OP-XY OS 1.2.1, the SDK's `opxy-synth-c8-sweep-v1` diagnostic measured
Tesla commit `299017eeca2bb2cf81480c20937bc92cc6a7af46`. The median of three
complete captures was **3.282216%** of the stock audio block budget at its
heaviest setting (Spread 75%), below the recommended 3.5%. The midpoint was
2.6215%, and the combined heaviest-control run was 2.796979%.

This is a synthetic whole-voice test, including oscillator and filter; it is
not a measurement of a full project, graphics, or maximum polyphony. Normal
playing and the machine's device checklist still need separate validation.

For [catalog submission](https://github.com/etcetc000/open-xy/blob/staging/docs/catalog.md#submit-a-machine),
the SDK-generated `cpu` record for the measured revision is:

```json
{
  "benchmark": "opxy-synth-c8-sweep-v1",
  "commit": "299017eeca2bb2cf81480c20937bc92cc6a7af46",
  "firmware_sha256": "68e3ecf9391d14f9d9850c7d1d77f8f6eab878e7ce20fb7f241b5e4755125bc2",
  "runs": 3,
  "block_percent": 3.282216,
  "midpoint_percent": 2.6215,
  "combined_percent": 2.796979,
  "worst_setting": {"control": 5, "label": "Spread", "value": 1610612735}
}
```

The catalog must pin the measured commit. Committing this README creates a new
commit and does not automatically transfer this record to it; remeasure the
new revision or request a maintainer-approved CPU carry for a change that
cannot affect performance. Keep raw captures and firmware private; publish the
generated report separately if a public evidence link is needed.

The generated starter is covered by [LICENSE](LICENSE). Preserve notices for
code you reuse and include the licences of any dependencies or ports.
