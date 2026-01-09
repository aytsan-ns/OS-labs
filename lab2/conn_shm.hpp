#pragma once
#include <cstddef>
#include <semaphore.h>

class Conn {
public:
    Conn(int kidId, bool isHost);
    ~Conn();

    static bool initParent(int numKids);
    static void cleanupParent();

    bool Read(void* buf, size_t count);
    bool Write(const void* buf, size_t count);

private:
    static int s_numKids;

    int m_kidId;
    bool m_isHost;
    int m_fd;
    void* m_mem;

    sem_t* m_reqEmpty;
    sem_t* m_reqFull;
    sem_t* m_respEmpty;
    sem_t* m_respFull;
};
