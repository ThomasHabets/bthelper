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

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
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

std::pair<std::string, std::string> hostport_split(const std::string& in)
{
    const auto count = std::count(in.begin(), in.end(), ':');
    if (count == 0) {
        return { "", "" };
    }

    if (count == 1) {
        // IPv4 or hostname and port.
        const auto pos = in.find(':');
        return { in.substr(0, pos), in.substr(pos + 1) };
    }

    // More than one colon. IPv6 address. E.g. [::1]:22
    const auto pos = in.find_last_of(":");
    const auto host1 = in.substr(0, pos);
    const auto port = in.substr(pos + 1);
    if (host1.size() <= 2) {
        return { "", "" };
    }
    if (host1[0] != '[') {
        return { "", "" };
    }
    if (host1[host1.size() - 1] != ']') {
        return { "", "" };
    }
    return { host1.substr(1, host1.size() - 2), port };
}

int tcp_connect(const std::string& target)
{
    const auto hostport = hostport_split(target);
    const auto host = hostport.first;
    const auto port = hostport.second;
    if (host.empty() || port.empty()) {
        std::cerr << "Failed to parse " << target << "\n";
        return -1;
    }
    if (false) {
        std::cerr << "Host and port: <" << host << "> & <" << port << ">\n";
    }

    struct addrinfo hints {
    };
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    // hints.ai_flags = A
    struct addrinfo* addrs;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &addrs)) {
        perror("getaddrinfo()");
        return -1;
    }

    int sock = -1;
    for (struct addrinfo* ai = addrs; ai; ai = ai->ai_next) {
        int s = socket(ai->ai_family, SOCK_STREAM, 0);
        if (s == -1) {
            continue;
        }
        if (!connect(s, ai->ai_addr, ai->ai_addrlen)) {
            sock = s;
            break;
        }
    }
    return sock;
}

void connection(int sock, const std::string& target)
{
    int ar = STDIN_FILENO;
    int aw = STDOUT_FILENO;

    if (!target.empty()) {
        ar = aw = tcp_connect(target);
        if (ar == -1) {
            std::cerr << "Failed to connect\n";
            exit(1);
        }
    }

    if (!set_nonblock(ar) || !set_nonblock(aw) || !set_nonblock(sock)) {
        exit(1);
    }

    if (!shuffle(ar, aw, sock)) {
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
        while ((opt = getopt(argc, argv, "c:ht:")) != -1) {
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
        connection(con, target);
    }
}
