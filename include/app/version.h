#pragma once

// This revival's own version - independent of the original's "v1.0.378",
// which the UI still displays as a startup banner/status-line easter egg
// (see main.cpp's texStatus/marqueeText fallback strings). Bump this by
// hand as the project reaches meaningful milestones; no automation reads
// or writes it.

namespace xmad::app {

constexpr const char* kVersion = "0.1.0";

} // namespace xmad::app
