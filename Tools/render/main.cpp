/**
 * @file main.cpp
 * @brief parh_render: composes a track from a seed and renders it offline -- the determinism oracle, the MIDI export,
 *        the stems, the loudness of every section.
 *
 * Usage:
 *   parh_render [--seed N] [--style S] [--bpm B] [--minutes M] [--set "key=value; ..."] [--reroll unit]
 *               [--save-set f.parhset] [--load-set f.parhset] [--study] [--dj minutes]
 *               [--out track.wav] [--midi track.mid] [--stems dir] [--loops dir] [--plan-json f.json]
 *               [--seconds S] [--rate 48000] [--block 512] [--bench] [--quality desktop|quest] [--plan] [--list] [--version]
 *
 * --seconds renders only the first S seconds (how a track or a mix begins: 01.10.2026).
 *
 * A track is composed from its seed (compose/Composer.h); --study renders the fixed study of Phase 1 instead; --dj (or
 * set.minutes) a DJ set of that many minutes (compose/Set.h), every track levelled before it is placed. Beside the WAV
 * the cues go into its cue chunk, into \<out\>.cues.json and into a rekordbox collection \<out\>.rekordbox.xml (the beat
 * grid, the breakdowns and drops as memory cues); --loops writes the intro's, the main drop's and the outro's eight bars
 * as seamless loops (Export.h); --plan-json the plan as JSON (the style, the key, the tempo, every section with its
 * bar and second; a set's tracks with theirs) for Tools/eval_report.py. Without
 * --out nothing is written and the render only measures: the loudness of the whole and of every section, and the
 * distance between the main breakdown's and the drop's loudest three seconds (the references: 0.5 to 4 LU).
 *
 * @note The frame (arguments, stems, the measurement, the timing) follows Totality `Tools/render/main.cpp` at 4d3c0d2
 *       (29.09.2026).
 * @author Rene Weller
 *
 * @par Compile Flags:
 *   @arg @c PARH_PROFILE - prints what each stage of the engine cost after a render (Profile.h)
 */
#include "parh/Engine.h"
#include "parh/Export.h"
#include "parh/Leveler.h"
#include "parh/Loudness.h"
#include "parh/Midi.h"
#include "parh/Presets.h"
#include "parh/Profile.h"
#include "parh/WavWriter.h"
#include "parh/SetFile.h"
#include "parh/compose/Composer.h"
#include "parh/compose/Set.h"
#include "parh/compose/Study.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace parh;

