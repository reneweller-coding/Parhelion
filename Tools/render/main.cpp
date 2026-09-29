/**
 * @file main.cpp
 * @brief parh_render: composes a track from a seed and renders it offline -- the determinism oracle, the MIDI export,
 *        the stems, the loudness of every section.
 *
 * Usage:
 *   parh_render [--seed N] [--style S] [--bpm B] [--minutes M] [--set "key=value; ..."] [--reroll unit]
 *               [--save-set f.parhset] [--load-set f.parhset] [--study] [--dj minutes]
 *               [--out track.wav] [--midi track.mid] [--stems dir] [--loops dir] [--plan-json f.json]
 *               [--rate 48000] [--block 512] [--bench] [--quality desktop|quest] [--plan] [--list] [--version]
 *
 * A track is composed from its seed (compose/Composer.h); --study renders the fixed study of Phase 1 instead; --dj (or
 * set.minutes) a DJ set of that many minutes (compose/Set.h), every track levelled before it is placed. Beside the WAV
 * the cues go into its cue chunk, into <out>.cues.json and into a rekordbox collection <out>.rekordbox.xml (the beat
 * grid, the breakdowns and drops as memory cues); --loops writes the intro's, the main drop's and the outro's eight bars
 * as seamless loops (Export.h); --plan-json the plan as JSON (the style, the key, the tempo, every section with its
 * bar and second; a set's tracks with theirs) for Tools/eval_report.py. Without
 * --out nothing is written and the render only measures: the loudness of the whole and of every section, and the
 * distance between the main breakdown's and the drop's loudest three seconds (the references: 0.5 to 4 LU).
 *
 * @note The frame (arguments, stems, the measurement, the timing) follows Totality `Tools/render/main.cpp` at 4d3c0d2
 *       (29.09.2026).
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

void usage()
{
    std::printf("parh_render [--seed N] [--style Uplifting|Progressive|Dream House|Acid|Deep] [--bpm B] [--minutes M]\n"
                "            [--set \"key=value; ...\"] [--reroll unit] [--save-set f.parhset] [--load-set f.parhset] [--study]\n"
                "            [--dj minutes] [--out track.wav] [--midi track.mid] [--stems dir] [--loops dir] [--plan-json f.json]\n"
                "            [--rate 48000]\n"
                "            [--block 512] [--bench]\n"
                "            [--quality desktop|quest] [--plan] [--list] [--version]\n");
    std::printf("units for --reroll:");
    for (const char* u : kUnitNames) std::printf(" %s", u);
    std::printf(" section<n>; in a set track<n>, track<n>.<unit> and set\n");
}

/** @brief Prints the plan: the sections and the layer matrix, one row per element, one column per 8-bar block. */
void printPlan(const Score& sc)
{
    std::printf("sections:\n");
    for (const Section& s : sc.sections)
        std::printf("  bar %3d  %-9s %3d bars  energy %.0f -> %.0f\n", static_cast<int>(s.beat / 4.0), kSectionNames[static_cast<int>(s.kind)],
                    static_cast<int>(s.length / 4.0), s.energyFrom, s.energyTo);
    std::printf("layer matrix (8-bar blocks; # on, - filtered):\n");
    for (int l = 0; l < kNumLayers; ++l) {
        std::string row;
        bool any = false;
        for (const LayerBlock& b : sc.layers) {
            const LayerState s = b.state[static_cast<size_t>(l)];
            row += s == LayerState::On ? '#' : s == LayerState::Filtered ? '-' : '.';
            any = any || s != LayerState::Off;
        }
        if (any) std::printf("  %-10s %s\n", kLayerNames[l], row.c_str());
    }
}

