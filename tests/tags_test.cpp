// Covers ParseId3v2Title/ParseId3v1Title, the pure parts of title reading -
// ReadTrackTitle's file I/O (and dr_flac's own VORBIS_COMMENT decoding, not
// reimplemented here) aren't exercised by this binary; see decoder_test /
// a manual check against a real tagged file for that (audio/tags.h).

#include "audio/tags.h"

#include <cstdio>
#include <cstring>
#include <string>

using xmad::audio::ParseId3v1Tags;
using xmad::audio::ParseId3v1Title;
using xmad::audio::ParseId3v2Tags;
using xmad::audio::ParseId3v2Title;
using xmad::audio::TagInfo;

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

    if (g_failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
