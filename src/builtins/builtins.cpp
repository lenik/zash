#include "builtins.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace os::zash {

static void out(Interpreter &interp, std::string_view s) {
    if (interp.writeOut)
        interp.writeOut(s);
}

static int bi_echo(int argc, char **argv, Interpreter &interp) {
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            out(interp, " ");
        out(interp, argv[i] ? argv[i] : "");
    }
    out(interp, "\r\n");
    return 0;
}

static int bi_echoln(int argc, char **argv, Interpreter &interp) {
    for (int i = 1; i < argc; i++) {
        out(interp, argv[i] ? argv[i] : "");
        out(interp, "\r\n");
    }
    return 0;
}

static int bi_printf(int argc, char **argv, Interpreter &interp) {
    if (argc < 2)
        return 1;
    const char *fmt = argv[1];
    std::string acc;
    int ai = 2;
    for (const char *p = fmt; *p; p++) {
        if (*p == '%' && p[1] == 's' && ai < argc) {
            acc += (argv[ai] ? argv[ai] : "");
            ai++;
            p++;
        } else if (*p == '%' && p[1] == 'd' && ai < argc) {
            acc += std::to_string(atoi(argv[ai]));
            ai++;
            p++;
        } else if (*p == '\\' && p[1] == 'n') {
            acc += '\n';
            p++;
        } else if (*p == '%' && p[1] == '%')
            acc += '%', p++;
        else
            acc += *p;
    }
    out(interp, acc);
    if (acc.empty() || acc.back() != '\n')
        out(interp, "\r\n");
    return 0;
}

static int bi_cd(int argc, char **argv, Interpreter &interp) {
    (void)interp;
#if defined(__unix__) || defined(__APPLE__)
    const char *d = (argc > 1 && argv[1]) ? argv[1] : getenv("HOME");
    if (!d)
        return 1;
    return chdir(d) == 0 ? 0 : 1;
#else
    (void)argc;
    (void)argv;
    return 1;
#endif
}

static int bi_pwd(int, char **, Interpreter &interp) {
#if defined(__unix__) || defined(__APPLE__)
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) {
        out(interp, buf);
        out(interp, "\r\n");
        return 0;
    }
#endif
    return 1;
}

static int bi_export(int argc, char **argv, Interpreter &interp) {
    for (int i = 1; i < argc; i++) {
        const char *eq = strchr(argv[i], '=');
        if (eq) {
            std::string k(argv[i], static_cast<size_t>(eq - argv[i]));
            std::string v(eq + 1);
            interp.setVar(k, v);
#if defined(__unix__) || defined(__APPLE__)
            setenv(k.c_str(), v.c_str(), 1);
#endif
        }
    }
    return 0;
}

static int bi_unset(int argc, char **argv, Interpreter &interp) {
    for (int i = 1; i < argc; i++)
        interp.unsetVar(argv[i]);
    (void)argc;
    return 0;
}

static int bi_local(int argc, char **argv, Interpreter &interp) {
    for (int i = 1; i < argc; i++) {
        const char *eq = strchr(argv[i], '=');
        if (eq) {
            std::string k(argv[i], static_cast<size_t>(eq - argv[i]));
            std::string v(eq + 1);
            interp.setVar(k, v);
        }
    }
    return 0;
}

static int bi_declare(int argc, char **argv, Interpreter &interp) {
    int i = 1;
    bool exportf = false;
    if (i < argc && argv[i][0] == '-' && argv[i][1] == 'x') {
        exportf = true;
        i++;
    }
    for (; i < argc; i++) {
        const char *eq = strchr(argv[i], '=');
        if (eq) {
            std::string k(argv[i], static_cast<size_t>(eq - argv[i]));
            std::string v(eq + 1);
            interp.setVar(k, v);
            if (exportf) {
#if defined(__unix__) || defined(__APPLE__)
                setenv(k.c_str(), v.c_str(), 1);
#endif
            }
        }
    }
    return 0;
}

