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

#include <cassert>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
namespace telnet {
constexpr char iac = static_cast<char>(255);
constexpr char iac_window_size = 1;
constexpr char iac_ping = 2;
constexpr char iac_pong = 3;
} // namespace telnet

} // namespace

void TelnetDecoderBuffer::write(std::string_view sv)
{
    static const std::map<char, int> iac_sizes = {
        { telnet::iac, 2 },
        { telnet::iac_window_size, 6 },
        { telnet::iac_ping, 6 },
        { telnet::iac_pong, 6 },
    };
    std::vector<char> to_add;
    std::vector<char> tbuf = iac_buffer_;
    for (const auto& ch : sv) {
        // Normal data.
        if (tbuf.empty() && (ch != telnet::iac)) {
            to_add.push_back(ch);
            continue;
        }

        // Add to iac buffer.
        tbuf.push_back(ch);
        if (tbuf.size() == 1) {
            continue;
        }

        // Check if iac buffer is full.
        const auto type = tbuf[1];
        const auto siz = iac_sizes.find(type);
        if (siz == iac_sizes.end()) {
            throw std::runtime_error("invalid iac");
        }
        if (tbuf.size() == siz->second) {
            switch (type) {
            case telnet::iac:
                to_add.push_back(telnet::iac);
                break;
            case telnet::iac_ping: {
                uint32_t cookie = 0;
                cookie |= tbuf[2] << 24;
                cookie |= tbuf[3] << 16;
                cookie |= tbuf[4] << 8;
                cookie |= tbuf[5];
                ping_(cookie);
                break;
            }
            case telnet::iac_pong: {
                uint32_t cookie = 0;
                cookie |= tbuf[2] << 24;
                cookie |= tbuf[3] << 16;
                cookie |= tbuf[4] << 8;
                cookie |= tbuf[5];
                pong_(cookie);
                break;
            }
            case telnet::iac_window_size: {
                uint16_t rows = 0;
                uint16_t cols = 0;
                rows |= tbuf[2] << 8;
                rows |= tbuf[3];
                cols |= tbuf[4] << 8;
                cols |= tbuf[5];
                winch_(rows, cols);
                break;
            }
            }
            tbuf.clear();
        }
    }
    iac_buffer_ = tbuf;
    data_.insert(data_.end(), to_add.begin(), to_add.end());
}

void TelnetEncoderBuffer::write(std::string_view sv)
{
    for (const auto& ch : sv) {
        data_.push_back(ch);
        if (ch == telnet::iac) {
            data_.push_back(ch);
        }
    }
}

void TelnetEncoderBuffer::window_size(uint16_t rows, uint16_t cols)
{
    data_.push_back(telnet::iac);
    data_.push_back(telnet::iac_window_size);
    data_.push_back(0xff & (rows >> 8));
    data_.push_back(0xff & rows);
    data_.push_back(0xff & (cols >> 8));
    data_.push_back(0xff & cols);
}

void TelnetEncoderBuffer::ping(uint32_t cookie)
{
    data_.push_back(telnet::iac);
    data_.push_back(telnet::iac_ping);
    data_.push_back(0xff & (cookie >> 24));
    data_.push_back(0xff & (cookie >> 16));
    data_.push_back(0xff & (cookie >> 8));
    data_.push_back(0xff & cookie);
}

void TelnetEncoderBuffer::pong(uint32_t cookie)
{
    data_.push_back(telnet::iac);
    data_.push_back(telnet::iac_pong);
    data_.push_back(0xff & (cookie >> 24));
    data_.push_back(0xff & (cookie >> 16));
    data_.push_back(0xff & (cookie >> 8));
    data_.push_back(0xff & cookie);
}

void RawBuffer::write(std::string_view sv)
{
    data_.insert(data_.end(), sv.begin(), sv.end());
}

std::string_view RawBuffer::peek() const
{
    if (data_.empty()) {
        return {};
    }
    return { &data_[0], data_.size() };
}

