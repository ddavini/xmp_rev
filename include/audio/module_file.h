#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Tracker module files (ProTracker MOD, FastTracker II XM, Scream Tracker 3
// S3M) and their gzip-wrapped variants (.mdz/.xmz/.s3z - OpenMPT's
// space-saving convention, gzip(.mod)/gzip(.xm)/gzip(.s3m)). Shared by
// tracker_decoder.cpp (playback) and tags.cpp (title) so both see the same
// decompressed bytes - neither ibxm nor any other tracker library handles
// the gzip layer itself.

namespace xmad::audio {

// True for mod/xm/s3m/mdz/xmz/s3z (lowercase, no dot - same convention as
// every other extension check in this codebase).
bool IsTrackerExtension(const std::string& ext);

// Reads up to `maxBytes` of the file's *decompressed* content. Goes through
// zlib's gzread, which inflates a gzip stream and passes any other file
// through byte-for-byte, so a misnamed file (a plain .mod saved as .mdz, or
// vice versa) still loads - deciding by content instead of trusting the
// extension. Throws std::runtime_error if the file can't be opened, is a
// corrupt gzip stream, or decompresses past `maxBytes` (a gzip bomb guard,
// not an expected case - real modules are a few MB at most).
std::vector<char> ReadModuleFile(const std::string& path, size_t maxBytes);

// Like ReadModuleFile, but stops quietly at `maxBytes` instead of treating
// that as an error - for reading just the header. Never throws; returns
// whatever it could read (possibly empty).
std::vector<char> ReadModuleHead(const std::string& path, size_t maxBytes);

// Pure parsing over already-decompressed bytes (see tags_test.cpp). The
// song name lives at a fixed offset whose position depends on the format,
// detected the same way ibxm's module_load() does: "Extended Module:" at 0
// is XM (name at 17, 20 bytes), "SCRM" at 44 is S3M (name at 0, 28 bytes),
// anything else is MOD (name at 0, 20 bytes). Stops at the first NUL,
// drops non-printable bytes, trims surrounding spaces. "" if too short or
// the name is blank - callers fall back to the filename, as for MP3/FLAC.
std::string ParseModuleTitle(const std::vector<char>& head);

// Short format label for the Info window ("MOD"/"XM"/"S3M"), same content
// detection as ParseModuleTitle. "" if `head` is too short to tell.
std::string DetectModuleFormat(const std::vector<char>& head);

} // namespace xmad::audio
