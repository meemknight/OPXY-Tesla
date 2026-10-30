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
    render_once(clean, pitch, clean_output);
    render_once(saw_stack, pitch, saw_output);
    render_once(square_arc, pitch, square_output);
    render_once(low_pass, pitch, low_pass_output);
    render_once(noisy, pitch, noisy_output);
    render_once(crushed, pitch, crushed_output);
    render_blocks(saw_stack, pitch, dry_long_output, 16);
    render_blocks(ensemble, pitch, spread_output, 16);
    render_once(lfo_motion, pitch, lfo_output);
    const bool saw_changes_shape = differs(clean_output, saw_output);
    const bool square_changes_shape = differs(saw_output, square_output);
    const bool filter_changes_sound = differs(low_pass_output, saw_output);
    const bool noise_changes_sound = differs(noisy_output, saw_output);
    const bool crush_changes_sound = differs(saw_output, crushed_output);
    const bool spread_changes_sound = differs(dry_long_output, spread_output);
    const bool lfo_changes_sound = differs(saw_output, lfo_output);
    const bool hard_sounds_bounded = inside_peak(saw_output) && inside_peak(square_output)
        && inside_peak(low_pass_output) && inside_peak(noisy_output) && inside_peak(crushed_output)
        && inside_peak(spread_output) && inside_peak(lfo_output);
    const bool tone_zones = tesla::detail::tone_filter_mix(0) == 0
        && tesla::detail::tone_filter_mix(tesla::detail::tone_third) == opxy::q31::maximum
        && tesla::detail::tone_noise_amount(0) == 0
        && tesla::detail::tone_noise_amount(tesla::detail::tone_two_thirds) == 0
        && tesla::detail::tone_noise_amount(opxy::q31::maximum) > 0;
    const bool unison = tesla::detail::modulator_increment(42852281u, 0) == 42852281u;
    const bool octave_down = tesla::detail::modulator_increment(42852281u, 0x7fffffff)
        == 21426140u;
    if (!sounding) std::puts("FAIL silent");
    if (!bounded) std::puts("FAIL above the declared peak");
    if (!saw_changes_shape || !square_changes_shape) std::puts("FAIL waveform shapes do not differ");
    if (!filter_changes_sound || !noise_changes_sound) std::puts("FAIL filter zones do not differ");
    if (!crush_changes_sound) std::puts("FAIL bit crush does not change the signal");
    if (!spread_changes_sound) std::puts("FAIL spread does not change the signal");
    if (!lfo_changes_sound) std::puts("FAIL LFO does not change the signal");
    if (!hard_sounds_bounded) std::puts("FAIL high-character settings exceed the declared peak");
    if (!tone_zones) std::puts("FAIL filter zones do not open, add then remove noise");
    if (!unison || !octave_down) std::puts("FAIL detune range");
    return sounding && bounded && saw_changes_shape && square_changes_shape && filter_changes_sound
        && noise_changes_sound && crush_changes_sound && spread_changes_sound && lfo_changes_sound
        && hard_sounds_bounded && tone_zones
        && unison && octave_down ? 0 : 1;
}
