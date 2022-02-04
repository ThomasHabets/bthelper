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

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <signal.h>
#include <sys/signalfd.h>
#include <termios.h>
#include <unistd.h>

#include <system_error>
#include <cstdio>
#include <cstring>
#include <ios>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace bthelper;

namespace {
struct termios orig_tio;
sig_atomic_t reset_terminal = 0;
constexpr uint8_t escape = 0x1d; // ^]

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

int setup_signalfd()
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGWINCH);
    const int fd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (-1 == fd) {
        throw std::system_error(errno, std::generic_category(), "signalfd()");
    }
    if (-1 == sigprocmask(SIG_BLOCK, &mask, nullptr)) {
        close(fd);
        throw std::system_error(errno, std::generic_category(), "sigprocmask()");
    }
    return fd;
}

void sigint_handler(int)
{
    if (reset_terminal) {
        tcsetattr(0, TCSADRAIN, &orig_tio);
    }
    exit(1);
}

void send_window(int terminal, TelnetEncoderBuffer* buf)
{
    struct winsize ws;
    if (-1 == ioctl(terminal, TIOCGWINSZ, reinterpret_cast<char*>(&ws))) {
        perror("ioctl");
    } else {
        buf->window_size(ws.ws_row, ws.ws_col);
    }
}

void set_raw_terminal(int terminal)
{
    struct termios tio;
    cfmakeraw(&tio);
    tio.c_lflag &= ~ECHO;
    if (tcsetattr(terminal, TCSADRAIN, &tio)) {
        throw std::system_error(
            errno, std::generic_category(), "tcsetattr(raw minus echo)");
    }
}

} // namespace

int wrapmain(int argc, char** argv)
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

    Shuffler shuf;

    if (do_terminal) {
        auto txbuf = std::make_unique<TelnetEncoderBuffer>();
        send_window(STDIN_FILENO, txbuf.get());

        auto sigfd = setup_signalfd();
        shuf.watch(sigfd, [sigfd, txbuf = txbuf.get()](int) {
            send_window(STDIN_FILENO, txbuf);
            struct signalfd_siginfo tmp;
            if (-1 == read(sigfd, &tmp, sizeof tmp)) {
                perror("read(signalfd)");
            }
        });

        signal(SIGINT, sigint_handler);
        set_raw_terminal(STDIN_FILENO);

        // shuf.copy(sock, STDOUT_FILENO, std::make_unique<TelnetEncoderBuffer>());
        shuf.copy(sock, STDOUT_FILENO);
        shuf.copy(STDIN_FILENO, sock, std::move(txbuf), escape);
    } else {
        shuf.copy(sock, STDOUT_FILENO);
        shuf.copy(STDIN_FILENO, sock);
    }

    try {
        shuf.run();
    } catch (const std::system_error& e) {
        // Actually a normal way for the connection to end.
        if (e.code() == std::errc::connection_reset) {
            std::cerr << "<Disconnected>\n\r";
        } else {
            throw;
        }
    }
    return EXIT_SUCCESS;
}
