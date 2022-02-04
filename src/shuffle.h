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
#include "buffer.h"
#include <functional>
#include <memory>
#include <vector>
class Shuffler
{
public:
    using watch_handler_t = std::function<void(int)>;

    void copy(int src, int dst, std::unique_ptr<Buffer>&& buf = nullptr, int escape = -1);
    void watch(int fd, watch_handler_t);
    void run();

private:
    class Stream
    {
    public:
        Stream(int src, int dst, std::unique_ptr<Buffer>&& buf, int esc);
        int src() const { return src_; }
        int dst() const { return dst_; };
        bool empty() const { return buf_->peek().empty(); }
        std::string_view peek() const { return buf_->peek(); }
        void write(std::string_view v) { buf_->write(v); }
        void ack(size_t n) { buf_->ack(n); }
        bool check_esc();

    private:
        // fds unowned.
        int src_ = -1;
        int dst_ = -1;
        std::unique_ptr<Buffer> buf_;
        int esc_;
    };

    struct Watcher {
        int fd;
        watch_handler_t cb;
    };

    std::vector<Stream> streams_;
    std::vector<Watcher> watchers_;
};
