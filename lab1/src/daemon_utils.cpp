#include "daemon_utils.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <cerrno>

namespace fs = std::filesystem;

static bool proc_alive(pid_t pid) {
    return (pid > 1) && (kill(pid, 0) == 0 || errno == EPERM);
}

static bool proc_exists(pid_t pid) {
    if (pid <= 1)
        return false;
    return fs::exists(fs::path("/proc") / std::to_string(pid));
}

void daemonize() {
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

void ensure_singleton(const std::string &pid_path) {
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

void write_pid(const std::string &pid_path) {
    std::error_code ec;
    fs::create_directories(fs::path(pid_path).parent_path(), ec);

    std::ofstream pout(pid_path, std::ios::trunc);
    if (!pout) {
        syslog(LOG_ERR, "cannot write pid file: %s", pid_path.c_str());
        return;
    }
    pout << getpid() << "\n";
}