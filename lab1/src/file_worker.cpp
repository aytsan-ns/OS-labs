#include "file_worker.h"
#include "utils.h"

#include <syslog.h>

#include <cerrno>
#include <filesystem>

void process_rule(const Rule &r) {
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