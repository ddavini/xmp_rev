#pragma once

#include "audio/decoder.h"

namespace xmad::audio {
std::unique_ptr<Decoder> OpenMp3Decoder(const std::string& path);
}
