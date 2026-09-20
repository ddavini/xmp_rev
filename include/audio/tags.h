#pragma once

#include <string>

// Reads just enough tag metadata to answer "does this track have a real
// title, and if so what is it" - TODO's "PL has the name of the song if
// present, otherwise the filename" - plus, for the Info window's "reports
// ID3 values if present", the handful of other fields most players show
// (artist/album/genre/track). Not a general ID3/Vorbis comment library,
// and not an editor (see the already-flagged "ID3 tag editing not
// implemented" divergence) - read-only, best-effort.

namespace xmad::audio {

// Deliberately narrower than the original's frmInfo (which also showed
// VBR/Emphasis/CRC/Padding and Comment): those first four need real MPEG
// frame-header parsing dr_mp3 doesn't expose, and Comment's ID3v2 COMM
// frame has a more involved three-part (language/short-description/text)
// layout not worth the complexity for a rarely-populated field. Genre is
// only ever populated from ID3v2's TCON/FLAC's GENRE= text - ID3v1's
// fallback tail stores genre as a numeric index into a ~192-entry genre
// list, which isn't decoded here (empty rather than a wrong guess).
// Every field is "" if absent, same as ReadTrackTitle's existing
// contract - the Info window only shows a line for a field that's
// actually populated.
struct TagInfo {
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::string track;
};

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

// Same scan as ParseId3v2Title, generalized to collect every field
// TagInfo has (TIT2/TPE1/TALB/TCON/TRCK) in one pass instead of
// early-returning on the first TIT2 - ParseId3v2Title is now a thin
// wrapper over this. Fields already set in `out` are left alone if the
// corresponding frame is absent (so this can be called after
// ParseId3v1Tags to let ID3v2 override rather than clobber with "").
void ParseId3v2Tags(const std::string& head, TagInfo& out);

// `tail` must be exactly the file's last 128 bytes - ID3v1 is a fixed-size
// trailer, so anything shorter or from the wrong offset can't be one.
std::string ParseId3v1Title(const std::string& tail);

// Same fixed-offset layout as ParseId3v1Title, generalized to also pull
// artist (33..62) and album (63..92). Track uses the ID3v1.1 convention
// (comment field's next-to-last byte is 0, last byte is the track
// number) - genre is deliberately not decoded, see TagInfo's comment.
void ParseId3v1Tags(const std::string& tail, TagInfo& out);

// Thin I/O + dispatch, by extension: for .mp3, reads the ID3v2 header to
// learn its real size and then exactly that many more bytes (not the
// whole file) before falling back to the last 128 bytes for ID3v1; for
// .flac, decodes a VORBIS_COMMENT TITLE via dr_flac's own metadata
// callback rather than hand-rolling FLAC container parsing. Returns "" on
// any failure, missing file, or absent tag.
std::string ReadTrackTitle(const std::string& path);

// Same I/O + dispatch as ReadTrackTitle, returning every TagInfo field at
// once instead of just the title - for MP3, ID3v2 fields win over ID3v1
// wherever both are present, field by field, not "whichever tag has a
// title wins for everything" (a file can have a full ID3v2 tag missing
// just album art metadata, say, without losing e.g. ID3v1's own genre -
// though genre is never populated from ID3v1, see TagInfo's comment).
TagInfo ReadTrackTags(const std::string& path);

// Rewrites path's ID3v2 tag so its TIT2/TPE1/TALB/TCON/TRCK frames match
// `tags` field-by-field (a field left "" omits/removes that frame). Any
// other existing frame (APIC album art, COMM, TXXX, ...) is preserved
// byte-for-byte, untouched. Returns false, leaving the file completely
// untouched, when the existing tag is something this can't safely
// round-trip (ID3v2.2, an extended header, or the unsynchronisation flag)
// rather than guessing. A file with no ID3v2 tag gets a fresh ID3v2.3 one.
// Writes via a sibling "<path>.xmadtmp" file, atomically renamed over
// `path` only once fully written - a failure partway through never
// touches the original. MP3 only; FLAC's tags are a different format
// (Vorbis comments) with no write path here.
bool WriteMp3Id3v2Tags(const std::string& path, const TagInfo& tags);

} // namespace xmad::audio
