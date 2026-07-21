#!/usr/bin/env bash
set -ueo pipefail
./bootstrap.sh
./configure CXXFLAGS='-Wall -pedantic -Wextra'
make clean
exec make CXXFLAGS='-Wall -pedantic -Wextra -Werror -Warith-conversion -Wconversion -Wsign-conversion -Wfloat-conversion -Wformat-signedness -Wno-c++20-extensions'
