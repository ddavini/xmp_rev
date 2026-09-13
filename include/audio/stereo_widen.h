#pragma once

#include <cstdint>

// Clean-room approximation of the original's XSound "Surround" toggle
// (Source/modxmMP3Interface.bas's mXSrnd, wired to XSOUND_SURROUND in the
// proprietary xmMP3.dll - not xmMP3.bas's separate XSOUND_NORMALIZE/Level
// path, which is an unrelated loudness-normalization feature and isn't
// implemented here). The original's actual DSP internals aren't available
// to port, so this substitutes a standard mid-side stereo widener: boost
// the L-R "side" energy relative to the L+R "mid" energy. A no-op on mono
// input, since there's no stereo image to widen.

namespace xmad::audio {

// interleaved: in-place, frames * channels floats. width: 1.0 leaves the
// signal unchanged; >1.0 widens the stereo image. Output is clamped to
// [-1, 1] since boosting the side channel can otherwise exceed it.
void ApplyStereoWiden(float* interleaved, uint64_t frames, int channels, float width);

} // namespace xmad::audio
