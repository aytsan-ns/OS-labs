#include "daemon.h"

#include "config.h"
#include "daemon_utils.h"
#include "file_worker.h"

#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>

Daemon& Daemon::instance() {
    static Daemon d;
    return d;
}

void Daemon::init(const std::string& cfg, const std::string& pid, const std::string& tag) {
    if (initialized_) {
        syslog(LOG_WARNING, "daemon reinit ignored: already initialized");
        return;
    }
    config_path = cfg;
    pid_path = pid;
    log_tag = tag;
    initialized_ = true;
}

void Daemon::run() {
    openlog(log_tag.c_str(), LOG_PID, LOG_USER);

    rules = load_config(config_path);
    if (!try_load_interval(config_path, interval_sec)) {
        syslog(LOG_ERR, "cannot start without valid 'interval'");
        std::fprintf(stderr, "lab1d: cannot start without valid 'interval' in %s\n", config_path.c_str());
        closelog();
        _exit(2);
    }

    ensure_singleton(pid_path);
    daemonize();

    closelog();
    openlog(log_tag.c_str(), LOG_PID, LOG_USER);

    install_signals();

    write_pid(pid_path);
    syslog(LOG_INFO, "started; config=%s pidfile=%s interval=%d", config_path.c_str(), pid_path.c_str(), interval_sec);

    while (!stop) {
        if (reload) {
            reload = 0;
            rules = load_config(config_path);
            int ni = 0;
            if (try_load_interval(config_path, ni)) {
                interval_sec = ni;
                syslog(LOG_INFO, "reloaded config; interval=%d", interval_sec);
            }
            else {
                syslog(LOG_WARNING, "no valid 'interval' on reload; keep %d", interval_sec);
            }
        }
        for (const auto& r : rules) process_rule(r);
        sleep(interval_sec);
    }

    syslog(LOG_INFO, "stopped");
    unlink(pid_path.c_str());
    closelog();
}

void Daemon::on_sighup(int)  { instance().reload = 1; }
void Daemon::on_sigterm(int) { instance().stop   = 1; }

void Daemon::install_signals() {
    struct sigaction sa{};
    sa.sa_handler = &Daemon::on_sighup;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, nullptr);

    sa.sa_handler = &Daemon::on_sigterm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, nullptr);
}