#pragma once

#include <string>

// Reads just enough tag metadata to answer "does this track have a real
// title, and if so what is it" - TODO's "PL has the name of the song if
// present, otherwise the filename". Not a general ID3/Vorbis comment
// library, and not an editor (see the already-flagged "ID3 tag editing not
// implemented" divergence) - read-only, title-only, best-effort.

namespace xmad::audio {

// Pure parsing over already-read bytes - no file I/O, so directly
// unit-testable (see tags_test.cpp). Each returns "" if the expected tag
// isn't present or doesn't parse cleanly; ReadTrackTitle's caller (and the
// playlist row renderer) falls back to the filename in that case.
//
// `head` must start at the beginning of the file and be at least as long
// as the ID3v2 tag actually is (10-byte header + its declared size) for a
// tag to be found at all - a short head that cuts off the TIT2 frame just
// yields "", not a crash. Malformed/adversarial frame sizes are bounds-
// checked against `head`'s actual length rather than trusted.
std::string ParseId3v2Title(const std::string& head);

// `tail` must be exactly the file's last 128 bytes - ID3v1 is a fixed-size
// trailer, so anything shorter or from the wrong offset can't be one.
std::string ParseId3v1Title(const std::string& tail);

// Thin I/O + dispatch, by extension: for .mp3, reads the ID3v2 header to
// learn its real size and then exactly that many more bytes (not the
// whole file) before falling back to the last 128 bytes for ID3v1; for
// .flac, decodes a VORBIS_COMMENT TITLE via dr_flac's own metadata
// callback rather than hand-rolling FLAC container parsing. Returns "" on
// any failure, missing file, or absent tag.
std::string ReadTrackTitle(const std::string& path);

} // namespace xmad::audio
