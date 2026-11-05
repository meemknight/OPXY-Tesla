// The machine's own sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>. Extend them as the sound grows.
#include "../engine.cpp"
#include <cstdio>

static void render_once(const opxy::i32* parameters, const opxy::u32* pitch,
                        opxy::i32* output) {
    tesla::Voice voice;
    tesla::Shared shared;
    tesla::initialize(voice, shared, 1);
    tesla::start(voice, shared, 1);
    tesla::render(voice, shared, 1, output, parameters, pitch);
}

static void render_blocks(const opxy::i32* parameters, const opxy::u32* pitch,
                          opxy::i32* output, unsigned blocks) {
    tesla::Voice voice;
    tesla::Shared shared;
    tesla::initialize(voice, shared, 1);
    tesla::start(voice, shared, 1);
    for (unsigned block = 0; block < blocks; ++block)
        tesla::render(voice, shared, 1, output, parameters, pitch);
}

static bool differs(const opxy::i32* first, const opxy::i32* second) {
    for (unsigned i = 0; i < opxy::block_frames; ++i)
        if (first[i] != second[i]) return true;
    return false;
}

static bool inside_peak(const opxy::i32* signal) {
    for (unsigned i = 0; i < opxy::block_frames; ++i)
        if (signal[i] < -2147483647 || signal[i] > 2147483647) return false;
    return true;
}

