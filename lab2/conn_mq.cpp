#include "conn_mq.hpp"
#include "protocol.hpp"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <string>

static constexpr size_t kMsgSize = sizeof(KidRequest) > sizeof(KidResponse) ? sizeof(KidRequest) : sizeof(KidResponse);

static std::string makeName(bool hostToKid, int kidId) {
    char buf[64];
    if (hostToKid) {
        std::snprintf(buf, sizeof(buf), "/wolf_mq_h2k_%d", kidId);
    } else {
        std::snprintf(buf, sizeof(buf), "/wolf_mq_k2h_%d", kidId);
    }
    return std::string(buf);
}

bool Conn::initParent(int numKids) {
    (void)numKids;
    return true;
}

void Conn::cleanupParent() {
}

Conn::Conn(int kidId, bool isHost) : m_kidId(kidId), m_isHost(isHost), m_qHostToKid((mqd_t)-1), m_qKidToHost((mqd_t)-1) {
    std::string nameH2K = makeName(true, kidId);
    std::string nameK2H = makeName(false, kidId);

    struct mq_attr attr;
    std::memset(&attr, 0, sizeof(attr));
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = kMsgSize;

    int oflag = O_RDWR | O_CREAT;

    m_qHostToKid = mq_open(nameH2K.c_str(), oflag, 0666, &attr);
    if (m_qHostToKid == (mqd_t)-1) {
        perror("mq_open h2k");
    }

    m_qKidToHost = mq_open(nameK2H.c_str(), oflag, 0666, &attr);
    if (m_qKidToHost == (mqd_t)-1) {
        perror("mq_open k2h");
    }
}

Conn::~Conn() {
    if (m_qHostToKid != (mqd_t)-1) {
        mq_close(m_qHostToKid);
    }
    if (m_qKidToHost != (mqd_t)-1) {
        mq_close(m_qKidToHost);
    }

    if (m_isHost) {
        std::string nameH2K = makeName(true, m_kidId);
        std::string nameK2H = makeName(false, m_kidId);
        mq_unlink(nameH2K.c_str());
        mq_unlink(nameK2H.c_str());
    }
}

bool Conn::Write(const void* buf, size_t count) {
    mqd_t q = m_isHost ? m_qHostToKid : m_qKidToHost;
    if (q == (mqd_t)-1) return false;

    if (count > kMsgSize) {
        std::cerr << "mq: Write count > kMsgSize\n";
        return false;
    }

    char tmp[kMsgSize];
    std::memset(tmp, 0, kMsgSize);
    std::memcpy(tmp, buf, count);

    if (mq_send(q, tmp, kMsgSize, 0) == -1) {
        perror("mq_send");
        return false;
    }
    return true;
}


bool Conn::Read(void* buf, size_t count) {
    mqd_t q = m_isHost ? m_qKidToHost : m_qHostToKid;
    if (q == (mqd_t)-1) return false;

    if (count > kMsgSize) {
        std::cerr << "mq: Read count > kMsgSize\n";
        return false;
    }

    char tmp[kMsgSize];
    ssize_t n = mq_receive(q, tmp, kMsgSize, nullptr);
    if (n == -1) {
        perror("mq_receive");
        return false;
    }

    std::memcpy(buf, tmp, count);
    return true;
}

