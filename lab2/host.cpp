#include <iostream>
#include <vector>
#include <memory>
#include <random>
#include <chrono>
#include <thread>
#include <string>
#include <cstdlib>
#include <cmath>

#include <unistd.h>
#include <sys/wait.h>
#include <poll.h>

#include "protocol.hpp"

#define STRINGIFY2(x) #x
#define STRINGIFY(x) STRINGIFY2(x)

#ifdef CURRENT_CONN_HEADER
#include STRINGIFY(CURRENT_CONN_HEADER)
#else
#include "conn_pipe.hpp"
#endif

struct KidState {
    pid_t pid{};
    bool alive{true};
    int lastNumber{-1};
};

int get_wolf_number() {
    std::cout << "Введите число волка [1..100] за 3 секунды "
                 "(иначе выберется случайно): " << std::flush;

    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 3000);
    if (ret > 0) {
        int num;
        if (std::cin >> num) {
            if (num < 1) num = 1;
            if (num > 100) num = 100;
            return num;
        } else {
            std::cin.clear();
            std::string dummy;
            std::getline(std::cin, dummy);
        }
    } else if (ret == -1) {
        perror("poll");
    }

    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(1, 100);
    int num = dist(rng);
    std::cout << "\nВвод не получен, волк выбросил: " << num << std::endl;
    return num;
}

void child_process(int kidId) {
    Conn conn(kidId, false);
    KidRequest req{};
    KidResponse resp{};

    std::mt19937 rng(
        static_cast<unsigned>(
            std::chrono::steady_clock::now().time_since_epoch().count() ^
            (kidId * 1337) ^ getpid()
        )
    );

    while (true) {
        if (!conn.Read(&req, sizeof(req))) {
            std::cerr << "[kid " << kidId << "] ошибка чтения или конец соединения\n";
            break;
        }

        if (req.command == KidCommand::Shutdown) {
            break;
        }

        bool alive = (req.alive != 0);
        int low = 1;
        int high = alive ? 100 : 50;

        std::uniform_int_distribution<int> dist(low, high);
        int number = dist(rng);
        resp.number = number;

        if (!conn.Write(&resp, sizeof(resp))) {
            std::cerr << "[kid " << kidId << "] ошибка записи\n";
            break;
        }
    }

    _exit(0);
}

int main(int argc, char* argv[]) {
    int nKids = 3;
    if (argc > 1) {
        nKids = std::atoi(argv[1]);
        if (nKids <= 0) {
            std::cerr << "Неверное количество козлят\n";
            return 1;
        }
    }

    if (!Conn::initParent(nKids)) {
        std::cerr << "Не удалось инициализировать IPC в родителе\n";
        return 1;
    }

    std::vector<KidState> kids(nKids);
    std::vector<std::unique_ptr<Conn>> conns(nKids);

    for (int i = 0; i < nKids; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid == 0) {
            child_process(i);
        }

        kids[i].pid = pid;
        kids[i].alive = true;
        kids[i].lastNumber = -1;

        conns[i] = std::make_unique<Conn>(i, true);
        std::cout << "Запущен козлёнок " << i << ", pid = " << pid << std::endl;
    }

    int consecutiveAllDead = 0;

    while (true) {
        int wolfNum = get_wolf_number();
        std::vector<int> kidNums(nKids, 0);

        for (int i = 0; i < nKids; ++i) {
            KidRequest req{};
            req.command = KidCommand::PlayMove;
            req.alive = kids[i].alive ? 1 : 0;
            if (!conns[i]->Write(&req, sizeof(req))) {
                std::cerr << "Не удалось отправить запрос козлёнку " << i << std::endl;
            }
        }

        for (int i = 0; i < nKids; ++i) {
            KidResponse resp{};
            if (!conns[i]->Read(&resp, sizeof(resp))) {
                std::cerr << "Не удалось прочитать ответ от козлёнка " << i << std::endl;
                kidNums[i] = 0;
            } else {
                kidNums[i] = resp.number;
                kids[i].lastNumber = resp.number;
            }
        }

        int hidden = 0;
        int caught = 0;
        int deadCount = 0;

        for (int i = 0; i < nKids; ++i) {
            bool wasAlive = kids[i].alive;
            int kidNum = kidNums[i];
            double diff = std::abs(kidNum - wolfNum);

            if (wasAlive) {
                if (diff <= 70.0 / nKids) {
                    hidden++;
                } else {
                    kids[i].alive = false;
                    caught++;
                }
            } else {
                if (diff <= 20.0 / nKids) {
                    kids[i].alive = true;
                }
            }
        }

        for (int i = 0; i < nKids; ++i) {
            if (!kids[i].alive) deadCount++;
        }

        std::cout << "                  РАУНД\n";
        std::cout << "Число волка: " << wolfNum << "\n";
        for (int i = 0; i < nKids; ++i) {
            std::cout << "Козлёнок " << i
                      << " выбросил " << kidNums[i]
                      << " -> " << (kids[i].alive ? "ЖИВ" : "МЁРТВ") << "\n";
        }
        std::cout << "Спрятались: " << hidden
                  << "  Попались: " << caught
                  << "  Мёртвых всего: " << deadCount << "\n\n";

        if (deadCount == nKids) {
            consecutiveAllDead++;
        } else {
            consecutiveAllDead = 0;
        }

        if (consecutiveAllDead >= 2) {
            std::cout << "Все козлята мертвы два хода подряд. Игра окончена.\n";
            break;
        }
    }

    KidRequest stopReq{};
    stopReq.command = KidCommand::Shutdown;
    stopReq.alive = 0;

    for (int i = 0; i < nKids; ++i) {
        conns[i]->Write(&stopReq, sizeof(stopReq));
    }

    for (int i = 0; i < nKids; ++i) {
        int status = 0;
        waitpid(kids[i].pid, &status, 0);
    }

    conns.clear();
    Conn::cleanupParent();

    return 0;
}
