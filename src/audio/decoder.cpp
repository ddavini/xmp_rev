#include "audio/decoder.h"

#include <algorithm>
#include <stdexcept>

#include "audio/flac_decoder.h"
#include "audio/mp3_decoder.h"

namespace xmad::audio {

namespace {

std::string LowerExt(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

} // namespace

std::unique_ptr<Decoder> OpenDecoder(const std::string& path) {
    const std::string ext = LowerExt(path);
    if (ext == "mp3") return OpenMp3Decoder(path);
    if (ext == "flac") return OpenFlacDecoder(path);
    throw std::runtime_error("OpenDecoder: unrecognized extension for " + path);
}

} // namespace xmad::audio