namespace {

/** @brief Prints the command line. */
void usage()
{
    std::printf("parh_render [--seed N] [--style Uplifting|Progressive|Dream House|Acid|Deep] [--bpm B] [--minutes M]\n"
                "            [--set \"key=value; ...\"] [--reroll unit] [--save-set f.parhset] [--load-set f.parhset] [--study]\n"
                "            [--dj minutes] [--out track.wav] [--midi track.mid] [--stems dir] [--loops dir] [--plan-json f.json]\n"
                "            [--seconds S]  (its first S seconds only)  [--rate 48000]\n"
                "            [--block 512] [--bench]\n"
                "            [--quality desktop|quest] [--plan] [--list] [--dump-params f.json] [--version]\n");
    std::printf("units for --reroll:");
    for (const char* u : UnitNames) std::printf(" %s", u);
    std::printf(" section<n>; in a set track<n>, track<n>.<unit> and set\n");
}

/**
 * @brief Prints the plan: the sections and the layer matrix, one row per element, one column per 8-bar block.
 * @param[in] sc  the score
 */
void printPlan(const Score& sc)
{
    std::printf("sections:\n");
    for (const Section& s : sc.m_sections)
        std::printf("  bar %3d  %-9s %3d bars  energy %.0f -> %.0f\n", static_cast<int>(s.m_beat / 4.0), SectionNames[static_cast<int>(s.m_kind)],
                    static_cast<int>(s.m_length / 4.0), s.m_energyFrom, s.m_energyTo);
    std::printf("layer matrix (8-bar blocks; # on, - filtered):\n");
    for (int l = 0; l < NumLayers; ++l) {
        std::string row;
        bool any = false;
        for (const LayerBlock& b : sc.m_layers) {
            const LayerStateE s = b.m_state[static_cast<size_t>(l)];
            row += s == LayerStateE::ON ? '#' : s == LayerStateE::FILTERED ? '-' : '.';
            any = any || s != LayerStateE::OFF;
        }
        if (any) std::printf("  %-10s %s\n", LayerNames[l], row.c_str());
    }
}

/**
 * @brief The label of a synth (its bank's): "kick", "lead", "perc3".
 * @param[in] module    the module
 * @param[in] instance  its instance
 * @return the label
 */
std::string synthLabel(int module, int instance)
{
    const ModuleE m = static_cast<ModuleE>(module);
    if (m == ModuleE::POLY) return PolyInstanceNames[instance];
    if (m == ModuleE::PERC) return "perc" + std::to_string(instance + 1);
    static const std::pair<ModuleE, const char*> Names[] = { { ModuleE::KICK, "kick" }, { ModuleE::SUB, "sub" }, { ModuleE::BASS, "bass" },
        { ModuleE::ACID, "acid" }, { ModuleE::PIANO, "piano" }, { ModuleE::STRINGS, "strings" }, { ModuleE::CHOIR, "choir" },
        { ModuleE::BRASS, "brass" }, { ModuleE::TIMPANI, "timpani" }, { ModuleE::SFX, "sfx" }, { ModuleE::CLOUD, "cloud" } };
    for (const auto& [mm, name] : Names) if (mm == m) return name;
    return "?";
}

/**
 * @brief The presets a track plays from beat @p from (its SoundPicks there): "lead Anthem Supersaw / Golden Zenith",
 *        ...
 * @param[in] sc    the score
 * @param[in] from  the track's first beat
 * @return (synth, preset) pairs
 */
std::vector<std::pair<std::string, std::string>> soundsAt(const Score& sc, double from)
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const SoundPick& s : sc.m_sounds) {
        if (s.m_beat != from) continue;
        const std::vector<SoundPreset>& ps = factoryPresets(static_cast<ModuleE>(s.m_module), s.m_instance);
        if (s.m_preset < 0 || static_cast<size_t>(s.m_preset) >= ps.size()) continue;
        out.push_back({ synthLabel(s.m_module, s.m_instance), ps[static_cast<size_t>(s.m_preset)].m_group + " / " + ps[static_cast<size_t>(s.m_preset)].m_name });
    }
    return out;
}

/**
 * @brief One track's plan as a JSON object: what the evaluation compares the audio with.
 * @param[in] sc     the score
 * @param[in] info   the track's facts
 * @param[in] at     the track's first beat
 * @param[in] tempo  the tempo map
 * @return the JSON object
 */
std::string trackJson(const Score& sc, const TrackInfo& info, double at, const TempoMap& tempo)
{
    auto sec = [&](double beat) { return tempo.secondsAt(at + beat); };
    std::string s = "{\"style\":\"" + info.m_style + "\",\"bpm\":" + std::to_string(info.m_bpm) + ",\"key\":" + std::to_string(info.m_key)
                  + ",\"scale\":" + std::to_string(info.m_scale) + ",\"camelot\":\"" + info.m_camelot + "\",\"bars\":" + std::to_string(info.m_bars)
                  + ",\"start\":" + std::to_string(sec(0.0)) + ",\"main_drop_bar\":" + std::to_string(info.m_mainDropBar)
                  + ",\"sections\":[";
    for (size_t i = 0; i < sc.m_sections.size(); ++i) {
        const Section& x = sc.m_sections[i];
        s += std::string(i ? "," : "") + "{\"kind\":\"" + SectionNames[static_cast<int>(x.m_kind)] + "\",\"bar\":" + std::to_string(static_cast<int>(x.m_beat / 4.0))
           + ",\"bars\":" + std::to_string(static_cast<int>(x.m_length / 4.0)) + ",\"seconds\":" + std::to_string(sec(x.m_beat)) + "}";
    }
    s += "],\"sounds\":{";
    const auto sounds = soundsAt(sc, 0.0);
    for (size_t i = 0; i < sounds.size(); ++i) s += std::string(i ? "," : "") + "\"" + sounds[i].first + "\":\"" + sounds[i].second + "\"";
    return s + "}}";
}

/**
 * @brief Writes @p text to @p path; false if it cannot.
 * @param[in] path  the file's path
 * @param[in] text  the text
 * @return false if it cannot be written
 */
