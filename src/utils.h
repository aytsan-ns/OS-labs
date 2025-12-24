#pragma once

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

std::string to_lower(std::string s);
std::string ext_lower_of(const fs::path &p);
fs::path unique_dest_path(fs::path dest_dir, fs::path base_name);