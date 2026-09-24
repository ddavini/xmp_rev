// Covers ParseId3v2Title/ParseId3v1Title and ParseModuleTitle, the pure
// parts of title reading -
// ReadTrackTitle's file I/O (and dr_flac's own VORBIS_COMMENT decoding, not
// reimplemented here) aren't exercised by this binary; see decoder_test /
// a manual check against a real tagged file for that (audio/tags.h).

#include "audio/module_file.h"
#include "audio/tags.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using xmad::audio::DetectModuleFormat;
using xmad::audio::ParseModuleTitle;
using xmad::audio::ReadTrackTitle;
using xmad::audio::ParseId3v1Tags;
using xmad::audio::ParseId3v1Title;
using xmad::audio::ParseId3v2Tags;
using xmad::audio::ParseId3v2Title;
using xmad::audio::ReadTrackTags;
using xmad::audio::TagInfo;
using xmad::audio::WriteMp3Id3v2Tags;

namespace {
int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

void AppendSyncSafe32(std::string& s, uint32_t v) {
    s += static_cast<char>((v >> 21) & 0x7F);
    s += static_cast<char>((v >> 14) & 0x7F);
    s += static_cast<char>((v >> 7) & 0x7F);
    s += static_cast<char>(v & 0x7F);
}

void AppendPlain32(std::string& s, uint32_t v) {
    s += static_cast<char>((v >> 24) & 0xFF);
    s += static_cast<char>((v >> 16) & 0xFF);
    s += static_cast<char>((v >> 8) & 0xFF);
    s += static_cast<char>(v & 0xFF);
}

// Builds a minimal ID3v2.3 tag containing one TIT2 frame with the given
// encoding byte + text body.
std::string BuildId3v23(const std::string& frameBody) {
    std::string frame = "TIT2";
    AppendPlain32(frame, static_cast<uint32_t>(frameBody.size()));
    frame += std::string(2, '\0'); // flags
    frame += frameBody;

    std::string tag = "ID3";
    tag += static_cast<char>(3); // version major
    tag += static_cast<char>(0); // version minor
    tag += static_cast<char>(0); // flags
    AppendSyncSafe32(tag, static_cast<uint32_t>(frame.size()));
    tag += frame;
    return tag;
}

// Builds one ID3v2.3 frame: 4-char id, plain-32 size, 2 flag bytes, body.
std::string BuildFrame(const std::string& id, const std::string& body) {
    std::string frame = id;
    AppendPlain32(frame, static_cast<uint32_t>(body.size()));
    frame += std::string(2, '\0'); // flags
    frame += body;
    return frame;
}

// Wraps one or more already-built frames (BuildFrame) in an ID3v2.3 tag
// header.
std::string BuildId3v23Tag(const std::string& frames) {
    std::string tag = "ID3";
    tag += static_cast<char>(3); // version major
    tag += static_cast<char>(0); // version minor
    tag += static_cast<char>(0); // flags
    AppendSyncSafe32(tag, static_cast<uint32_t>(frames.size()));
    tag += frames;
    return tag;
}

// Latin1 (encoding 0) text frame body.
std::string Latin1Body(const std::string& text) { return std::string(1, static_cast<char>(0)) + text; }

std::string Utf16LE(const std::string& ascii) {
    std::string out;
    out += static_cast<char>(0xFF);
    out += static_cast<char>(0xFE); // BOM (LE)
    for (char c : ascii) {
        out += c;
        out += '\0';
    }
    out += std::string(2, '\0'); // null terminator
    return out;
}

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void WriteFile(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

} // namespace

int main() {
    // Latin1 (encoding 0) TIT2.
    {
        std::string frameBody(1, static_cast<char>(0));
        frameBody += "Luv 4 Luv";
        const std::string tag = BuildId3v23(frameBody);
        Check(ParseId3v2Title(tag) == "Luv 4 Luv", "id3v2.3 Latin1 TIT2");
    }

    // UTF-8 (encoding 3) TIT2 - ID3v2.4, but parser doesn't gate encoding
    // byte on tag version (some v2.3 taggers use it anyway).
    {
        std::string frameBody(1, static_cast<char>(3));
        frameBody += "Robin S";
        const std::string tag = BuildId3v23(frameBody);
        Check(ParseId3v2Title(tag) == "Robin S", "encoding 3 (UTF-8) TIT2");
    }

    // UTF-16 with BOM (encoding 1).
    {
        std::string frameBody(1, static_cast<char>(1));
        frameBody += Utf16LE("Show Me Love");
        const std::string tag = BuildId3v23(frameBody);
        Check(ParseId3v2Title(tag) == "Show Me Love", "encoding 1 (UTF-16 LE+BOM) TIT2");
    }

    // No ID3 header at all.
    { Check(ParseId3v2Title("not a tag at all").empty(), "no ID3 header yields empty"); }

    // Frame size larger than the buffer actually holds - must not overread
    // (and ASan/UBSan, if enabled, would catch a real bug here) and must
    // yield empty rather than garbage.
    {
        std::string frame = "TIT2";
        AppendPlain32(frame, 9999); // claims way more data than exists
        frame += std::string(2, '\0');
        frame += "short";
        std::string tag = "ID3";
        tag += static_cast<char>(3);
        tag += static_cast<char>(0);
        tag += static_cast<char>(0);
        AppendSyncSafe32(tag, static_cast<uint32_t>(frame.size()));
        tag += frame;
        Check(ParseId3v2Title(tag).empty(), "oversized frame size doesn't overread / yields empty");
    }

    // A tag with only an unrelated frame (TPE1/artist) - no TIT2 present.
    {
        std::string frame = "TPE1";
        std::string body(1, static_cast<char>(0));
        body += "Some Artist";
        AppendPlain32(frame, static_cast<uint32_t>(body.size()));
        frame += std::string(2, '\0');
        frame += body;
        std::string tag = "ID3";
        tag += static_cast<char>(3);
        tag += static_cast<char>(0);
        tag += static_cast<char>(0);
        AppendSyncSafe32(tag, static_cast<uint32_t>(frame.size()));
        tag += frame;
        Check(ParseId3v2Title(tag).empty(), "tag without TIT2 yields empty");
    }

    // ID3v1: "TAG" + 30-byte title (space-padded) + ... (only title matters here).
    {
        std::string tail = "TAG";
        std::string title = "Finally";
        title.resize(30, ' ');
        tail += title;
        tail.resize(128, '\0');
        Check(ParseId3v1Title(tail) == "Finally", "id3v1 title, space-padded");
    }

    // ID3v1 requires exactly 128 bytes starting with "TAG".
    { Check(ParseId3v1Title("TAGtoo short").empty(), "id3v1 wrong length yields empty"); }
    {
        std::string tail(128, 'x');
        Check(ParseId3v1Title(tail).empty(), "id3v1 missing TAG magic yields empty");
    }

    // ParseId3v2Tags: a tag with TIT2/TPE1/TALB/TCON/TRCK all present
    // extracts every field in one pass, including TRCK's "track/total"
    // format displaying just the track.
    {
        std::string frames = BuildFrame("TIT2", Latin1Body("Show Me Love"));
        frames += BuildFrame("TPE1", Latin1Body("Robin S"));
        frames += BuildFrame("TALB", Latin1Body("Show Me Love"));
        frames += BuildFrame("TCON", Latin1Body("House"));
        frames += BuildFrame("TRCK", Latin1Body("5/12"));
        const std::string tag = BuildId3v23Tag(frames);

        TagInfo t;
        ParseId3v2Tags(tag, t);
        Check(t.title == "Show Me Love", "id3v2 multi-field: title");
        Check(t.artist == "Robin S", "id3v2 multi-field: artist");
        Check(t.album == "Show Me Love", "id3v2 multi-field: album");
        Check(t.genre == "House", "id3v2 multi-field: genre");
        Check(t.track == "5", "id3v2 multi-field: track (total stripped)");
    }

    // ParseId3v2Tags: only some fields present - the rest stay empty
    // rather than getting clobbered with garbage.
    {
        const std::string tag = BuildId3v23Tag(BuildFrame("TPE1", Latin1Body("Some Artist")));
        TagInfo t;
        ParseId3v2Tags(tag, t);
        Check(t.artist == "Some Artist", "id3v2 partial: artist present");
        Check(t.title.empty(), "id3v2 partial: title absent stays empty");
        Check(t.album.empty(), "id3v2 partial: album absent stays empty");
    }

    // ParseId3v1Tags: title/artist/album at their fixed offsets, plain
    // ID3v1 (not the v1.1 track convention) - no track extracted.
    {
        std::string tail = "TAG";
        std::string title = "Finally";
        title.resize(30, ' ');
        std::string artist = "CeCe Peniston";
        artist.resize(30, ' ');
        std::string album = "Finally";
        album.resize(30, ' ');
        tail += title;
        tail += artist;
        tail += album;
        tail.resize(128, '\0'); // year/comment/genre left zeroed

        TagInfo t;
        ParseId3v1Tags(tail, t);
        Check(t.title == "Finally", "id3v1 multi-field: title");
        Check(t.artist == "CeCe Peniston", "id3v1 multi-field: artist");
        Check(t.album == "Finally", "id3v1 multi-field: album");
        Check(t.track.empty(), "id3v1 multi-field: no v1.1 track marker means no track");
        Check(t.genre.empty(), "id3v1 multi-field: genre never decoded (numeric-only)");
    }

    // ParseId3v1Tags: ID3v1.1's track-number convention (comment field's
    // next-to-last byte 0, last byte the track number).
    {
        std::string tail = "TAG";
        tail += std::string(90, ' ');       // title/artist/album (30 each), unused here
        tail += std::string(4, '\0');       // year
        tail += std::string(28, ' ');       // comment (first 28 of the 30-byte field)
        tail += static_cast<char>(0);       // v1.1 marker
        tail += static_cast<char>(7);       // track number
        tail += static_cast<char>(17);      // genre (still not decoded)
        Check(tail.size() == 128, "test fixture: id3v1.1 tail is 128 bytes");

        TagInfo t;
        ParseId3v1Tags(tail, t);
        Check(t.track == "7", "id3v1.1: track number extracted");
    }

    // WriteMp3Id3v2Tags: untagged file gets a fresh ID3v2.3 tag, and the
    // original bytes end up preserved exactly after it.
    {
        const std::string path = "tags_test_new_tag.mp3";
        const std::string audioBytes = "FAKE-MP3-AUDIO-DATA-1234567890";
        WriteFile(path, audioBytes);

        TagInfo t;
        t.title = "New Title";
        Check(WriteMp3Id3v2Tags(path, t), "write: create fresh tag on untagged file succeeds");

        const std::string full = ReadFile(path);
        Check(full.size() > audioBytes.size(), "write: file grew (a tag got prefixed)");
        Check(full.substr(full.size() - audioBytes.size()) == audioBytes,
              "write: original audio bytes preserved exactly after the new tag");
        Check(ReadTrackTags(path).title == "New Title", "write: title round-trips after creating a fresh tag");

        std::remove(path.c_str());
    }

    // WriteMp3Id3v2Tags: editing one field on an existing tag leaves an
    // unrelated (binary-looking, APIC-like) frame byte-for-byte untouched.
    {
        const std::string path = "tags_test_preserve.mp3";
        std::string frames = BuildFrame("TIT2", Latin1Body("Old Title"));
        std::string binaryPayload;
        for (int i = 0; i < 40; ++i) binaryPayload += static_cast<char>(i);
        frames += BuildFrame("APIC", binaryPayload);
        const std::string tag = BuildId3v23Tag(frames);
        const std::string audioBytes = "REST-OF-FILE-AUDIO-BYTES";
        WriteFile(path, tag + audioBytes);

        TagInfo t = ReadTrackTags(path);
        Check(t.title == "Old Title", "write/preserve fixture: title reads back before editing");
        t.title = "Edited Title";
        Check(WriteMp3Id3v2Tags(path, t), "write: edit succeeds on existing v2.3 tag");

        const std::string full = ReadFile(path);
        Check(full.find(binaryPayload) != std::string::npos,
              "write: unrelated (APIC-like) frame bytes preserved untouched");
        Check(full.substr(full.size() - audioBytes.size()) == audioBytes,
              "write: audio data after the tag preserved exactly");
        Check(ReadTrackTags(path).title == "Edited Title", "write: edited title round-trips");

        std::remove(path.c_str());
    }

    // WriteMp3Id3v2Tags: clearing a field to "" removes its frame entirely
    // without disturbing a sibling field.
    {
        const std::string path = "tags_test_clear.mp3";
        std::string frames = BuildFrame("TIT2", Latin1Body("Has A Title"));
        frames += BuildFrame("TPE1", Latin1Body("Has An Artist"));
        WriteFile(path, BuildId3v23Tag(frames) + "AUDIO");

        TagInfo t = ReadTrackTags(path);
        t.title.clear();
        Check(WriteMp3Id3v2Tags(path, t), "write: clearing a field succeeds");

        const TagInfo readBack = ReadTrackTags(path);
        Check(readBack.title.empty(), "write: cleared field's frame is gone after rewrite");
        Check(readBack.artist == "Has An Artist", "write: untouched field survives a sibling's clear");

        std::remove(path.c_str());
    }

    // WriteMp3Id3v2Tags: an ID3v2.2 tag (3-char frame IDs, a shape this
    // writer doesn't parse) is refused rather than guessed at, and the
    // file is left completely untouched.
    {
        const std::string path = "tags_test_v22.mp3";
        std::string tag = "ID3";
        tag += static_cast<char>(2); // version major 2 - unsupported
        tag += static_cast<char>(0);
        tag += static_cast<char>(0);
        AppendSyncSafe32(tag, 4);
        tag += "TT2X"; // stand-in for a v2.2-shaped frame; refused before frames are ever walked
        const std::string original = tag + "AUDIO-UNCHANGED";
        WriteFile(path, original);

        TagInfo t;
        t.title = "Should Not Apply";
        Check(!WriteMp3Id3v2Tags(path, t), "write: v2.2 tag is refused, not guessed at");
        Check(ReadFile(path) == original, "write: file left byte-for-byte unchanged after a refused write");

        std::remove(path.c_str());
    }

    // WriteMp3Id3v2Tags: the unsynchronisation flag is refused the same way.
    {
        const std::string path = "tags_test_unsync.mp3";
        std::string tag = "ID3";
        tag += static_cast<char>(3);
        tag += static_cast<char>(0);
        tag += static_cast<char>(0x80); // unsynchronisation flag set - unsupported
        AppendSyncSafe32(tag, 0);
        const std::string original = tag + "AUDIO-UNCHANGED-2";
        WriteFile(path, original);

        TagInfo t;
        t.title = "Should Not Apply";
        Check(!WriteMp3Id3v2Tags(path, t), "write: unsynchronisation-flagged tag is refused");
        Check(ReadFile(path) == original, "write: file left byte-for-byte unchanged (unsync refused)");

        std::remove(path.c_str());
    }

    // WriteMp3Id3v2Tags: a multi-byte UTF-8 title round-trips exactly.
    {
        const std::string path = "tags_test_utf8.mp3";
        WriteFile(path, "AUDIO-ONLY-NO-TAG");

        TagInfo t;
        t.title = "Caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC"; // "Café 日本"
        Check(WriteMp3Id3v2Tags(path, t), "write: multi-byte UTF-8 title write succeeds");
        Check(ReadTrackTags(path).title == t.title, "write: multi-byte UTF-8 title round-trips exactly");

        std::remove(path.c_str());
    }

    // Security review: ReadMp3Tags must not trust a forged/oversized ID3v2
    // declared tag size and allocate for it before checking the file is
    // actually that large - a ~30-byte file claiming the syncsafe-max
    // (~256 MiB) tag size used to force a transient ~256 MiB allocation
    // here. The forged tag must simply be skipped (same as "no tag").
    {
        const std::string path = "tags_test_forged_size.mp3";
        std::string header = "ID3";
        header += static_cast<char>(3); // version major
        header += static_cast<char>(0); // revision
        header += static_cast<char>(0); // flags
        AppendSyncSafe32(header, 0x0FFFFFFFu); // max syncsafe value (~256 MiB)
        WriteFile(path, header + "TRAILING-BYTES-DONT-MATTER");

        const TagInfo t = ReadTrackTags(path);
        Check(t.title.empty(), "read: forged oversized ID3v2 size yields empty tags, not a huge allocation");

        std::remove(path.c_str());
    }

    // Security review: ScanExistingId3v2's frame-size arithmetic must not
    // wrap in 32 bits. A v2.3 (Plain32) frame whose declared size is
    // within 9 of UINT32_MAX used to make `10 + frameSize` wrap to a tiny
    // value, letting the bounds check that's supposed to reject an
    // oversized/malformed frame silently pass instead - corrupting bytes
    // from that frame (and whatever followed it) into the rewritten tag.
    // It must instead be recognized as malformed and dropped outright.
    {
        const std::string path = "tags_test_frame_overflow.mp3";
        const std::string poisonPayload = "payload that must never end up copied into the new tag";
        std::string frame = "TXXX"; // unmanaged id, so survival is directly observable in the output bytes
        AppendPlain32(frame, 0xFFFFFFFAu); // 10 + this wraps to 0 in 32-bit arithmetic
        frame += std::string(2, '\0');     // flags
        frame += poisonPayload;
        WriteFile(path, BuildId3v23Tag(frame) + "AUDIO");

        TagInfo t;
        t.title = "New Title";
        Check(WriteMp3Id3v2Tags(path, t), "write: succeeds even when the existing tag has an overflow-crafted frame");

        const std::string full = ReadFile(path);
        Check(full.size() >= 10, "write: rewritten file has at least a tag header");
        const auto tagSizeByte = [&](int i) { return static_cast<uint8_t>(full[static_cast<size_t>(6 + i)]); };
        const uint32_t writtenTagSize = (static_cast<uint32_t>(tagSizeByte(0)) << 21) |
                                         (static_cast<uint32_t>(tagSizeByte(1)) << 14) |
                                         (static_cast<uint32_t>(tagSizeByte(2)) << 7) | tagSizeByte(3);
        const uint32_t expectedTit2FrameSize = 10 + 1 + static_cast<uint32_t>(std::string("New Title").size());
        Check(writtenTagSize == expectedTit2FrameSize,
              "write: rewritten tag holds only the fresh TIT2 frame - the overflow-crafted frame was rejected");
        Check(full.find(poisonPayload) == std::string::npos,
              "write: the overflow-crafted frame's payload bytes are not present anywhere in the rewritten file");

        std::remove(path.c_str());
    }

    // --- Tracker module titles (module_file.h) ---
    {
        auto bytes = [](const std::string& s) { return std::vector<char>(s.begin(), s.end()); };
        // MOD: name is bytes 0..19, NUL-padded; anything without an XM/S3M
        // signature is treated as MOD.
        std::string mod = std::string("  Space Debris") + std::string(6, '\0') + std::string(100, 'x');
        Check(DetectModuleFormat(bytes(mod)) == "MOD", "module: no XM/S3M signature detects as MOD");
        Check(ParseModuleTitle(bytes(mod)) == "Space Debris", "module: MOD title trimmed at NUL and leading spaces");

        // XM: "Extended Module: " then the 20-byte name at 17.
        std::string xm = std::string("Extended Module: ") + "Unreal ][         " + "  " + "\x1a" + std::string(40, ' ');
        Check(DetectModuleFormat(bytes(xm)) == "XM", "module: XM signature detected");
        Check(ParseModuleTitle(bytes(xm)) == "Unreal ][", "module: XM title at offset 17, trailing spaces trimmed");

        // S3M: 28-byte name at 0, "SCRM" at 44.
        std::string s3m = std::string("Second Reality") + std::string(14, '\0') + std::string(16, '\0') + "SCRM" +
                          std::string(16, '\0');
        Check(DetectModuleFormat(bytes(s3m)) == "S3M", "module: SCRM at 44 detects as S3M");
        Check(ParseModuleTitle(bytes(s3m)) == "Second Reality", "module: S3M title from 28-byte field");

        // Non-printable bytes (codepage graphics) are dropped, not passed to the font.
        std::string odd = std::string("A\x01\xb0" "B") + std::string(16, '\0') + std::string(40, '\0');
        Check(ParseModuleTitle(bytes(odd)) == "AB", "module: non-printable bytes dropped from title");

        // Blank or truncated headers: "" so callers fall back to the filename.
        Check(ParseModuleTitle(bytes(std::string(64, '\0'))).empty(), "module: all-NUL name yields empty title");
        Check(ParseModuleTitle(bytes("short")).empty(), "module: header shorter than the name field yields empty");
        Check(DetectModuleFormat({}).empty(), "module: empty input has no format");

        // End to end through ReadTrackTitle, including the gzip layer
        // (tests/fixtures/make_tracker_fixtures.py).
        Check(ReadTrackTitle("tests/fixtures/tone.mod") == "MOD Tone Fixture", "module: ReadTrackTitle on .mod");
        Check(ReadTrackTitle("tests/fixtures/tone.mdz") == "MOD Tone Fixture", "module: ReadTrackTitle on zipped .mdz");
        Check(ReadTrackTitle("tests/fixtures/tone.xmz") == "XM Tone Fixture",
              "module: ReadTrackTitle on zipped .xmz picks the module, not the readme before it");
        Check(ReadTrackTitle("tests/fixtures/tone.s3z") == "S3M Tone Fixture", "module: ReadTrackTitle on stored-zip .s3z");
        Check(ReadTrackTitle("tests/fixtures/tone_gzip.mdz") == "MOD Tone Fixture",
              "module: ReadTrackTitle on gzipped .mdz");
        Check(xmad::audio::DetectModuleCompression("tests/fixtures/tone.mdz") == "zip", "module: zip detected");
        Check(xmad::audio::DetectModuleCompression("tests/fixtures/tone_gzip.mdz") == "gzip", "module: gzip detected");
        Check(xmad::audio::DetectModuleCompression("tests/fixtures/tone.mod").empty(), "module: raw has no compression");
        Check(ReadTrackTitle("tests/fixtures/tone.s3m") == "S3M Tone Fixture", "module: ReadTrackTitle on .s3m");
        Check(ReadTrackTitle("tests/fixtures/does_not_exist.xm").empty(), "module: missing file yields empty title");
    }

    if (g_failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
