/**
 * @file Export.cpp
 * @brief The cues and the DJ loops of an export (Export.h).
 */
#include "parh/Export.h"
#include "parh/Engine.h"
#include "parh/WavWriter.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace parh {

namespace {

/** @brief @p s escaped for a JSON string. */
std::string jsonEscape(const std::string& s)
{
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
        else o += c;
    }
    return o;
}

/** @brief @p s escaped for XML. */
std::string xmlEscape(const std::string& s)
{
    std::string o;
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        default: o += c; break;
        }
    }
    return o;
}

/** @brief A file URI as rekordbox writes them: file://localhost/G:/dir/name.wav, reserved characters percent-encoded. */
std::string fileUri(const std::string& path)
{
    std::error_code ec;
    std::string p = std::filesystem::absolute(std::filesystem::path(path), ec).generic_string();
    if (ec) p = path;
    std::string o = "file://localhost/";
    for (unsigned char c : p) {
        if (std::isalnum(c) || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
        else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            o += buf;
        }
    }
    return o;
}

} // namespace

void trackCues(const TrackInfo& t, double at, const TempoMap& tempo, const std::string& prefix, std::vector<CueAt>& out)
{
    const auto sec = [&](int bar) { return tempo.secondsAt(at + 4.0 * bar); };
    if (t.m_bassBar > 0) out.push_back({ sec(t.m_bassBar), prefix + "Bass in" });
    for (const auto& b : t.m_breakdowns) out.push_back({ sec(b.first), prefix + (b.second - b.first >= 16 ? "Breakdown" : "Break") });
    for (int d : t.m_drops) out.push_back({ sec(d), prefix + (d == t.m_mainDropBar ? "Main drop" : "Drop") });
    if (t.m_outroBar < t.m_bars) out.push_back({ sec(t.m_outroBar), prefix + "Outro" });
    std::sort(out.begin(), out.end(), [](const CueAt& a, const CueAt& b) { return a.m_seconds < b.m_seconds; });
}

std::vector<CueAt> setCues(const SetInfo& si, const TempoMap& tm)
{
    std::vector<CueAt> cues;
    for (size_t i = 0; i < si.m_tracks.size(); ++i) {
        const SetTrack& t = si.m_tracks[i];
        const std::string name = "T" + std::to_string(i + 1);
        cues.push_back({ tm.secondsAt(t.m_start), name + " " + t.m_info.m_style + " " + t.m_info.m_camelot });
        if (i > 0) cues.push_back({ tm.secondsAt(t.m_swapIn), "Swap to " + name });
        if (t.m_info.m_mainDropBar >= 0) cues.push_back({ tm.secondsAt(t.m_start + 4.0 * t.m_info.m_mainDropBar), name + " main drop" });
    }
    for (const SetTease& s : si.m_teases) cues.push_back({ tm.secondsAt(s.m_start), "Tease of T" + std::to_string(s.m_from + 1) });
    std::sort(cues.begin(), cues.end(), [](const CueAt& a, const CueAt& b) { return a.m_seconds < b.m_seconds; });
    return cues;
}

bool writeCuesJson(const std::string& path, const std::vector<CueAt>& cues, double rate)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    std::fprintf(f, "[\n");
    for (size_t i = 0; i < cues.size(); ++i)
        std::fprintf(f, "{\"seconds\":%.4f,\"sample\":%lld,\"label\":\"%s\"}%s\n", cues[i].m_seconds,
                     static_cast<long long>(std::llround(cues[i].m_seconds * rate)), jsonEscape(cues[i].m_label).c_str(),
                     i + 1 < cues.size() ? "," : "");
    std::fprintf(f, "]\n");
    return std::fclose(f) == 0;
}

