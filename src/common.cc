#include "buffer.h"
#include "common.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <tuple>


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

std::string stringify_addr(const bdaddr_t* in)
{
    std::stringstream ss;
    for (int c = 5; c >= 0; c--) {
        ss << std::hex << std::setfill('0') << std::setw(2)
           << (static_cast<unsigned int>(in->b[c]) & 0xff);
        if (c) {
            ss << std::setw(1) << ':';
        }
    }
    return ss.str();
}

std::pair<int, bool> xatoi(const char* v)
{
    char* end = nullptr;
    int ret = strtol(v, &end, 0);
    return { ret, !*end };
}

} // namespace bthelper
