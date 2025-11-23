#include "conn_pipe.hpp"
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <iostream>

int Conn::s_numKids = 0;
int (*Conn::s_parentToChild)[2] = nullptr;
int (*Conn::s_childToParent)[2] = nullptr;

bool Conn::initParent(int numKids) {
    s_numKids = numKids;
    s_parentToChild = new int[numKids][2];
    s_childToParent = new int[numKids][2];

    for (int i = 0; i < numKids; ++i) {
        if (pipe(s_parentToChild[i]) == -1) {
            perror("pipe parentToChild");
            return false;
        }
        if (pipe(s_childToParent[i]) == -1) {
            perror("pipe childToParent");
            return false;
        }
    }
    return true;
}

void Conn::cleanupParent() {
    if (!s_parentToChild || !s_childToParent) return;

    for (int i = 0; i < s_numKids; ++i) {
        close(s_parentToChild[i][0]);
        close(s_parentToChild[i][1]);
        close(s_childToParent[i][0]);
        close(s_childToParent[i][1]);
    }

    delete[] s_parentToChild;
    delete[] s_childToParent;
    s_parentToChild = nullptr;
    s_childToParent = nullptr;
    s_numKids = 0;
}

Conn::Conn(int kidId, bool isHost) : m_readFd(-1), m_writeFd(-1), m_isHost(isHost), m_kidId(kidId) {
    if (!s_parentToChild || !s_childToParent) {
        std::cerr << "Conn(pipe): initParent не был вызван\n";
        return;
    }

    if (kidId < 0 || kidId >= s_numKids) {
        std::cerr << "Conn(pipe): неверный kidId\n";
        return;
    }

    if (isHost) {
        m_writeFd = s_parentToChild[kidId][1];
        m_readFd  = s_childToParent[kidId][0];

        close(s_parentToChild[kidId][0]);
        close(s_childToParent[kidId][1]);
    } else {
        m_writeFd = s_childToParent[kidId][1];
        m_readFd  = s_parentToChild[kidId][0];

        close(s_parentToChild[kidId][1]);
        close(s_childToParent[kidId][0]);
    }
}

Conn::~Conn() {
    if (m_readFd != -1) 
        close(m_readFd);
    if (m_writeFd != -1)
        close(m_writeFd);
}

bool Conn::Write(const void* buf, size_t count) {
    const char* data = static_cast<const char*>(buf);
    size_t total = 0;
    while (total < count) {
        ssize_t n = write(m_writeFd, data + total, count - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("write(pipe)");
            return false;
        }
        if (n == 0) break;
        total += static_cast<size_t>(n);
    }
    return total == count;
}

bool Conn::Read(void* buf, size_t count) {
    char* data = static_cast<char*>(buf);
    size_t total = 0;
    while (total < count) {
        ssize_t n = read(m_readFd, data + total, count - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read(pipe)");
            return false;
        }
        if (n == 0) {
            return false;
        }
        total += static_cast<size_t>(n);
    }
    return total == count;
}
