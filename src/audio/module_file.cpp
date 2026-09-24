#include "audio/module_file.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

#include <zlib.h>

namespace xmad::audio {

namespace {

enum class Container { Raw, Gzip, Zip };

Container DetectContainer(const std::vector<char>& raw) {
    auto at = [&](size_t i) { return static_cast<unsigned char>(raw[i]); };
    if (raw.size() >= 4 && at(0) == 'P' && at(1) == 'K' && at(2) == 3 && at(3) == 4) return Container::Zip;
    if (raw.size() >= 2 && at(0) == 0x1f && at(1) == 0x8b) return Container::Gzip;
    return Container::Raw;
}

uint16_t U16(const std::vector<char>& b, size_t off) {
    return static_cast<uint16_t>(static_cast<unsigned char>(b[off]) | (static_cast<unsigned char>(b[off + 1]) << 8));
}

uint32_t U32(const std::vector<char>& b, size_t off) {
    return static_cast<uint32_t>(U16(b, off)) | (static_cast<uint32_t>(U16(b, off + 2)) << 16);
}

// Reads the whole file, or just its first `prefixBytes` when that's > 0.
// Throws if it can't be opened or is bigger than `maxBytes`.
std::vector<char> ReadRaw(const std::string& path, size_t maxBytes, size_t prefixBytes = 0) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("module: failed to open " + path);
    const std::streamoff size = f.tellg();
    if (size < 0) throw std::runtime_error("module: failed to read " + path);
    if (prefixBytes == 0 && static_cast<uint64_t>(size) > maxBytes)
        throw std::runtime_error("module: file too large: " + path);
    const size_t want = prefixBytes > 0 ? std::min<size_t>(prefixBytes, static_cast<size_t>(size))
                                        : static_cast<size_t>(size);
    std::vector<char> out(want);
    f.seekg(0, std::ios::beg);
    if (want > 0 && !f.read(out.data(), static_cast<std::streamsize>(want)))
        throw std::runtime_error("module: failed to read " + path);
    return out;
}

// zlib inflate of `in` (windowBits picks the framing: 16+MAX_WBITS = gzip,
// -MAX_WBITS = raw deflate as stored in a zip entry) into at most `maxOut`
// bytes. `truncated` is set when there was more output than that - an
// error for playback, expected when only reading the header. Throws on a
// corrupt stream.
std::vector<char> Inflate(const char* in, size_t inLen, int windowBits, size_t maxOut, bool& truncated) {
    truncated = false;
    z_stream zs{};
    if (inflateInit2(&zs, windowBits) != Z_OK) throw std::runtime_error("module: zlib init failed");
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in));
    zs.avail_in = static_cast<uInt>(inLen);
    std::vector<char> out;
    constexpr size_t kChunk = 256 * 1024;
    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
        if (out.size() >= maxOut) {
            truncated = true;
            break;
        }
        const size_t old = out.size();
        const size_t want = std::min(kChunk, maxOut - old);
        out.resize(old + want);
        zs.next_out = reinterpret_cast<Bytef*>(out.data() + old);
        zs.avail_out = static_cast<uInt>(want);
        rc = inflate(&zs, Z_NO_FLUSH);
        out.resize(old + (want - zs.avail_out));
        if (rc == Z_STREAM_END) break;
        if (rc != Z_OK || (zs.avail_out > 0 && zs.avail_in == 0)) {
            inflateEnd(&zs);
            throw std::runtime_error("module: corrupt or truncated compressed data");
        }
    }
    inflateEnd(&zs);
    return out;
}

bool HasModuleName(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == "mod" || ext == "xm" || ext == "s3m";
}