bool writeRekordboxXml(const std::string& path, const std::string& wavPath, const std::string& title, const std::string& tonality,
                       const std::vector<CueAt>& cues, const TempoMap& tempo, double lengthBeats, double rate)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    const double seconds = tempo.secondsAt(lengthBeats);
    std::fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<DJ_PLAYLISTS Version=\"1.0.0\">\n");
    std::fprintf(f, "  <PRODUCT Name=\"Parhelion\" Version=\"%s\" Company=\"Rene Weller\"/>\n", PARH_VERSION);
    std::fprintf(f, "  <COLLECTION Entries=\"1\">\n");
    std::fprintf(f, "    <TRACK TrackID=\"1\" Name=\"%s\" Artist=\"Parhelion\" Genre=\"Trance\" Kind=\"WAV File\" TotalTime=\"%d\" "
                    "AverageBpm=\"%.2f\" SampleRate=\"%d\" BitRate=\"%d\" Tonality=\"%s\" Location=\"%s\">\n",
                 xmlEscape(title).c_str(), static_cast<int>(std::lround(seconds)), tempo.bpmAt(0.0), static_cast<int>(rate),
                 static_cast<int>(rate * 2 * 24 / 1000), xmlEscape(tonality).c_str(), xmlEscape(fileUri(wavPath)).c_str());
    // The beat grid: a TEMPO from every bar whose tempo differs from the last one written (a set's ramps bar by bar).
    double last = -1.0;
    for (double beat = 0.0; beat < lengthBeats - 1e-9; beat += 4.0) {
        const double a = tempo.secondsAt(beat), b = tempo.secondsAt(std::min(lengthBeats, beat + 4.0));
        const double beats = std::min(4.0, lengthBeats - beat);
        const double bpm = b > a ? beats * 60.0 / (b - a) : tempo.bpmAt(beat);
        if (std::fabs(bpm - last) < 0.005) continue;
        std::fprintf(f, "      <TEMPO Inizio=\"%.3f\" Bpm=\"%.2f\" Metro=\"4/4\" Battito=\"1\"/>\n", a, bpm);
        last = bpm;
    }
    // Every cue a memory cue; the first eight also hot cues A to H.
    for (const CueAt& c : cues)
        std::fprintf(f, "      <POSITION_MARK Name=\"%s\" Type=\"0\" Start=\"%.3f\" Num=\"-1\"/>\n", xmlEscape(c.m_label).c_str(), c.m_seconds);
    for (size_t i = 0; i < cues.size() && i < 8; ++i)
        std::fprintf(f, "      <POSITION_MARK Name=\"%s\" Type=\"0\" Start=\"%.3f\" Num=\"%zu\" Red=\"40\" Green=\"160\" Blue=\"240\"/>\n",
                     xmlEscape(cues[i].m_label).c_str(), cues[i].m_seconds, i);
    std::fprintf(f, "    </TRACK>\n  </COLLECTION>\n  <PLAYLISTS>\n    <NODE Type=\"0\" Name=\"ROOT\" Count=\"1\">\n"
                    "      <NODE Name=\"Parhelion\" Type=\"1\" KeyType=\"0\" Entries=\"1\">\n        <TRACK Key=\"1\"/>\n"
                    "      </NODE>\n    </NODE>\n  </PLAYLISTS>\n</DJ_PLAYLISTS>\n");
    return std::fclose(f) == 0;
}

bool renderLoops(const ParamStore& knobs, const Score& track, const TrackInfo& info, const std::string& dir, double rate)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    struct Loop { const char* m_name; int m_bar; };
    const Loop loops[3] = { { "intro", std::max(0, info.m_introBars - 8) }, { "drop", std::max(0, info.m_mainDropBar) },
                            { "outro", std::min(info.m_outroBar, std::max(0, info.m_bars - 8)) } };
    for (const Loop& lp : loops) {
        const double src = 4.0 * lp.m_bar, len = 32.0;
        Score loop;
        loop.clear(track.m_tempo.bpmAt(src));
        loop.m_seed = track.m_seed;
        loop.m_keyRoot = track.m_keyRoot;
        loop.m_scale = track.m_scale;
        for (int pass = 0; pass < 3; ++pass)
            for (NoteEvent n : track.m_notes)
                if (n.m_beat >= src - 0.05 && n.m_beat < src + len - 0.05) { n.m_beat = n.m_beat - src + pass * len; loop.m_notes.push_back(n); }
        // Every knob where the track has it at the loop's start.
        std::vector<int> ids;
        for (const Gesture& g : track.m_gestures) ids.push_back(g.m_param);
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        for (int id : ids) {
            Gesture g;
            g.m_param = id;
            g.m_beat = 0.0;
            g.m_length = 0.0;
            g.m_from = g.m_to = track.gestureOffset(id, src + 0.01);
            g.m_shape = GestureShapeE::STEP;
            loop.m_gestures.push_back(g);
        }
        LevelMark m;
        if (!track.m_levels.empty()) m = track.m_levels.front();
        m.m_beat = 0.0;
        m.m_peakBeat = 0.0;
        m.m_trimDb = track.trimAt(src);
        m.m_balDb = track.balanceAt(src);
        m.m_breakDb = m.m_buildDb = 0.0f;
        loop.m_levels.push_back(m);
        for (KnobSet k : track.m_knobs) if (k.m_beat <= src + 1e-9) { k.m_beat = 0.0; loop.m_knobs.push_back(k); }
        loop.m_lengthBeats = 3.0 * len;
        loop.sort();
        auto e = std::make_unique<Engine>();
        e->params().copyValuesFrom(knobs);
        e->prepare(rate, 512);
        e->load(loop);
        const int64_t from = std::llround(loop.m_tempo.secondsAt(2.0 * len) * rate);
        const int64_t count = std::llround(loop.m_tempo.secondsAt(len) * rate);
        WavWriter w;
        if (!w.open((dir + "/loop_" + lp.m_name + ".wav").c_str(), static_cast<int>(rate), 2, WavFormatE::PCM24)) return false;
        std::vector<float> L(512), R(512);
        for (int64_t at = 0; at < from + count;) {
            const int n = static_cast<int>(std::min<int64_t>(512, from + count - at));
            e->process(L.data(), R.data(), n);
            const int skip = static_cast<int>(std::clamp<int64_t>(from - at, 0, n));   // only the last pass is written
            if (skip < n) w.write(L.data() + skip, R.data() + skip, n - skip);
            at += n;
        }
        w.close();
    }
    return true;
}

} // namespace parh
