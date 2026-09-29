/**
 * @file Preferences.cpp
 * @brief The ratings and the preferences they give (Preferences.h).
 * @note Copied from Totality `Core/src/Preferences.cpp` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 */
#include "parh/Preferences.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace parh {

namespace {

float clampFactor(float f) { return std::clamp(f, 0.25f, 3.0f); }

} // namespace

Preferences preferencesFrom(const std::vector<Rating>& ratings)
{
    Preferences p;
    for (const Rating& r : ratings) {
        const float step = r.value > 0 ? 0.25f : -0.25f;
        if (r.form >= 0 && r.form < 5) p.form[r.form] = clampFactor(p.form[r.form] + step);
        for (const std::string& g : r.groups) p.groups[g] = clampFactor(p.group(g) + step);
    }
    return p;
}

std::vector<Rating> loadRatings(const std::string& path)
{
    // One rating a line: value, form, style, seed, the groups separated by '|', tab-separated.
    std::vector<Rating> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        std::stringstream ss(line);
        for (std::string cell; std::getline(ss, cell, '\t');) f.push_back(cell);
        if (f.size() < 4) continue;
        Rating r;
        r.value = std::atoi(f[0].c_str()) >= 0 ? 1 : -1;
        r.form = std::atoi(f[1].c_str());
        r.style = f[2];
        r.seed = std::strtoull(f[3].c_str(), nullptr, 10);
        if (f.size() > 4) {
            std::stringstream gs(f[4]);
            for (std::string g; std::getline(gs, g, '|');) if (!g.empty()) r.groups.push_back(g);
        }
        out.push_back(r);
    }
    return out;
}

bool appendRating(const std::string& path, const Rating& r)
{
    std::FILE* f = std::fopen(path.c_str(), "ab");
    if (f == nullptr) return false;
    std::string groups;
    for (const std::string& g : r.groups) groups += (groups.empty() ? "" : "|") + g;
    std::fprintf(f, "%d\t%d\t%s\t%llu\t%s\n", r.value > 0 ? 1 : -1, r.form, r.style.c_str(), r.seed, groups.c_str());
    return std::fclose(f) == 0;
}

} // namespace parh
