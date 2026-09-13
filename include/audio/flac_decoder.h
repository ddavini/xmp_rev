#pragma once

#include "audio/decoder.h"

namespace xmad::audio {
std::unique_ptr<Decoder> OpenFlacDecoder(const std::string& path);
}
