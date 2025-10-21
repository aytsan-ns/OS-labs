#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <cstdio>

namespace fs = std::filesystem;

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
}

struct Rule {
    fs::path from;
    fs::path to;
    std::string ext; // без точки, в нижнем регистре
};

static std::string ext_lower_of(const fs::path &p) {
    auto e = p.extension().string();
    if (!e.empty() && e.front() == '.')
        e.erase(0, 1);
    return to_lower(e);
}

static fs::path unique_dest_path(fs::path dest_dir, fs::path base_name) {
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

static std::vector<Rule> load_config(const std::string &conf_path) {
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

static void daemonize() {
    pid_t pid = fork();
    if (pid < 0) { 
        syslog(LOG_ERR, "fork #1: %m");
        _exit(1);
    }
    if (pid > 0) 
        _exit(0);

    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid: %m");
        _exit(1);
    }

    pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "fork #2: %m");
        _exit(1);
    }
    if (pid > 0) 
        _exit(0);

    umask(0);

    if (chdir("/") != 0) {
        syslog(LOG_ERR, "chdir('/'): %m");
        _exit(1);
    }

    int fd0 = open("/dev/null", O_RDWR);
    if (fd0 < 0) {
        syslog(LOG_ERR, "open /dev/null: %m");
        _exit(1);
    }
    if (dup2(fd0, STDIN_FILENO)  == -1) {
        syslog(LOG_ERR, "dup2 stdin: %m");
        _exit(1);
    }
    if (dup2(fd0, STDOUT_FILENO) == -1) {
        syslog(LOG_ERR, "dup2 stdout: %m");
        _exit(1);
    }
    if (dup2(fd0, STDERR_FILENO) == -1) {
        syslog(LOG_ERR, "dup2 stderr: %m");
        _exit(1);
    }
    if (fd0 > 2) 
        close(fd0);
}

static bool proc_alive(pid_t pid) {
    return (pid > 1) && (kill(pid, 0) == 0 || errno == EPERM);
}

static bool proc_exists(pid_t pid) {
    if (pid <= 1)
        return false;
    return fs::exists(fs::path("/proc") / std::to_string(pid));
}

static void ensure_singleton(const std::string &pid_path) {
    std::ifstream pin(pid_path);
    if (pin) {
        pid_t old = 0; pin >> old;
        if (proc_exists(old) || proc_alive(old)) {
            syslog(LOG_INFO, "found running instance pid=%d, sending SIGTERM", old);
            kill(old, SIGTERM);
            for (int i = 0; i < 50; ++i) {
                usleep(100000); // 0.1s
                if (!(proc_exists(old) || proc_alive(old)))
                    break;
            }
        }
        else {
            syslog(LOG_WARNING, "stale pid file at %s; removing", pid_path.c_str());
            std::error_code ec;
            fs::remove(pid_path, ec);
        }
    }
}

static void write_pid(const std::string &pid_path) {
    std::error_code ec;
    fs::create_directories(fs::path(pid_path).parent_path(), ec);

    std::ofstream pout(pid_path, std::ios::trunc);
    if (!pout) {
        syslog(LOG_ERR, "cannot write pid file: %s", pid_path.c_str());
        return;
    }
    pout << getpid() << "\n";
}

static void process_rule(const Rule &r) {
    size_t moved = 0, skipped = 0;
    for (auto &de : fs::directory_iterator(r.from)) {
        if (!de.is_regular_file()) {
            ++skipped;
            continue;
        }
        const auto &src = de.path();
        std::string ex = ext_lower_of(src);
        if (ex == r.ext) {
            ++skipped;
            continue;
        }
        fs::path dst = unique_dest_path(r.to, src.filename());
        try {
            fs::rename(src, dst);
            ++moved;
        } catch (const fs::filesystem_error &e) {
            if (e.code().value() == EXDEV) {
                try {
                    fs::copy_file(src, dst, fs::copy_options::none);
                    fs::remove(src);
                    ++moved;
                } catch (const fs::filesystem_error &e2) {
                    syslog(LOG_ERR, "copy/remove failed: %s -> %s: %s", src.c_str(), dst.c_str(), e2.what());
                }
            } else {
                syslog(LOG_ERR, "rename failed: %s -> %s: %s", src.c_str(), dst.c_str(), e.what());
            }
        }
    }
    syslog(LOG_INFO, "rule: from=%s to=%s ext!=%s moved=%zu skipped=%zu", r.from.c_str(), r.to.c_str(), r.ext.c_str(), moved, skipped);
}

static bool try_load_interval(const std::string &conf_path, int &out) {
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

class Daemon {
public:
    static Daemon& instance() {
        static Daemon d;
        return d;
    }

    void init(const std::string& cfg, const std::string& pid, const std::string& tag) {
        if (initialized_) {
            syslog(LOG_WARNING, "daemon reinit ignored: already initialized");
            return;
        }
        config_path = cfg;
        pid_path = pid;
        log_tag = tag;
        initialized_ = true;
    }

    void run() {
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

private:
    Daemon() = default;
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    bool initialized_ = false;

    static void on_sighup(int)  { instance().reload = 1; }
    static void on_sigterm(int) { instance().stop   = 1; }

    void install_signals() {
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

    std::string config_path;
    std::string pid_path   = "/tmp/lab1d.pid";
    std::string log_tag    = "lab1d";
    std::vector<Rule> rules;
    int interval_sec = 0;
    volatile sig_atomic_t reload = 0;
    volatile sig_atomic_t stop   = 0;
};


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
