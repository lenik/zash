#include "zash/zash_driver.hpp"
#include "zash/zash_interpreter.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace {

using os::zash::Interpreter;

static int run_line(Interpreter &interp, const char *line) {
    ZashProgram *prog = nullptr;
    const int pr = zash_parse_line(line, &prog);
    if (pr != 0) {
        if (interp.writeErr) {
            const char *e = zash_parse_last_error();
            interp.writeErr("zash: ");
            interp.writeErr(e && e[0] ? e : "parse error");
            interp.writeErr("\r\n");
        }
        return 1;
    }
    return interp.runProgram(prog);
}

static void usage(std::FILE *out) {
    std::fprintf(out,
                 "Usage: zash [options]\n"
                 "       zash -c command\n"
                 "\n"
                 "Interactive bash-like shell (pipelines, functions, alias, PS1).\n"
                 "\n"
                 "  -c CMD    run CMD and exit\n"
                 "  -h, --help   show this help and exit\n");
}

} // namespace

int main(int argc, char **argv) {
    Interpreter interp;
    interp.writeOut = [](std::string_view s) {
        std::fwrite(s.data(), 1, s.size(), stdout);
        std::fflush(stdout);
    };
    interp.writeErr = [](std::string_view s) {
        std::fwrite(s.data(), 1, s.size(), stderr);
        std::fflush(stderr);
    };
    bool exit_requested = false;
    interp.requestExit = [&]() { exit_requested = true; };

    const char *cmd = nullptr;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if (std::strcmp(argv[i], "-c") == 0) {
            if (i + 1 >= argc) {
                usage(stderr);
                return 2;
            }
            cmd = argv[++i];
            continue;
        }
        interp.writeErr("zash: unknown option: ");
        interp.writeErr(argv[i]);
        interp.writeErr("\r\n");
        usage(stderr);
        return 2;
    }

    if (cmd) {
        interp.syncEnvToProcess();
        return run_line(interp, cmd);
    }

#if defined(__unix__) || defined(__APPLE__)
    const bool interactive = isatty(STDIN_FILENO) != 0;
#else
    const bool interactive = false;
#endif

    interp.syncEnvToProcess();
    std::string line;
    while (!exit_requested && std::getline(std::cin, line)) {
        if (interactive) {
            const std::string p = interp.expandPS1(interp.getVar("PS1"));
            std::fwrite(p.data(), 1, p.size(), stdout);
            std::fflush(stdout);
        }
        (void)run_line(interp, line.c_str());
    }
    return interp.lastStatus();
}
