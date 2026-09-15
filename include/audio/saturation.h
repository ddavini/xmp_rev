#pragma once

#include <cstdint>

// Fixed-drive tanh soft-clip waveshaper, the "Saturation/Distortion" entry
// in the Effects menu. Memoryless (no per-sample-rate state), so - like
// stereo_widen.h - this is a stateless free function rather than a class
// with Reset/Process.

namespace xmad::audio {

// interleaved: in-place, frames * channels floats. Output is clamped to
// [-1, 1] as a defensive backstop (the waveshaper is mathematically bounded
// to that range for inputs already within it, but not for out-of-range
// input, e.g. a pathological DC offset above 1.0).
void ApplySaturation(float* interleaved, uint64_t frames, int channels);

} // namespace xmad::audio