// MODPlug-era .mdz/.xmz/.s3z: a regular PKZIP archive holding the module
// (sometimes alongside a readme). Walks the central directory - not the
// local headers, whose sizes are zero when the archiver streamed them - and
// takes the first entry named .mod/.xm/.s3m, else the largest file.
std::vector<char> ExtractZipModule(const std::vector<char>& zip, size_t maxOut, bool& truncated) {
    truncated = false;
    // End-of-central-directory record: 22 bytes plus up to a 64K comment.
    size_t eocd = std::string::npos;
    if (zip.size() >= 22) {
        const size_t stop = zip.size() > 22 + 65535 ? zip.size() - 22 - 65535 : 0;
        for (size_t i = zip.size() - 22 + 1; i-- > stop;) {
            if (U32(zip, i) == 0x06054b50) {
                eocd = i;
                break;
            }
        }
    }
    if (eocd == std::string::npos) throw std::runtime_error("module: zip has no central directory");
    const uint16_t entries = U16(zip, eocd + 10);
    size_t p = U32(zip, eocd + 16);

    struct Entry {
        uint16_t flags = 0, method = 0;
        uint32_t compSize = 0, size = 0, localOff = 0;
        bool named = false;
    };
    Entry best;
    bool found = false;
    for (uint16_t n = 0; n < entries; ++n) {
        if (p + 46 > zip.size() || U32(zip, p) != 0x02014b50) throw std::runtime_error("module: corrupt zip directory");
        Entry e;
        e.flags = U16(zip, p + 8);
        e.method = U16(zip, p + 10);
        e.compSize = U32(zip, p + 20);
        e.size = U32(zip, p + 24);
        const uint16_t nameLen = U16(zip, p + 28), extraLen = U16(zip, p + 30), commentLen = U16(zip, p + 32);
        e.localOff = U32(zip, p + 42);
        if (p + 46 + nameLen > zip.size()) throw std::runtime_error("module: corrupt zip directory");
        const std::string name(zip.data() + p + 46, nameLen);
        p += 46 + static_cast<size_t>(nameLen) + extraLen + commentLen;
        if (name.empty() || name.back() == '/' || e.size == 0) continue; // directory / empty
        e.named = HasModuleName(name);
        // The first .mod/.xm/.s3m-named entry wins outright; failing
        // that, the largest file.
        const bool better = !found || (e.named != best.named ? e.named : !e.named && e.size > best.size);
        if (better) {
            best = e;
            found = true;
        }
    }
    if (!found) throw std::runtime_error("module: zip contains no files");
    if (best.flags & 0x1) throw std::runtime_error("module: zip entry is encrypted");

    const size_t lh = best.localOff;
    if (lh + 30 > zip.size() || U32(zip, lh) != 0x04034b50) throw std::runtime_error("module: corrupt zip entry");
    const size_t dataOff = lh + 30 + U16(zip, lh + 26) + U16(zip, lh + 28);
    if (dataOff > zip.size() || best.compSize > zip.size() - dataOff)
        throw std::runtime_error("module: zip entry runs past end of file");

    if (best.method == 0) { // stored
        truncated = best.compSize > maxOut;
        const size_t n = std::min<size_t>(best.compSize, maxOut);
        return std::vector<char>(zip.begin() + static_cast<long>(dataOff), zip.begin() + static_cast<long>(dataOff + n));
    }
    if (best.method == 8) return Inflate(zip.data() + dataOff, best.compSize, -MAX_WBITS, maxOut, truncated);
    // 6 = implode, 9 = deflate64, etc. - pre-PKZIP-2.0 and exotic
    // archivers. Say so plainly rather than feeding garbage to ibxm.
    throw std::runtime_error("module: unsupported zip compression method " + std::to_string(best.method));
}

std::vector<char> Unwrap(const std::vector<char>& raw, size_t maxOut, bool& truncated) {
    truncated = false;
    switch (DetectContainer(raw)) {
    case Container::Zip:
        return ExtractZipModule(raw, maxOut, truncated);
    case Container::Gzip:
        return Inflate(raw.data(), raw.size(), 16 + MAX_WBITS, maxOut, truncated);
    case Container::Raw:
        break;
    }
    truncated = raw.size() > maxOut;
    return std::vector<char>(raw.begin(), raw.begin() + static_cast<long>(std::min(raw.size(), maxOut)));
}

bool HasAt(const std::vector<char>& head, size_t offset, const char* magic) {
    const size_t len = std::strlen(magic);
    return head.size() >= offset + len && std::memcmp(head.data() + offset, magic, len) == 0;
}

std::string FixedName(const std::vector<char>& head, size_t offset, size_t len) {
    if (head.size() < offset + len) return "";
    std::string name;
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(head[offset + i]);
        if (c == 0) break;
        // Printable ASCII only - module names are DOS/Amiga codepage
        // bytes, and the skin's bitmap font only covers ASCII anyway.
        if (c >= 0x20 && c < 0x7f) name.push_back(static_cast<char>(c));
    }
    const auto first = name.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    const auto last = name.find_last_not_of(' ');
    return name.substr(first, last - first + 1);
}

} // namespace

