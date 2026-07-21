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
#include "shuffle.h"

#include <fcntl.h>
#include <system_error>
#include <unistd.h>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <sys/select.h>

namespace {
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

std::vector<uint8_t> do_read(int fd)
{
    constexpr size_t read_size = 64 * 1024;
    std::vector<uint8_t> ret(read_size);
    for (;;) {
        const auto s = read(fd, ret.data(), ret.size());
        if (s < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "read()");
        }
        ret.resize(static_cast<size_t>(s));
        return ret;
    }
}

size_t do_write(int fd, const ustring_view data)
{
    for (;;) {
        const auto rc = write(fd, data.data(), data.size());
        if (rc < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "write()");
        }
        return static_cast<size_t>(rc);
    }
}
} // namespace

void Shuffler::copy(int src, int dst, std::shared_ptr<Buffer> buf, std::optional<uint8_t> esc)
{
    if (!buf) {
        buf = std::make_shared<RawBuffer>();
    }
    streams_.emplace_back(src, dst, std::move(buf), esc);
}

void Shuffler::watch(int fd, Shuffler::watch_handler_t cb)
{
    watchers_.emplace_back(Watcher{ .fd = fd, .cb = cb });
}

void Shuffler::run()
{
    // Set nonblock.
    for (const auto& s : streams_) {
        set_nonblock(s.src());
    }

    // Event loop.
    for (;;) {
        if (streams_.empty()) {
            return;
        }

        fd_set rfds;
        fd_set wfds;
        fd_set efds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
        int mx = -1;

        // Add readers & writers.
        for (const auto& s : streams_) {
            FD_SET(s.src(), &efds);
            FD_SET(s.dst(), &efds);
            mx = std::max({ mx, s.dst(), s.src() });
            if (s.empty()) {
                FD_SET(s.src(), &rfds);
            } else {
                FD_SET(s.dst(), &wfds);
            }
        }

        // Add watchers.
        for (const auto& w : watchers_) {
            mx = std::max(mx, w.fd);
            FD_SET(w.fd, &rfds);
        }

        // select()
        const auto rc = select(mx + 1, &rfds, &wfds, &efds, NULL);
        if (rc < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "select()");
        }

        // Check watchers.
        for (const auto& w : watchers_) {
            if (FD_ISSET(w.fd, &rfds)) {
                w.cb(w.fd);
            }
        }

        // Check for errors.
        for (size_t c = 0; c < streams_.size();) {
            auto& s = streams_[c];
            if (FD_ISSET(s.src(), &efds) || FD_ISSET(s.dst(), &efds)) {
                streams_.erase(streams_.begin() + static_cast<ssize_t>(c));
                continue;
            }
            c++;
        }

        // Write.
        for (auto& s : streams_) {
            if (FD_ISSET(s.dst(), &wfds)) {
                s.ack(do_write(s.dst(), s.peek()));
            }
        }

        // Read.
        for (size_t c = 0; c < streams_.size();) {
            auto& s = streams_[c];

            if (FD_ISSET(s.src(), &rfds)) {
                auto buf = do_read(s.src());
                if (buf.empty()) {
                    return;
                }
                if (s.check_esc(buf)) {
                    return;
                }
                s.write(buf);
            }
            c++;
        }
    }
}

Shuffler::Stream::Stream(int src, int dst, std::shared_ptr<Buffer> buf, std::optional<uint8_t> esc)
    : src_(src), dst_(dst), buf_(std::move(buf)), esc_(esc)
{
}

bool Shuffler::Stream::check_esc(const std::vector<uint8_t>& input) const
{
    if (!esc_.has_value()) {
        return false;
    }
    const auto esc = esc_.value();
    return std::any_of(input.begin(), input.end(), [esc](char ch) {
        return static_cast<uint8_t>(ch) == esc;
    });
}

#if 0
int main()
{
    Shuffler shuf;
    shuf.watch(0, [](int fd) { std::cout << "Readable\n"; });
    shuf.copy(0, 1);
    shuf.run();
}
#endif
/*
 * vim: ts=4 sw=4
 */
