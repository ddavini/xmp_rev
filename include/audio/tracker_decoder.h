#pragma once

#include "audio/decoder.h"

namespace xmad::audio {
// MOD/XM/S3M (and zipped/gzipped .mdz/.xmz/.s3z) via ibxm-ac - see
// module_file.h for the zip/gzip layer.
std::unique_ptr<Decoder> OpenTrackerDecoder(const std::string& path);
}
