#include "app/playlist.h"

#include <fstream>
#include <utility>

namespace xmad::app {

void Playlist::RemoveAt(size_t i) {
    if (i >= tracks_.size()) return;
    tracks_.erase(tracks_.begin() + static_cast<long>(i));
    ++generation_;
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
    ++generation_;
    if (currentIndex_ == static_cast<int>(i)) {
        currentIndex_ = static_cast<int>(i) - 1;
    } else if (currentIndex_ == static_cast<int>(i) - 1) {
        currentIndex_ = static_cast<int>(i);
    }
}

void Playlist::MoveDown(size_t i) {
    if (i + 1 >= tracks_.size()) return;
    std::swap(tracks_[i], tracks_[i + 1]);
    ++generation_;
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
    // A relative entry is resolved against the M3U file's own directory,
    // not the process's cwd - the only interpretation that makes a
    // playlist usable independently of wherever the app happens to be
    // launched from, and what every other M3U-reading player does. Bare
    // filenames (no path at all) are the common case for a playlist saved
    // alongside its own tracks - previously these only "worked" by
    // coincidence when cwd already happened to be that same directory.
    const auto slash = path.find_last_of('/');
    const std::string dir = slash == std::string::npos ? "" : path.substr(0, slash + 1);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const bool isAbsolute = line[0] == '/';
        tracks_.push_back(isAbsolute || dir.empty() ? line : dir + line);
        ++generation_;
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
