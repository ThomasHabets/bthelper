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
#ifndef __INCLUDE_BUFFER_H__
#define __INCLUDE_BUFFER_H__
#include <string_view>
#include <functional>
#include <vector>

class Buffer
{
public:
    virtual void write(std::string_view sv) = 0;

    void write(const std::vector<char>& in)
    {
        write(std::string_view(in.data(), in.size()));
    }

    // Invalidated on any non-const
    virtual std::string_view peek() const = 0;

    virtual void ack(size_t n) = 0;

    bool empty() const { return peek().empty(); }
};

class RawBuffer : public Buffer
{
public:
    void write(std::string_view sv) override;
    std::string_view peek() const override;
    void ack(size_t n) override;

private:
    // TODO: optimization opportunity: partial consumtion of data could
    // contain an offset into the buffer.
    std::vector<char> data_;
};

class TelnetEncoderBuffer : public Buffer
{
public:
    void write(std::string_view sv) override;
    std::string_view peek() const override;
    void ack(size_t n) override;

    void ping(uint32_t cookie);
    void pong(uint32_t cookie);
    void window_size(uint16_t rows, uint16_t cols);

private:
    std::vector<char> data_;
};

class TelnetDecoderBuffer : public Buffer
{
public:
    using ping_handler_t = std::function<void(uint32_t)>;
    using window_size_handler_t = std::function<void(uint16_t, uint16_t)>;
    TelnetDecoderBuffer(window_size_handler_t winch,
                        ping_handler_t ping,
                        ping_handler_t pong)
        : winch_(std::move(winch)), ping_(std::move(ping)), pong_(std::move(pong))
    {
    }

    void write(std::string_view sv) override;
    std::string_view peek() const override;
    void ack(size_t n) override;

private:
    ping_handler_t ping_;
    ping_handler_t pong_;
    window_size_handler_t winch_;
    std::vector<char> data_;
    std::vector<char> iac_buffer_;
};
#endif