/** @brief The label of a synth (its bank's): "kick", "lead", "perc3". */
std::string synthLabel(int module, int instance)
{
    const Module m = static_cast<Module>(module);
    if (m == Module::Poly) return kPolyInstanceNames[instance];
    if (m == Module::Perc) return "perc" + std::to_string(instance + 1);
    static const std::pair<Module, const char*> kNames[] = { { Module::Kick, "kick" }, { Module::Sub, "sub" }, { Module::Bass, "bass" },
        { Module::Acid, "acid" }, { Module::Piano, "piano" }, { Module::Strings, "strings" }, { Module::Choir, "choir" },
        { Module::Brass, "brass" }, { Module::Timpani, "timpani" }, { Module::Sfx, "sfx" }, { Module::Cloud, "cloud" } };
    for (const auto& [mm, name] : kNames) if (mm == m) return name;
    return "?";
}

/** @brief The presets a track plays from beat @p from (its SoundPicks there): "lead Anthem Supersaw / Golden Zenith", ... */
std::vector<std::pair<std::string, std::string>> soundsAt(const Score& sc, double from)
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const SoundPick& s : sc.sounds) {
        if (s.beat != from) continue;
        const std::vector<SoundPreset>& ps = factoryPresets(static_cast<Module>(s.module), s.instance);
        if (s.preset < 0 || static_cast<size_t>(s.preset) >= ps.size()) continue;
        out.push_back({ synthLabel(s.module, s.instance), ps[static_cast<size_t>(s.preset)].group + " / " + ps[static_cast<size_t>(s.preset)].name });
    }
    return out;
}

/** @brief One track's plan as a JSON object: what the evaluation compares the audio with. */
std::string trackJson(const Score& sc, const TrackInfo& info, double at, const TempoMap& tempo)
{
    auto sec = [&](double beat) { return tempo.secondsAt(at + beat); };
    std::string s = "{\"style\":\"" + info.style + "\",\"bpm\":" + std::to_string(info.bpm) + ",\"key\":" + std::to_string(info.key)
                  + ",\"scale\":" + std::to_string(info.scale) + ",\"camelot\":\"" + info.camelot + "\",\"bars\":" + std::to_string(info.bars)
                  + ",\"start\":" + std::to_string(sec(0.0)) + ",\"main_drop_bar\":" + std::to_string(info.mainDropBar)
                  + ",\"sections\":[";
    for (size_t i = 0; i < sc.sections.size(); ++i) {
        const Section& x = sc.sections[i];
        s += std::string(i ? "," : "") + "{\"kind\":\"" + kSectionNames[static_cast<int>(x.kind)] + "\",\"bar\":" + std::to_string(static_cast<int>(x.beat / 4.0))
           + ",\"bars\":" + std::to_string(static_cast<int>(x.length / 4.0)) + ",\"seconds\":" + std::to_string(sec(x.beat)) + "}";
    }
    s += "],\"sounds\":{";
    const auto sounds = soundsAt(sc, 0.0);
    for (size_t i = 0; i < sounds.size(); ++i) s += std::string(i ? "," : "") + "\"" + sounds[i].first + "\":\"" + sounds[i].second + "\"";
    return s + "}}";
}

bool writeText(const std::string& path, const std::string& text)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    std::fwrite(text.data(), 1, text.size(), f);
    return std::fclose(f) == 0;
}

} // namespace

