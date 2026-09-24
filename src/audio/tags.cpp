#include "audio/tags.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>

#include "audio/module_file.h"
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

// Write-direction counterparts of SyncSafe32/Plain32, for building a new
// ID3v2 tag/frame header rather than parsing an existing one.
void WriteSyncSafe32(std::string& s, size_t off, uint32_t v) {
    s[off] = static_cast<char>((v >> 21) & 0x7F);
    s[off + 1] = static_cast<char>((v >> 14) & 0x7F);
    s[off + 2] = static_cast<char>((v >> 7) & 0x7F);
    s[off + 3] = static_cast<char>(v & 0x7F);
}

void WritePlain32(std::string& s, size_t off, uint32_t v) {
    s[off] = static_cast<char>((v >> 24) & 0xFF);
    s[off + 1] = static_cast<char>((v >> 16) & 0xFF);
    s[off + 2] = static_cast<char>((v >> 8) & 0xFF);
    s[off + 3] = static_cast<char>(v & 0xFF);
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

// One ID3v2 frame's complete raw bytes (10-byte frame header + payload),
// captured as-is during a write-path scan so it can be copied straight
// through into the rewritten tag without needing to understand it.
struct RawFrame {
    std::string id;
    std::string bytes;
};

// Write-path counterpart to ParseId3v2Tags' header/frame walk: instead of
// decoding known frames into a TagInfo, captures every frame found as raw
// bytes (so unrecognized frames - APIC art, COMM, TXXX, ... - round-trip
// untouched) and reports which parts of the existing tag (if any) this
// can't safely reproduce. `fileStart` must hold the first
// `10 + declaredTagSize` bytes of the file (or fewer, if the file itself
// is shorter - handled as "malformed, refuse" below rather than guessing).
//
// Returns false only for a tag this genuinely can't round-trip safely:
// ID3v2.2 (3-char frame IDs/3-byte plain sizes - ParseId3v2Tags already
// treats this as unsupported for reading; here, blindly copying such
// frames through under a v2.3/v2.4 frame-size format would misread their
// sizes and corrupt them), an extended header, or the unsynchronisation
// flag (both rare in practice and not worth threading through a first
// pass). No ID3v2 tag at all is NOT a failure - it's the common "add tags
// to a previously untagged file" case, reported as majorVersion=3 (the
// default for a freshly created tag), an empty frame list, and
// tagTotalSize=0.
bool ScanExistingId3v2(const std::string& fileStart, int& majorVersion, std::vector<RawFrame>& frames,
                       size_t& tagTotalSize) {
    majorVersion = 3;
    frames.clear();
    tagTotalSize = 0;
    if (fileStart.size() < 10 || fileStart[0] != 'I' || fileStart[1] != 'D' || fileStart[2] != '3') {
        return true; // no existing tag - nothing to preserve, not an error
    }
    const uint8_t verMajor = U8(fileStart, 3);
    const uint8_t flags = U8(fileStart, 5);
    if (verMajor < 3 || verMajor > 4) return false; // v2.2 (or a bogus version) - can't safely round-trip
    if (flags & 0x80) return false;                 // unsynchronisation - not handled
    if (flags & 0x40) return false;                 // extended header - not handled (keeps scope small)

    const uint32_t declaredSize = SyncSafe32(fileStart, 6);
    tagTotalSize = 10 + declaredSize;
    if (fileStart.size() < tagTotalSize) return false; // truncated read - caller didn't pass enough bytes
    majorVersion = verMajor;

    size_t off = 10;
    while (off + 10 <= tagTotalSize) {
        const std::string frameId = fileStart.substr(off, 4);
        if (frameId == std::string(4, '\0')) break; // padding reached
        const uint32_t frameSize = majorVersion >= 4 ? SyncSafe32(fileStart, off + 4) : Plain32(fileStart, off + 4);
        // frameSize cast to size_t BEFORE adding 10 - security-review
        // finding: `10 + frameSize` (both uint32_t/int) computes in
        // 32-bit arithmetic and only widens to size_t afterward, so a
        // v2.3 (Plain32, not syncsafe-bounded) frameSize within 9 of
        // UINT32_MAX wrapped to a tiny frameTotal, letting the bounds
        // check below silently pass a bogus "small" frame instead of
        // rejecting it - mis-parsing the tag into spurious frames rather
        // than any actual out-of-bounds access (substr stays safe either
        // way). ParseId3v2Tags avoids this by computing dataStart as
        // size_t first; same fix here.
        const size_t frameTotal = static_cast<size_t>(frameSize) + 10;
        if (frameSize == 0 || off + frameTotal > tagTotalSize) break; // malformed - stop, same as ParseId3v2Tags
        frames.push_back({frameId, fileStart.substr(off, frameTotal)});
        off += frameTotal;
    }
    return true;
}

// Builds one complete text frame (header + payload) for the given 4-char
// frame id and value, always as UTF-8 (encoding byte 3) - DecodeId3Text
// already reads that permissively even under v2.3 ("some v2.3 taggers use
// it too"), and writing a full UTF-16 encoder just to satisfy the letter
// of the v2.3 spec isn't worth it for a first pass. Frame size is
// syncsafe for v2.4, plain for v2.3, matching the version this frame is
// being written under (must match whichever format the tag's own header
// declares, or a reader would misparse this frame's size).
std::string BuildTextFrame(const std::string& frameId, const std::string& utf8Value, int majorVersion) {
    std::string payload;
    payload += static_cast<char>(3); // UTF-8
    payload += utf8Value;

    std::string frame(10, '\0');
    frame[0] = frameId[0];
    frame[1] = frameId[1];
    frame[2] = frameId[2];
    frame[3] = frameId[3];
    if (majorVersion >= 4) {
        WriteSyncSafe32(frame, 4, static_cast<uint32_t>(payload.size()));
    } else {
        WritePlain32(frame, 4, static_cast<uint32_t>(payload.size()));
    }
    // Bytes 8-9 (frame flags) already zeroed by the 10-byte init above.
    frame += payload;
    return frame;
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

    // Read once, up front, so the ID3v2 branch below can check a
    // forged/oversized declared tag size against the file's real length
    // before allocating for it - a security-review finding: a ~10-byte
    // file with a header claiming the syncsafe-max ~256 MiB tag size used
    // to force a transient ~256 MiB allocation here on nothing more than
    // being shown in the playlist/Info window. Mirrors the same guard
    // WriteMp3Id3v2Tags already has (`if (need > fileSize) return false`).
    f.seekg(0, std::ios::end);
    const std::streamoff fileSize = f.tellg();
    f.seekg(0, std::ios::beg);

    std::string header(10, '\0');
    f.read(header.data(), 10);
    if (f.gcount() == 10 && header[0] == 'I' && header[1] == 'D' && header[2] == '3') {
        const uint32_t tagSize = SyncSafe32(header, 6);
        const std::streamoff need = 10 + static_cast<std::streamoff>(tagSize);
        if (need <= fileSize) {
            std::string full = header;
            full.resize(static_cast<size_t>(need));
            f.read(full.data() + 10, static_cast<std::streamsize>(tagSize));
            full.resize(10 + static_cast<size_t>(std::max<std::streamsize>(0, f.gcount())));
            ParseId3v2Tags(full, out);
        }
        // else: declared tag runs past EOF - malformed/forged, skip it
        // entirely rather than allocating for it; falls through to the
        // ID3v1/filename fallback below, same as "no ID3v2 tag at all".
    }

    f.clear();
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

// Enough for every format's name field (S3M's "SCRM" magic at 44..47 is the
// furthest byte ParseModuleTitle looks at). Read through the same gzip layer
// as playback, so .mdz/.xmz/.s3z only inflate this far, not the whole file.
constexpr size_t kModuleHeadBytes = 64;

} // namespace

std::string ReadTrackTitle(const std::string& path) {
    const auto dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    try {
        if (ext == "mp3") return ReadMp3Title(path);
        if (ext == "flac") return ReadFlacTitle(path);
        if (IsTrackerExtension(ext)) return ParseModuleTitle(ReadModuleHead(path, kModuleHeadBytes));
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
        if (IsTrackerExtension(ext)) {
            // Modules only carry a song name - no artist/album/genre/track.
            TagInfo out;
            out.title = ParseModuleTitle(ReadModuleHead(path, kModuleHeadBytes));
            return out;
        }
    } catch (const std::exception&) {
        return TagInfo{};
    }
    return TagInfo{};
}

bool WriteMp3Id3v2Tags(const std::string& path, const TagInfo& tags) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff fileSize = in.tellg();
    if (fileSize < 0) return false;
    in.seekg(0, std::ios::beg);

    // Read the fixed 10-byte header first - only once we've seen its
    // declared size do we know how much more of the tag to read.
    std::string head(static_cast<size_t>(std::min<std::streamoff>(fileSize, 10)), '\0');
    if (!head.empty()) in.read(head.data(), static_cast<std::streamsize>(head.size()));
    if (head.size() == 10 && head[0] == 'I' && head[1] == 'D' && head[2] == '3') {
        const uint32_t declaredSize = SyncSafe32(head, 6);
        const std::streamoff need = 10 + static_cast<std::streamoff>(declaredSize);
        if (need > fileSize) return false; // declared tag runs past EOF - malformed, refuse
        head.resize(static_cast<size_t>(need));
        in.read(head.data() + 10, need - 10);
        if (in.gcount() != need - 10) return false;
    }

    int majorVersion = 3;
    std::vector<RawFrame> frames;
    size_t tagTotalSize = 0;
    if (!ScanExistingId3v2(head, majorVersion, frames, tagTotalSize)) return false;
    const size_t audioStart = tagTotalSize; // 0 if the file had no ID3v2 tag

    // Preserve every frame this call doesn't manage (APIC art, COMM,
    // TXXX, ...) byte-for-byte; the 5 managed fields get fresh frames
    // appended below instead of being patched in place - frame order
    // inside a tag carries no meaning to any reader.
    auto isManaged = [](const std::string& id) {
        return id == "TIT2" || id == "TPE1" || id == "TALB" || id == "TCON" || id == "TRCK";
    };
    std::string frameBytes;
    for (const RawFrame& f : frames) {
        if (!isManaged(f.id)) frameBytes += f.bytes;
    }
    auto addFrame = [&](const char* id, const std::string& value) {
        if (!value.empty()) frameBytes += BuildTextFrame(id, value, majorVersion);
    };
    addFrame("TIT2", tags.title);
    addFrame("TPE1", tags.artist);
    addFrame("TALB", tags.album);
    addFrame("TCON", tags.genre);
    addFrame("TRCK", tags.track);

    std::string newHeader(10, '\0');
    newHeader[0] = 'I';
    newHeader[1] = 'D';
    newHeader[2] = '3';
    newHeader[3] = static_cast<char>(majorVersion);
    newHeader[4] = 0; // revision
    newHeader[5] = 0; // flags
    WriteSyncSafe32(newHeader, 6, static_cast<uint32_t>(frameBytes.size()));

    const std::string tmpPath = path + ".xmadtmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(newHeader.data(), static_cast<std::streamsize>(newHeader.size()));
        out.write(frameBytes.data(), static_cast<std::streamsize>(frameBytes.size()));

        in.clear();
        in.seekg(static_cast<std::streamoff>(audioStart), std::ios::beg);
        std::array<char, 1 << 16> buf{};
        while (in && out) {
            in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            const std::streamsize got = in.gcount();
            if (got <= 0) break;
            out.write(buf.data(), got);
        }
        if (!out) {
            out.close();
            std::remove(tmpPath.c_str());
            return false;
        }
    }
    in.close();

    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        std::remove(tmpPath.c_str());
        return false;
    }
    return true;
}

} // namespace xmad::audio