static int bi_colon(int, char **, Interpreter &) {
    return 0;
}
static int bi_true(int, char **, Interpreter &) {
    return 0;
}
static int bi_false(int, char **, Interpreter &) {
    return 1;
}

static int bi_help(int, char **, Interpreter &interp) {
    out(interp,
        "zash: bash-like shell (if/then/fi, && ||, pipelines |, for/while/case, functions, alias, PS1).\r\n"
        "Default aliases: ls, ll, la, l, d, grep (--color=auto where applicable), cd.., .., ...\r\n"
        "Builtins: echo echoln printf cd pwd export unset local declare alias unalias : true false help "
        "exit clear\r\n"
        "Prompt: export PS1='...' (\\e \\a \\t \\n \\[ \\] \\u \\h \\w \\W \\$ $?); wxTerminal renders "
        "ANSI SGR and skips OSC (Kitty shell integration).\r\n");
    return 0;
}

static int bi_exit(int, char **, Interpreter &interp) {
    if (interp.requestExit)
        interp.requestExit();
    return 0;
}

static int bi_alias(int argc, char **argv, Interpreter &interp) {
    if (argc == 1) {
        interp.writeAliasesToOut();
        return 0;
    }
    int st = 0;
    for (int i = 1; i < argc; i++) {
        const char *eq = strchr(argv[i], '=');
        if (eq) {
            std::string name(argv[i], static_cast<size_t>(eq - argv[i]));
            interp.setAlias(name, std::string(eq + 1));
        } else {
            if (!interp.writeAliasLine(argv[i])) {
                if (interp.writeErr) {
                    interp.writeErr("zash: alias: ");
                    interp.writeErr(argv[i]);
                    interp.writeErr(": not found\r\n");
                }
                st = 1;
            }
        }
    }
    return st;
}

static int bi_unalias(int argc, char **argv, Interpreter &interp) {
    if (argc < 2) {
        if (interp.writeErr)
            interp.writeErr("zash: unalias: missing operand\r\n");
        return 1;
    }
    if (strcmp(argv[1], "-a") == 0 && argc == 2) {
        interp.clearAliases();
        return 0;
    }
    int st = 0;
    for (int i = 1; i < argc; i++) {
        if (!interp.removeAlias(argv[i])) {
            if (interp.writeErr) {
                interp.writeErr("zash: unalias: ");
                interp.writeErr(argv[i]);
                interp.writeErr(": not found\r\n");
            }
            st = 1;
        }
    }
    return st;
}

void register_default_aliases(Interpreter &interp) {
    interp.setAlias("ls", "ls --color=auto");
    interp.setAlias("l", "ls -al");
    interp.setAlias("d", "ls -FANo");
    interp.setAlias("cd..", "cd ..");
    interp.setAlias("..", "cd ..");
    interp.setAlias("...", "cd ../..");
    interp.setAlias("ll", "ls -l");
    interp.setAlias("la", "ls -A");
    interp.setAlias("grep", "grep --color=auto");
}

void register_default_builtins(Interpreter &interp) {
    interp.registerBuiltin("echo", bi_echo);
    interp.registerBuiltin("echoln", bi_echoln);
    interp.registerBuiltin("printf", bi_printf);
    interp.registerBuiltin("cd", bi_cd);
    interp.registerBuiltin("pwd", bi_pwd);
    interp.registerBuiltin("export", bi_export);
    interp.registerBuiltin("unset", bi_unset);
    interp.registerBuiltin("alias", bi_alias);
    interp.registerBuiltin("unalias", bi_unalias);
    interp.registerBuiltin("local", bi_local);
    interp.registerBuiltin("declare", bi_declare);
    interp.registerBuiltin(":", bi_colon);
    interp.registerBuiltin("true", bi_true);
    interp.registerBuiltin("false", bi_false);
    interp.registerBuiltin("help", bi_help);
    interp.registerBuiltin("exit", bi_exit);
    interp.registerBuiltin("quit", bi_exit);
    register_default_aliases(interp);
}

} // namespace os::zash
