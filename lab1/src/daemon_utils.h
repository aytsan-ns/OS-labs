#pragma once

#include <string>

void daemonize();
void ensure_singleton(const std::string &pid_path);
void write_pid(const std::string &pid_path);