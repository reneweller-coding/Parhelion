/**
 * @file PresetBank.cpp
 * @brief The factory banks (Presets.h): sixteen groups of sixty-four presets for each of eighteen sound modules, built
 *        from the table Tools/presets/gen_bank.py writes (PresetBankData.inl), with the trims PresetTrims.inl holds.
 * @note The builder follows Phosphene `Core/src/PresetBank.cpp` at 76f7100 (29.09.2026): the grid, the axes in the
 *       normalised space, the recipes' '@' (the next free LFO) and '#' (the next free slot); new are the lists (a knob
 *       drawn from a few values), the kit's roles and the banks of the physical voices.
 */
#include "parh/Presets.h"
#include "parh/synth/Modulation.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace parh {

namespace bank {

/** @brief One knob of a group: its key inside the module, its range, its axis; or a list of values to draw from. */
struct Knob {
    const char* key;     ///< e.g. "cutoff", "lfo@_rate", "mx#_amount"
    float lo, hi;        ///< the range ('C': lo)
    char axis;           ///< 'A' the row, 'B' the column, 'R' drawn, 'C' constant
    const char* list;    ///< "list:2,6,7": one of these (by the axis: the row's, the column's or drawn); else null
};
/** @brief A modulation recipe: its chance and its knobs. */
struct Mod { float chance; std::vector<Knob> knobs; };
/** @brief A group: its name, its eight nouns, its weight in each style, the kit roles it is for, its knobs, its recipes. */
struct Group {
    const char* name;
    const char* nouns[8];
    float style[5];      ///< Uplifting, Progressive, Dream House, Acid, Deep
    uint32_t roles;      ///< bit PercRole (the kit), 0: any
    std::vector<Knob> knobs;
    std::vector<Mod> mods;
};
/** @brief A bank: its label (the module's key prefix), module and instance, its eight adjectives, its sixteen groups. */
struct Bank {
    const char* label;
    Module module;
    int instance;
    const char* adj[8];
    std::vector<Group> groups;
};

#include "PresetBankData.inl"
#include "PresetTrims.inl"

namespace {

/** @brief Replaces every @p from in @p s with the number @p to. */
std::string replaceAll(const std::string& s, char from, int to)
{
    std::string out;
    for (char c : s) {
        if (c == from) out += std::to_string(to);
        else out += c;
    }
    return out;
}

/** @brief The values of a "list:a,b,c" spec. */
std::vector<float> listOf(const char* spec)
{
    std::vector<float> out;
    const std::string s(spec != nullptr ? spec : "");
    if (s.rfind("list:", 0) == 0) {
        size_t i = 5;
        while (i < s.size()) {
            out.push_back(static_cast<float>(std::atof(s.c_str() + i)));
            const size_t comma = s.find(',', i);
            if (comma == std::string::npos) break;
            i = comma + 1;
        }
    }
    if (out.empty()) out.push_back(0.0f);
    return out;
}

/** @brief The key prefix of a bank's knobs ("lead.", "perc1.", "bass."). */
std::string prefixOf(const Bank& b)
{
    if (b.module == Module::Poly) return std::string(kPolyInstanceNames[b.instance]) + ".";
    if (b.module == Module::Perc) return "perc1.";
    return std::string(b.label) + ".";
}

/** @brief Sets knob @p key to what its range (or list) and axis give at row @p row, column @p col. */
bool setKnob(ParamStore& p, const std::string& prefix, const std::string& key, const Knob& k, int row, int col, Rng& rng)
{
    const int id = p.find(prefix + key);
    if (id < 0) return false;
    float t = 0.0f;
    switch (k.axis) {
    case 'A': t = static_cast<float>(row) / 7.0f; break;
    case 'B': t = static_cast<float>(col) / 7.0f; break;
    case 'R': t = rng.uniform(); break;
    default: t = 0.0f; break;
    }
    if (k.list != nullptr) {
        const std::vector<float> l = listOf(k.list);
        const int n = static_cast<int>(l.size());
        const int i = k.axis == 'R' ? std::min(n - 1, static_cast<int>(t * n)) : k.axis == 'C' ? 0 : std::min(n - 1, static_cast<int>(std::lround(t * (n - 1))));
        p.set(id, l[static_cast<size_t>(i)]);
        return true;
    }
    const ParamDesc& d = p.desc(id);
    if (d.curve == Curve::Choice || d.curve == Curve::Int || d.curve == Curve::Toggle) {
        p.set(id, std::round(k.lo + t * (k.hi - k.lo)));
    } else if (k.axis == 'C' || k.lo == k.hi) {
        p.set(id, k.lo);
    } else {
        const float a = p.toNormalised(id, k.lo), b = p.toNormalised(id, k.hi);
        p.setNormalised(id, a + t * (b - a));
    }
    return true;
}

/** @brief The presets of one bank. */
std::vector<SoundPreset> build(const Bank& b, int index)
{
    std::vector<SoundPreset> out;
    auto p = std::make_unique<ParamStore>();
    const int base = p->base(b.module, b.module == Module::Perc ? 0 : b.instance);
    const int count = ParamStore::moduleCount(b.module);
    const std::string prefix = prefixOf(b);
    uint64_t salt = 0x50415248'42414E4Bull;   // "PARHBANK"
    for (const char* c = b.label; *c != 0; ++c) salt = mixSeed(salt, static_cast<uint64_t>(*c));
    out.reserve(b.groups.size() * 64);
    for (size_t g = 0; g < b.groups.size(); ++g) {
        const Group& grp = b.groups[g];
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col) {
                for (int k = 0; k < count; ++k) p->set(base + k, p->defaultValue(base + k));
                Rng rng;
                rng.seed(mixSeed(salt, static_cast<uint64_t>(g * 64 + row * 8 + col)));
                for (const Knob& k : grp.knobs) setKnob(*p, prefix, k.key, k, row, col, rng);
                int lfoNext = 1, slotNext = 1;
                for (const Mod& m : grp.mods) {
                    const float roll = rng.uniform();
                    if (roll >= m.chance || slotNext > kModSlots) continue;
                    bool usesLfo = false;
                    for (const Knob& k : m.knobs) usesLfo = usesLfo || std::string(k.key).find('@') != std::string::npos;
                    if (usesLfo && lfoNext > kLfos) continue;
                    for (const Knob& k : m.knobs) {
                        const std::string key = replaceAll(replaceAll(k.key, '@', lfoNext), '#', slotNext);
                        // A source of -1 is the LFO this recipe took (ModSource: LFO n is n).
                        if (key.find("_src") != std::string::npos && k.lo < 0.0f) {
                            const Knob c{ k.key, static_cast<float>(lfoNext), static_cast<float>(lfoNext), 'C', nullptr };
                            setKnob(*p, prefix, key, c, row, col, rng);
                        } else {
                            setKnob(*p, prefix, key, k, row, col, rng);
                        }
                    }
                    if (usesLfo) ++lfoNext;
                    ++slotNext;
                }
                SoundPreset sp;
                sp.group = grp.name;
                sp.name = std::string(b.adj[row]) + " " + grp.nouns[col];
                sp.groupIndex = static_cast<int>(g);
                for (int k = 0; k < 5; ++k) sp.style[k] = grp.style[k];
                sp.roles = grp.roles;
                if (index >= 0 && out.size() < static_cast<size_t>(kPresetsPerBank))
                    sp.trimDb = 0.1f * static_cast<float>(kPresetTrimTenths[index][out.size()]);
                for (int k = 0; k < count; ++k)
                    if (!presetLeaves(b.module, k)) sp.values.emplace_back(k, p->get(base + k));
                out.push_back(std::move(sp));
            }
    }
    return out;
}

const Bank* bankOf(Module m, int instance)
{
    for (const Bank& b : banks())
        if (b.module == m && (m != Module::Poly || b.instance == instance)) return &b;
    return nullptr;
}

} // namespace

} // namespace bank

