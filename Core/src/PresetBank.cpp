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
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace parh {

namespace bank {

/** @brief One knob of a group: its key inside the module, its range, its axis; or a list of values to draw from. */
struct Knob {
    const char* m_key;     ///< e.g. "cutoff", "lfo@_rate", "mx#_amount"
    float m_lo;   ///< the low end of the range (the constant for an axis of C)
    float m_hi;   ///< the high end of the range
    char m_axis;           ///< 'A' the row, 'B' the column, 'R' drawn, 'C' constant
    const char* m_list;    ///< "list:2,6,7": one of these (by the axis: the row's, the column's or drawn); else null
};
/** @brief A modulation recipe: its chance and its knobs. */
struct Mod {
    float m_chance;              ///< how often a preset of the group gets it, 0..1
    std::vector<Knob> m_knobs;   ///< the knobs it sets
};
/** @brief A group: its name, its eight nouns, its weight in each style, the kit roles it is for, its knobs, its recipes. */
struct Group {
    const char* m_name;   ///< the group's name
    const char* m_nouns[8];   ///< its eight nouns
    float m_style[5];      ///< Uplifting, Progressive, Dream House, Acid, Deep
    uint32_t m_roles;      ///< bit PercRole (the kit), 0: any
    std::vector<Knob> m_knobs;   ///< its knobs
    std::vector<Mod> m_mods;   ///< its modulation recipes
};
/** @brief A bank: its label (the module's key prefix), module and instance, its eight adjectives, its sixteen groups. */
struct Bank {
    const char* m_label;   ///< the bank's label: the module's key prefix
    ModuleE m_module;   ///< the module
    int m_instance;   ///< the instance
    const char* m_adj[8];   ///< its eight adjectives
    std::vector<Group> m_groups;   ///< its sixteen groups
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
    if (b.m_module == ModuleE::POLY) return std::string(PolyInstanceNames[b.m_instance]) + ".";
    if (b.m_module == ModuleE::PERC) return "perc1.";
    return std::string(b.m_label) + ".";
}

/** @brief Sets knob @p key to what its range (or list) and axis give at row @p row, column @p col. */
bool setKnob(ParamStore& p, const std::string& prefix, const std::string& key, const Knob& k, int row, int col, Rng& rng)
{
    const int id = p.find(prefix + key);
    if (id < 0) return false;
    float t = 0.0f;
    switch (k.m_axis) {
    case 'A': t = static_cast<float>(row) / 7.0f; break;
    case 'B': t = static_cast<float>(col) / 7.0f; break;
    case 'R': t = rng.uniform(); break;
    default: t = 0.0f; break;
    }
    if (k.m_list != nullptr) {
        const std::vector<float> l = listOf(k.m_list);
        const int n = static_cast<int>(l.size());
        const int i = k.m_axis == 'R' ? std::min(n - 1, static_cast<int>(t * n)) : k.m_axis == 'C' ? 0 : std::min(n - 1, static_cast<int>(std::lround(t * (n - 1))));
        p.set(id, l[static_cast<size_t>(i)]);
        return true;
    }
    const ParamDesc& d = p.desc(id);
    if (d.m_curve == CurveE::CHOICE || d.m_curve == CurveE::INT || d.m_curve == CurveE::TOGGLE) {
        p.set(id, std::round(k.m_lo + t * (k.m_hi - k.m_lo)));
    } else if (k.m_axis == 'C' || k.m_lo == k.m_hi) {
        p.set(id, k.m_lo);
    } else {
        const float a = p.toNormalised(id, k.m_lo), b = p.toNormalised(id, k.m_hi);
        p.setNormalised(id, a + t * (b - a));
    }
    return true;
}

/** @brief The presets of one bank. */
std::vector<SoundPreset> build(const Bank& b, int index)
{
    std::vector<SoundPreset> out;
    auto p = std::make_unique<ParamStore>();
    const int base = p->base(b.m_module, b.m_module == ModuleE::PERC ? 0 : b.m_instance);
    const int count = ParamStore::moduleCount(b.m_module);
    const std::string prefix = prefixOf(b);
    uint64_t salt = 0x50415248'42414E4Bull;   // "PARHBANK"
    for (const char* c = b.m_label; *c != 0; ++c) salt = mixSeed(salt, static_cast<uint64_t>(*c));
    out.reserve(b.m_groups.size() * 64);
    for (size_t g = 0; g < b.m_groups.size(); ++g) {
        const Group& grp = b.m_groups[g];
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col) {
                for (int k = 0; k < count; ++k) p->set(base + k, p->defaultValue(base + k));
                Rng rng;
                rng.seed(mixSeed(salt, static_cast<uint64_t>(g * 64 + row * 8 + col)));
                for (const Knob& k : grp.m_knobs) setKnob(*p, prefix, k.m_key, k, row, col, rng);
                int lfoNext = 1, slotNext = 1;
                for (const Mod& m : grp.m_mods) {
                    const float roll = rng.uniform();
                    if (roll >= m.m_chance || slotNext > ModSlots) continue;
                    bool usesLfo = false;
                    for (const Knob& k : m.m_knobs) usesLfo = usesLfo || std::string(k.m_key).find('@') != std::string::npos;
                    if (usesLfo && lfoNext > Lfos) continue;
                    for (const Knob& k : m.m_knobs) {
                        const std::string key = replaceAll(replaceAll(k.m_key, '@', lfoNext), '#', slotNext);
                        // A source of -1 is the LFO this recipe took (ModSource: LFO n is n).
                        if (key.find("_src") != std::string::npos && k.m_lo < 0.0f) {
                            const Knob c{ k.m_key, static_cast<float>(lfoNext), static_cast<float>(lfoNext), 'C', nullptr };
                            setKnob(*p, prefix, key, c, row, col, rng);
                        } else {
                            setKnob(*p, prefix, key, k, row, col, rng);
                        }
                    }
                    if (usesLfo) ++lfoNext;
                    ++slotNext;
                }
                SoundPreset sp;
                sp.m_group = grp.m_name;
                sp.m_name = std::string(b.m_adj[row]) + " " + grp.m_nouns[col];
                sp.m_groupIndex = static_cast<int>(g);
                for (int k = 0; k < 5; ++k) sp.m_style[k] = grp.m_style[k];
                sp.m_roles = grp.m_roles;
                if (index >= 0 && out.size() < static_cast<size_t>(PresetsPerBank))
                    sp.m_trimDb = 0.1f * static_cast<float>(PresetTrimTenths[index][out.size()]);
                for (int k = 0; k < count; ++k)
                    if (!presetLeaves(b.m_module, k)) sp.m_values.emplace_back(k, p->get(base + k));
                out.push_back(std::move(sp));
            }
    }
    return out;
}

/** @brief The bank of instance @p instance of module @p m, null for one without presets. */
const Bank* bankOf(ModuleE m, int instance)
{
    for (const Bank& b : banks())
        if (b.m_module == m && (m != ModuleE::POLY || b.m_instance == instance)) return &b;
    return nullptr;
}

} // namespace

} // namespace bank

