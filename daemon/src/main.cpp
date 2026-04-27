// SPDX-License-Identifier: Apache-2.0
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

#include "fuvr/daemon.hpp"

namespace {
std::atomic<bool> g_quit{false};
void onSignal(int) { g_quit.store(true); }
}

int main(int argc, char** argv) {
    std::string socketPath;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--socket" && i + 1 < argc) socketPath = argv[++i];
        else if (a == "--help" || a == "-h") {
            std::fprintf(stderr, "fuvrd [--socket PATH]\n");
            return 0;
        }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    fuvr::daemon::Daemon d;
    if (!d.start(socketPath)) {
        std::fprintf(stderr, "fuvrd: failed to start\n");
        return 1;
    }
    std::fprintf(stderr, "fuvrd: listening\n");

    while (!g_quit.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    d.stop();
    return 0;
}
