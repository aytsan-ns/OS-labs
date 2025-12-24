#include "utils.h"

#include <algorithm>
#include <cctype>

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
}

std::string ext_lower_of(const fs::path &p) {
    auto e = p.extension().string();
    if (!e.empty() && e.front() == '.')
        e.erase(0, 1);
    return to_lower(e);
}

fs::path unique_dest_path(fs::path dest_dir, fs::path base_name) {
    fs::path candidate = dest_dir / base_name;
    if (!fs::exists(candidate))
        return candidate;
    auto stem = base_name.stem().string();
    auto ext  = base_name.extension().string();
    for (int i = 1; i < 10000; ++i) {
        fs::path cand = dest_dir / fs::path(stem + "(" + std::to_string(i) + ")" + ext);
        if (!fs::exists(cand))
            return cand;
    }
    return candidate;
}