int main(int argc, char** argv)
{
    uint64_t seed = 1;
    double rate = 48000.0;
    int block = 512;
    float bpm = 0.0f, minutes = 0.0f;
    bool bench = false, quest = false, planOnly = false, list = false, study = false, seedGiven = false;
    std::string set, out, midi, stems, style, saveSetPath, loadSetPath, loopsDir, planJson;
    double djArg = 0.0;
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
    std::string error;
    if (!loadSetPath.empty()) {
        SetFile sf;
        if (!loadSet(loadSetPath.c_str(), sf, p, &error)) { std::fprintf(stderr, "--load-set: %s\n", error.c_str()); return 2; }
        if (!seedGiven) seed = sf.seed;
        for (const auto& [u, n] : sf.curation.rerolls) curation.rerolls[u] += n;
    }
    if (!style.empty()) set += "compose.style=" + style + ";";
    if (bpm > 0.0f || minutes > 0.0f) set += "compose.auto=0;";
    if (bpm > 0.0f) set += "compose.bpm=" + std::to_string(bpm) + ";";
    if (minutes > 0.0f) set += "compose.minutes=" + std::to_string(minutes) + ";";
    if (!set.empty() && !p.parseText(set, &error)) { std::fprintf(stderr, "--set: %s\n", error.c_str()); return 2; }
    if (!saveSetPath.empty()) {
        SetFile sf;
        sf.seed = seed;
        sf.curation = curation;
        if (!saveSet(saveSetPath.c_str(), sf, p)) { std::fprintf(stderr, "cannot write %s\n", saveSetPath.c_str()); return 1; }
    }

    const auto t0 = std::chrono::steady_clock::now();
    const double djMinutes = djArg > 0.0 ? djArg : p.get(p.id(Module::Set, 0, set::Minutes));
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
        const SetScore setScore = composeSet(p, seed, djMinutes, &curation, &si, level);
        t1 = std::chrono::steady_clock::now();
        const TempoMap& tm = setScore.decks[0].tempo;
        std::printf("Parhelion %s  set of seed %llu, %s, %zu tracks, %zu teases, %.1f min\n", PARH_VERSION,
                    static_cast<unsigned long long>(seed), kDramaturgyNames[static_cast<int>(si.dramaturgy)], si.tracks.size(),
                    si.teases.size(), tm.secondsAt(setScore.lengthBeats) / 60.0);
        for (size_t i = 0; i < si.tracks.size(); ++i) {
            const SetTrack& st = si.tracks[i];
            std::printf("  T%-2zu deck %c  %6.2f min  %5.1f BPM  %-12s %-4s %-3s %-16s %3d bars, intro %2d, swap at %6.2f min, energy %.2f%s\n",
                        i + 1, 'A' + st.deck, tm.secondsAt(st.start) / 60.0, st.info.bpm, st.info.style.c_str(), st.info.camelot.c_str(),
                        kKeyNames[st.info.key], kScaleNames[st.info.scale], st.info.bars, st.info.introBars, tm.secondsAt(st.swapIn) / 60.0,
                        st.energy, st.info.orchestra ? ", orchestra" : "");
        }
        cues = setCues(si, tm);
        if (!planJson.empty()) {
            std::string j = "{\"set\":true,\"tracks\":[";
            for (size_t i = 0; i < si.tracks.size(); ++i) {
                const SetTrack& st = si.tracks[i];
                Score own;
                own.tempo = tm;
                for (const Section& x : setScore.decks[st.deck].sections)
                    if (x.beat >= st.start - 1e-9 && x.beat < st.end - 1e-9) { Section y = x; y.beat -= st.start; own.sections.push_back(y); }
                j += std::string(i ? "," : "") + trackJson(own, st.info, st.start, tm);
            }
            j += "]}\n";
            if (!writeText(planJson, j)) { std::fprintf(stderr, "cannot write %s\n", planJson.c_str()); return 1; }
        }
        title = std::string("Parhelion set ") + kDramaturgyNames[static_cast<int>(si.dramaturgy)] + " seed " + std::to_string(seed);
        if (!si.tracks.empty())
            tonality = std::string(kKeyNames[si.tracks[0].info.key]) + (si.tracks[0].info.scale == static_cast<int>(Scale::Ionian) ? "" : "m");
        cueTempo = tm;
        cueLength = setScore.lengthBeats;
        if (!midi.empty()) {
            if (!writeMidiFile(flattenSet(setScore), midi.c_str(), "Parhelion", &p)) { std::fprintf(stderr, "cannot write %s\n", midi.c_str()); return 1; }
            std::printf("MIDI: %s\n", midi.c_str());
        }
        if (planOnly) return 0;
        engine->prepare(rate, block);
        if (quest) engine->setQuality(Engine::Quality::Quest);
        engine->loadSet(setScore);
    } else {
        score = study ? composeStudy(p, seed) : composeTrack(p, seed, TrackRequest{}, &curation, std::string(), &info);
        // The Leveler (PLAN 7.5): the balance against the kick, the breakdown against the drop, the loudness.
        if (!bench && !planOnly) {
            const std::vector<LevelReading> lr = levelScore(score, p);
            for (const LevelReading& r : lr) {
                std::printf("level: the drop %.1f LUFS -> target %.1f, trim %+.1f dB (after %.1f); breakdown under the drop %.1f -> %.1f LU"
                            " (%+.1f dB); builds %+.1f dB\n", r.measured, r.target, r.trim, r.after, r.gapBefore, r.gapAfter, r.breakDb, r.buildDb);
                std::string bal = "balance against the kick (dB, correction):";
                for (int k = 0; k < kBalParts; ++k)
                    if (!std::isnan(r.found[static_cast<size_t>(k)]))
                        bal += " " + std::string(kBalPartNames[k]) + " " + std::to_string(static_cast<int>(std::lround(r.found[static_cast<size_t>(k)])))
                             + (r.bal[static_cast<size_t>(k)] != 0.0f ? "(" + std::to_string(static_cast<int>(std::lround(r.bal[static_cast<size_t>(k)]))) + ")" : "");
                std::printf("%s\n", bal.c_str());
            }
        }
        t1 = std::chrono::steady_clock::now();
        std::printf("Parhelion %s  seed %llu  %.1f BPM  %s %s  %.0f bars (%.1f min)\n", PARH_VERSION, static_cast<unsigned long long>(seed),
                    score.tempo.bpmAt(0.0), kKeyNames[score.keyRoot], kScaleNames[score.scale], score.lengthBeats / 4.0,
                    score.tempo.secondsAt(score.lengthBeats) / 60.0);
        if (!study)
            std::printf("%s, %s form; %s, bass %s; %s; main drop at bar %d, breakdown at bar %d, the lead from bar %d%s%s\n",
                        info.style.c_str(), info.form == FormTemplate::Anthem ? "anthem" : info.form == FormTemplate::Dream ? "dream"
                        : info.form == FormTemplate::Acid ? "acid" : info.form == FormTemplate::Plateau ? "plateau" : "drift",
                        info.progression.c_str(), info.bass.c_str(), info.camelot.c_str(), info.mainDropBar, info.breakdownBar,
                        info.firstLeadBar, info.keyChangeBar >= 0 ? (", key change at bar " + std::to_string(info.keyChangeBar)).c_str() : "",
                        info.orchestra ? "; the orchestra" : "");
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
        if (!planJson.empty() && !study && !writeText(planJson, "{\"set\":false,\"tracks\":[" + trackJson(score, info, 0.0, score.tempo) + "]}\n")) {
            std::fprintf(stderr, "cannot write %s\n", planJson.c_str());
            return 1;
        }
        if (planOnly) return 0;
        if (study) {
            for (const Section& s : score.sections) cues.push_back({ score.tempo.secondsAt(s.beat), kSectionNames[static_cast<int>(s.kind)] });
        } else {
            trackCues(info, 0.0, score.tempo, "", cues);
        }
        title = study ? std::string("Parhelion study") : "Parhelion " + info.style + " " + info.camelot + " seed " + std::to_string(seed);
        tonality = std::string(kKeyNames[score.keyRoot]) + (score.scale == static_cast<int>(Scale::Ionian) ? "" : "m");
        cueTempo = score.tempo;
        cueLength = score.lengthBeats;
        if (!loopsDir.empty() && !study) {
            if (!renderLoops(p, score, info, loopsDir, rate)) { std::fprintf(stderr, "cannot write the loops\n"); return 1; }
            std::printf("loops: %s (loop_intro, loop_drop, loop_outro: eight bars each)\n", loopsDir.c_str());
        }
        engine->prepare(rate, block);
        if (quest) engine->setQuality(Engine::Quality::Quest);
        engine->load(score);
    }

    WavWriter wav;
    if (!out.empty() && !wav.open(out.c_str(), static_cast<int>(rate), 2, WavFormat::Pcm24)) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    if (!out.empty()) {
        // The cues (PLAN 8): in the WAV's cue chunk, as JSON beside it, and in a rekordbox collection with the beat grid.
        for (const CueAt& c : cues) wav.addCue(static_cast<uint64_t>(std::llround(c.seconds * rate)), c.label);
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
        for (int s = 0; s < Engine::kStems; ++s) {
            stemFiles.push_back(std::make_unique<WavWriter>());
            const std::string path = stems + "/" + Engine::stemName(s) + ".wav";
            if (!stemFiles.back()->open(path.c_str(), static_cast<int>(rate), 2, WavFormat::Float32)) {
                std::fprintf(stderr, "cannot write %s\n", path.c_str());
                return 1;
            }
            stemBufL.emplace_back(static_cast<size_t>(block), 0.0f);
            stemBufR.emplace_back(static_cast<size_t>(block), 0.0f);
        }
        for (int s = 0; s < Engine::kStems; ++s) { stemL.push_back(stemBufL[static_cast<size_t>(s)].data()); stemR.push_back(stemBufR[static_cast<size_t>(s)].data()); }
        engine->setStems(stemL.data(), stemR.data());
    }

    // The meters: the whole, and one per section (PLAN 2.7, rule 3).
    LoudnessMeter meter;
    meter.prepare(rate);
    std::vector<LoudnessMeter> sectionMeters(score.sections.size());
    for (LoudnessMeter& m : sectionMeters) m.prepare(rate);
    std::vector<int64_t> sectionStart;
    for (const Section& s : score.sections) sectionStart.push_back(static_cast<int64_t>(std::llround(score.tempo.secondsAt(s.beat) * rate)));

    std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
    const int64_t total = static_cast<int64_t>(engine->lengthSeconds() * rate) + static_cast<int64_t>(2.0 * rate);
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
        for (int k = 0; k < prof::Count; ++k)
            std::printf("  %-18s %5.1f %% of the render  %6.3f %% of a core\n", prof::kNames[k], 100.0 * prof::ns[k] / std::max(1.0, sum),
                        100.0 * prof::ns[k] * 1.0e-9 / std::max(1e-9, audioS));
    }