bool writeText(const std::string& path, const std::string& text)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    std::fwrite(text.data(), 1, text.size(), f);
    return std::fclose(f) == 0;
}

} // namespace

/**
 * @brief Renders what the command line asks for; the exit code is 0 on success.
 * @param[in]     argc  the number of arguments
 * @param[in,out] argv  the arguments: sections to run
 * @return the number of failures
 */
int main(int argc, char** argv)
{
    uint64_t seed = 1;
    double rate = 48000.0;
    int block = 512;
    float bpm = 0.0f, minutes = 0.0f;
    bool bench = false, quest = false, planOnly = false, list = false, study = false, seedGiven = false;
    std::string dump;
    std::string set, out, midi, stems, style, saveSetPath, loadSetPath, loopsDir, planJson;
    double djArg = 0.0, seconds = 0.0;
    Curation curation;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            return argv[++i];
        };
        if (!std::strcmp(a, "--seed")) { seed = std::strtoull(next(), nullptr, 10); seedGiven = true; }
        else if (!std::strcmp(a, "--style")) style = next();
        else if (!std::strcmp(a, "--minutes")) minutes = static_cast<float>(std::atof(next()));
        else if (!std::strcmp(a, "--reroll")) curation.reroll(next());
        else if (!std::strcmp(a, "--save-set")) saveSetPath = next();
        else if (!std::strcmp(a, "--load-set")) loadSetPath = next();
        else if (!std::strcmp(a, "--study")) study = true;
        else if (!std::strcmp(a, "--dj")) djArg = std::atof(next());
        else if (!std::strcmp(a, "--seconds")) seconds = std::atof(next());
        else if (!std::strcmp(a, "--loops")) loopsDir = next();
        else if (!std::strcmp(a, "--plan-json")) planJson = next();
        else if (!std::strcmp(a, "--bpm")) bpm = static_cast<float>(std::atof(next()));
        else if (!std::strcmp(a, "--set")) { set += next(); set += ";"; }
        else if (!std::strcmp(a, "--out")) out = next();
        else if (!std::strcmp(a, "--midi")) midi = next();
        else if (!std::strcmp(a, "--stems")) stems = next();
        else if (!std::strcmp(a, "--rate")) rate = std::atof(next());
        else if (!std::strcmp(a, "--block")) block = std::max(1, std::atoi(next()));
        else if (!std::strcmp(a, "--bench")) bench = true;
        else if (!std::strcmp(a, "--quality")) quest = !std::strcmp(next(), "quest");
        else if (!std::strcmp(a, "--plan")) planOnly = true;
        else if (!std::strcmp(a, "--list")) list = true;
        else if (!std::strcmp(a, "--dump-params")) dump = next();
        else if (!std::strcmp(a, "--version")) { std::printf("Parhelion %s\n", PARH_VERSION); return 0; }
        else { usage(); return 2; }
    }

    auto engine = std::make_unique<Engine>();
    ParamStore& p = engine->params();
    if (list) {
        for (int id = 0; id < p.count(); ++id)
            std::printf("%-28s %s\n", p.key(id).c_str(), p.format(id).c_str());
        return 0;
    }
    if (!dump.empty()) {
        // One object per parameter: the key, the descriptor, the default of this instance (Tools/manual; after Totality's).
        FILE* f = std::fopen(dump.c_str(), "wb");
        if (f == nullptr) { std::fprintf(stderr, "cannot write %s\n", dump.c_str()); return 1; }
        auto quoted = [](const char* s) {
            std::string o = "\"";
            for (const char* c = s != nullptr ? s : ""; *c != 0; ++c) { if (*c == '"' || *c == '\\') o += '\\'; o += *c; }
            return o + "\"";
        };
        static const char* const curves[] = { "linear", "log", "int", "choice", "toggle" };
        std::fprintf(f, "[\n");
        for (int id = 0; id < p.count(); ++id) {
            const ParamDesc& d = p.desc(id);
            std::string choices = "[]";
            if (d.m_curve == CurveE::CHOICE && d.m_choices != nullptr) {
                choices = "[";
                for (int c = 0; c <= static_cast<int>(d.m_maxValue); ++c) choices += (c ? ", " : "") + quoted(d.m_choices[c]);
                choices += "]";
            }
            std::fprintf(f, "  {\"key\": %s, \"name\": %s, \"unit\": %s, \"min\": %g, \"max\": %g, \"default\": %g, \"curve\": \"%s\", \"choices\": %s}%s\n",
                         quoted(p.key(id).c_str()).c_str(), quoted(d.m_name).c_str(), quoted(d.m_unit).c_str(),
                         static_cast<double>(d.m_minValue), static_cast<double>(d.m_maxValue), static_cast<double>(p.defaultValue(id)),
                         curves[static_cast<int>(d.m_curve)], choices.c_str(), id + 1 < p.count() ? "," : "");
        }
        std::fprintf(f, "]\n");
        std::fclose(f);
        return 0;
    }
    std::string error;
    if (!loadSetPath.empty()) {
        SetFile sf;
        if (!loadSet(loadSetPath.c_str(), sf, p, &error)) { std::fprintf(stderr, "--load-set: %s\n", error.c_str()); return 2; }
        if (!seedGiven) seed = sf.m_seed;
        for (const auto& [u, n] : sf.m_curation.m_rerolls) curation.m_rerolls[u] += n;
    }
    if (!style.empty()) set += "compose.style=" + style + ";";
    if (bpm > 0.0f || minutes > 0.0f) set += "compose.auto=0;";
    if (bpm > 0.0f) set += "compose.bpm=" + std::to_string(bpm) + ";";
    if (minutes > 0.0f) set += "compose.minutes=" + std::to_string(minutes) + ";";
    if (!set.empty() && !p.parseText(set, &error)) { std::fprintf(stderr, "--set: %s\n", error.c_str()); return 2; }
    if (!saveSetPath.empty()) {
        SetFile sf;
        sf.m_seed = seed;
        sf.m_curation = curation;
        if (!saveSet(saveSetPath.c_str(), sf, p)) { std::fprintf(stderr, "cannot write %s\n", saveSetPath.c_str()); return 1; }
    }

    const auto t0 = std::chrono::steady_clock::now();
    const double djMinutes = djArg > 0.0 ? djArg : p.get(p.id(ModuleE::SET, 0, set::MINUTES));
    const bool isSet = djMinutes > 0.0 && !study;
    TrackInfo info;
    Score score;
    std::vector<CueAt> cues;
    std::string title, tonality;
    TempoMap cueTempo;
    double cueLength = 0.0;
    auto t1 = std::chrono::steady_clock::now();
    if (isSet) {
        // A set (PLAN 6.8): every track levelled on its own before it is placed, then mixed on the decks.
        SetInfo si;
        auto level = [&](Score& s) { if (!bench && !planOnly) levelScore(s, p); };
        const SetScore setScore = composeSet(p, seed, djMinutes, &curation, &si, nullptr, level);
        t1 = std::chrono::steady_clock::now();
        const TempoMap& tm = setScore.m_decks[0].m_tempo;
        std::printf("Parhelion %s  set of seed %llu, %s, %zu tracks, %zu teases, %.1f min\n", PARH_VERSION,
                    static_cast<unsigned long long>(seed), DramaturgyNames[static_cast<int>(si.m_dramaturgy)], si.m_tracks.size(),
                    si.m_teases.size(), tm.secondsAt(setScore.m_lengthBeats) / 60.0);
        for (size_t i = 0; i < si.m_tracks.size(); ++i) {
            const SetTrack& st = si.m_tracks[i];
            std::printf("  T%-2zu deck %c  %6.2f min  %5.1f BPM  %-12s %-4s %-3s %-16s %3d bars, intro %2d, swap at %6.2f min, energy %.2f%s\n",
                        i + 1, 'A' + st.m_deck, tm.secondsAt(st.m_start) / 60.0, st.m_info.m_bpm, st.m_info.m_style.c_str(), st.m_info.m_camelot.c_str(),
                        KeyNames[st.m_info.m_key], ScaleNames[st.m_info.m_scale], st.m_info.m_bars, st.m_info.m_introBars, tm.secondsAt(st.m_swapIn) / 60.0,
                        st.m_energy, st.m_info.m_orchestra ? ", orchestra" : "");
        }
        cues = setCues(si, tm);
        if (!planJson.empty()) {
            std::string j = "{\"set\":true,\"tracks\":[";
            for (size_t i = 0; i < si.m_tracks.size(); ++i) {
                const SetTrack& st = si.m_tracks[i];
                Score own;
                own.m_tempo = tm;
                for (const Section& x : setScore.m_decks[st.m_deck].m_sections)
                    if (x.m_beat >= st.m_start - 1e-9 && x.m_beat < st.m_end - 1e-9) { Section y = x; y.m_beat -= st.m_start; own.m_sections.push_back(y); }
                j += std::string(i ? "," : "") + trackJson(own, st.m_info, st.m_start, tm);
            }
            j += "]}\n";
            if (!writeText(planJson, j)) { std::fprintf(stderr, "cannot write %s\n", planJson.c_str()); return 1; }
        }
        title = std::string("Parhelion set ") + DramaturgyNames[static_cast<int>(si.m_dramaturgy)] + " seed " + std::to_string(seed);
        if (!si.m_tracks.empty())
            tonality = std::string(KeyNames[si.m_tracks[0].m_info.m_key]) + (si.m_tracks[0].m_info.m_scale == static_cast<int>(ScaleE::IONIAN) ? "" : "m");
        cueTempo = tm;
        cueLength = setScore.m_lengthBeats;
        if (!midi.empty()) {
            if (!writeMidiFile(flattenSet(setScore), midi.c_str(), "Parhelion", &p)) { std::fprintf(stderr, "cannot write %s\n", midi.c_str()); return 1; }
            std::printf("MIDI: %s\n", midi.c_str());
        }
        if (planOnly) return 0;
        engine->prepare(rate, block);
        if (quest) engine->setQuality(Engine::QualityE::QUEST);
        engine->loadSet(setScore);
    } else {
        score = study ? composeStudy(p, seed) : composeTrack(p, seed, TrackRequest{}, &curation, std::string(), &info);
        // The Leveler (PLAN 7.5): the balance against the kick, the breakdown against the drop, the loudness.
        if (!bench && !planOnly) {
            const std::vector<LevelReading> lr = levelScore(score, p);
            for (const LevelReading& r : lr) {
                std::printf("level: the drop %.1f LUFS -> target %.1f, trim %+.1f dB (after %.1f); breakdown under the drop %.1f -> %.1f LU"
                            " (%+.1f dB); builds %+.1f dB\n", r.m_measured, r.m_target, r.m_trim, r.m_after, r.m_gapBefore, r.m_gapAfter, r.m_breakDb, r.m_buildDb);
                std::string bal = "balance against the kick (dB, correction):";
                for (int k = 0; k < BalParts; ++k)
                    if (!std::isnan(r.m_found[static_cast<size_t>(k)]))
                        bal += " " + std::string(BalPartNames[k]) + " " + std::to_string(static_cast<int>(std::lround(r.m_found[static_cast<size_t>(k)])))
                             + (r.m_bal[static_cast<size_t>(k)] != 0.0f ? "(" + std::to_string(static_cast<int>(std::lround(r.m_bal[static_cast<size_t>(k)]))) + ")" : "");
                std::printf("%s\n", bal.c_str());
            }
        }
        t1 = std::chrono::steady_clock::now();
        std::printf("Parhelion %s  seed %llu  %.1f BPM  %s %s  %.0f bars (%.1f min)\n", PARH_VERSION, static_cast<unsigned long long>(seed),
                    score.m_tempo.bpmAt(0.0), KeyNames[score.m_keyRoot], ScaleNames[score.m_scale], score.m_lengthBeats / 4.0,
                    score.m_tempo.secondsAt(score.m_lengthBeats) / 60.0);
        if (!study)
            std::printf("%s, %s form; %s, bass %s; %s; main drop at bar %d, breakdown at bar %d, the lead from bar %d%s%s\n",
                        info.m_style.c_str(), info.m_form == FormTemplateE::ANTHEM ? "anthem" : info.m_form == FormTemplateE::DREAM ? "dream"
                        : info.m_form == FormTemplateE::ACID ? "acid" : info.m_form == FormTemplateE::PLATEAU ? "plateau" : "drift",
                        info.m_progression.c_str(), info.m_bass.c_str(), info.m_camelot.c_str(), info.m_mainDropBar, info.m_breakdownBar,
                        info.m_firstLeadBar, info.m_keyChangeBar >= 0 ? (", key change at bar " + std::to_string(info.m_keyChangeBar)).c_str() : "",
                        info.m_orchestra ? "; the orchestra" : "");
        if (!study) {
            const auto sounds = soundsAt(score, 0.0);
            if (!sounds.empty()) {
                std::printf("sounds:");
                for (size_t i = 0; i < sounds.size(); ++i) std::printf("%s %s: %s", i ? "," : "", sounds[i].first.c_str(), sounds[i].second.c_str());
                std::printf("\n");
            }
        }
        printPlan(score);
        if (!midi.empty()) {
            if (!writeMidiFile(score, midi.c_str(), "Parhelion", &p)) { std::fprintf(stderr, "cannot write %s\n", midi.c_str()); return 1; }
            std::printf("MIDI: %s\n", midi.c_str());
        }
        if (!planJson.empty() && !study && !writeText(planJson, "{\"set\":false,\"tracks\":[" + trackJson(score, info, 0.0, score.m_tempo) + "]}\n")) {
            std::fprintf(stderr, "cannot write %s\n", planJson.c_str());
            return 1;
        }
        if (planOnly) return 0;
        if (study) {
            for (const Section& s : score.m_sections) cues.push_back({ score.m_tempo.secondsAt(s.m_beat), SectionNames[static_cast<int>(s.m_kind)] });
        } else {
            trackCues(info, 0.0, score.m_tempo, "", cues);
        }
        title = study ? std::string("Parhelion study") : "Parhelion " + info.m_style + " " + info.m_camelot + " seed " + std::to_string(seed);
        tonality = std::string(KeyNames[score.m_keyRoot]) + (score.m_scale == static_cast<int>(ScaleE::IONIAN) ? "" : "m");
        cueTempo = score.m_tempo;
        cueLength = score.m_lengthBeats;
        if (!loopsDir.empty() && !study) {
            if (!renderLoops(p, score, info, loopsDir, rate)) { std::fprintf(stderr, "cannot write the loops\n"); return 1; }
            std::printf("loops: %s (loop_intro, loop_drop, loop_outro: eight bars each)\n", loopsDir.c_str());
        }
        engine->prepare(rate, block);
        if (quest) engine->setQuality(Engine::QualityE::QUEST);
        engine->load(score);
    }

    WavWriter wav;
    if (!out.empty() && !wav.open(out.c_str(), static_cast<int>(rate), 2, WavFormatE::PCM24)) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    if (!out.empty()) {
        // The cues (PLAN 8): in the WAV's cue chunk, as JSON beside it, and in a rekordbox collection with the beat grid.
        for (const CueAt& c : cues) wav.addCue(static_cast<uint64_t>(std::llround(c.m_seconds * rate)), c.m_label);
        wav.setInfo("INAM", title);
        wav.setInfo("ISFT", std::string("Parhelion ") + PARH_VERSION);
        wav.setInfo("IGNR", "Trance");
        if (!cues.empty() && !writeCuesJson(out + ".cues.json", cues, rate)) { std::fprintf(stderr, "cannot write the cues\n"); return 1; }
        if (cueLength > 0.0 && !writeRekordboxXml(out + ".rekordbox.xml", out, title, tonality, cues, cueTempo, cueLength, rate)) {
            std::fprintf(stderr, "cannot write the rekordbox xml\n");
            return 1;
        }
    }
    std::vector<std::unique_ptr<WavWriter>> stemFiles;
    std::vector<std::vector<float>> stemBufL, stemBufR;
    std::vector<float*> stemL, stemR;
    if (!stems.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(stems, ec);
        for (int s = 0; s < Engine::STEMS; ++s) {
            stemFiles.push_back(std::make_unique<WavWriter>());
            const std::string path = stems + "/" + Engine::stemName(s) + ".wav";
            if (!stemFiles.back()->open(path.c_str(), static_cast<int>(rate), 2, WavFormatE::FLOAT32)) {
                std::fprintf(stderr, "cannot write %s\n", path.c_str());
                return 1;
            }
            stemBufL.emplace_back(static_cast<size_t>(block), 0.0f);
            stemBufR.emplace_back(static_cast<size_t>(block), 0.0f);
        }
        for (int s = 0; s < Engine::STEMS; ++s) { stemL.push_back(stemBufL[static_cast<size_t>(s)].data()); stemR.push_back(stemBufR[static_cast<size_t>(s)].data()); }
        engine->setStems(stemL.data(), stemR.data());
    }

    // The meters: the whole, and one per section (PLAN 2.7, rule 3).
    LoudnessMeter meter;
    meter.prepare(rate);
    std::vector<LoudnessMeter> sectionMeters(score.m_sections.size());
    for (LoudnessMeter& m : sectionMeters) m.prepare(rate);
    std::vector<int64_t> sectionStart;
    for (const Section& s : score.m_sections) sectionStart.push_back(static_cast<int64_t>(std::llround(score.m_tempo.secondsAt(s.m_beat) * rate)));

    std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
    int64_t total = static_cast<int64_t>(engine->lengthSeconds() * rate) + static_cast<int64_t>(2.0 * rate);
    if (seconds > 0.0) total = std::min(total, static_cast<int64_t>(seconds * rate));   // --seconds: the beginning only
    int64_t done = 0;
    size_t sec = 0;
    const auto t2 = std::chrono::steady_clock::now();
    while (done < total) {
        int n = static_cast<int>(std::min<int64_t>(block, total - done));
        // A block never crosses a section's start, so every sample is measured in its own section.
        if (sec + 1 < sectionStart.size() && done < sectionStart[sec + 1] && done + n > sectionStart[sec + 1])
            n = static_cast<int>(sectionStart[sec + 1] - done);
        engine->process(L.data(), R.data(), n);
        if (!bench) {
            meter.process(L.data(), R.data(), n);
            if (!sectionMeters.empty()) sectionMeters[sec].process(L.data(), R.data(), n);
        }
        if (!out.empty()) wav.write(L.data(), R.data(), n);
        for (size_t s = 0; s < stemFiles.size(); ++s) stemFiles[s]->write(stemL[s], stemR[s], n);
        done += n;
        if (sec + 1 < sectionStart.size() && done >= sectionStart[sec + 1]) ++sec;
    }
    const auto t3 = std::chrono::steady_clock::now();
    if (!out.empty()) wav.close();
    for (auto& f : stemFiles) f->close();

    const double composeS = std::chrono::duration<double>(t1 - t0).count();
    const double renderS = std::chrono::duration<double>(t3 - t2).count();
    const double audioS = static_cast<double>(total) / rate;
    std::printf("composed in %.3f s; rendered %.1f s of audio in %.2f s: %.0f x real time, %.2f %% of a core\n", composeS, audioS,
                renderS, audioS / renderS, 100.0 * renderS / audioS);
