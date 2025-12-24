#include "config.h"
#include "utils.h"

#include <syslog.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

std::vector<Rule> load_config(const std::string &conf_path) {
    std::vector<Rule> out;
    std::ifstream in(conf_path);
    if (!in) {
        syslog(LOG_ERR, "cannot open config: %s", conf_path.c_str());
        return out;
    }
    fs::path conf_dir = fs::absolute(fs::path(conf_path)).parent_path();
    std::string line;
    size_t lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (auto pos = line.find('#'); pos != std::string::npos)
            line.erase(pos);
        
        if (line.find_first_not_of(" \t\r\n") == std::string::npos)
            continue;

        std::istringstream iss(line);
        std::string f1, f2, ext;

        {
            std::istringstream t(line);
            std::string key; t >> key;
            if (key == "interval")
                continue;
        }

        if (!(iss >> f1 >> f2 >> ext)) {
            syslog(LOG_WARNING, "bad config line %zu: expected '<from> <to> <ext>'", lineno);
            continue;
        }

        Rule r;
        r.from = fs::path(f1);
        r.to = fs::path(f2);
        if (!r.from.is_absolute())
            r.from = fs::absolute(conf_dir / r.from);
        if (!r.to.is_absolute())
            r.to = fs::absolute(conf_dir / r.to);
        if (!ext.empty() && ext.front() == '.')
            ext.erase(0, 1);
        if (ext.empty()) {
            syslog(LOG_WARNING, "config line %zu: empty extension", lineno);
            continue;
        }
        r.ext = to_lower(ext);

        if (!fs::exists(r.from) || !fs::is_directory(r.from)) {
            syslog(LOG_WARNING, "config line %zu: source not exists or not a directory: %s", lineno, r.from.c_str());
            continue;
        }

        std::error_code ec;
        fs::create_directories(r.to, ec);
        if (ec || !fs::is_directory(r.to)) {
            syslog(LOG_WARNING, "config line %zu: cannot create target dir %s: %s", lineno, r.to.c_str(), ec.message().c_str());
            continue;
        }

        out.push_back(std::move(r));
    }
    syslog(LOG_INFO, "config loaded: %zu rule(s)", out.size());
    return out;
}

bool try_load_interval(const std::string &conf_path, int &out) {
    std::ifstream in(conf_path);
    if (!in) {
        syslog(LOG_ERR, "cannot open config: %s", conf_path.c_str());
        return false;
    }
    std::string line;
    size_t lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (auto pos = line.find('#'); pos != std::string::npos) line.erase(pos);
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;

        std::istringstream iss(line);
        std::string key;
        if (!(iss >> key)) continue;

        if (key == "interval") {
            int v = 0;
            if ((iss >> v) && v > 0) { out = v; return true; }
            syslog(LOG_WARNING, "bad 'interval' at line %zu: expected positive integer", lineno);
            return false;
        }
    }
    syslog(LOG_ERR, "missing 'interval' in config");
    return false;
}