bool hasPresets(ModuleE module) { return bank::bankOf(module, 0) != nullptr; }

int bankIndex(ModuleE module, int instance)
{
    const std::vector<bank::Bank>& all = bank::banks();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].m_module == module && (module != ModuleE::POLY || all[i].m_instance == instance)) return static_cast<int>(i);
    return -1;
}

const std::vector<SoundPreset>& factoryPresets(ModuleE module, int instance)
{
    static std::mutex lock;
    static std::vector<std::vector<SoundPreset>> built(PresetBanks);
    static std::vector<bool> done(PresetBanks, false);
    static const std::vector<SoundPreset> none;
    const int i = bankIndex(module, instance);
    if (i < 0 || i >= PresetBanks) return none;
    std::lock_guard<std::mutex> g(lock);
    if (!done[static_cast<size_t>(i)]) {
        built[static_cast<size_t>(i)] = bank::build(bank::banks()[static_cast<size_t>(i)], i);
        done[static_cast<size_t>(i)] = true;
    }
    return built[static_cast<size_t>(i)];
}

bool presetLeaves(ModuleE module, int k)
{
    switch (module) {
    case ModuleE::KICK: return k == kick::LEVEL || k == kick::TUNE || k == kick::LOW_CUT;
    case ModuleE::SUB: return k == sub::LEVEL || k == sub::LOCK || k == sub::DUCK || k == sub::DUCK_HOLD || k == sub::DUCK_RELEASE;
    case ModuleE::PERC:
        return k == perc::ACTIVE || k == perc::ROLE || k == perc::LEVEL || k == perc::PAN || k == perc::CHOKE || k == perc::SHIFT
            || k == perc::DENSITY || k == perc::PAN_DEPTH || k == perc::PAN_BARS || k == perc::CUT_TRACK;
    case ModuleE::BASS: case ModuleE::ACID:
        return k == synth::LEVEL || k == synth::PAN || k == synth::PLATE_SEND || k == synth::ROOM_SEND || k == synth::DUCK
            || k == synth::DUCK_RELEASE || k == synth::LOW_CUT;
    case ModuleE::POLY:
        return k == poly::LEVEL || k == poly::PAN || k == poly::DUCK || (k >= poly::GATE && k <= poly::GATE_TONE) || k == poly::ROOM_SEND
            || k == poly::HALL_SEND || k == poly::PLATE_SEND || k == poly::HP_FLOOR || k == poly::HP_TRACK;
    case ModuleE::PIANO:
        return k == piano::LEVEL || k == piano::PEDAL || k == piano::WIDTH || k == piano::LOW_CUT || k == piano::DUCK
            || k == piano::ROOM_SEND || k == piano::PLATE_SEND || k == piano::HALL_SEND;
    case ModuleE::STRINGS:
        return k == strings::LEVEL || k == strings::WIDTH || k == strings::LOW_CUT || k == strings::DUCK || k == strings::ROOM_SEND
            || k == strings::PLATE_SEND || k == strings::HALL_SEND;
    case ModuleE::CHOIR:
        return k == choir::LEVEL || k == choir::WIDTH || k == choir::LOW_CUT || k == choir::DUCK || k == choir::ROOM_SEND
            || k == choir::PLATE_SEND || k == choir::HALL_SEND;
    case ModuleE::BRASS:
        return k == brass::LEVEL || k == brass::WIDTH || k == brass::LOW_CUT || k == brass::DUCK || k == brass::ROOM_SEND
            || k == brass::PLATE_SEND || k == brass::HALL_SEND;
    case ModuleE::TIMPANI:
        return k == timpani::LEVEL || k == timpani::WIDTH || k == timpani::LOW_CUT || k == timpani::DUCK || k == timpani::ROOM_SEND
            || k == timpani::PLATE_SEND || k == timpani::HALL_SEND;
    case ModuleE::SFX:
        return k == sfx::LEVEL || k == sfx::DUCK || k == sfx::ROOM_SEND || k == sfx::HALL_SEND || k == sfx::PLATE_SEND || k == sfx::SUB_DUCK;
    case ModuleE::CLOUD: return k == cloud::LEVEL || k == cloud::PAD_SEND || k == cloud::KEYS_SEND || k == cloud::PLATE_SEND;
    default: return true;
    }
}