int main() {
    static tesla::Voice voice;
    static tesla::Shared shared;
    const opxy::i32 clean[8] = {0, 0, 0x7fffffff, 0, 0, 0, 0, 0};
    const opxy::i32 saw_stack[8] = {0x40000000, 0x40000000, 0x7fffffff,
                                     0x051eb852, 0, 0, 0, 0};
    const opxy::i32 square_arc[8] = {0x7fffffff, 0x7fffffff, 0x7fffffff,
                                      0x0369d037, 0, 0, 0, 0};
    const opxy::i32 low_pass[8] = {0x40000000, 0x40000000, 0,
                                    0x051eb852, 0, 0, 0, 0};
    const opxy::i32 noisy[8] = {0x40000000, 0x40000000, 0x6aaaaaaa,
                                 0x051eb852, 0, 0, 0, 0};
    const opxy::i32 crushed[8] = {0x40000000, 0x40000000, 0x7fffffff,
                                   0x051eb852, 0x7fffffff, 0, 0, 0};
    const opxy::i32 ensemble[8] = {0x40000000, 0x40000000, 0x7fffffff,
                                    0x051eb852, 0, 0x60000000, 0, 0};
    const opxy::i32 lfo_motion[8] = {0x40000000, 0x40000000, 0x7fffffff,
                                      0x051eb852, 0, 0, 0x20000000, 0x7fffffff};
    const opxy::i32 lfo_stopped[8] = {0x40000000, 0x40000000, 0x7fffffff,
                                       0x051eb852, 0, 0, 0, 0x7fffffff};
    const opxy::i32 worst_case[8] = {0x7fffffff, 0x7fffffff, 0x7fffffff,
                                      0x7fffffff, 0, 0x7fffffff, 0x7fffffff, 0x7fffffff};
    opxy::u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = 42852281u;  // 440 Hz at 44.1 kHz
    opxy::i32 output[opxy::block_frames];
    tesla::initialize(voice, shared, 1);
    tesla::start(voice, shared, 1);
    bool sounding = false, bounded = true;
    for (unsigned block = 0; block < 690; ++block) {  // one second
        tesla::render(voice, shared, 1, output, clean, pitch);
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            sounding = sounding || output[i] != 0;
            // definition.json declares outputPeakQ31: 2147483647.
            bounded = bounded && output[i] >= -2147483647 && output[i] <= 2147483647;
        }
    }
    opxy::i32 clean_output[opxy::block_frames];
    opxy::i32 saw_output[opxy::block_frames];
    opxy::i32 square_output[opxy::block_frames];
    opxy::i32 low_pass_output[opxy::block_frames];
    opxy::i32 noisy_output[opxy::block_frames];
    opxy::i32 crushed_output[opxy::block_frames];
    opxy::i32 dry_long_output[opxy::block_frames];
    opxy::i32 spread_output[opxy::block_frames];
    opxy::i32 lfo_output[opxy::block_frames];
    opxy::i32 lfo_stopped_output[opxy::block_frames];
    opxy::i32 worst_case_output[opxy::block_frames];
    render_once(clean, pitch, clean_output);
    render_once(saw_stack, pitch, saw_output);
    render_once(square_arc, pitch, square_output);
    render_once(low_pass, pitch, low_pass_output);
    render_once(noisy, pitch, noisy_output);
    render_once(crushed, pitch, crushed_output);
    render_blocks(saw_stack, pitch, dry_long_output, 16);
    render_blocks(ensemble, pitch, spread_output, 16);
    render_once(lfo_motion, pitch, lfo_output);
    render_once(lfo_stopped, pitch, lfo_stopped_output);
    render_blocks(worst_case, pitch, worst_case_output, 64);
    const bool saw_changes_shape = differs(clean_output, saw_output);
    const bool square_changes_shape = differs(saw_output, square_output);
    const bool filter_changes_sound = differs(low_pass_output, saw_output);
    const bool noise_changes_sound = differs(noisy_output, saw_output);
    const bool crush_changes_sound = differs(saw_output, crushed_output);
    const bool spread_changes_sound = differs(dry_long_output, spread_output);
    const bool lfo_changes_sound = differs(saw_output, lfo_output);
    const bool stopped_lfo_is_neutral = !differs(saw_output, lfo_stopped_output);
    const bool hard_sounds_bounded = inside_peak(saw_output) && inside_peak(square_output)
        && inside_peak(low_pass_output) && inside_peak(noisy_output) && inside_peak(crushed_output)
        && inside_peak(spread_output) && inside_peak(lfo_output) && inside_peak(worst_case_output);
    const bool tone_zones = tesla::detail::tone_filter_mix(0) == 0
        && tesla::detail::tone_filter_mix(tesla::detail::tone_third) == opxy::q31::maximum
        && tesla::detail::tone_noise_amount(0) == 0
        && tesla::detail::tone_noise_amount(tesla::detail::tone_two_thirds) == 0
        && tesla::detail::tone_noise_amount(opxy::q31::maximum) > 0;
    const bool unison = tesla::detail::modulator_increment(42852281u, 0) == 42852281u;
    const bool octave_down = tesla::detail::modulator_increment(42852281u, 0x7fffffff)
        == 21426140u;
    // A mix of steady notes, a one-sample change and a glide must advance
    // both oscillators exactly as the original per-sample calculation did.
    tesla::Voice pitch_voice;
    tesla::initialize(pitch_voice, shared, 1);
    tesla::start(pitch_voice, shared, 1);
    opxy::u32 varying_pitch[opxy::block_frames];
    opxy::u32 expected_carrier = 0, expected_modulator = 0;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        varying_pitch[i] = i < 16 ? 42852281u : (i < 32 ? 50000000u : 50000000u + i * 1234u);
        expected_carrier += varying_pitch[i];
        expected_modulator += tesla::detail::modulator_increment(varying_pitch[i], saw_stack[3]);
    }
    tesla::render(pitch_voice, shared, 1, output, saw_stack, varying_pitch);
    const bool pitch_is_sample_accurate = pitch_voice.carrier_phase == expected_carrier
        && pitch_voice.modulator_phase == expected_modulator;
    const bool detune_tames_lfo = tesla::detail::detune_lfo_depth(opxy::q31::maximum, 0)
        > 0x70000000
        && tesla::detail::detune_lfo_depth(opxy::q31::maximum, opxy::q31::maximum)
            < 0x40000000;
    const bool saturating_mix = tesla::detail::saturating_add(opxy::q31::maximum, 1)
        == opxy::q31::maximum && tesla::detail::saturating_add(-opxy::q31::maximum, -1)
        == -opxy::q31::maximum;
    const opxy::i32 edge_values[] = {-2147483647 - 1, -2147483647, -1073741824, -1,
                                     0, 1, 1073741824, 2147483647};
    bool spread_saturation_matches = true;
    for (unsigned a = 0; a < 8; ++a) for (unsigned b = 0; b < 8; ++b) {
        const opxy::i32 sum = opxy::q31::add(edge_values[a], edge_values[b]);
        const opxy::i32 bounded = sum < -opxy::q31::maximum ? -opxy::q31::maximum : sum;
        spread_saturation_matches = spread_saturation_matches
            && bounded == tesla::detail::saturating_add(edge_values[a], edge_values[b]);
    }
    bool crush_symmetric = true, crush_continuous = true, crush_level_bounded = true;
    bool crush_quiet_signal_audible = true;
    for (unsigned step = 1; step <= 16; ++step) {
        tesla::Voice quiet_voice = {};
        const opxy::i32 amount = static_cast<opxy::i32>((static_cast<opxy::i64>(opxy::q31::maximum) * step) / 16);
        const tesla::detail::CrushSettings setting = tesla::detail::crush_settings(amount);
        opxy::i32 result = 0;
        for (unsigned sample = 0; sample < 64; ++sample)
            result = tesla::detail::bit_crush(quiet_voice, 0x10000000, setting);
        crush_quiet_signal_audible = crush_quiet_signal_audible && result >= 0x07000000;
    }
    const opxy::i32 boundaries[] = {0x40000000, 0x60000000};
    for (unsigned b = 0; b < 2; ++b) {
        for (opxy::i32 sample = 0x02000000; sample < 0x70000000; sample += 0x02000000) {
            tesla::Voice plus = {}, minus = {}, before = {}, after = {};
            const tesla::detail::CrushSettings setting = tesla::detail::crush_settings(boundaries[b]);
            const opxy::i32 positive = tesla::detail::bit_crush(plus, sample, setting);
            const opxy::i32 negative = tesla::detail::bit_crush(minus, -sample, setting);
            crush_symmetric = crush_symmetric && positive + negative >= -4 && positive + negative <= 4;
            const opxy::i64 upper = static_cast<opxy::i64>(sample) * 5 / 8 + 4;
            crush_level_bounded = crush_level_bounded && positive <= upper && negative >= -upper;
            const opxy::i32 a = tesla::detail::bit_crush(before, sample,
                tesla::detail::crush_settings(boundaries[b] - 0x10000));
            const opxy::i32 z = tesla::detail::bit_crush(after, sample,
                tesla::detail::crush_settings(boundaries[b] + 0x10000));
            const opxy::i64 difference = static_cast<opxy::i64>(a) - z;
            crush_continuous = crush_continuous && difference > -0x100000 && difference < 0x100000;
        }
    }
    if (!sounding) std::puts("FAIL silent");
    if (!bounded) std::puts("FAIL above the declared peak");
    if (!saw_changes_shape || !square_changes_shape) std::puts("FAIL waveform shapes do not differ");
    if (!filter_changes_sound || !noise_changes_sound) std::puts("FAIL filter zones do not differ");
    if (!crush_changes_sound) std::puts("FAIL bit crush does not change the signal");
    if (!spread_changes_sound) std::puts("FAIL spread does not change the signal");
    if (!lfo_changes_sound) std::puts("FAIL LFO does not change the signal");
    if (!stopped_lfo_is_neutral) std::puts("FAIL Rate 0 LFO changes the sound");
    if (!hard_sounds_bounded) std::puts("FAIL high-character settings exceed the declared peak");
    if (!tone_zones) std::puts("FAIL filter zones do not open, add then remove noise");
    if (!unison || !octave_down) std::puts("FAIL detune range");
    if (!pitch_is_sample_accurate) std::puts("FAIL changing pitch phase accuracy");
    if (!detune_tames_lfo) std::puts("FAIL detune does not tame LFO depth");
    if (!saturating_mix) std::puts("FAIL saturating mix");
    if (!spread_saturation_matches) std::puts("FAIL spread mixer saturation equivalence");
    if (!crush_symmetric || !crush_continuous || !crush_level_bounded)
        std::puts("FAIL crush symmetry, control continuity or level");
    if (!crush_quiet_signal_audible) std::puts("FAIL crush silences quiet input");
    return sounding && bounded && saw_changes_shape && square_changes_shape && filter_changes_sound
        && noise_changes_sound && crush_changes_sound && spread_changes_sound && lfo_changes_sound
        && stopped_lfo_is_neutral
        && hard_sounds_bounded && tone_zones
        && unison && octave_down && pitch_is_sample_accurate && detune_tames_lfo && saturating_mix
        && spread_saturation_matches
        && crush_symmetric && crush_continuous && crush_level_bounded
        && crush_quiet_signal_audible ? 0 : 1;
}
