#ifndef ZASH_INTERPRETER_HPP
#define ZASH_INTERPRETER_HPP

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include "zash_ast.h"
}

#if defined(__unix__) || defined(__APPLE__)
#include <sys/types.h>
#endif

namespace os::zash {

using BuiltinFn = int (*)(int argc, char **argv, class Interpreter &interp);

class Interpreter {
public:
    void *userData{nullptr};

    std::function<void(std::string_view)> writeOut;
    std::function<void(std::string_view)> writeErr;
    std::function<void()> requestExit;
    /** True while a foreground external command owns the PTY (for UI to forward keys). */
    std::function<void(bool)> onForegroundPty;
    /** Queue bytes to the child process stdin (thread-safe; no-op if no foreground PTY). */
    void queueForegroundPtyInput(std::string_view utf8);
    bool foregroundPtyActive() const { return foregroundPtyActive_.load(std::memory_order_acquire); }

    Interpreter();

    /** Run parsed program; frees prog. */
    int runProgram(ZashProgram *prog);

    void registerBuiltin(const char *name, BuiltinFn fn);

    std::string expandWord(const char *w) const;
    int lastStatus() const { return lastStatus_; }
    void setVar(const std::string &k, const std::string &v);
    void unsetVar(const std::string &k);
    std::string getVar(const std::string &k) const;
    bool isBuiltin(const char *name) const;

    /** Sorted unique names for tab completion. */
    void builtinNames(std::vector<std::string> &out) const;
    void functionNames(std::vector<std::string> &out) const;
    void aliasNames(std::vector<std::string> &out) const;

    void setAlias(std::string name, std::string value);
    bool removeAlias(const std::string &name);
    void clearAliases();
    /** For `alias name` without '='. */
    bool lookupAlias(const std::string &name, std::string &outValue) const;
    /** `alias` with no arguments: print all definitions. */
    void writeAliasesToOut() const;
    /** Print one `alias name='...'` line; false if name is unknown. */
    bool writeAliasLine(const std::string &name) const;

    /**
     * Expand PS1 for wxTerminal (bash-like): \\, \n, \e / octal → ESC, \a, \t (time),
     * \[ ... \] (strip delimiters, keep inner escapes), \u \h \w \W \$.
     */
    std::string expandPS1(const std::string &raw) const;

    /** Push env_ to the process environment (POSIX). */
    void syncEnvToProcess() const;

private:
    int runStmt(ZashStmt *st);
    int runList(ZashList *list);
    int runPipeline(ZashPipeline *pl);
    int runCmd(ZashCmd *cmd, bool inChild = false);
    int runCase(const char *word, ZashCaseArm *arms);
    void registerFunction(ZashStmt *st);

#if defined(__unix__) || defined(__APPLE__)
    int relayPtySession(int master, pid_t pid);
#endif

    std::mutex ptyStdinMu_;
    std::vector<uint8_t> ptyStdinQueue_;
    std::atomic<bool> foregroundPtyActive_{false};

    std::map<std::string, std::string> env_;
    std::vector<std::map<std::string, std::string>> localStack_;
    std::map<std::string, ZashProgram *> functions_;
    std::map<std::string, BuiltinFn> builtins_;
    std::map<std::string, std::string> aliases_;
    /** Names of aliases currently being expanded (innermost = back); suppresses re-expansion when
     *  the command word matches the alias name (bash-compatible ls='ls --color=auto'). */
    std::vector<std::string> aliasExpandStack_;
    int aliasDepth_{0};
    int lastStatus_{0};
};

void register_default_builtins(Interpreter &interp);

} // namespace os::zash

#endif
