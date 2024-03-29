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
#include "shuffle.h"

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
const std::string escape_term = "{}";
const std::string escape_addr = "{addr}";
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

void connection(int sock, std::string_view remote, const std::string& target)
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
    Shuffler shuf;
    shuf.copy(ar, sock);
    shuf.copy(sock, aw);

    try {
        shuf.run();
    } catch (const std::system_error& e) {
        // Actually a normal way for the connection to end.
        if (e.code() == std::errc::connection_reset) {
            std::cerr << remote << " Disconnected\n";
        } else {
            throw;
        }
    }
}

std::vector<const char*> exec_c_args(const std::vector<std::string>& in)
{
    std::vector<const char*> ret;
    for (const auto& s : in) {
        ret.push_back(s.c_str());
    }
    ret.push_back(nullptr);
    return ret;
}


std::string subst(const std::string& from, const std::string& to, std::string s)
{
    for (;;) {
        auto pos = s.find(from);
        if (pos == std::string::npos) {
            return s;
        }
        s = s.substr(0, pos) + to + s.substr(pos + from.size());
    }
}

std::vector<std::string> substitute_args(const std::vector<std::string>& in,
                                         const std::string& term,
                                         const std::string& addr)
{
    std::vector<std::string> ret;
    for (const auto& s : in) {
        ret.push_back(subst(escape_term, term, subst(escape_addr, addr, s)));
    }
    return ret;
}


int exec_child(const std::vector<std::string>& exec_args, const std::string& addr)
{
    const auto tty = xttyname(0);
    const auto args = substitute_args(exec_args, tty, addr);
    struct termios tio {
    };
    cfmakeraw(&tio);
    if (tcsetattr(0, TCSADRAIN, &tio)) {
        std::cerr << "tcsetattr(raw)\n";
        exit(EXIT_FAILURE);
    }

    const auto cargs = exec_c_args(args);
    execvp(cargs[0], const_cast<char* const*>(&cargs[0]));
    perror("exec()");
    return EXIT_FAILURE;
}


int handle_exec(int con,
                std::string_view remote,
                const std::vector<std::string>& exec_args,
                const std::string& addr)
{
    int amaster;
    const auto pid = forkpty(&amaster, NULL, NULL, NULL);
    if (pid == -1) {
        perror("forkpty()");
        return EXIT_FAILURE;
    }

    if (!pid) {
        close(con);
        exec_child(exec_args, addr);
    }
    auto rx = std::make_unique<TelnetDecoderBuffer>(
        [amaster](uint16_t rows, uint16_t cols) {
            struct winsize ws {
            };
            ws.ws_row = rows;
            ws.ws_col = cols;
            if (-1 == ioctl(amaster, TIOCSWINSZ, &ws)) {
                perror("ioctl()");
            }
        },
        [](uint32_t cookie) { std::cerr << "PING\n"; },
        [](uint32_t cookie) { std::cerr << "PONG\n"; });

    Shuffler shuf;
    shuf.copy(amaster, con);
    shuf.copy(con, amaster, std::move(rx));
    try {
        shuf.run();
    } catch (const std::system_error& e) {
        // Actually a normal way for the connection to end, apparently.
        if (e.code() == std::errc::connection_reset) {
            std::cerr << remote << " Disconnected\n";
        } else if (e.code() == std::errc::io_error) {
            std::cerr << remote << " Terminal closed\n";
        } else {
            throw;
        }
    }
    close(con);
    close(amaster);

    int status;
    const auto rpid = waitpid(pid, &status, 0);
    if (rpid != pid) {
        std::cerr << remote << " waitpid(): " << strerror(errno) << "\n";
        return EXIT_FAILURE;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        std::cerr << remote << " Child process terminated due to signal: "
                  << strsignal(WTERMSIG(status)) << "\n";
        return EXIT_FAILURE;
    }

    std::cerr << remote << " waitpid(): Child process failed in unknown way\n";
    return EXIT_FAILURE;
}

} // namespace

int wrapmain(int argc, char** argv)
{
    int channel = -1;
    std::string target;
    bool do_exec = false;
    {
        int opt;
        while ((opt = getopt(argc, argv, "c:ht:ev")) != -1) {
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
    std::vector<std::string> exec_args;
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

    if (verbose) {
        std::cerr << "Listening…\n";
    }
    for (;;) {
        struct sockaddr_rc raddr {
        };
        socklen_t socklen = sizeof(raddr);
        const int con = accept(sock, reinterpret_cast<sockaddr*>(&raddr), &socklen);
        const auto remote = stringify_addr(&raddr.rc_bdaddr);
        if (verbose) {
            std::cerr << remote << " Client connected\n";
        }
        // TODO: log remote address
        // TODO: fork.
        if (do_exec) {
            handle_exec(con, remote, exec_args, remote);
        } else {
            connection(con, remote, target);
        }
    }
}
