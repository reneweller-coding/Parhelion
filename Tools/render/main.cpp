/**
 * @file main.cpp
 * @brief parh_render: composes a track from a seed and renders it offline -- the determinism oracle, the MIDI export,
 *        the stems, the loudness of every section.
 *
 * Usage:
 *   parh_render [--seed N] [--style S] [--bpm B] [--minutes M] [--set "key=value; ..."] [--reroll unit]
 *               [--save-set f.parhset] [--load-set f.parhset] [--study]
 *               [--out track.wav] [--midi track.mid] [--stems dir]
 *               [--rate 48000] [--block 512] [--bench] [--quality desktop|quest] [--plan] [--list] [--version]
 *
 * A track is composed from its seed (compose/Composer.h); --study renders the fixed study of Phase 1 instead. Without
 * --out nothing is written and the render only measures: the loudness of the whole and of every section, and the
 * distance between the main breakdown's and the drop's loudest three seconds (the references: 0.5 to 4 LU).
 *
 * @note The frame (arguments, stems, the measurement, the timing) follows Totality `Tools/render/main.cpp` at 4d3c0d2
 *       (29.09.2026).
 */
#include "parh/Engine.h"
#include "parh/Leveler.h"
#include "parh/Loudness.h"
#include "parh/Midi.h"
#include "parh/Profile.h"
#include "parh/WavWriter.h"
#include "parh/SetFile.h"
#include "parh/compose/Composer.h"
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
                "            [--out track.wav] [--midi track.mid] [--stems dir] [--rate 48000] [--block 512] [--bench]\n"
                "            [--quality desktop|quest] [--plan] [--list] [--version]\n");
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

} // namespace

int main(int argc, char** argv)
{
    uint64_t seed = 1;
    double rate = 48000.0;
    int block = 512;
    float bpm = 0.0f, minutes = 0.0f;
    bool bench = false, quest = false, planOnly = false, list = false, study = false, seedGiven = false;
    std::string set, out, midi, stems, style, saveSetPath, loadSetPath;
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
    TrackInfo info;
    Score score = study ? composeStudy(p, seed) : composeTrack(p, seed, TrackRequest{}, &curation, std::string(), &info);
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
    const auto t1 = std::chrono::steady_clock::now();
    std::printf("Parhelion %s  seed %llu  %.1f BPM  %s %s  %.0f bars (%.1f min)\n", PARH_VERSION, static_cast<unsigned long long>(seed),
                score.tempo.bpmAt(0.0), kKeyNames[score.keyRoot], kScaleNames[score.scale], score.lengthBeats / 4.0,
                score.tempo.secondsAt(score.lengthBeats) / 60.0);
    if (!study)
        std::printf("%s, %s form; %s, bass %s; %s; main drop at bar %d, breakdown at bar %d, the lead from bar %d%s\n",
                    info.style.c_str(), info.form == FormTemplate::Anthem ? "anthem" : info.form == FormTemplate::Dream ? "dream"
                    : info.form == FormTemplate::Acid ? "acid" : info.form == FormTemplate::Plateau ? "plateau" : "drift",
                    info.progression.c_str(), info.bass.c_str(), info.camelot.c_str(), info.mainDropBar, info.breakdownBar,
                    info.firstLeadBar, info.keyChangeBar >= 0 ? (", key change at bar " + std::to_string(info.keyChangeBar)).c_str() : "");
    printPlan(score);
    if (!midi.empty()) {
        if (!writeMidiFile(score, midi.c_str(), "Parhelion", &p)) { std::fprintf(stderr, "cannot write %s\n", midi.c_str()); return 1; }
        std::printf("MIDI: %s\n", midi.c_str());
    }
    if (planOnly) return 0;

    engine->prepare(rate, block);
    if (quest) engine->setQuality(Engine::Quality::Quest);
    engine->load(score);

    WavWriter wav;
    if (!out.empty() && !wav.open(out.c_str(), static_cast<int>(rate), 2, WavFormat::Pcm24)) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    if (!out.empty()) {
        for (const Section& s : score.sections)
            wav.addCue(static_cast<uint64_t>(std::llround(score.tempo.secondsAt(s.beat) * rate)), kSectionNames[static_cast<int>(s.kind)]);
        wav.setInfo("ISFT", std::string("Parhelion ") + PARH_VERSION);
        wav.setInfo("IGNR", "Trance");
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
