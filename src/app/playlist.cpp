#include "app/playlist.h"

#include <fstream>
#include <utility>

namespace xmad::app {

void Playlist::RemoveAt(size_t i) {
    if (i >= tracks_.size()) return;
    tracks_.erase(tracks_.begin() + static_cast<long>(i));
    if (tracks_.empty()) {
        currentIndex_ = -1;
    } else if (currentIndex_ >= static_cast<int>(tracks_.size())) {
        currentIndex_ = static_cast<int>(tracks_.size()) - 1;
    }
    // else: currentIndex_ < removed index is untouched; currentIndex_ >
    // removed index now points one slot too high in the *old* numbering,
    // but since everything above `i` shifted down by one, it still names
    // the same track it did before removing i (as long as it wasn't the
    // removed index itself, i.e. currentIndex_ == i, which the original
    // handles by stopping playback first - callers are expected to check
    // that before calling RemoveAt on the playing track).
}

void Playlist::MoveUp(size_t i) {
    if (i == 0 || i >= tracks_.size()) return;
    std::swap(tracks_[i], tracks_[i - 1]);
    if (currentIndex_ == static_cast<int>(i)) {
        currentIndex_ = static_cast<int>(i) - 1;
    } else if (currentIndex_ == static_cast<int>(i) - 1) {
        currentIndex_ = static_cast<int>(i);
    }
}

void Playlist::MoveDown(size_t i) {
    if (i + 1 >= tracks_.size()) return;
    std::swap(tracks_[i], tracks_[i + 1]);
    if (currentIndex_ == static_cast<int>(i)) {
        currentIndex_ = static_cast<int>(i) + 1;
    } else if (currentIndex_ == static_cast<int>(i) + 1) {
        currentIndex_ = static_cast<int>(i);
    }
}

int Playlist::WrappedIndex(int delta) const {
    if (tracks_.empty()) return -1;
    int idx = currentIndex_ + delta;
    const int last = static_cast<int>(tracks_.size()) - 1;
    if (idx < 0) idx = last;
    if (idx > last) idx = 0;
    return idx;
}

bool Playlist::LoadM3U(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        tracks_.push_back(line);
    }
    return true;
}

bool Playlist::SaveM3U(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return false;
    out << "#EXTM3U\n";
    for (const auto& t : tracks_) out << t << "\n";
    return true;
}

} // namespace xmad::app
