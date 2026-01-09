#include "conn_shm.hpp"
#include "protocol.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <semaphore.h>
#include <time.h>

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <string>

static constexpr int kShmTimeoutMs = 2000;

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

static std::string makeSemName(const char* kind, int kidId) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "/wolf_kid_%s_%d", kind, kidId);
    return std::string(buf);
}

static timespec makeAbsTimeoutMs(int timeoutMs) {
    timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        ts.tv_sec = 0;
        ts.tv_nsec = 0;
        return ts;
    }
    ts.tv_sec += timeoutMs / 1000;
    long addNs = (timeoutMs % 1000) * 1000L * 1000L;
    ts.tv_nsec += addNs;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
    }
    return ts;
}

int Conn::s_numKids = 0;

bool Conn::initParent(int numKids) {
    s_numKids = numKids;

    for (int kidId = 0; kidId < numKids; ++kidId) {
        std::string shmName = makeName(kidId);

        std::string reqEmptyName  = makeSemName("req_empty", kidId);
        std::string reqFullName   = makeSemName("req_full", kidId);
        std::string respEmptyName = makeSemName("resp_empty", kidId);
        std::string respFullName  = makeSemName("resp_full", kidId);

        shm_unlink(shmName.c_str());
        sem_unlink(reqEmptyName.c_str());
        sem_unlink(reqFullName.c_str());
        sem_unlink(respEmptyName.c_str());
        sem_unlink(respFullName.c_str());

        int fd = shm_open(shmName.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd == -1) { perror("shm_open(initParent)"); return false; }

        if (ftruncate(fd, static_cast<off_t>(kShmSize)) == -1) {
            perror("ftruncate(initParent)"); close(fd); return false;
        }

        void* mem = mmap(nullptr, kShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mem == MAP_FAILED) { perror("mmap(initParent)"); close(fd); return false; }

        auto* ch = static_cast<ShmChannel*>(mem);
        ch->requestReady = 0;
        ch->responseReady = 0;
        std::memset(&ch->request, 0, sizeof(KidRequest));
        std::memset(&ch->response, 0, sizeof(KidResponse));

        munmap(mem, kShmSize);
        close(fd);

        sem_t* s1 = sem_open(reqEmptyName.c_str(), O_CREAT | O_EXCL, 0666, 1);
        if (s1 == SEM_FAILED) { perror("sem_open(req_empty)"); return false; }
        sem_close(s1);

        sem_t* s2 = sem_open(reqFullName.c_str(), O_CREAT | O_EXCL, 0666, 0);
        if (s2 == SEM_FAILED) { perror("sem_open(req_full)"); return false; }
        sem_close(s2);

        sem_t* s3 = sem_open(respEmptyName.c_str(), O_CREAT | O_EXCL, 0666, 1);
        if (s3 == SEM_FAILED) { perror("sem_open(resp_empty)"); return false; }
        sem_close(s3);

        sem_t* s4 = sem_open(respFullName.c_str(), O_CREAT | O_EXCL, 0666, 0);
        if (s4 == SEM_FAILED) { perror("sem_open(resp_full)"); return false; }
        sem_close(s4);
    }

    return true;
}

void Conn::cleanupParent() {
    for (int kidId = 0; kidId < s_numKids; ++kidId) {
        shm_unlink(makeName(kidId).c_str());
        sem_unlink(makeSemName("req_empty", kidId).c_str());
        sem_unlink(makeSemName("req_full", kidId).c_str());
        sem_unlink(makeSemName("resp_empty", kidId).c_str());
        sem_unlink(makeSemName("resp_full", kidId).c_str());
    }
    s_numKids = 0;
}

Conn::Conn(int kidId, bool isHost)
    : m_kidId(kidId), m_isHost(isHost), m_fd(-1), m_mem(nullptr),
      m_reqEmpty(SEM_FAILED), m_reqFull(SEM_FAILED), m_respEmpty(SEM_FAILED), m_respFull(SEM_FAILED) {

    std::string shmName = makeName(kidId);

    int flags = O_RDWR;
    if (isHost) flags |= O_CREAT;
    m_fd = shm_open(shmName.c_str(), flags, 0666);
    if (m_fd == -1) { perror("shm_open"); return; }

    m_mem = mmap(nullptr, kShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
    if (m_mem == MAP_FAILED) { perror("mmap"); m_mem = nullptr; return; }

    m_reqEmpty  = sem_open(makeSemName("req_empty", kidId).c_str(), 0);
    m_reqFull   = sem_open(makeSemName("req_full", kidId).c_str(), 0);
    m_respEmpty = sem_open(makeSemName("resp_empty", kidId).c_str(), 0);
    m_respFull  = sem_open(makeSemName("resp_full", kidId).c_str(), 0);

    if (m_reqEmpty == SEM_FAILED || m_reqFull == SEM_FAILED ||
        m_respEmpty == SEM_FAILED || m_respFull == SEM_FAILED) {
        perror("sem_open");
        return;
    }
}

Conn::~Conn() {
    if (m_reqEmpty != SEM_FAILED)  sem_close(m_reqEmpty);
    if (m_reqFull != SEM_FAILED)   sem_close(m_reqFull);
    if (m_respEmpty != SEM_FAILED) sem_close(m_respEmpty);
    if (m_respFull != SEM_FAILED)  sem_close(m_respFull);

    if (m_mem) munmap(m_mem, kShmSize);
    if (m_fd != -1) close(m_fd);

    if (m_isHost) {
        shm_unlink(makeName(m_kidId).c_str());
        sem_unlink(makeSemName("req_empty", m_kidId).c_str());
        sem_unlink(makeSemName("req_full", m_kidId).c_str());
        sem_unlink(makeSemName("resp_empty", m_kidId).c_str());
        sem_unlink(makeSemName("resp_full", m_kidId).c_str());
    }
}

bool Conn::Write(const void* buf, size_t count) {
    if (!m_mem) return false;
    auto* ch = static_cast<ShmChannel*>(m_mem);

    if (m_isHost) {
        if (count > sizeof(KidRequest)) return false;

        timespec absTs = makeAbsTimeoutMs(kShmTimeoutMs);
        if (sem_timedwait(m_reqEmpty, &absTs) == -1) return false;

        std::memcpy(&ch->request, buf, count);
        ch->requestReady = 1;
        sem_post(m_reqFull);
        return true;
    } else {
        if (count > sizeof(KidResponse)) return false;

        timespec absTs = makeAbsTimeoutMs(kShmTimeoutMs);
        if (sem_timedwait(m_respEmpty, &absTs) == -1) return false;

        std::memcpy(&ch->response, buf, count);
        ch->responseReady = 1;
        sem_post(m_respFull);
        return true;
    }
}

bool Conn::Read(void* buf, size_t count) {
    if (!m_mem) return false;
    auto* ch = static_cast<ShmChannel*>(m_mem);

    if (m_isHost) {
        if (count > sizeof(KidResponse)) return false;

        timespec absTs = makeAbsTimeoutMs(kShmTimeoutMs);
        if (sem_timedwait(m_respFull, &absTs) == -1) return false;

        std::memcpy(buf, &ch->response, count);
        ch->responseReady = 0;
        sem_post(m_respEmpty);
        return true;
    } else {
        if (count > sizeof(KidRequest)) return false;

        if (sem_wait(m_reqFull) == -1) return false;

        std::memcpy(buf, &ch->request, count);
        ch->requestReady = 0;
        sem_post(m_reqEmpty);
        return true;
    }
}
