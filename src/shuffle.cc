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
#include <stdexcept>

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

std::string do_read(int fd)
{
    constexpr size_t read_size = 128;
    std::vector<char> ret(read_size);
    const auto s = read(fd, ret.data(), ret.size());
    if (s < 0) {
        throw std::system_error(errno, std::generic_category(), "read()");
    }
    ret.resize(s);
    return { ret.begin(), ret.end() };
}

size_t do_write(int fd, const std::string_view data)
{
    const auto rc = write(fd, data.data(), data.size());
    if (rc < 0) {
        throw std::system_error(errno, std::generic_category(), "write()");
    }
    return rc;
}
} // namespace

void Shuffler::copy(int src, int dst, std::unique_ptr<Buffer>&& buf, int esc)
{
    if (!buf) {
        buf = std::make_unique<RawBuffer>();
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
            throw std::system_error(errno, std::generic_category(), "select()");
        }

        // Check watchers.
        for (const auto& w : watchers_) {
            if (FD_ISSET(w.fd, &rfds)) {
                w.cb(w.fd);
            }
        }

        // Check for errors.
        for (int c = 0; c < streams_.size();) {
            auto& s = streams_[c];
            if (FD_ISSET(s.src(), &efds) || FD_ISSET(s.dst(), &efds)) {
                streams_.erase(streams_.begin() + c);
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
        for (int c = 0; c < streams_.size();) {
            auto& s = streams_[c];

            if (FD_ISSET(s.src(), &rfds)) {
                auto buf = do_read(s.src());
                if (buf.empty()) {
                    streams_.erase(streams_.begin() + c);
                    continue;
                }
                s.write(buf);
                if (s.check_esc()) {
                    return;
                }
            }
            c++;
        }
    }
}

Shuffler::Stream::Stream(int src, int dst, std::unique_ptr<Buffer>&& buf, int esc)
    : src_(src), dst_(dst), buf_(std::move(buf)), esc_(esc)
{
}

bool Shuffler::Stream::check_esc()
{
    if (esc_ < 0) {
        return false;
    }
    auto b = buf_->peek();
    return std::find(b.begin(), b.end(), esc_) != b.end();
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
