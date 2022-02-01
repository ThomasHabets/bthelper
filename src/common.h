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

#include <sys/socket.h>

#include <cinttypes>
#include <string>
#include <vector>

namespace bthelper {

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

bool parse_addr(const std::string& in, bdaddr_t* out);

bool set_nonblock(int fd);

std::vector<char> do_read(int fd);
std::vector<char> do_write(int fd, const std::vector<char>& data);
bool shuffle(int ar, int aw, int b);
std::pair<int, bool> xatoi(const char* v);


} // namespace bthelper
