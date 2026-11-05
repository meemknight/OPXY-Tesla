#include "display.h"
#include <opxy/q31.hpp>

namespace tesla {
inline void display_reset(DisplayState& state) { state.phase = 0; state.lfo_phase = 0; state.header.reset(); }

// The display mirrors the oscillator control law: sine → saw → square. The
// waveform scrolls slowly but its shape is always determined by its control.
inline opxy::i32 waveform_y(opxy::i32 phase, opxy::u32 point, opxy::i32 shape,
                            opxy::i32 step, opxy::i32 crush) {
    using namespace opxy::draw;
    opxy::i32 angle = (phase + static_cast<opxy::i32>(point) * step) % 3600;
    if (angle < 0) angle += 3600;
    const opxy::i32 sine_wave = -sine(angle) * 26 / unit;
    const opxy::i32 saw_wave = 26 - angle * 52 / 3600;
    const opxy::i32 square_wave = angle < 1800 ? -26 : 26;
    const opxy::i32 half = 0x40000000;
    opxy::i32 wave;
    if (shape < half) {
        const opxy::i32 mix = shape >> 15;
        wave = sine_wave + (saw_wave - sine_wave) * mix / 32767;
    } else {
        const opxy::i32 mix = (shape - half) >> 15;
        wave = saw_wave + (square_wave - saw_wave) * mix / 32767;
    }
    // Crush gives the moving arc a visible stepped resolution, matching its
    // rate/bit-reduced sound without adding any display resources.
    const opxy::i32 grid = 1 + (crush >> 29);
    return wave / grid * grid;
}

// A thin closed ribbon is reliable on both display renderers.
// `join_y` gives the right half the precise end height of the left half. Only
// its first point is pinned, so Ground keeps its own height and shape rather
// than being shifted outside the compact display frame.
inline opxy::i32 waveform(Frame::path_type& path, opxy::i32 phase, opxy::i32 shape,
                          opxy::i32 thickness, opxy::i32 step, opxy::i32 offset,
                          opxy::i32 crush, opxy::i32 join_y, bool join) {
    using namespace opxy::draw;
    opxy::i32 y[32];
    for (opxy::u32 i = 0; i < 32; ++i) {
        y[i] = waveform_y(phase, i, shape, step, crush) + offset;
    }
    if (join) {
        y[0] = join_y;
    }
    for (opxy::u32 i = 0; i < 32; ++i) {
        path.points[i].x = (-60 + static_cast<opxy::i32>(i) * 4) * unit;
        path.points[i].y = y[i] * unit;
    }
    for (opxy::u32 i = 0; i < 32; ++i) {
        const opxy::u32 source = 31u - i;
        path.points[32u + i].x = (-60 + static_cast<opxy::i32>(source) * 4) * unit;
        path.points[32u + i].y = (y[source] + thickness) * unit;
    }
    // Parameter turns can redraw while reduced motion holds phase still.
    path.key = static_cast<opxy::u32>(phase) * 17u + static_cast<opxy::u32>(shape >> 15) + 1u;
    return y[31];
}

inline opxy::i32 tone_zone_amount(opxy::i32 distance) {
    const opxy::i32 third = 0x2aaaaaaa;
    return distance >= third ? opxy::q31::maximum : distance * 3;
}

inline void tone_caption(char* out, opxy::u32 capacity, opxy::i32 tone) {
    using namespace opxy::draw;
    const opxy::i32 third = 0x2aaaaaaa;
    const opxy::i32 two_thirds = 0x55555554;
    const char* label = "raw";
    opxy::i32 amount = opxy::q31::maximum;
    if (tone < third) {
        label = "lp";
        amount = third - tone;
    } else if (tone >= two_thirds) {
        label = "nz";
        amount = tone - two_thirds;
    } else {
        copy(out, capacity, label);
        return;
    }
    char number[4] = {};
    header_amount(number, sizeof(number), tone_zone_amount(amount));
    copy(out, capacity, label);
    opxy::u32 at = 0;
    while (at + 1 < capacity && out[at]) ++at;
    if (at + 1 < capacity) out[at++] = ' ';
    for (opxy::u32 n = 0; at + 1 < capacity && number[n]; ++n) out[at++] = number[n];
    out[at] = 0;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    const char* const titles[8] = {"Ampere", "Ground", "Filter", "Detune", "Crush", "Spread", "LFO Rate", "LFO Amt"};
    char values[8][header_capacity] = {};
    for (opxy::u32 i = 0; i < 8; ++i)
        header_amount(values[i], header_capacity, input.slots[i]);
    tone_caption(values[2], header_capacity, input.slots[2]);
    const opxy::u8 pages[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    page_header(state.header, input, frame, titles, values, 22, pages);

    const bool held = input.held_notes > 0 && input.held_notes <= 128;
    if (!input.reduced_motion && held)
        state.phase = (state.phase + static_cast<opxy::u32>(clamp(input.elapsed, 0, 102)) * 4u) % 3600u;

    // Follow the engine's cubic rate curve. The display has its own phase;
    // audio LFO phases are per voice and are not available to the UI.
    const opxy::i32 lfo_rate = input.slots[6];
    if (!input.reduced_motion && held && lfo_rate) {
        const opxy::i32 square = opxy::q31::multiply(lfo_rate, lfo_rate);
        const opxy::u32 curve = static_cast<opxy::u32>(opxy::q31::multiply(square, lfo_rate));
        const opxy::u32 increment = 1949u + (curve >> 11) + (curve >> 14);
        // elapsed is Q10 seconds, not milliseconds.
        state.lfo_phase += static_cast<opxy::u32>(static_cast<opxy::i64>(increment)
            * static_cast<opxy::u32>(clamp(input.elapsed, 0, 102)) * 44100u / 1024u);
    }
    const opxy::i32 lfo_angle = static_cast<opxy::i32>((static_cast<opxy::i64>(state.lfo_phase) * 3600u) >> 32);
    const opxy::i32 lfo_level = lfo_rate ? (sine(lfo_angle) + unit) / 2 : 0;
    const opxy::i32 color_motion = (lfo_level * (input.slots[7] >> 21)) / 1024;
    const opxy::u32 left_shade = color_motion >= 320 ? 8 : color_motion >= 64 ? 7 : 1;
    const opxy::u32 right_shade = color_motion >= 320 ? 8 : color_motion >= 64 ? 9 : 2;

    const opxy::i32 phase = static_cast<opxy::i32>(state.phase);
    // The left scope is Ampere and the right scope is Ground. The right edge
    // starts at Ampere's centre endpoint, making one continuous electric arc.
    const opxy::i32 crush = input.slots[4];
    const opxy::i32 spread = input.slots[5];
    // Spread widens the two-edge electrical ribbon itself: at high values the
    // upper and lower trace lines visibly separate, like an ensemble current.
    // 4 px at zero, exactly 24 px at maximum Spread.
    const opxy::i32 primary_thickness = 4 + (spread >> 26) * 20 / 31;
    const opxy::i32 primary_join = waveform(frame.paths[0], phase, input.slots[0],
                                             primary_thickness, 120, 0, crush, 0, false);
    waveform(frame.paths[1], phase, input.slots[1], primary_thickness, 120, 0,
             crush, primary_join, true);
    // Ground's second oscillator sits beneath the main current. Its cycle
    // length halves and its vertical separation increases toward octave-down.
    const opxy::i32 secondary_step = 120 - (input.slots[3] >> 25);
    const opxy::i32 secondary_offset = 8 + (input.slots[3] >> 27);
    const opxy::i32 secondary_thickness = 2;
    const opxy::i32 secondary_join = waveform(frame.paths[2], phase, input.slots[1],
                                               secondary_thickness, secondary_step, secondary_offset,
                                               crush, 0, false);
    waveform(frame.paths[3], phase, input.slots[1], secondary_thickness, secondary_step,
             secondary_offset, crush, secondary_join, true);
    // Blue Ampere and gold Ground meet at the centre with a shared endpoint.
    // Filter also sets their separation: a low setting pulls the coils close;
    // the raw midpoint is neutral; the noisy end sends them further apart.
    // Keep the filter gesture readable without letting the coils dominate the
    // screen: its end-to-end travel is now 60 pixels, centred at raw.
    const opxy::i32 coil_offset = 30 - (input.slots[2] >> 24) * 60 / 127;
    const opxy::i32 arc_span = 124 - coil_offset;
    const opxy::i32 arc_scale = arc_span * unit / 124;
    const opxy::i32 left_arc_x = (116 + coil_offset) * unit + 60 * arc_scale;
    const opxy::i32 right_arc_x = 240 * unit + 60 * arc_scale;
    place(frame, 0, left_arc_x, 90 * unit, arc_scale, left_shade, fill_and_stroke);
    place(frame, 1, right_arc_x, 90 * unit, arc_scale, right_shade, fill_and_stroke);
    place(frame, 2, left_arc_x, 90 * unit, arc_scale, 1, fill_and_stroke);
    place(frame, 3, right_arc_x, 90 * unit, arc_scale, 2, fill_and_stroke);
    // Static silhouettes are bound nodes so the native retained renderer keeps
    // them with the animated arcs instead of treating them as loose ornament.
    place(frame, 4, coil_offset * unit, 0, unit, 0, keep);
    place(frame, 5, -coil_offset * unit, 0, unit, 0, keep);
    // The solid post and real SVG circle are individual bound nodes, so they
    // follow the same Filter-driven transform as their parent coil.
    place(frame, 6, coil_offset * unit, 0, unit, 0, keep);
    place(frame, 7, coil_offset * unit, 0, unit, 0, keep);
    place(frame, 8, -coil_offset * unit, 0, unit, 0, keep);
    place(frame, 9, -coil_offset * unit, 0, unit, 0, keep);
    // LFO controls fan out only the five lighter winding strips in Y. The
    // tower bodies, crowns and terminals above remain Filter-controlled.
    const opxy::i32 left_winding_spread = input.slots[6] >> 29;
    const opxy::i32 right_winding_spread = input.slots[7] >> 29;
    place(frame, 10, coil_offset * unit, -2 * left_winding_spread * unit, unit, 0, keep);
    place(frame, 11, coil_offset * unit, -left_winding_spread * unit, unit, 0, keep);
    place(frame, 12, coil_offset * unit, 0, unit, 0, keep);
    place(frame, 13, coil_offset * unit, left_winding_spread * unit, unit, 0, keep);
    place(frame, 14, coil_offset * unit, 2 * left_winding_spread * unit, unit, 0, keep);
    place(frame, 15, -coil_offset * unit, -2 * right_winding_spread * unit, unit, 0, keep);
    place(frame, 16, -coil_offset * unit, -right_winding_spread * unit, unit, 0, keep);
    place(frame, 17, -coil_offset * unit, 0, unit, 0, keep);
    place(frame, 18, -coil_offset * unit, right_winding_spread * unit, unit, 0, keep);
    place(frame, 19, -coil_offset * unit, 2 * right_winding_spread * unit, unit, 0, keep);
    place(frame, 20, coil_offset * unit, 0, unit, 0, keep);
    place(frame, 21, -coil_offset * unit, 0, unit, 0, keep);
}
}
