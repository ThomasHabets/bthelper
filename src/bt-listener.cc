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

#include <limits.h>
#include <netdb.h>
#include <pty.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

using namespace bthelper;

namespace {
const std::string escape_word = "{}";
int verbose = 0;

void usage(const char* av0, int err)
{
    fprintf(
        stderr, "Usage: %s [ -hv ] [ -t <target> ] [ -e <exec> ] -c <channel>\n", av0);
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
    if (verbose > 1) {
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
    freeaddrinfo(addrs);
    return sock;
}


// ttyname() except with "/dev" stripped.
std::string xttyname(int fd)
{
    char buf[PATH_MAX] = { 0 };
    const auto err = ttyname_r(fd, buf, sizeof buf);
    if (err) {
        fprintf(stderr, "ttyname_r(): %d: %s\n", err, strerror(errno));
        exit(EXIT_FAILURE);
    }
    const std::string s = buf;
    const std::string prefix = "/dev";
    if (prefix == s.substr(0, prefix.size())) {
        return s.substr(prefix.size());
    }
    return s;
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
    if (!shuffle(ar, aw, sock)) {
        std::cerr << "Connection failed\n";
    }
}

int exec_child(std::vector<const char*> exec_args)
{
    exec_args.push_back(nullptr);
    execvp(exec_args[0], const_cast<char* const*>(&exec_args[0]));
    perror("exec()");
    return EXIT_FAILURE;
}

int handle_exec(int con, std::vector<const char*> exec_args)
{
    int amaster;
    const auto pid = forkpty(&amaster, NULL, NULL, NULL);
    if (pid == -1) {
        perror("forkpty()");
        return EXIT_FAILURE;
    }

    if (!pid) {
        close(con);
        const auto tty = xttyname(0);
        for (int i = 0; i < exec_args.size(); i++) {
            if (exec_args[i] == escape_word) {
                exec_args[i] = tty.c_str();
            }
        }
        struct termios tio;
        cfmakeraw(&tio);
        if (tcsetattr(0, TCSADRAIN, &tio)) {
            std::cerr << "tcsetattr(raw)\n";
            exit(EXIT_FAILURE);
        }
        execvp(exec_args[0], const_cast<char* const*>(&exec_args[0]));
        perror("exec()");
        exit(EXIT_FAILURE);
    }
    if (!shuffle(amaster, amaster, con)) {
        std::cerr << "Connection failed\n";
    }
    int status;
    close(con);
    close(amaster);
    const auto rpid = waitpid(pid, &status, 0);
    if (rpid != pid) {
        std::cerr << "waitpid(): failed " << strerror(errno) << "\n";
        return EXIT_FAILURE;
    }

    if (!WIFEXITED(status)) {
        std::cerr << "waitpid(): did not fail normally\n";
        return EXIT_FAILURE;
    }
    return WEXITSTATUS(status);
}

} // namespace

int main(int argc, char** argv)
{
    int channel = -1;
    std::string target;
    bool do_exec = false;
    {
        int opt;
        while ((opt = getopt(argc, argv, "c:ht:e:v")) != -1) {
            switch (opt) {
            case 'e':
                do_exec = true;
                break;
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
            case 'v':
                verbose++;
                break;
            default:
                usage(argv[0], EXIT_FAILURE);
            }
        }
    }
    std::vector<const char*> exec_args;
    if (do_exec) {
        if (optind == argc) {
            std::cerr << argv[0] << ": -e specified but no command line given\n";
            exit(EXIT_FAILURE);
        }
        for (int i = optind; i < argc; i++) {
            exec_args.push_back(argv[i]);
        }
    } else {
        if (optind != argc) {
            std::cerr << argv[0] << ": got trailing args on command line\n";
            exit(EXIT_FAILURE);
        }
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
        const int con = accept(sock, reinterpret_cast<sockaddr*>(&raddr), &socklen);
        if (verbose) {
            std::cerr << "Client connected\n";
        }
        // TODO: log remote address
        // TODO: fork.
        if (do_exec) {
            handle_exec(con, exec_args);
        } else {
            connection(con, target);
        }
    }
}
