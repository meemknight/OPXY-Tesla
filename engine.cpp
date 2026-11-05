#include "core.h"
#include <opxy/q31.hpp>

namespace tesla {
namespace detail {
// Local experiment switch. False leaves the LFO on Ampere only; change to
// true to restore its clean low-pass Filter sweep without changing noise.
const bool lfo_filter_enabled = false;
// A 65-entry quarter-wave lookup gives an inexpensive table-based sine source
// without floating point, allocations or a dependency on a platform math ABI.
static const opxy::i16 sine_q15[65] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962,
    8739, 9512, 10278, 11039, 11793, 12539, 13279, 14010, 14732,
    15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787,
    21403, 22005, 22594, 23170, 23731, 24279, 24811, 25329, 25832,
    26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621,
    29956, 30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757, 32767,
};

inline opxy::i32 sine(opxy::u32 phase) {
    opxy::u32 index = (phase >> 24) & 63u;
    if (phase & 0x40000000u) index = 64u - index;
    const opxy::i32 sample = static_cast<opxy::i32>(sine_q15[index]) << 16;
    return phase & 0x80000000u ? -sample : sample;
}

inline opxy::i32 crossfade(opxy::i32 dry, opxy::i32 wet, opxy::i32 amount) {
    return opxy::q31::multiply(dry, opxy::q31::maximum - amount)
         + opxy::q31::multiply(wet, amount);
}

// Q31 multiplication is saturating, but combining independently bounded
// signals still needs a saturating sum to preserve the declared output peak.
inline opxy::i32 saturating_add(opxy::i32 first, opxy::i32 second) {
    const opxy::i32 sum = opxy::q31::add(first, second);
    return sum < -opxy::q31::maximum ? -opxy::q31::maximum : sum;
}

inline opxy::i32 saw(opxy::u32 phase) {
    // Recenter the unsigned phase ramp without a signed overflow. The result
    // is a full-scale, deliberately non-band-limited saw for a strong edge.
    return (static_cast<opxy::i32>(phase >> 1) - 0x40000000) * 2;
}

inline opxy::i32 square(opxy::u32 phase) {
    return phase & 0x80000000u ? -opxy::q31::maximum : opxy::q31::maximum;
}

// Both oscillator controls crossfade through useful harmonic shapes:
// 0 = sine, midpoint = saw and maximum = square. This leaves two nearby saws
// available at once when Detune is only slightly raised.
struct WaveformSettings { opxy::i32 mix, inverse; bool lower; };
inline WaveformSettings waveform_settings(opxy::i32 shape) {
    const bool lower = shape < 0x40000000;
    const opxy::i32 mix = lower ? shape * 2 : (shape - 0x40000000) * 2;
    const WaveformSettings settings = {mix, opxy::q31::maximum - mix, lower};
    return settings;
}
inline opxy::i32 waveform(const WaveformSettings& settings, opxy::u32 phase) {
    const opxy::i32 saw_wave = saw(phase);
    const opxy::i32 first = settings.lower ? sine(phase) : saw_wave;
    const opxy::i32 second = settings.lower ? saw_wave : square(phase);
    return opxy::q31::multiply(first, settings.inverse) + opxy::q31::multiply(second, settings.mix);
}

const opxy::i32 tone_third = 0x2aaaaaaa;
const opxy::i32 tone_two_thirds = 0x55555554;

inline opxy::i32 scale_tone_zone(opxy::i32 distance) {
    return distance >= tone_third ? opxy::q31::maximum : distance * 3;
}

// Filter is a three-part path. It first opens a low-pass filter, then holds
// the signal raw and noise-free, and finally adds noise to those unfiltered
// oscillator harmonics.
inline opxy::i32 tone_filter_mix(opxy::i32 tone) {
    return tone < tone_third ? scale_tone_zone(tone) : opxy::q31::maximum;
}

inline opxy::i32 tone_noise_amount(opxy::i32 tone) {
    return tone < tone_two_thirds ? 0 : scale_tone_zone(tone - tone_two_thirds);
}

inline opxy::i32 lfo_filter_mix(opxy::i32 base_mix, opxy::i32 lfo_wave,
                                opxy::i32 lfo_amount) {
    if (!lfo_filter_enabled || !lfo_amount) return base_mix;
    const opxy::u32 unipolar = (static_cast<opxy::u32>(lfo_wave) + 0x80000000u) >> 1;
    const opxy::i32 drop = opxy::q31::multiply(static_cast<opxy::i32>(unipolar), lfo_amount);
    return base_mix < drop ? 0 : base_mix - drop;
}

inline opxy::i32 offset_control(opxy::i32 value, opxy::i32 offset) {
    if (offset < 0) return value < -offset ? 0 : value + offset;
    return value > opxy::q31::maximum - offset ? opxy::q31::maximum : value + offset;
}

inline opxy::i32 detune_lfo_depth(opxy::i32 depth, opxy::i32 detune) {
    // The Ampere sweep becomes much more aggressive when the second oscillator
    // is far from unison. Retain the full LFO at unison, tapering to 37.5% at
    // maximum Detune so the modulation stays musical instead of abrasive.
    const opxy::i32 attenuation = opxy::q31::maximum
        - opxy::q31::multiply(detune, 0x50000000);
    return opxy::q31::multiply(depth, attenuation);
}

inline opxy::i32 next_noise(Voice& voice) {
    voice.noise = voice.noise * 1664525u + 1013904223u;
    // Keep within +/- 0.5 so every later crossfade remains safely in Q31.
    return static_cast<opxy::i32>(voice.noise >> 1) - 0x40000000;
}

// The original control sweeps the modulator from unison down one octave.
inline opxy::u32 modulator_increment(opxy::u32 carrier, opxy::i32 detune) {
    // The extra unit compensates for Q31's maximum being one LSB below 1.0,
    // so a fully clockwise Detune lands exactly at the octave-down increment.
    const opxy::i32 octave_span = static_cast<opxy::i32>(carrier - (carrier >> 1) + 1u);
    return carrier - static_cast<opxy::u32>(opxy::q31::multiply(octave_span, detune));
}

struct CrushSettings {
    opxy::i32 wet;
};

inline CrushSettings crush_settings(opxy::i32 amount) {
    CrushSettings settings = {amount};
    return settings;
}

inline opxy::i32 quantize_crush(opxy::u32 magnitude, bool negative) {
    // Round symmetrically to avoid the negative DC offset of signed masking.
    const opxy::u32 quantized = (magnitude + 0x20000000u) & 0xc0000000u;
    const opxy::i32 bounded = quantized > static_cast<opxy::u32>(opxy::q31::maximum)
        ? opxy::q31::maximum : static_cast<opxy::i32>(quantized);
    return negative ? -bounded : bounded;
}

inline opxy::i32 bit_crush(Voice& voice, opxy::i32 signal, const CrushSettings& settings) {
    if (!settings.wet) {
        voice.crush_smooth = signal;
        voice.crushed = 0;
        return signal;
    }
    const bool negative = signal < 0;
    const opxy::u32 magnitude_unsigned = negative
        ? 0u - static_cast<opxy::u32>(signal) : static_cast<opxy::u32>(signal);
    const opxy::i32 stepped = quantize_crush(magnitude_unsigned, negative);
    // Limit the error to 3/8 of the input, protecting quiet waveforms from
    // large changes while leaving room for a pronounced quantization edge.
    const opxy::i64 difference = static_cast<opxy::i64>(stepped) - signal;
    const opxy::i32 magnitude = negative ? static_cast<opxy::i32>(magnitude_unsigned - 1u) : signal;
    const opxy::i32 limit = (magnitude >> 2) + (magnitude >> 3);
    const opxy::i32 bounded_error = difference > limit ? limit : difference < -static_cast<opxy::i64>(limit) ? -limit : static_cast<opxy::i32>(difference);
    const opxy::i32 crushed_signal = saturating_add(signal, opxy::q31::multiply(bounded_error, settings.wet));
    voice.crush_smooth = crushed_signal;
    return voice.crush_smooth;
}

struct SpreadSettings {
    opxy::u32 active, phase_increment;
    opxy::i32 wet;
};

inline SpreadSettings spread_settings(opxy::i32 amount) {
    SpreadSettings settings = {0, 0, 0};
    if (!amount) return settings;
    settings.active = 1;
    settings.phase_increment = 20000u + (static_cast<opxy::u32>(amount) >> 18);
    settings.wet = opxy::q31::multiply(amount, 0x2aaaaaaa);
    return settings;
}

inline opxy::i32 spread(Voice& voice, opxy::i32 signal, const SpreadSettings& settings) {
    const opxy::u32 index = voice.chorus_index;
    voice.chorus[index] = signal;
    voice.chorus_index = (index + 1u) & 511u;
    if (!settings.active) return signal;

    // 4.7–16.2 ms, smoothly swept at roughly a quarter hertz: a broader
    // ensemble impression without the static beating of another detuned
    // oscillator. A triangle selects the tap without a division.
    const opxy::u32 folded = voice.chorus_phase & 0x80000000u
        ? ~voice.chorus_phase : voice.chorus_phase;
    const opxy::u32 delay = 208u + ((folded >> 24) * 4u);
    const opxy::i32 delayed = voice.chorus[(index - delay) & 511u];
    voice.chorus_phase += settings.phase_increment;
    // Keep dry at full level and add a bounded wet layer. A replacing
    // crossfade can comb-cancel the dry source and make Spread sound quieter.
    // Tone peaks at 0.75; the stronger one-third wet gain reaches, but cannot
    // exceed, the Q31 full-scale output at Spread's maximum setting.
    // SDK 1.7 provides native saturating addition on the device. Preserve the
    // engine's symmetric peak limit, including the single negative endpoint.
    const opxy::i32 mixed = opxy::q31::add(signal, opxy::q31::multiply(delayed, settings.wet));
    return mixed < -opxy::q31::maximum ? -opxy::q31::maximum : mixed;
}
}  // namespace detail

