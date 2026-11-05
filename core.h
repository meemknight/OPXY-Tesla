#pragma once
#include <opxy/audio.hpp>

namespace tesla {
// Every voice owns its two oscillator phases, a deterministic noise stream,
// one filter delay and a fixed ensemble buffer. Eight voices use 16,704 bytes.
struct Voice {
    opxy::u32 carrier_phase;
    opxy::u32 modulator_phase;
    opxy::u32 noise;
    opxy::i32 filter;
    opxy::u32 crush_clock;
    opxy::i32 crushed;
    opxy::i32 crush_smooth;
    opxy::u32 lfo_phase;
    opxy::u32 chorus_phase;
    opxy::u32 chorus_index;
    // A short fixed delay makes Spread an ensemble effect, not another
    // oscillator detune. It is owned by the voice and never allocated.
    opxy::i32 chorus[512];
};
// State shared by every voice; empty when voices share nothing.
struct Shared {};
}
