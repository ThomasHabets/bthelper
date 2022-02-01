/*
   Copyright 2022 Google LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "common.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

using namespace bthelper;

namespace {
void usage(const char* av0, int err)
{
    fprintf(stderr, "Usage: %s [ -h ] [-t <target> ] -c <channel>\n", av0);
    exit(err);
}

void connection(int sock)
{
    if (!set_nonblock(STDIN_FILENO) || !set_nonblock(STDOUT_FILENO)
        || !set_nonblock(sock)) {
        exit(1);
    }

    if (!shuffle(STDIN_FILENO, STDOUT_FILENO, sock)) {
        std::cerr << "Connection failed\n";
    }
}
} // namespace

int main(int argc, char** argv)
{
    int channel = -1;
    std::string target;

    {
        int opt;
        while ((opt = getopt(argc, argv, "c:h")) != -1) {
            switch (opt) {
            case 'h':
                usage(argv[0], EXIT_SUCCESS);
            case 'c': {
                const auto ch_ok = xatoi(optarg);
                if (!ch_ok.second) {
                    std::cerr << argv[0]
                              << ": channel number (-c) not a number: " << optarg << "\n";
                    exit(EXIT_FAILURE);
                }
                channel = ch_ok.first;
                if (channel < 1 || channel > 30) {
                    std::cerr << argv[0] << ": channel needs to be a number 1-30\n";
                    exit(EXIT_FAILURE);
                }
                break;
            }
            case 't':
                target = optarg;
                break;
            default:
                usage(argv[0], EXIT_FAILURE);
            }
        }
    }
    if (optind != argc) {
        std::cerr << argv[0] << ": got trailing args on command line\n";
        exit(EXIT_FAILURE);
    }

    if (!target.empty()) {
        std::cerr << argv[0] << ": -t not yet implemented\n";
        exit(EXIT_FAILURE);
    }
    if (channel < 0) {
        std::cerr << argv[0] << ": channel (-c) not specified\n";
        exit(EXIT_FAILURE);
    }

    int sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

    // Bind to zeroes.
    struct sockaddr_rc laddr {
    };
    laddr.rc_family = AF_BLUETOOTH;
    laddr.rc_channel = channel;
    if (bind(sock, reinterpret_cast<sockaddr*>(&laddr), sizeof(laddr))) {
        perror("Failed to bind");
        close(sock);
        return EXIT_FAILURE;
    }
    if (listen(sock, 10)) {
        perror("listen()");
        return EXIT_FAILURE;
    }

    for (;;) {
        struct sockaddr_rc raddr {
        };
        socklen_t socklen = sizeof(raddr);
        int con = accept(sock, reinterpret_cast<sockaddr*>(&raddr), &socklen);
        // TODO: fork.
        connection(con);
    }
}
