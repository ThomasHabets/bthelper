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
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

/*
 * Bluetooth stuff.
 */
typedef struct {
    uint8_t b[6];
} __attribute__((packed)) bdaddr_t;
struct sockaddr_rc {
    sa_family_t rc_family;
    bdaddr_t rc_bdaddr;
    uint8_t rc_channel;
};
constexpr int BTPROTO_RFCOMM = 3;

namespace {
void usage(const char* av0, int err)
{
    fprintf(stderr, "Usage: %s [ -h ] <bluetooth destination> <channel>\n", av0);
    exit(err);
}

bool parse_addr(const std::string& in, bdaddr_t* out)
{
    const char* fmt = "%2x:%2x:%2x:%2x:%2x:%2x%c";
    unsigned int n[6];
    char dummy;
    const auto rc =
        sscanf(in.c_str(), fmt, &n[0], &n[1], &n[2], &n[3], &n[4], &n[5], &dummy);
    if (rc != 6) {
        return false;
    }
    for (int c = 0; c < 6; c++) {
        out->b[c] = n[5 - c] & 0xff;
    }
    return true;
}

bool set_nonblock(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl(F_GETFL)");
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
        perror("fcntl(F_SETFL)");
        return false;
    }
    return true;
}

std::vector<char> do_read(int fd)
{
    constexpr size_t read_size = 128;
    std::vector<char> ret;
    ret.resize(read_size);
    const auto s = read(fd, ret.data(), ret.size());
    if (s < 0) {
        perror("read()");
        return {};
    }
    ret.resize(s);
    return ret;
}

std::vector<char> do_write(int fd, const std::vector<char>& data)
{
    const auto rc = write(fd, data.data(), data.size());
    if (rc < 0) {
        perror("write()");
        return data;
    }
    return { data.begin() + rc, data.end() };
}

// Copy from a to b, and b to a. Specifically:
//   ar -> b
//   b -> aw
//
// TODO: this function should return 0 on EOF, but doesn't.
int shuffle(int ar, int aw, int b)
{
    std::vector<char> de_a, de_b;
    for (;;) {
        fd_set rfds, wfds, efds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
        FD_SET(ar, &efds);
        FD_SET(aw, &efds);
        FD_SET(b, &efds);

        if (de_a.empty()) {
            FD_SET(ar, &rfds);
        } else {
            FD_SET(b, &wfds);
        }

        if (de_b.empty()) {
            FD_SET(b, &rfds);
        } else {
            FD_SET(aw, &wfds);
        }

        const auto rc =
            select(std::max(std::max(ar, aw), b) + 1, &rfds, &wfds, &efds, NULL);
        if (rc < 0) {
            perror("select()");
            return EXIT_FAILURE;
        }

        if (FD_ISSET(ar, &efds) || FD_ISSET(aw, &efds) || FD_ISSET(b, &efds)) {
            fprintf(stderr, "select() error\n");
            return EXIT_FAILURE;
        }

        if (de_a.empty() && FD_ISSET(ar, &rfds)) {
            de_a = do_read(ar);
        }
        if (de_b.empty() && FD_ISSET(b, &rfds)) {
            de_b = do_read(b);
        }
        if (!de_a.empty() && FD_ISSET(b, &wfds)) {
            de_a = do_write(b, de_a);
        }
        if (!de_b.empty() && FD_ISSET(aw, &wfds)) {
            de_b = do_write(aw, de_b);
        }
    }
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
    int channel;
    {
        char* end = nullptr;
        channel = strtol(argv[optind + 1], &end, 0);
        if (*end) {
            fprintf(stderr, "Unable to parse channel number: %s\n", argv[optind + 1]);
            exit(EXIT_FAILURE);
        }
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
    if (!set_nonblock(STDIN_FILENO) || !set_nonblock(STDOUT_FILENO) ||
        !set_nonblock(sock)) {
        return EXIT_FAILURE;
    }
    return shuffle(STDIN_FILENO, STDOUT_FILENO, sock);
}