std::vector<char> ConvertSoundtracker15(const std::vector<char>& mod) {
    // 15-sample layout: title (20) + 15 * 30-byte sample headers = 470,
    // then song length, a restart/tempo byte, 128 orders = 600; patterns
    // follow at 600. The 31-sample layout puts the song length at 950 and
    // "M.K." at 1080, patterns at 1084.
    constexpr size_t kHeader15 = 20 + 15 * 30; // 470
    constexpr size_t kPatterns15 = kHeader15 + 2 + 128; // 600
    if (mod.size() < kPatterns15 + 1024) return mod;
    if (HasAt(mod, 0, "Extended Module:") || HasAt(mod, 44, "SCRM")) return mod;
    // Tags ibxm already recognizes at 1080 (its own check: the u16 at 1082).
    if (mod.size() >= 1084) {
        const unsigned tag = (static_cast<unsigned char>(mod[1082]) << 8) | static_cast<unsigned char>(mod[1083]);
        if (tag == 0x4b2e || tag == 0x4b21 || tag == 0x5434 || tag == 0x484e || tag == 0x4348) return mod;
    }
    auto u8 = [&](size_t i) { return static_cast<unsigned char>(mod[i]); };
    for (size_t i = 0; i < 15; ++i) {
        const size_t h = 20 + i * 30;
        if (u8(h + 25) > 64) return mod; // volume
    }
    const unsigned songLen = u8(kHeader15);
    if (songLen < 1 || songLen > 128) return mod;
    unsigned numPatterns = 0;
    for (size_t i = 0; i < 128; ++i) {
        const unsigned pat = u8(kHeader15 + 2 + i);
        if (pat >= 64) return mod;
        numPatterns = std::max(numPatterns, pat + 1);
    }
    if (kPatterns15 + static_cast<size_t>(numPatterns) * 1024 > mod.size()) return mod;

    std::vector<char> out;
    out.reserve(mod.size() + 16 * 30 + 4);
    out.insert(out.end(), mod.begin(), mod.begin() + kHeader15);
    for (int i = 0; i < 16; ++i) {
        char empty[30] = {};
        empty[29] = 1; // loop length 1 word = "no loop", as ProTracker writes unused slots
        out.insert(out.end(), empty, empty + 30);
    }
    out.insert(out.end(), mod.begin() + kHeader15, mod.begin() + kPatterns15);
    out.insert(out.end(), {'M', '.', 'K', '.'});
    out.insert(out.end(), mod.begin() + kPatterns15, mod.end());
    return out;
}

bool IsTrackerExtension(const std::string& ext) {
    return ext == "mod" || ext == "xm" || ext == "s3m" || ext == "mdz" || ext == "xmz" || ext == "s3z";
}

std::vector<char> ReadModuleFile(const std::string& path, size_t maxBytes) {
    bool truncated = false;
    std::vector<char> bytes = Unwrap(ReadRaw(path, maxBytes), maxBytes, truncated);
    if (truncated) throw std::runtime_error("module: decompressed size too large for " + path);
    return bytes;
}

std::vector<char> ReadModuleHead(const std::string& path, size_t maxBytes) {
    // A zip's directory is at its end, so compressed files are read whole
    // (they're small); a raw module only needs its first bytes. Either
    // way, decompression stops as soon as `maxBytes` are out.
    constexpr size_t kMaxCompressedBytes = 64 * 1024 * 1024;
    try {
        std::vector<char> prefix = ReadRaw(path, kMaxCompressedBytes, std::max<size_t>(maxBytes, 4));
        bool truncated = false;
        if (DetectContainer(prefix) == Container::Raw) return Unwrap(prefix, maxBytes, truncated);
        return Unwrap(ReadRaw(path, kMaxCompressedBytes), maxBytes, truncated);
    } catch (const std::exception&) {
        return {};
    }
}

std::string DetectModuleCompression(const std::string& path) {
    try {
        switch (DetectContainer(ReadRaw(path, 4, 4))) {
        case Container::Zip: return "zip";
        case Container::Gzip: return "gzip";
        case Container::Raw: return "";
        }
    } catch (const std::exception&) {
    }
    return "";
}

std::string DetectModuleFormat(const std::vector<char>& head) {
    if (HasAt(head, 0, "Extended Module:")) return "XM";
    if (HasAt(head, 44, "SCRM")) return "S3M";
    return head.size() >= 20 ? "MOD" : "";
}

std::string ParseModuleTitle(const std::vector<char>& head) {
    const std::string format = DetectModuleFormat(head);
    if (format == "XM") return FixedName(head, 17, 20);
    if (format == "S3M") return FixedName(head, 0, 28);
    if (format == "MOD") return FixedName(head, 0, 20);
    return "";
}

} // namespace xmad::audio
