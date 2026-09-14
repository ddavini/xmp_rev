#include "audio/tags.h"

#include <algorithm>
#include <cstdint>
#include <fstream>

#include "dr_flac.h"

namespace xmad::audio {

namespace {

uint8_t U8(const std::string& s, size_t i) { return static_cast<uint8_t>(s[i]); }

// ID3v2 frame/tag sizes are "syncsafe": 4 bytes, 7 significant bits each
// (top bit always 0), to guarantee no accidental frame-sync pattern
// appears inside a size field.
uint32_t SyncSafe32(const std::string& s, size_t off) {
    return (static_cast<uint32_t>(U8(s, off)) << 21) | (static_cast<uint32_t>(U8(s, off + 1)) << 14) |
           (static_cast<uint32_t>(U8(s, off + 2)) << 7) | static_cast<uint32_t>(U8(s, off + 3));
}

uint32_t Plain32(const std::string& s, size_t off) {
    return (static_cast<uint32_t>(U8(s, off)) << 24) | (static_cast<uint32_t>(U8(s, off + 1)) << 16) |
           (static_cast<uint32_t>(U8(s, off + 2)) << 8) | static_cast<uint32_t>(U8(s, off + 3));
}

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string Latin1ToUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) AppendUtf8(out, c);
    return out;
}

// Decodes UTF-16 (with a leading BOM to pick LE/BE, per ID3v2 encoding 1)
// to UTF-8. Unpaired/invalid surrogates are dropped rather than crashing -
// this is best-effort tag text, not a validated stream.
std::string Utf16ToUtf8(const std::string& s, bool bigEndian) {
    std::string out;
    size_t i = 0;
    auto readUnit = [&](size_t off) -> uint16_t {
        const uint8_t a = U8(s, off), b = U8(s, off + 1);
        return bigEndian ? static_cast<uint16_t>((a << 8) | b) : static_cast<uint16_t>((b << 8) | a);
    };
    while (i + 1 < s.size()) {
        const uint16_t u = readUnit(i);
        i += 2;
        if (u == 0) break; // null terminator
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < s.size()) {
            const uint16_t lo = readUnit(i);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                i += 2;
                const uint32_t cp = 0x10000 + ((static_cast<uint32_t>(u) - 0xD800) << 10) + (lo - 0xDC00);
                AppendUtf8(out, cp);
                continue;
            }
        }
        if (u >= 0xD800 && u <= 0xDFFF) continue; // unpaired surrogate - drop
        AppendUtf8(out, u);
    }
    return out;
}

std::string TrimTrailing(std::string s) {
    while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
    return s;
}

// data is the frame payload: [encoding byte][text...]. Frame-size bounds
// (not just null-termination) are what keep this from reading past the
// caller-supplied buffer.
std::string DecodeId3Text(const std::string& data) {
    if (data.empty()) return "";
    const uint8_t encoding = U8(data, 0);
    const std::string body = data.substr(1);
    switch (encoding) {
        case 0: // ISO-8859-1
            return TrimTrailing(Latin1ToUtf8(body));
        case 3: // UTF-8 (ID3v2.4; some v2.3 taggers use it too - permissive)
            return TrimTrailing(body);
        case 1: { // UTF-16 with BOM
            if (body.size() < 2) return "";
            const bool bigEndian = U8(body, 0) == 0xFE && U8(body, 1) == 0xFF;
            return TrimTrailing(Utf16ToUtf8(body.substr(2), bigEndian));
        }
        case 2: // UTF-16BE, no BOM (ID3v2.4)
            return TrimTrailing(Utf16ToUtf8(body, /*bigEndian=*/true));
        default:
            return "";
    }
}

} // namespace

void ParseId3v2Tags(const std::string& head, TagInfo& out) {
    if (head.size() < 10 || head[0] != 'I' || head[1] != 'D' || head[2] != '3') return;
    const uint8_t versionMajor = U8(head, 3);
    if (versionMajor < 2 || versionMajor > 4) return;
    const uint32_t tagSize = SyncSafe32(head, 6);
    const size_t tagEnd = std::min(head.size(), static_cast<size_t>(10) + tagSize);

    size_t off = 10;
    if (U8(head, 5) & 0x40) { // extended header present
        if (off + 4 > tagEnd) return;
        // v2.4 encodes the extended header size syncsafe; v2.3 doesn't.
        // Either way it's the first 4 bytes at `off`, so this skip works
        // for both once we know which reader to use.
        const uint32_t extSize = versionMajor >= 4 ? SyncSafe32(head, off) : Plain32(head, off);
        off += (versionMajor >= 4 ? extSize : extSize + 4); // v2.3's size excludes itself; v2.4's includes itself
        if (off > tagEnd) return;
    }

    if (versionMajor == 2) {
        // v2.2: 3-char frame IDs, 3-byte plain sizes, no flags - different
        // enough from v2.3/v2.4 that treating it as unsupported and
        // falling back to ID3v1/filename is a cleaner scope cut than
        // threading a second frame-header shape through the loop below.
        return;
    }

    while (off + 10 <= tagEnd) {
        const std::string frameId = head.substr(off, 4);
        if (frameId == std::string(4, '\0')) break; // padding reached
        const uint32_t frameSize =
            versionMajor >= 4 ? SyncSafe32(head, off + 4) : Plain32(head, off + 4);
        const size_t dataStart = off + 10;
        if (frameSize == 0 || dataStart + frameSize > tagEnd) break; // malformed - stop, don't overread
        if (frameId == "TIT2") {
            out.title = DecodeId3Text(head.substr(dataStart, frameSize));
        } else if (frameId == "TPE1") {
            out.artist = DecodeId3Text(head.substr(dataStart, frameSize));
        } else if (frameId == "TALB") {
            out.album = DecodeId3Text(head.substr(dataStart, frameSize));
        } else if (frameId == "TCON") {
            out.genre = DecodeId3Text(head.substr(dataStart, frameSize));
        } else if (frameId == "TRCK") {
            // "5" or "5/12" (track/total) - display just the track.
            const std::string t = DecodeId3Text(head.substr(dataStart, frameSize));
            out.track = t.substr(0, t.find('/'));
        }
        off = dataStart + frameSize;
    }
}

