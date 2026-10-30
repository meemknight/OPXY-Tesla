#pragma once
#include <opxy/page_header.hpp>

namespace tesla {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<4, 64, 30, 16, 16>;
struct DisplayState { opxy::u32 phase; opxy::draw::PageHeader header; };
}