std::vector<std::pair<int, float>> presetKnobs(ModuleE module, int instance, const SoundPreset& preset)
{
    std::vector<std::pair<int, float>> out;
    ParamStore defaults;
    const int base = defaults.base(module, instance);
    const int count = ParamStore::moduleCount(module);
    std::vector<float> v(static_cast<size_t>(count));
    for (int k = 0; k < count; ++k) v[static_cast<size_t>(k)] = defaults.defaultValue(base + k);
    for (const auto& [k, value] : preset.m_values) if (k >= 0 && k < count) v[static_cast<size_t>(k)] = value;
    for (int k = 0; k < count; ++k) if (!presetLeaves(module, k)) out.emplace_back(k, v[static_cast<size_t>(k)]);
    return out;
}

void applyPreset(ParamStore& params, ModuleE module, int instance, const SoundPreset& preset)
{
    const int base = params.base(module, instance);
    if (base < 0) return;
    for (const auto& [k, value] : presetKnobs(module, instance, preset)) params.set(base + k, value);
}

std::string moduleText(const ParamStore& params, ModuleE module, int instance)
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

std::string userPresetText(const ParamStore& params, ModuleE module, int instance)
{
    std::string out;
    const int base = params.base(module, instance);
    if (base < 0) return out;
    for (int k = 0; k < ParamStore::moduleCount(module); ++k) {
        if (presetLeaves(module, k)) continue;
        const int id = base + k;
        const std::string& key = params.key(id);
        char value[32];
        std::snprintf(value, sizeof value, "%.9g", static_cast<double>(params.get(id)));
        out += key.substr(key.find('.') + 1) + "=" + value + "\n";
    }
    return out;
}

