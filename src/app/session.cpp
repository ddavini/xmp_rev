#include "app/session.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace xmad::app {

std::string SerializeSettings(const Settings& s) {
    std::ostringstream out;
    out << "SPECMODE=" << s.specMode << "\n";
    out << "VOLUME=" << s.volumePercent << "\n";
    out << "PLAYING=" << (s.wasPlaying ? 1 : 0) << "\n";
    out << "INDEX=" << s.currentIndex << "\n";
    out << "XSOUND=" << (s.xSound ? 1 : 0) << "\n";
    out << "EQPRESET=" << s.eqPreset << "\n";
    out << "EQBANDS=";
    for (size_t i = 0; i < s.eqBands.size(); ++i) {
        if (i != 0) out << ",";
        out << s.eqBands[i];
    }
    out << "\n";
    out << "VISPANEL=" << s.visPanel << "\n";
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
            else if (key == "INDEX") out.currentIndex = std::stoi(value);
            else if (key == "XSOUND") out.xSound = std::stoi(value) != 0;
            else if (key == "VISPANEL") out.visPanel = std::stoi(value);
            else if (key == "EQPRESET") out.eqPreset = std::stoi(value);
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

namespace {
std::string SettingsDir() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.xmad-revival";
}
} // namespace

std::string SettingsFilePath() { return SettingsDir() + "/settings.cfg"; }
std::string SessionPlaylistPath() { return SettingsDir() + "/session.m3u"; }

bool SaveSettingsFile(const std::string& path, const Settings& s) {
    mkdir(SettingsDir().c_str(), 0755); // ignores EEXIST; failure surfaces via the ofstream below
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << SerializeSettings(s);
    return static_cast<bool>(f);
}

bool LoadSettingsFile(const std::string& path, Settings& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::ostringstream buf;
    buf << f.rdbuf();
    return ParseSettings(buf.str(), out);
}

} // namespace xmad::app