std::string_view TelnetEncoderBuffer::peek() const
{
    if (data_.empty()) {
        return {};
    }
    return { &data_[0], data_.size() };
}

std::string_view TelnetDecoderBuffer::peek() const
{
    if (data_.empty()) {
        return {};
    }
    return { &data_[0], data_.size() };
}

void TelnetEncoderBuffer::ack(size_t n)
{
    if (n > data_.size()) {
        throw std::invalid_argument("RawBuffer::ack(): n > data_.size(): "
                                    + std::to_string(n) + " > "
                                    + std::to_string(data_.size()));
    }
    data_.erase(data_.begin(), data_.begin() + n);
}

void TelnetDecoderBuffer::ack(size_t n)
{
    if (n > data_.size()) {
        throw std::invalid_argument("RawBuffer::ack(): n > data_.size(): "
                                    + std::to_string(n) + " > "
                                    + std::to_string(data_.size()));
    }
    data_.erase(data_.begin(), data_.begin() + n);
}

void RawBuffer::ack(size_t n)
{
    if (n > data_.size()) {
        throw std::invalid_argument("RawBuffer::ack(): n > data_.size(): "
                                    + std::to_string(n) + " > "
                                    + std::to_string(data_.size()));
    }
    data_.erase(data_.begin(), data_.begin() + n);
}

#if 0
int main()
{
    {
        RawBuffer buf;
        buf.write("h");
        buf.write("ello");
        assert(buf.peek() == "hello");
        assert(buf.peek() == "hello");
        buf.ack(3);
        assert(buf.peek() == "lo");
    }

    {
        TelnetEncoderBuffer buf;
        buf.write("he");
        buf.write("llo");
        assert(buf.peek() == "hello");
        buf.ack(5);
        buf.write("y\xFFo");
        assert(buf.peek() == "y\xFF\xFFo");
        buf.ping(0x41424344);
        assert(buf.peek()
               == "y\xFF\xFFo\xFF\x02"
                  "ABCD");
        buf.ack(buf.peek().size());

        // Pong.
        buf.pong(0x44434241);
        assert(buf.peek()
               == "\xFF\x03"
                  "DCBA");

        buf.ack(buf.peek().size());

        // Window size change.
        buf.write("yo");
        buf.window_size(0x4142, 0x4344);
        buf.write("plait");
        assert(buf.peek() == "yo\xFF\x01\x41\x42\x43\x44plait");
    }

    {
        std::vector<uint32_t> pings;
        std::vector<uint32_t> pongs;
        std::vector<std::pair<uint16_t, uint16_t>> winchs;
        TelnetDecoderBuffer buf([&winchs](uint16_t rows, uint16_t cols) { winchs.push_back({rows, cols});},
				[&pings](uint32_t cookie) { pings.push_back(cookie); },
				[&pongs](uint32_t cookie) { pongs.push_back(cookie); });
        buf.write("he");
        buf.write("llo");
        assert(buf.peek() == "hello");
        buf.ack(5);

        // Escape.
        buf.write("y\xFF");
        buf.write("\xFFo");
        assert(buf.peek() == "y\xFFo");

        // Ping.
        buf.write("\xFF\x02"
                  "ABCD");
        assert(buf.peek() == "y\xFFo");
	assert(pings == std::vector<uint32_t>{0x41424344});

        // Pong.
        buf.write("\xFF\x03");
        buf.write("DCBA");
        assert(buf.peek() == "y\xFFo");
	assert(pongs == std::vector<uint32_t>{0x44434241});

        // Window size change.
        buf.write("\xFF\x01\x41\x42\x43\x44plait");
        assert(buf.peek() == "y\xFFoplait");
	std::vector<std::pair<uint16_t, uint16_t>> want = {{0x4142, 0x4344}};
	assert(winchs == want);
    }
}
#endif
