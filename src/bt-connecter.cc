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

#include <signal.h>
#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace bthelper;

namespace {
struct termios orig_tio;
sig_atomic_t reset_terminal = 0;
constexpr uint8_t escape = 0x1d; // ^]

void sigint_handler(int)
{
    if (reset_terminal) {
        tcsetattr(0, TCSADRAIN, &orig_tio);
    }
    exit(1);
}

void usage(const char* av0, int err)
{
    fprintf(stderr,
            "Usage: %s [ -ht ] <bluetooth destination> <channel>\n"
            "  Options:\n"
            "    -h       Show this help.\n"
            "    -t       Use a raw terminal. E.g. when the other side is a getty.\n"
            "             Press ^] to abort.\n",
            av0);
    exit(err);
}
} // namespace

int main(int argc, char** argv)
{
    bool do_terminal = false;
    {
        int opt;
        while ((opt = getopt(argc, argv, "ht")) != -1) {
            switch (opt) {
            case 'h':
                usage(argv[0], EXIT_SUCCESS);
            case 't':
                do_terminal = true;
                break;
            default:
                usage(argv[0], EXIT_FAILURE);
            }
        }
    }

    if (optind + 2 != argc) {
        fprintf(stderr, "Need exactly two args, the destination and the channel\n");
        usage(argv[0], EXIT_FAILURE);
    }

    // Args.
    const std::string addrs = argv[optind];
    const std::string chans = argv[optind + 1];
    int channel;
    {
        const auto ch_ok = xatoi(chans.c_str());
        if (!ch_ok.second) {
            fprintf(stderr, "Unable to parse channel number: %s\n", chans.c_str());
            exit(EXIT_FAILURE);
        }
        channel = ch_ok.first;
    }

    int sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

    // Bind to zeroes.
    struct sockaddr_rc laddr {
    };
    laddr.rc_family = AF_BLUETOOTH;
    if (bind(sock, reinterpret_cast<sockaddr*>(&laddr), sizeof(laddr))) {
        perror("bind()");
        close(sock);
        return EXIT_FAILURE;
    }

    // Connect to remote end.
    struct sockaddr_rc addr {
    };
    addr.rc_family = AF_BLUETOOTH;
    if (!parse_addr(addrs, &addr.rc_bdaddr)) {
        fprintf(stderr, "Failed to parse <%s> as a bluetooth address\n", addrs.c_str());
        return EXIT_FAILURE;
    }
    addr.rc_channel = channel;
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))) {
        perror("connect()");
        return EXIT_FAILURE;
    }

    if (do_terminal) {
        if (tcgetattr(0, &orig_tio)) {
            perror("tcgetattr(stdin)");
            exit(EXIT_FAILURE);
        }
        reset_terminal = 1;
        signal(SIGINT, sigint_handler);

        struct termios tio;
        cfmakeraw(&tio);
        tio.c_lflag &= ~ECHO;

        if (tcsetattr(0, TCSADRAIN, &tio)) {
            perror("tcsetattr(raw minus echo)");
            exit(EXIT_FAILURE);
        }
    }

    // Start copying data.
    const auto ret = shuffle(STDIN_FILENO, STDOUT_FILENO, sock, do_terminal ? escape : -1)
                         ? EXIT_SUCCESS
                         : EXIT_FAILURE;
    if (reset_terminal) {
        if (tcsetattr(0, TCSADRAIN, &orig_tio)) {
            perror("tcsetattr(reset)");
            exit(EXIT_FAILURE);
        }
        std::cout << "\n";
    }
    return ret;
}
