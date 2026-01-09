#pragma once
#include <cstdint>

enum class KidCommand : int32_t {
    PlayMove = 1,
    Shutdown = 2
};

struct KidRequest {
    KidCommand command;
    int32_t alive;
};

struct KidResponse {
    int32_t number;
};
