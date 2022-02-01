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
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace bthelper;

namespace {
void usage(const char* av0, int err)
{
    fprintf(stderr, "Usage: %s [ -h ] <bluetooth destination> <channel>\n", av0);
    exit(err);
}
} // namespace

int main(int argc, char** argv)
{
    {
        int opt;
        while ((opt = getopt(argc, argv, "h")) != -1) {
            switch (opt) {
            case 'h':
                usage(argv[0], EXIT_SUCCESS);
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
        perror("Failed to bind");
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
        perror("Failed to connect");
        return EXIT_FAILURE;
    }

    // Start copying data.
    if (!set_nonblock(STDIN_FILENO) || !set_nonblock(STDOUT_FILENO)
        || !set_nonblock(sock)) {
        return EXIT_FAILURE;
    }
    return shuffle(STDIN_FILENO, STDOUT_FILENO, sock) ? EXIT_SUCCESS : EXIT_FAILURE;
}
