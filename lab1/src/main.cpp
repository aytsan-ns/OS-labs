#include "daemon.h"

#include <filesystem>
#include <cstdio>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char **argv) {
    std::string cfg, pid = "/tmp/lab1d.pid", tag = "lab1d";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto getv = [&](int &i)->std::string { if (i+1 < argc) return std::string(argv[++i]); return std::string(); };
        if (a == "--config") {
            cfg = getv(i);
        }
        else if (a == "--pid") {
            pid = getv(i);
        }
        else if (a == "--tag") {
            tag = getv(i);
        }
    }

    if (cfg.empty()) {
        std::fprintf(stderr, "Usage: %s --config <path> [--pid /tmp/lab1d.pid] [--tag lab1d]\n", argv[0]);
        return 2;
    }

    try {
        cfg = fs::absolute(cfg).string();
        pid = fs::absolute(pid).string();
    } catch (...) {}

    auto& d = Daemon::instance();
    d.init(cfg, pid, tag);
    d.run();
    return 0;
}