bool hasPresets(Module module) { return bank::bankOf(module, 0) != nullptr; }

int bankIndex(Module module, int instance)
{
    const std::vector<bank::Bank>& all = bank::banks();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].module == module && (module != Module::Poly || all[i].instance == instance)) return static_cast<int>(i);
    return -1;
}

const std::vector<SoundPreset>& factoryPresets(Module module, int instance)
{
    static std::mutex lock;
    static std::vector<std::vector<SoundPreset>> built(kPresetBanks);
    static std::vector<bool> done(kPresetBanks, false);
    static const std::vector<SoundPreset> none;
    const int i = bankIndex(module, instance);
    if (i < 0 || i >= kPresetBanks) return none;
    std::lock_guard<std::mutex> g(lock);
    if (!done[static_cast<size_t>(i)]) {
        built[static_cast<size_t>(i)] = bank::build(bank::banks()[static_cast<size_t>(i)], i);
        done[static_cast<size_t>(i)] = true;
    }
    return built[static_cast<size_t>(i)];
}

bool presetLeaves(Module module, int k)
{
    switch (module) {
    case Module::Kick: return k == kick::Level || k == kick::Tune || k == kick::LowCut;
    case Module::Sub: return k == sub::Level || k == sub::Lock || k == sub::Duck || k == sub::DuckHold || k == sub::DuckRelease;
    case Module::Perc:
        return k == perc::Active || k == perc::Role || k == perc::Level || k == perc::Pan || k == perc::Choke || k == perc::Shift
            || k == perc::Density || k == perc::PanDepth || k == perc::PanBars || k == perc::CutTrack;
    case Module::Bass: case Module::Acid:
        return k == synth::Level || k == synth::Pan || k == synth::PlateSend || k == synth::RoomSend || k == synth::Duck
            || k == synth::DuckRelease || k == synth::LowCut;
    case Module::Poly:
        return k == poly::Level || k == poly::Pan || k == poly::Duck || (k >= poly::Gate && k <= poly::GateTone) || k == poly::RoomSend
            || k == poly::HallSend || k == poly::PlateSend || k == poly::HpFloor || k == poly::HpTrack;
    case Module::Piano:
        return k == piano::Level || k == piano::Pedal || k == piano::Width || k == piano::LowCut || k == piano::Duck
            || k == piano::RoomSend || k == piano::PlateSend || k == piano::HallSend;
    case Module::Strings:
        return k == strings::Level || k == strings::Width || k == strings::LowCut || k == strings::Duck || k == strings::RoomSend
            || k == strings::PlateSend || k == strings::HallSend;
    case Module::Choir:
        return k == choir::Level || k == choir::Width || k == choir::LowCut || k == choir::Duck || k == choir::RoomSend
            || k == choir::PlateSend || k == choir::HallSend;
    case Module::Brass:
        return k == brass::Level || k == brass::Width || k == brass::LowCut || k == brass::Duck || k == brass::RoomSend
            || k == brass::PlateSend || k == brass::HallSend;
    case Module::Timpani:
        return k == timpani::Level || k == timpani::Width || k == timpani::LowCut || k == timpani::Duck || k == timpani::RoomSend
            || k == timpani::PlateSend || k == timpani::HallSend;
    case Module::Sfx:
        return k == sfx::Level || k == sfx::Duck || k == sfx::RoomSend || k == sfx::HallSend || k == sfx::PlateSend || k == sfx::SubDuck;
    case Module::Cloud: return k == cloud::Level || k == cloud::PadSend || k == cloud::KeysSend || k == cloud::PlateSend;
    default: return true;
    }
}