std::vector<std::pair<int, float>> userPresetKnobs(const ParamStore& params, ModuleE module, int instance, std::string_view text)
{
    std::vector<std::pair<int, float>> out;
    const int base = params.base(module, instance);
    if (base < 0) return out;
    const std::string& first = params.key(base);
    const std::string prefix = first.substr(0, first.find('.') + 1);
    std::vector<float> values(static_cast<size_t>(ParamStore::moduleCount(module)));
    for (int k = 0; k < ParamStore::moduleCount(module); ++k) values[static_cast<size_t>(k)] = params.defaultValue(base + k);
    auto trim = [](std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
        return s;
    };
    while (!text.empty()) {
        const size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const int id = params.find(prefix + std::string(trim(line.substr(0, eq))));
        const int k = id - base;
        if (id < 0 || k < 0 || k >= ParamStore::moduleCount(module) || presetLeaves(module, k)) continue;
        const std::string value(trim(line.substr(eq + 1)));
        char* stop = nullptr;
        const float v = std::strtof(value.c_str(), &stop);
        if (stop != value.c_str()) values[static_cast<size_t>(k)] = v;
    }
    for (int k = 0; k < ParamStore::moduleCount(module); ++k)
        if (!presetLeaves(module, k)) out.push_back({ k, values[static_cast<size_t>(k)] });
    return out;
}

int pickPreset(ModuleE module, int instance, const float* style, int role, Rng& rng, const Preferences* prefs)
{
    const std::vector<SoundPreset>& all = factoryPresets(module, instance);
    if (all.empty()) return -1;
    // The groups by their weight in the track's styles, cubed (among those made for the lane's role): with the plain
    // weights an Uplifting lead was its anthem supersaw one time in eight (sixteen groups, most of them half at home
    // in every style); cubed, a style's own groups carry it and the others come now and then.
    const int groups = all.back().m_groupIndex + 1;
    std::vector<float> w(static_cast<size_t>(groups), 0.0f);
    for (int g = 0; g < groups; ++g) {
        const SoundPreset& first = all[static_cast<size_t>(g * 64)];
        if (role >= 0 && first.m_roles != 0u && ((first.m_roles >> role) & 1u) == 0u) continue;
        float s = 0.0f;
        for (int k = 0; k < 5; ++k) s += first.m_style[k] * style[k];
        s = std::max(0.0f, s);
        w[static_cast<size_t>(g)] = s * s * s * (prefs != nullptr ? prefs->group(first.m_group) : 1.0f);
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
    const float target = 7.0f * styleBrightness(style);
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

float styleBrightness(const float* style)
{
    static const float Bright[5] = { 0.65f, 0.55f, 0.45f, 0.6f, 0.4f };   // Uplifting, Progressive, Dream House, Acid, Deep
    float sw = 0.0f, b = 0.0f;
    for (int k = 0; k < 5; ++k) { sw += std::max(0.0f, style[k]); b += std::max(0.0f, style[k]) * Bright[k]; }
    return sw > 0.0f ? b / sw : 0.5f;
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
        for (const bank::Group& g : b.m_groups) {
            for (const bank::Knob& k : g.m_knobs) check(k.m_key);
            for (const bank::Mod& m : g.m_mods)
                for (const bank::Knob& k : m.m_knobs) {
                    std::string key = k.m_key;
                    for (char& c : key) if (c == '@' || c == '#') c = '1';
                    check(key);
                }
        }
    }
    return bad;
}

} // namespace parh
