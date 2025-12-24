#pragma once

#include "config.h"

#include <signal.h>
#include <string>
#include <vector>

class Daemon {
public:
    static Daemon& instance();

    void init(const std::string& cfg, const std::string& pid, const std::string& tag);
    void run();

private:
    Daemon() = default;
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    void install_signals();

    static void on_sighup(int);
    static void on_sigterm(int);

    bool initialized_ = false;

    std::string config_path;
    std::string pid_path   = "/tmp/lab1d.pid";
    std::string log_tag    = "lab1d";
    std::vector<Rule> rules;
    int interval_sec = 0;
    volatile sig_atomic_t reload = 0;
    volatile sig_atomic_t stop   = 0;
};
