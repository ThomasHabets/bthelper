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
#include <termios.h>
#include <unistd.h>
#include <iostream>

extern int wrapmain(int argc, char** argv);

namespace {

bool tio_saved = false;
struct termios orig_tio = {};

void save_terminal()
{
    if (isatty(STDIN_FILENO)) {
        if (tcgetattr(STDIN_FILENO, &orig_tio)) {
            perror("tcgetattr(stdin)");
        } else {
            tio_saved = true;
        }
    }
}

void restore_terminal()
{
    if (tio_saved && isatty(STDIN_FILENO)) {
        if (tcsetattr(STDIN_FILENO, TCSADRAIN, &orig_tio)) {
            perror("tcsetattr(reset)");
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    save_terminal();
    try {
        try {
            const auto ret = wrapmain(argc, argv);
            restore_terminal();
            return ret;
        } catch (...) {
            restore_terminal();
            throw;
        }
    } catch (const std::exception& e) {
        std::cerr << argv[0] << ": Exception: " << e.what() << "\n";
        return 1;
    } catch (const char* e) {
        std::cerr << argv[0] << ": Exception (string): " << e << "\n";
        return 1;
    } catch (...) {
        std::cerr << argv[0] << ": Exception (other)\n";
        throw;
    }
}
