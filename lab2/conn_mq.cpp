#include "conn_mq.hpp"
#include "protocol.hpp"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <string>
#include <ctime>

static constexpr int kMqTimeoutMs = 2000;

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
        ts.tv_nsec = ts.tv_nsec % 1000000000L;
    }
    return ts;
}

static constexpr size_t kMsgSize =
    sizeof(KidRequest) > sizeof(KidResponse) ? sizeof(KidRequest) : sizeof(KidResponse);

static std::string makeName(bool hostToKid, int kidId) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), hostToKid ? "/wolf_mq_h2k_%d" : "/wolf_mq_k2h_%d", kidId);
    return std::string(buf);
}

bool Conn::initParent(int numKids) { (void)numKids; return true; }
void Conn::cleanupParent() {}

Conn::Conn(int kidId, bool isHost)
    : m_kidId(kidId), m_isHost(isHost), m_qHostToKid((mqd_t)-1), m_qKidToHost((mqd_t)-1) {

    std::string nameH2K = makeName(true, kidId);
    std::string nameK2H = makeName(false, kidId);

    mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = kMsgSize;

    int oflag = O_RDWR | O_CREAT;

    m_qHostToKid = mq_open(nameH2K.c_str(), oflag, 0666, &attr);
    if (m_qHostToKid == (mqd_t)-1) perror("mq_open h2k");

    m_qKidToHost = mq_open(nameK2H.c_str(), oflag, 0666, &attr);
    if (m_qKidToHost == (mqd_t)-1) perror("mq_open k2h");
}

Conn::~Conn() {
    if (m_qHostToKid != (mqd_t)-1) mq_close(m_qHostToKid);
    if (m_qKidToHost != (mqd_t)-1) mq_close(m_qKidToHost);

    if (m_isHost) {
        mq_unlink(makeName(true, m_kidId).c_str());
        mq_unlink(makeName(false, m_kidId).c_str());
    }
}

bool Conn::Write(const void* buf, size_t count) {
    mqd_t q = m_isHost ? m_qHostToKid : m_qKidToHost;
    if (q == (mqd_t)-1) return false;
    if (count > kMsgSize) return false;

    char tmp[kMsgSize]{};
    std::memcpy(tmp, buf, count);

    timespec absTs = makeAbsTimeoutMs(kMqTimeoutMs);
    if (mq_timedsend(q, tmp, kMsgSize, 0, &absTs) == -1) {
        if (errno == ETIMEDOUT) std::cerr << "mq_send: timeout\n";
        else perror("mq_timedsend");
        return false;
    }
    return true;
}

bool Conn::Read(void* buf, size_t count) {
    mqd_t q = m_isHost ? m_qKidToHost : m_qHostToKid;
    if (q == (mqd_t)-1) return false;
    if (count > kMsgSize) return false;

    char tmp[kMsgSize];
    ssize_t n;

    if (m_isHost) {
        timespec absTs = makeAbsTimeoutMs(kMqTimeoutMs);
        n = mq_timedreceive(q, tmp, kMsgSize, nullptr, &absTs);
    } else {
        n = mq_receive(q, tmp, kMsgSize, nullptr); // ребёнок ждёт бесконечно
    }

    if (n == -1) {
        if (errno == ETIMEDOUT) std::cerr << "mq_receive: timeout\n";
        else perror(m_isHost ? "mq_timedreceive" : "mq_receive");
        return false;
    }

    std::memcpy(buf, tmp, count);
    return true;
}
