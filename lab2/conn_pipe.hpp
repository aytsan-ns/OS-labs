#pragma once

#include <cstddef>

class Conn {
public:
    Conn(int kidId, bool isHost);
    ~Conn();

    static bool initParent(int numKids);
    static void cleanupParent();

    bool Read(void* buf, size_t count);
    bool Write(const void* buf, size_t count);

private:
    int m_readFd;
    int m_writeFd;
    bool m_isHost;
    int m_kidId;

    static int s_numKids;
    static int (*s_parentToChild)[2];
    static int (*s_childToParent)[2];
};
