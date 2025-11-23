#pragma once
#include <cstdint>

struct KidRequest {
    int32_t command; // 1 = сыграть ход, 2 = завершиться
    int32_t alive;   // 1 = жив, 0 = мёртв
};

struct KidResponse {
    int32_t number;  // число козлёнка
};