std::vector<std::pair<int, float>> presetKnobs(Module module, int instance, const SoundPreset& preset)
{
    std::vector<std::pair<int, float>> out;
    ParamStore defaults;
    const int base = defaults.base(module, instance);
    const int count = ParamStore::moduleCount(module);
    std::vector<float> v(static_cast<size_t>(count));
    for (int k = 0; k < count; ++k) v[static_cast<size_t>(k)] = defaults.defaultValue(base + k);
    for (const auto& [k, value] : preset.values) if (k >= 0 && k < count) v[static_cast<size_t>(k)] = value;
    for (int k = 0; k < count; ++k) if (!presetLeaves(module, k)) out.emplace_back(k, v[static_cast<size_t>(k)]);
    return out;
}

void applyPreset(ParamStore& params, Module module, int instance, const SoundPreset& preset)
{
    const int base = params.base(module, instance);
    if (base < 0) return;
    for (const auto& [k, value] : presetKnobs(module, instance, preset)) params.set(base + k, value);
}

std::string moduleText(const ParamStore& params, Module module, int instance)
{
    std::string out;
    const int base = params.base(module, instance);
    if (base < 0) return out;
    for (int k = 0; k < ParamStore::moduleCount(module); ++k) {
        if (presetLeaves(module, k)) continue;
        const int id = base + k;
        if (params.get(id) == params.defaultValue(id)) continue;
        out += params.key(id) + "=" + std::to_string(params.get(id)) + "\n";
    }
    return out;
}

