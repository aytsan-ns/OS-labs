#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Rule {
    fs::path from;
    fs::path to;
    std::string ext; // без точки, в нижнем регистре
};

std::vector<Rule> load_config(const std::string &conf_path);
bool try_load_interval(const std::string &conf_path, int &out);