#include "app/session.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

namespace xmad::app {

std::string SerializeSettings(const Settings& s) {
    std::ostringstream out;
    out << "SPECMODE=" << s.specMode << "\n";
    out << "VOLUME=" << s.volumePercent << "\n";
    out << "PLAYING=" << (s.wasPlaying ? 1 : 0) << "\n";
    out << "PAUSED=" << (s.wasPaused ? 1 : 0) << "\n";
    out << "INDEX=" << s.currentIndex << "\n";
    // Fixed 3 decimal places (millisecond precision), not operator<<'s
    // default 6-significant-digit precision - the default would silently
    // lose sub-second precision on any position past ~999s (16.6 minutes).
    out << "POSITION=" << std::fixed << std::setprecision(3) << s.positionSeconds << "\n";
    out << "XSOUND=" << (s.xSound ? 1 : 0) << "\n";
    out << "REVERB=" << (s.reverb ? 1 : 0) << "\n";
    out << "SATURATION=" << (s.saturation ? 1 : 0) << "\n";
    out << "COMPRESSION=" << (s.compression ? 1 : 0) << "\n";
    out << "CHORUS=" << (s.chorus ? 1 : 0) << "\n";
    out << "REPEAT=" << (s.repeat ? 1 : 0) << "\n";
    out << "RANDOM=" << (s.random ? 1 : 0) << "\n";
    out << "EQPRESET=" << s.eqPreset << "\n";
    out << "EQBANDS=";
    for (size_t i = 0; i < s.eqBands.size(); ++i) {
        if (i != 0) out << ",";
        out << s.eqBands[i];
    }
    out << "\n";
    out << "VISPANEL=" << s.visPanel << "\n";
    out << "PERSONGEQ=" << (s.perSongEq ? 1 : 0) << "\n";
    out << "UISCALE=" << s.uiScalePercent << "\n";
    out << "POTATOFPS=" << (s.potatoLowFps ? 1 : 0) << "\n";
    out << "POTATOVIS=" << (s.potatoCheapVisualizer ? 1 : 0) << "\n";
    return out.str();
}

bool ParseSettings(const std::string& text, Settings& out) {
    if (text.empty()) return false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        try {
            if (key == "SPECMODE") out.specMode = std::stoi(value);
            else if (key == "VOLUME") out.volumePercent = std::stoi(value);
            else if (key == "PLAYING") out.wasPlaying = std::stoi(value) != 0;
            else if (key == "PAUSED") out.wasPaused = std::stoi(value) != 0;
            else if (key == "INDEX") out.currentIndex = std::stoi(value);
            else if (key == "POSITION") out.positionSeconds = std::stod(value);
            else if (key == "XSOUND") out.xSound = std::stoi(value) != 0;
            else if (key == "REVERB") out.reverb = std::stoi(value) != 0;
            else if (key == "SATURATION") out.saturation = std::stoi(value) != 0;
            else if (key == "COMPRESSION") out.compression = std::stoi(value) != 0;
            else if (key == "CHORUS") out.chorus = std::stoi(value) != 0;
            else if (key == "REPEAT") out.repeat = std::stoi(value) != 0;
            else if (key == "RANDOM") out.random = std::stoi(value) != 0;
            else if (key == "VISPANEL") out.visPanel = std::stoi(value);
            else if (key == "EQPRESET") out.eqPreset = std::stoi(value);
            else if (key == "PERSONGEQ") out.perSongEq = std::stoi(value) != 0;
            else if (key == "UISCALE") out.uiScalePercent = std::stoi(value);
            else if (key == "POTATOFPS") out.potatoLowFps = std::stoi(value) != 0;
            else if (key == "POTATOVIS") out.potatoCheapVisualizer = std::stoi(value) != 0;
            else if (key == "EQBANDS") {
                // Comma-separated, same tolerant spirit as the rest of
                // this parser: fewer/more fields than expected just fills
                // what's present and leaves the rest at their defaults,
                // rather than rejecting the whole line.
                std::istringstream bandsIn(value);
                std::string tok;
                size_t i = 0;
                while (i < out.eqBands.size() && std::getline(bandsIn, tok, ',')) {
                    out.eqBands[i++] = std::stoi(tok);
                }
            }
        } catch (const std::exception&) {
            // Malformed value for a known key - skip it, keep the default.
        }
    }
    return true;
}

std::string SerializeEqPerSong(const std::unordered_map<std::string, std::array<int, 10>>& bands) {
    std::ostringstream out;
    for (const auto& [path, b] : bands) {
        out << path << "=";
        for (size_t i = 0; i < b.size(); ++i) {
            if (i != 0) out << ",";
            out << b[i];
        }
        out << "\n";
    }
    return out.str();
}

bool ParseEqPerSong(const std::string& text, std::unordered_map<std::string, std::array<int, 10>>& out) {
    if (text.empty()) return false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        // Same simple convention as ParseSettings: first '=' splits
        // key/path from value - a path containing '=' is a theoretical
        // edge case not worth the extra escaping complexity here.
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string path = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        std::array<int, 10> b{};
        std::istringstream bandsIn(value);
        std::string tok;
        size_t i = 0;
        try {
            while (i < b.size() && std::getline(bandsIn, tok, ',')) {
                b[i++] = std::stoi(tok);
            }
        } catch (const std::exception&) {
            continue; // malformed bands for this track - skip the whole line
        }
        out[path] = b;
    }
    return true;
}

namespace {
std::string SettingsDir() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.xmad-revival";
}
} // namespace

std::string SettingsFilePath() { return SettingsDir() + "/settings.cfg"; }
std::string SessionPlaylistPath() { return SettingsDir() + "/session.m3u"; }
std::string EqPerSongPath() { return SettingsDir() + "/eq_per_song.cfg"; }

bool SaveSettingsFile(const std::string& path, const Settings& s) {
    mkdir(SettingsDir().c_str(), 0755); // ignores EEXIST; failure surfaces via the ofstream below
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << SerializeSettings(s);
    return static_cast<bool>(f);
}

bool SaveEqPerSongFile(const std::string& path, const std::unordered_map<std::string, std::array<int, 10>>& bands) {
    mkdir(SettingsDir().c_str(), 0755); // ignores EEXIST; failure surfaces via the ofstream below
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << SerializeEqPerSong(bands);
    return static_cast<bool>(f);
}

bool LoadEqPerSongFile(const std::string& path, std::unordered_map<std::string, std::array<int, 10>>& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::ostringstream buf;
    buf << f.rdbuf();
    return ParseEqPerSong(buf.str(), out);
}

bool LoadSettingsFile(const std::string& path, Settings& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::ostringstream buf;
    buf << f.rdbuf();
    return ParseSettings(buf.str(), out);
}

} // namespace xmad::app