std::string ParseId3v2Title(const std::string& head) {
    TagInfo t;
    ParseId3v2Tags(head, t);
    return t.title;
}

void ParseId3v1Tags(const std::string& tail, TagInfo& out) {
    if (tail.size() != 128 || tail[0] != 'T' || tail[1] != 'A' || tail[2] != 'G') return;
    out.title = TrimTrailing(Latin1ToUtf8(tail.substr(3, 30)));
    out.artist = TrimTrailing(Latin1ToUtf8(tail.substr(33, 30)));
    out.album = TrimTrailing(Latin1ToUtf8(tail.substr(63, 30)));
    // ID3v1.1: the comment field's next-to-last byte is 0 and its last
    // byte is the track number, distinguishing it from plain ID3v1 (which
    // uses the full 30 bytes at this offset as free-text comment - no
    // track number to extract there).
    if (U8(tail, 125) == 0) {
        const uint8_t trackNum = U8(tail, 126);
        if (trackNum != 0) out.track = std::to_string(trackNum);
    }
}

std::string ParseId3v1Title(const std::string& tail) {
    TagInfo t;
    ParseId3v1Tags(tail, t);
    return t.title;
}

namespace {

// Fills any field still "" in `out` from `fallback` - used to let ID3v1
// fill gaps ID3v2 left, field by field, without clobbering fields ID3v2
// already populated.
void FillEmpty(TagInfo& out, const TagInfo& fallback) {
    if (out.title.empty()) out.title = fallback.title;
    if (out.artist.empty()) out.artist = fallback.artist;
    if (out.album.empty()) out.album = fallback.album;
    if (out.genre.empty()) out.genre = fallback.genre;
    if (out.track.empty()) out.track = fallback.track;
}

TagInfo ReadMp3Tags(const std::string& path) {
    TagInfo out;
    std::ifstream f(path, std::ios::binary);
    if (!f) return out;

    std::string header(10, '\0');
    f.read(header.data(), 10);
    if (f.gcount() == 10 && header[0] == 'I' && header[1] == 'D' && header[2] == '3') {
        const uint32_t tagSize = SyncSafe32(header, 6);
        std::string full = header;
        full.resize(10 + tagSize);
        f.read(full.data() + 10, static_cast<std::streamsize>(tagSize));
        full.resize(10 + static_cast<size_t>(std::max<std::streamsize>(0, f.gcount())));
        ParseId3v2Tags(full, out);
    }

    f.clear();
    f.seekg(0, std::ios::end);
    const std::streamoff fileSize = f.tellg();
    if (fileSize >= 128) {
        f.seekg(fileSize - 128);
        std::string tail(128, '\0');
        f.read(tail.data(), 128);
        if (f.gcount() == 128) {
            TagInfo v1;
            ParseId3v1Tags(tail, v1);
            FillEmpty(out, v1);
        }
    }
    return out;
}

std::string ReadMp3Title(const std::string& path) { return ReadMp3Tags(path).title; }

bool StartsWithCI(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    }
    return true;
}

TagInfo ReadFlacTags(const std::string& path) {
    TagInfo out;
    auto onMeta = [](void* userData, drflac_metadata* meta) {
        if (meta->type != DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) return;
        auto* out = static_cast<TagInfo*>(userData);
        const char* comment = nullptr;
        drflac_uint32 commentLen = 0;
        drflac_vorbis_comment_iterator it;
        drflac_init_vorbis_comment_iterator(&it, meta->data.vorbis_comment.commentCount,
                                             meta->data.vorbis_comment.pComments);
        while ((comment = drflac_next_vorbis_comment(&it, &commentLen)) != nullptr) {
            const std::string entry(comment, commentLen);
            // Vorbis comment keys are conventionally uppercase but are
            // case-insensitive per spec - taggers vary.
            auto extract = [&](const char* key) -> std::string {
                const std::string prefix = std::string(key) + "=";
                return StartsWithCI(entry, prefix) ? entry.substr(prefix.size()) : std::string();
            };
            if (std::string v = extract("TITLE"); !v.empty()) out->title = v;
            else if (std::string v = extract("ARTIST"); !v.empty()) out->artist = v;
            else if (std::string v = extract("ALBUM"); !v.empty()) out->album = v;
            else if (std::string v = extract("GENRE"); !v.empty()) out->genre = v;
            else if (std::string v = extract("TRACKNUMBER"); !v.empty()) out->track = v;
        }
    };
    drflac* dec = drflac_open_file_with_metadata(path.c_str(), onMeta, &out, nullptr);
    if (!dec) return out;
    drflac_close(dec);
    return out;
}

std::string ReadFlacTitle(const std::string& path) { return ReadFlacTags(path).title; }

} // namespace

std::string ReadTrackTitle(const std::string& path) {
    const auto dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    try {
        if (ext == "mp3") return ReadMp3Title(path);
        if (ext == "flac") return ReadFlacTitle(path);
    } catch (const std::exception&) {
        return "";
    }
    return "";
}

TagInfo ReadTrackTags(const std::string& path) {
    const auto dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    try {
        if (ext == "mp3") return ReadMp3Tags(path);
        if (ext == "flac") return ReadFlacTags(path);
    } catch (const std::exception&) {
        return TagInfo{};
    }
    return TagInfo{};
}

} // namespace xmad::audio
