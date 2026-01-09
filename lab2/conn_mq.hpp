#pragma once
#include <cstddef>
#include <mqueue.h>

class Conn {
public:
    Conn(int kidId, bool isHost);
    ~Conn();

    static bool initParent(int numKids);
    static void cleanupParent();

    bool Read(void* buf, size_t count);
    bool Write(const void* buf, size_t count);

private:
    int m_kidId;
    bool m_isHost;
    mqd_t m_qHostToKid;
    mqd_t m_qKidToHost;
};
