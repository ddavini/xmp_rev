// Covers ParseId3v2Title/ParseId3v1Title, the pure parts of title reading -
// ReadTrackTitle's file I/O (and dr_flac's own VORBIS_COMMENT decoding, not
// reimplemented here) aren't exercised by this binary; see decoder_test /
// a manual check against a real tagged file for that (audio/tags.h).

#include "audio/tags.h"

#include <cstdio>
#include <cstring>
#include <string>

using xmad::audio::ParseId3v1Title;
using xmad::audio::ParseId3v2Title;

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

    if (g_failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
