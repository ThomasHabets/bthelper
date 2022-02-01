#include "common.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


namespace bthelper {

bool parse_addr(const std::string& in, bdaddr_t* out)
{
    const char* fmt = "%2x:%2x:%2x:%2x:%2x:%2x%c";
    unsigned int n[6];
    char d;
    const auto rc = sscanf(in.c_str(), fmt, &n[0], &n[1], &n[2], &n[3], &n[4], &n[5], &d);
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
        exit(1); // TODO: return value.
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
        exit(1); // TODO: return value.
        return data;
    }
    return { data.begin() + rc, data.end() };
}

std::pair<int, bool> xatoi(const char* v)
{
    char* end = nullptr;
    int ret = strtol(v, &end, 0);
    return { ret, !*end };
}

// Copy from a to b, and b to a. Specifically:
//   ar -> b
//   b -> aw
//
// TODO: this function should return 0 on EOF, but doesn't.
//
// Return true on success.
bool shuffle(int ar, int aw, int b)
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

        const auto rc
            = select(std::max(std::max(ar, aw), b) + 1, &rfds, &wfds, &efds, NULL);
        if (rc < 0) {
            perror("select()");
            return false;
        }

        if (FD_ISSET(ar, &efds) || FD_ISSET(aw, &efds) || FD_ISSET(b, &efds)) {
            fprintf(stderr, "select() error\n");
            return false;
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
} // namespace bthelper
