#include "conn_shm.hpp"
#include "protocol.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

struct ShmChannel {
    volatile int requestReady;
    volatile int responseReady;
    int reserved[2];
    KidRequest request;
    KidResponse response;
};

static const size_t kShmSize = sizeof(ShmChannel);

static std::string makeName(int kidId) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "/wolf_kid_shm_%d", kidId);
    return std::string(buf);
}

bool Conn::initParent(int numKids) {
    (void)numKids;
    return true;
}

void Conn::cleanupParent() {
}

Conn::Conn(int kidId, bool isHost) : m_kidId(kidId), m_isHost(isHost), m_fd(-1), m_mem(nullptr) {
    std::string name = makeName(kidId);
    int flags = O_RDWR;
    if (isHost) {
        flags |= O_CREAT;
    }

    m_fd = shm_open(name.c_str(), flags, 0666);
    if (m_fd == -1) {
        perror("shm_open");
        return;
    }

    if (isHost) {
        if (ftruncate(m_fd, static_cast<off_t>(kShmSize)) == -1) {
            perror("ftruncate");
        }
    }

    m_mem = mmap(nullptr, kShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
    if (m_mem == MAP_FAILED) {
        perror("mmap");
        m_mem = nullptr;
        return;
    }

    if (isHost) {
        auto* ch = static_cast<ShmChannel*>(m_mem);
        ch->requestReady = 0;
        ch->responseReady = 0;
        std::memset(&ch->request, 0, sizeof(KidRequest));
        std::memset(&ch->response, 0, sizeof(KidResponse));
    }
}

Conn::~Conn() {
    if (m_mem) {
        munmap(m_mem, kShmSize);
    }
    if (m_fd != -1) {
        close(m_fd);
    }
    if (m_isHost) {
        std::string name = makeName(m_kidId);
        shm_unlink(name.c_str());
    }
}

bool Conn::Write(const void* buf, size_t count) {
    if (!m_mem) return false;
    auto* ch = static_cast<ShmChannel*>(m_mem);

    if (m_isHost) {
        if (count > sizeof(KidRequest)) {
            std::cerr << "Conn(shm): слишком большой пакет для запроса\n";
            return false;
        }
        while (ch->requestReady != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::memcpy(&ch->request, buf, count);
        ch->requestReady = 1;
        return true;
    } else {
        if (count > sizeof(KidResponse)) {
            std::cerr << "Conn(shm): слишком большой пакет для ответа\n";
            return false;
        }
        while (ch->responseReady != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::memcpy(&ch->response, buf, count);
        ch->responseReady = 1;
        return true;
    }
}

bool Conn::Read(void* buf, size_t count) {
    if (!m_mem) return false;
    auto* ch = static_cast<ShmChannel*>(m_mem);

    if (m_isHost) {
        if (count > sizeof(KidResponse)) {
            std::cerr << "Conn(shm): слишком большой буфер для ответа\n";
            return false;
        }
        while (ch->responseReady == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::memcpy(buf, &ch->response, count);
        ch->responseReady = 0;
        return true;
    } else {
        if (count > sizeof(KidRequest)) {
            std::cerr << "Conn(shm): слишком большой буфер для запроса\n";
            return false;
        }
        while (ch->requestReady == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::memcpy(buf, &ch->request, count);
        ch->requestReady = 0;
        return true;
    }
}