int pickPreset(Module module, int instance, const float* style, int role, Rng& rng)
{
    const std::vector<SoundPreset>& all = factoryPresets(module, instance);
    if (all.empty()) return -1;
    // The groups by their weight in the track's styles, cubed (among those made for the lane's role): with the plain
    // weights an Uplifting lead was its anthem supersaw one time in eight (sixteen groups, most of them half at home
    // in every style); cubed, a style's own groups carry it and the others come now and then.
    const int groups = all.back().groupIndex + 1;
    std::vector<float> w(static_cast<size_t>(groups), 0.0f);
    for (int g = 0; g < groups; ++g) {
        const SoundPreset& first = all[static_cast<size_t>(g * 64)];
        if (role >= 0 && first.roles != 0u && ((first.roles >> role) & 1u) == 0u) continue;
        float s = 0.0f;
        for (int k = 0; k < 5; ++k) s += first.style[k] * style[k];
        s = std::max(0.0f, s);
        w[static_cast<size_t>(g)] = s * s * s;
    }
    float total = 0.0f;
    for (float x : w) total += x;
    const float u = rng.uniform(), v = rng.uniform(), x = rng.uniform();   // (all drawn, whatever is found: the stream stays put)
    if (total <= 0.0f) return -1;
    float acc = 0.0f;
    int g = groups - 1;
    for (int k = 0; k < groups; ++k) {
        acc += w[static_cast<size_t>(k)] / total;
        if (u < acc) { g = k; break; }
    }
    while (w[static_cast<size_t>(g)] <= 0.0f && g > 0) --g;
    // The row (the adjective, dark to bright) around the styles' brightness, a bell of a row and a half; the column (the
    // noun) free. Drawn evenly, half the sounds came darker than the style (a Progressive track at a centroid of 220 Hz,
    // its references 420 to 800).
    static const float kBright[5] = { 0.65f, 0.55f, 0.45f, 0.6f, 0.4f };   // Uplifting, Progressive, Dream House, Acid, Deep
    float sw = 0.0f, target = 0.0f;
    for (int k = 0; k < 5; ++k) { sw += std::max(0.0f, style[k]); target += std::max(0.0f, style[k]) * kBright[k]; }
    target = sw > 0.0f ? 7.0f * target / sw : 3.5f;
    float rw[8], rs = 0.0f;
    for (int r = 0; r < 8; ++r) {
        const float d = (static_cast<float>(r) - target) / 1.5f;
        rw[r] = std::exp(-0.5f * d * d);
        rs += rw[r];
    }
    int row = 7;
    float ra = 0.0f;
    for (int r = 0; r < 8; ++r) {
        ra += rw[r] / rs;
        if (x < ra) { row = r; break; }
    }
    return g * 64 + row * 8 + std::min(7, static_cast<int>(v * 8.0f));
}

int bankUnknownKeys(std::string* first)
{
    ParamStore p;
    int bad = 0;
    for (const bank::Bank& b : bank::banks()) {
        const std::string prefix = bank::prefixOf(b);
        auto check = [&](const std::string& key) {
            if (p.find(prefix + key) >= 0) return;
            if (bad++ == 0 && first != nullptr) *first = prefix + key;
        };
        for (const bank::Group& g : b.groups) {
            for (const bank::Knob& k : g.knobs) check(k.key);
            for (const bank::Mod& m : g.mods)
                for (const bank::Knob& k : m.knobs) {
                    std::string key = k.key;
                    for (char& c : key) if (c == '@' || c == '#') c = '1';
                    check(key);
                }
        }
    }
    return bad;
}

} // namespace parh