#endif
    if (!bench) {
        const LoudnessReport r = meter.report();
        std::printf("loudness %.1f LUFS integrated, %.1f LUFS short-term max, true peak %.2f dBTP, LRA %.1f LU, crest %.1f dB,"
                    " correlation %.2f\n", r.integrated, r.shortTermMax, r.truePeak, r.range, r.crest, r.correlation);
        double breakdown = -200.0, drop = -200.0;
        for (size_t i = 0; i < score.sections.size(); ++i) {
            const LoudnessReport s = sectionMeters[i].report();
            const SectionKind k = score.sections[i].kind;
            std::printf("  %-9s bar %3d  %6.1f LUFS integrated, %6.1f LUFS short-term max\n", kSectionNames[static_cast<int>(k)],
                        static_cast<int>(score.sections[i].beat / 4.0), s.integrated, s.shortTermMax);
            if (k == SectionKind::Breakdown) breakdown = std::max(breakdown, s.shortTermMax);
            if (k == SectionKind::Drop) drop = std::max(drop, s.shortTermMax);
        }
        if (breakdown > -199.0 && drop > -199.0)
            std::printf("breakdown -> drop: %.1f LU (the references: 0.5 to 4, median 1.9)\n", drop - breakdown);
    }
    if (!out.empty()) std::printf("WAV: %s\n", out.c_str());
    return 0;
}