// Callbacks run serially per voice; owner is a stable nonzero voice ID.
inline void initialize(Voice& voice, Shared&, opxy::u32 owner) {
    voice.carrier_phase = 0;
    voice.modulator_phase = 0;
    voice.noise = 0xa341316cu + owner * 0x9e3779b9u;
    voice.filter = 0;
    voice.crush_clock = 0;
    voice.crushed = 0;
    voice.crush_smooth = 0;
    voice.lfo_phase = owner * 0x1745d174u;
    voice.chorus_phase = owner * 0x45d9f3bu;
    voice.chorus_index = 0;
    for (opxy::u32 i = 0; i < 512; ++i) voice.chorus[i] = 0;
}

inline void start(Voice& voice, Shared&, opxy::u32) {
    voice.carrier_phase = 0;
    voice.modulator_phase = 0;
    voice.filter = 0;
    voice.crush_clock = 0;
    voice.crushed = 0;
    voice.crush_smooth = 0;
}
inline void destroy(Voice&, Shared&, opxy::u32) {}

// A clean-room Tesla-inspired engine with a twin-oscillator electric core.
// Ampere and Ground sweep useful waveform shapes; Filter supplies a low-pass,
// noise and raw-harmonic path. All parameter/state access is bounded and uses
// Q31 primitives supplied by the SDK.
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    const opxy::i32 ampere = parameters[0];
    const opxy::i32 ground = parameters[1];
    const opxy::i32 filter_tone = parameters[2];
    const opxy::i32 detune = parameters[3];
    const opxy::i32 crush = parameters[4];
    const opxy::i32 spread = parameters[5];
    const opxy::i32 lfo_rate = parameters[6];
    const opxy::i32 lfo_amount = parameters[7];
    // The LFO moves Ampere plus a clean, block-rate low-pass sweep. It never
    // changes the Filter control's noise amount. A cubic rate curve gives the
    // lower part of the control genuinely slow motion while 100% is ~12 Hz.
    const opxy::i32 lfo_square = opxy::q31::multiply(lfo_rate, lfo_rate);
    const opxy::i32 lfo_curve = opxy::q31::multiply(lfo_square, lfo_rate);
    // Rate 0 is a true bypass: Amount must not impose a per-voice static
    // waveform offset (or advance a supposedly stopped LFO).
    const bool lfo_running = lfo_rate != 0;
    const opxy::u32 lfo_increment = lfo_running
        ? 1949u + (static_cast<opxy::u32>(lfo_curve) >> 11)
            + (static_cast<opxy::u32>(lfo_curve) >> 14)
        : 0;
    const opxy::i32 base_filter_mix = detail::tone_filter_mix(filter_tone);
    const opxy::i32 noise_amount = detail::tone_noise_amount(filter_tone);
    const opxy::i32 lfo_wave = lfo_running && lfo_amount
        ? detail::sine(voice.lfo_phase) : 0;
    const opxy::i32 filter_mix = detail::lfo_filter_mix(base_filter_mix, lfo_wave, lfo_amount);
    // Lift low/mid turns without ever passing a value above 1.0 to a Q31
    // operation. Full depth remains one complete, bounded control sweep.
    const opxy::i32 remaining_depth = opxy::q31::maximum - lfo_amount;
    const opxy::i32 lfo_lift = opxy::q31::half(
        opxy::q31::multiply(lfo_amount, remaining_depth));
    const opxy::i32 ampere_span = detail::detune_lfo_depth(lfo_amount + lfo_lift, detune);
    const opxy::i32 animated_ampere = detail::offset_control(ampere,
        opxy::q31::multiply(lfo_wave, ampere_span));
    const opxy::i32 filter_coefficient = 0x02000000
        + opxy::q31::multiply(filter_mix, 0x60000000);
    // Keep the AM component audible without obscuring the saw/square source
    // shapes. The paired oscillators remain the dominant part of the sound.
    const opxy::i32 am_amount = opxy::q31::multiply(animated_ampere, 0x30000000);
    // Up to 25% extra level near either control's sine end, fading out by
    // 50%. Use the animated setting so LFO sweeps receive the same balance.
    const opxy::i32 quieter_shape = animated_ampere < ground ? animated_ampere : ground;
    const opxy::i32 level_boost = quieter_shape < 0x40000000
        ? (0x40000000 - quieter_shape) / 2 : 0;
    const detail::CrushSettings crush_settings = detail::crush_settings(crush);
    const detail::SpreadSettings spread_settings = detail::spread_settings(spread);
    const detail::WaveformSettings primary_wave = detail::waveform_settings(animated_ampere);
    const detail::WaveformSettings secondary_wave = detail::waveform_settings(ground);
    // Pitch may change on any sample. Reuse the detuned increment only while
    // the actual pitch word is unchanged, including within a glide.
    opxy::u32 last_pitch = pitch[0];
    opxy::u32 detuned_pitch = detail::modulator_increment(last_pitch, detune);

    // The raw-harmonics position is a common high-cost patch: avoid evaluating
    // a crossfade that is algebraically identical to `source` on every sample.
    if (filter_mix == opxy::q31::maximum) {
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            const opxy::i32 primary = detail::waveform(primary_wave, voice.carrier_phase);
            const opxy::i32 secondary = detail::waveform(secondary_wave, voice.modulator_phase);
            const opxy::i32 pair = opxy::q31::half(primary) + opxy::q31::half(secondary);
            const opxy::i32 ring_modulated = opxy::q31::multiply(primary, secondary);
            opxy::i32 source = opxy::q31::half(
                detail::crossfade(pair, ring_modulated, am_amount));
            if (level_boost) source += opxy::q31::multiply(source, level_boost);
            voice.filter += opxy::q31::multiply(detail::saturating_add(source, -voice.filter), filter_coefficient);
            opxy::i32 tone = source;
            if (noise_amount)
                tone = detail::saturating_add(
                    tone, opxy::q31::multiply(opxy::q31::half(detail::next_noise(voice)), noise_amount));
            output[i] = detail::bit_crush(
                voice, detail::spread(voice, tone, spread_settings), crush_settings);
            const opxy::u32 current_pitch = pitch[i];
            if (current_pitch != last_pitch) {
                last_pitch = current_pitch;
                detuned_pitch = detail::modulator_increment(current_pitch, detune);
            }
            voice.carrier_phase += current_pitch;
            voice.modulator_phase += detuned_pitch;
        }
    } else {
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            const opxy::i32 primary = detail::waveform(primary_wave, voice.carrier_phase);
            const opxy::i32 secondary = detail::waveform(secondary_wave, voice.modulator_phase);
            // At a small detune, two saws make a compact supersaw-like stack;
            // square settings retain their fundamental because this is additive.
            const opxy::i32 pair = opxy::q31::half(primary) + opxy::q31::half(secondary);
            const opxy::i32 ring_modulated = opxy::q31::multiply(primary, secondary);
            const opxy::i32 tesla_signal = detail::crossfade(pair, ring_modulated, am_amount);
            // Reserve headroom for the optional noise layer and level boost.
            opxy::i32 source = opxy::q31::half(tesla_signal);
            if (level_boost) source += opxy::q31::multiply(source, level_boost);
            voice.filter += opxy::q31::multiply(detail::saturating_add(source, -voice.filter), filter_coefficient);
            opxy::i32 tone = detail::crossfade(voice.filter, source, filter_mix);
            if (noise_amount)
                tone = detail::saturating_add(
                    tone, opxy::q31::multiply(opxy::q31::half(detail::next_noise(voice)), noise_amount));
            output[i] = detail::bit_crush(
                voice, detail::spread(voice, tone, spread_settings), crush_settings);
            const opxy::u32 current_pitch = pitch[i];
            if (current_pitch != last_pitch) {
                last_pitch = current_pitch;
                detuned_pitch = detail::modulator_increment(current_pitch, detune);
            }
            voice.carrier_phase += current_pitch;
            voice.modulator_phase += detuned_pitch;
        }
    }
    voice.lfo_phase += lfo_increment * opxy::block_frames;
}
}
