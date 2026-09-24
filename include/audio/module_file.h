#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Tracker module files (ProTracker MOD, FastTracker II XM, Scream Tracker 3
// S3M) and their compressed variants (.mdz/.xmz/.s3z). Those come in two
// flavors: the classic MODPlug-era ones are ordinary PKZIP archives holding
// the module (the common case - a real collection's .mdz/.xmz files turned
// out to be these, not gzip as first assumed), while some later tools
// wrote plain gzip streams under the same extensions. Both are handled,
// told apart by content. Shared by tracker_decoder.cpp (playback) and
// tags.cpp (title) so both see the same decompressed bytes - neither ibxm
// nor any other tracker library handles the compression itself.

namespace xmad::audio {

// True for mod/xm/s3m/mdz/xmz/s3z (lowercase, no dot - same convention as
// every other extension check in this codebase).
bool IsTrackerExtension(const std::string& ext);

// Reads the file's *decompressed* module bytes: from a zip archive
// ("PK\3\4"), the first entry named .mod/.xm/.s3m (else the largest
// entry), stored or deflated; from a gzip stream, its content; anything
// else is taken as a raw module. Decided by content, not extension, so a
// misnamed file (a plain .mod saved as .mdz, or vice versa) still loads.
// Throws std::runtime_error if the file can't be opened, is corrupt, uses
// an unsupported zip method (pre-PKZIP-2.0 "implode", deflate64,
// encryption), or is/decompresses past `maxBytes` (a zip/gzip bomb guard,
// not an expected case - real modules are a few MB at most).
std::vector<char> ReadModuleFile(const std::string& path, size_t maxBytes);

// Like ReadModuleFile, but stops quietly at `maxBytes` instead of treating
// that as an error - for reading just the header. Never throws; returns
// whatever it could read (possibly empty).
std::vector<char> ReadModuleHead(const std::string& path, size_t maxBytes);

// The original Soundtracker MOD format (1987-1990, before ProTracker) has
// 15 sample slots instead of 31 and no "M.K."-style tag at 1080, so ibxm
// rejects it ("MOD Format not recognised"). If `mod` looks like one, returns
// it rewritten to the 31-sample "M.K." layout ibxm plays - 16 empty sample
// headers padded in after the 15 real ones, "M.K." inserted after the order
// table, pattern and sample data carried over untouched. Anything else
// (a tagged MOD, XM, S3M, garbage) comes back unchanged. Recognized by
// shape, since the format has no signature: volumes <= 64, a 1..128 song
// length, orders < 64 (Soundtracker's pattern limit), and the patterns
// those orders need actually fitting in the file.
// Not emulated: the very first Ultimate Soundtracker's own effect numbering
// (1 = arpeggio, 2 = pitch bend) and its tempo byte at 471 - later
// 15-sample trackers mostly used the ProTracker meanings ibxm assumes.
std::vector<char> ConvertSoundtracker15(const std::vector<char>& mod);

// "zip", "gzip", or "" (uncompressed / unreadable), from the file's first
// bytes - for the Info window's Format line.
std::string DetectModuleCompression(const std::string& path);

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
