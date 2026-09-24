#include "audio/module_file.h"

#include <cstring>
#include <stdexcept>

#include <zlib.h>

namespace xmad::audio {

namespace {

// Reads through gzread in chunks until EOF or `maxBytes`. `overflow` is set
// when the stream still had data past `maxBytes`.
std::vector<char> GzReadAll(const std::string& path, size_t maxBytes, bool& overflow, bool& failed) {
    std::vector<char> out;
    overflow = failed = false;
    gzFile f = gzopen(path.c_str(), "rb");
    if (!f) {
        failed = true;
        return out;
    }
    constexpr size_t kChunk = 64 * 1024;
    while (true) {
        const size_t room = maxBytes - out.size();
        if (room == 0) {
            char probe;
            overflow = gzread(f, &probe, 1) > 0;
            break;
        }
        const size_t want = room < kChunk ? room : kChunk;
        const size_t old = out.size();
        out.resize(old + want);
        const int got = gzread(f, out.data() + old, static_cast<unsigned>(want));
        if (got < 0) {
            out.resize(old);
            failed = true;
            break;
        }
        out.resize(old + static_cast<size_t>(got));
        if (static_cast<size_t>(got) < want) break; // EOF
    }
    gzclose(f);
    return out;
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

bool IsTrackerExtension(const std::string& ext) {
    return ext == "mod" || ext == "xm" || ext == "s3m" || ext == "mdz" || ext == "xmz" || ext == "s3z";
}

std::vector<char> ReadModuleFile(const std::string& path, size_t maxBytes) {
    bool overflow = false, failed = false;
    std::vector<char> bytes = GzReadAll(path, maxBytes, overflow, failed);
    if (failed) throw std::runtime_error("module: failed to read " + path);
    if (overflow) throw std::runtime_error("module: decompressed size too large for " + path);
    return bytes;
}

std::vector<char> ReadModuleHead(const std::string& path, size_t maxBytes) {
    bool overflow = false, failed = false;
    std::vector<char> bytes = GzReadAll(path, maxBytes, overflow, failed);
    if (failed) bytes.clear();
    return bytes;
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