#ifdef PARH_PROFILE
    {
        double sum = 0.0;
        for (double v : prof::ns) sum += v;
        for (int k = 0; k < prof::COUNT; ++k)
            std::printf("  %-18s %5.1f %% of the render  %6.3f %% of a core\n", prof::Names[k], 100.0 * prof::ns[k] / std::max(1.0, sum),
                        100.0 * prof::ns[k] * 1.0e-9 / std::max(1e-9, audioS));
    }
#endif
    if (!bench) {
        const LoudnessReport r = meter.report();
        std::printf("loudness %.1f LUFS integrated, %.1f LUFS short-term max, true peak %.2f dBTP, LRA %.1f LU, crest %.1f dB,"
                    " correlation %.2f\n", r.m_integrated, r.m_shortTermMax, r.m_truePeak, r.m_range, r.m_crest, r.m_correlation);
        double breakdown = -200.0, drop = -200.0;
        for (size_t i = 0; i < score.m_sections.size(); ++i) {
            const LoudnessReport s = sectionMeters[i].report();
            const SectionKindE k = score.m_sections[i].m_kind;
            std::printf("  %-9s bar %3d  %6.1f LUFS integrated, %6.1f LUFS short-term max\n", SectionNames[static_cast<int>(k)],
                        static_cast<int>(score.m_sections[i].m_beat / 4.0), s.m_integrated, s.m_shortTermMax);
            if (k == SectionKindE::BREAKDOWN) breakdown = std::max(breakdown, s.m_shortTermMax);
            if (k == SectionKindE::DROP) drop = std::max(drop, s.m_shortTermMax);
        }
        if (breakdown > -199.0 && drop > -199.0)
            std::printf("breakdown -> drop: %.1f LU (the references: 0.5 to 4, median 1.9)\n", drop - breakdown);
    }
    if (!out.empty()) std::printf("WAV: %s\n", out.c_str());
    return 0;
}
