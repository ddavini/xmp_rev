#pragma once

#include <string>
#include <vector>

// In-memory playlist model. The original persisted the list to a per-entry
// INI-style temp file (Mp3TmpFiles, sections "FILES0"/"FILES1"... keys
// "F0"/"F1"... - see FunzioniGlobali.bas's GetFile/SetFile/NFile) as a
// 2002-era crash-recovery mechanism. That storage format is an
// implementation detail, not behavior worth carrying forward - this is
// just a normal in-memory vector. What *is* ported faithfully is the
// observable behavior: MoveInMp3's wraparound at the ends, and
// SpostaItem's adjacent-swap-with-index-tracking.

namespace xmad::app {

class Playlist {
public:
    size_t size() const { return tracks_.size(); }
    bool empty() const { return tracks_.empty(); }
    const std::string& at(size_t i) const { return tracks_.at(i); }

    void Add(std::string path) { tracks_.push_back(std::move(path)); }

    void Clear() {
        tracks_.clear();
        currentIndex_ = -1;
    }

    // Mirrors Listone.frm's ListaMp3_KeyPress("d"): remove the entry,
    // keep currentIndex_ pointing at the same *track* where possible.
    void RemoveAt(size_t i);

    // Mirrors modFunzioniAccessorie.bas's SpostaItem: swap with the
    // neighbor in that direction, adjusting currentIndex_ if it pointed at
    // either swapped slot. No-op at the relevant boundary.
    void MoveUp(size_t i);   // swap (i, i-1)
    void MoveDown(size_t i); // swap (i, i+1)

    int currentIndex() const { return currentIndex_; }
    void SetCurrentIndex(int i) { currentIndex_ = i; }

    // Mirrors MoveInMp3: one step past the last entry wraps to 0, one step
    // before the first wraps to the last entry. Returns -1 if empty.
    int WrappedIndex(int delta) const;

    // Standard M3U (a portable, well-known format) rather than the
    // original's idiosyncratic .ls layout - #EXTM3U/#EXTINF lines are
    // skipped on read and not written, since duration/title metadata isn't
    // tracked here.
    bool LoadM3U(const std::string& path);
    bool SaveM3U(const std::string& path) const;

private:
    std::vector<std::string> tracks_;
    int currentIndex_ = -1;
};

} // namespace xmad::app
