#include "zash_interpreter.hpp"

#include "zash_driver.hpp"

#include "builtins/builtins.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <util.h>
#else
#include <pty.h>
#endif
extern char **environ;
#endif

namespace os::zash {

namespace {

constexpr int kAliasMaxDepth = 64;

void appendShellWord(std::string &line, const char *w) {
    if (!w)
        return;
    bool need = false;
    for (const char *p = w; *p; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c <= ' ' || c == '\'' || c == '"' || c == '\\')
            need = true;
        else if (strchr(";|<>{}()&", *p))
            need = true;
    }
    if (!need) {
        line += w;
        return;
    }
    line.push_back('\'');
    for (const char *p = w; *p; ++p) {
        if (*p == '\'')
            line += "'\\''";
        else
            line += *p;
    }
    line.push_back('\'');
}

std::string shellSingleQuoteValue(const std::string &s) {
    std::string o = "'";
    for (unsigned char c : s) {
        if (c == '\'')
            o += "'\\''";
        else
            o += static_cast<char>(c);
    }
    o += '\'';
    return o;
}

#if defined(__unix__) || defined(__APPLE__)

static void writeFdAll(int fd, const char *s, size_t len) {
    while (len > 0) {
        const ssize_t n = write(fd, s, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        s += static_cast<size_t>(n);
        len -= static_cast<size_t>(n);
    }
}

/** After execvp returns; safe in forked child (async-signal-safe path). */
static void reportExecvpFailed(const char *argv0) {
    if (!argv0)
        argv0 = "?";
    const int e = errno;
    char buf[512];
    if (e == ENOENT)
        snprintf(buf, sizeof(buf), "zash: %s: command not found\r\n", argv0);
    else
        snprintf(buf, sizeof(buf), "zash: %s: %s\r\n", argv0, strerror(e));
    writeFdAll(2, buf, std::strlen(buf));
}

#endif

} // namespace

void Interpreter::queueForegroundPtyInput(std::string_view utf8) {
#if defined(__unix__) || defined(__APPLE__)
    if (!foregroundPtyActive_.load(std::memory_order_acquire) || utf8.empty())
        return;
    std::lock_guard<std::mutex> lock(ptyStdinMu_);
    ptyStdinQueue_.insert(ptyStdinQueue_.end(), utf8.begin(), utf8.end());
#endif
}

#if defined(__unix__) || defined(__APPLE__)
int Interpreter::relayPtySession(int master, pid_t pid) {
    if (master < 0) {
        int st = 0;
        waitpid(pid, &st, 0);
        if (WIFEXITED(st))
            return WEXITSTATUS(st);
        return 1;
    }

    foregroundPtyActive_.store(true, std::memory_order_release);
    if (onForegroundPty)
        onForegroundPty(true);

    char buf[4096];
    auto flushQueue = [&]() {
        for (;;) {
            std::unique_lock<std::mutex> lock(ptyStdinMu_);
            if (ptyStdinQueue_.empty())
                return;
            std::vector<uint8_t> chunk(std::move(ptyStdinQueue_));
            ptyStdinQueue_.clear();
            lock.unlock();
            size_t off = 0;
            while (off < chunk.size()) {
                ssize_t w = write(master, chunk.data() + off, chunk.size() - off);
                if (w < 0) {
                    if (errno == EINTR)
                        continue;
                    return;
                }
                off += static_cast<size_t>(w);
            }
        }
    };

    bool child_reaped = false;
    int st = 0;

    while (!child_reaped) {
        flushQueue();

        const pid_t wr = waitpid(pid, &st, WNOHANG);
        if (wr == pid) {
            child_reaped = true;
            break;
        }

        struct pollfd pfd = { master, POLLIN, 0 };
        const int pr = poll(&pfd, 1, 100);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (pfd.revents & (POLLERR | POLLNVAL))
            break;

        if (pfd.revents & POLLIN) {
            const ssize_t n = read(master, buf, sizeof(buf));
            if (n > 0) {
                if (writeOut)
                    writeOut(std::string_view(buf, static_cast<size_t>(n)));
            } else if (n == 0) {
                waitpid(pid, &st, 0);
                child_reaped = true;
                break;
            }
        }
    }

    flushQueue();

    if (!child_reaped)
        waitpid(pid, &st, 0);

    for (;;) {
        const ssize_t n = read(master, buf, sizeof(buf));
        if (n > 0) {
            if (writeOut)
                writeOut(std::string_view(buf, static_cast<size_t>(n)));
            continue;
        }
        if (n == 0)
            break;
        if (errno == EINTR)
            continue;
        break;
    }

    close(master);

    {
        std::lock_guard<std::mutex> lk(ptyStdinMu_);
        ptyStdinQueue_.clear();
    }
    foregroundPtyActive_.store(false, std::memory_order_release);
    if (onForegroundPty)
        onForegroundPty(false);

    if (WIFSIGNALED(st))
        return 1;
    if (WIFEXITED(st))
        return WEXITSTATUS(st);
    return 1;
}
#endif

Interpreter::Interpreter() {
    register_default_builtins(*this);
    if (getVar("PS1").empty())
        setVar("PS1", "> ");
}

void Interpreter::registerBuiltin(const char *name, BuiltinFn fn) {
    if (name && fn)
        builtins_[name] = fn;
}

bool Interpreter::isBuiltin(const char *name) const {
    return name && builtins_.find(name) != builtins_.end();
}

void Interpreter::builtinNames(std::vector<std::string> &out) const {
    out.clear();
    out.reserve(builtins_.size());
    for (const auto &p : builtins_)
        out.push_back(p.first);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

void Interpreter::functionNames(std::vector<std::string> &out) const {
    out.clear();
    for (const auto &p : functions_) {
        if (p.second)
            out.push_back(p.first);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

void Interpreter::aliasNames(std::vector<std::string> &out) const {
    out.clear();
    out.reserve(aliases_.size());
    for (const auto &p : aliases_)
        out.push_back(p.first);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

void Interpreter::setAlias(std::string name, std::string value) {
    aliases_[std::move(name)] = std::move(value);
}

bool Interpreter::removeAlias(const std::string &name) {
    return aliases_.erase(name) > 0;
}

void Interpreter::clearAliases() {
    aliases_.clear();
}

bool Interpreter::lookupAlias(const std::string &name, std::string &outValue) const {
    auto it = aliases_.find(name);
    if (it == aliases_.end())
        return false;
    outValue = it->second;
    return true;
}

void Interpreter::writeAliasesToOut() const {
    if (!writeOut)
        return;
    std::vector<std::pair<std::string, std::string>> v(aliases_.begin(), aliases_.end());
    std::sort(v.begin(), v.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &p : v) {
        std::string line = "alias " + p.first + "=" + shellSingleQuoteValue(p.second) + "\r\n";
        writeOut(line);
    }
}

bool Interpreter::writeAliasLine(const std::string &name) const {
    std::string val;
    if (!lookupAlias(name, val))
        return false;
    if (writeOut) {
        std::string line = "alias " + name + "=" + shellSingleQuoteValue(val) + "\r\n";
        writeOut(line);
    }
    return true;
}

std::string Interpreter::expandPS1(const std::string &raw) const {
    namespace fs = std::filesystem;
    std::string out;
    out.reserve(raw.size() * 2);
    size_t i = 0;
    while (i < raw.size()) {
        if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '[') {
            const size_t inner_start = i + 2;
            size_t j = inner_start;
            bool closed = false;
            while (j + 1 < raw.size()) {
                if (raw[j] == '\\' && raw[j + 1] == ']') {
                    out += expandPS1(raw.substr(inner_start, j - inner_start));
                    i = j + 2;
                    closed = true;
                    break;
                }
                ++j;
            }
            if (closed)
                continue;
            out += '\\';
            out += '[';
            i += 2;
            continue;
        }
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            const unsigned char od = static_cast<unsigned char>(raw[i + 1]);
            if (od >= '0' && od <= '7') {
                unsigned v = 0;
                size_t k = i + 1;
                int nd = 0;
                while (k < raw.size() && nd < 3) {
                    const unsigned char d = static_cast<unsigned char>(raw[k]);
                    if (d < '0' || d > '7')
                        break;
                    v = (v << 3u) | static_cast<unsigned>(d - '0');
                    ++k;
                    ++nd;
                }
                out += static_cast<char>(v & 0xFFu);
                i = k;
                continue;
            }
            const char c = raw[i + 1];
            i += 2;
            switch (c) {
            case '\\':
                out += '\\';
                break;
            case 'n':
                out += '\n';
                break;
            case 'e':
                out += '\033';
                break;
            case 'a':
                out += '\a';
                break;
            case 't': {
                const std::time_t t = std::time(nullptr);
                struct tm lt {};
#if defined(_WIN32)
                localtime_s(&lt, &t);
#else
                localtime_r(&t, &lt);
#endif
                char tb[32];
                if (std::strftime(tb, sizeof(tb), "%H:%M:%S", &lt) > 0)
                    out += tb;
                break;
            }
            case 'u': {
                const char *u = getenv("USER");
                out += u ? u : "";
                break;
            }
            case 'h': {
#if defined(__unix__) || defined(__APPLE__)
                struct utsname un {};
                if (uname(&un) == 0) {
                    std::string hn(un.nodename);
                    const auto dot = hn.find('.');
                    if (dot != std::string::npos)
                        hn.resize(dot);
                    out += hn;
                }
#elif defined(_WIN32)
                const char *cn = getenv("COMPUTERNAME");
                out += cn ? cn : "";
#else
                out += "host";
#endif
                break;
            }
            case 'w': {
                std::error_code ec;
                out += fs::current_path(ec).string();
                if (ec)
                    out += "?";
                break;
            }
            case 'W': {
                std::error_code ec;
                const fs::path p = fs::current_path(ec);
                if (!ec) {
                    std::string leaf = p.filename().string();
                    out += leaf.empty() ? "/" : leaf;
                } else
                    out += "?";
                break;
            }
            case '$':
#if defined(__unix__) || defined(__APPLE__)
                out += (geteuid() == 0) ? "#" : "$";
#else
                out += "$";
#endif
                break;
            default:
                out += '\\';
                out += c;
                break;
            }
            continue;
        }
        out += raw[i];
        ++i;
    }
    for (size_t q = 0; (q = out.find("$?", q)) != std::string::npos;) {
        const std::string st = std::to_string(lastStatus_);
        out.replace(q, 2, st);
        q += st.length();
    }
    return out;
}

std::string Interpreter::expandWord(const char *w) const {
    if (!w)
        return {};
    if (w[0] == '$' && w[1])
        return getVar(std::string(w + 1));
    return std::string(w);
}

void Interpreter::unsetVar(const std::string &k) {
    env_.erase(k);
    for (auto &m : localStack_)
        m.erase(k);
#if defined(__unix__) || defined(__APPLE__)
    unsetenv(k.c_str());
#endif
}

void Interpreter::setVar(const std::string &k, const std::string &v) {
    if (!localStack_.empty())
        localStack_.back()[k] = v;
    else
        env_[k] = v;
}

std::string Interpreter::getVar(const std::string &k) const {
    for (auto it = localStack_.rbegin(); it != localStack_.rend(); ++it) {
        auto j = it->find(k);
        if (j != it->end())
            return j->second;
    }
    auto j = env_.find(k);
    if (j != env_.end())
        return j->second;
    const char *e = getenv(k.c_str());
    return e ? std::string(e) : std::string();
}

static void split_assign(const char *kv, std::string &k, std::string &v) {
    const char *eq = strchr(kv, '=');
    if (!eq) {
        k = kv;
        v.clear();
        return;
    }
    k.assign(kv, eq);
    v = eq + 1;
}

void Interpreter::registerFunction(ZashStmt *st) {
    if (!st || st->kind != ZST_FUNC || !st->u.fn.name)
        return;
    functions_[st->u.fn.name] = st->u.fn.body;
    st->u.fn.body = nullptr;
}

int Interpreter::runStmt(ZashStmt *st) {
    if (!st)
        return 0;
    switch (st->kind) {
    case ZST_LIST:
        return runList(st->u.line);
    case ZST_IF: {
        runList(st->u.iff.cond);
        if (lastStatus_ == 0)
            return runList(st->u.iff.then_part);
        if (st->u.iff.else_part)
            return runList(st->u.iff.else_part);
        return 0;
    }
    case ZST_FOR: {
        for (int i = 0; i < st->u.fr.nitems; i++) {
            setVar(st->u.fr.var, expandWord(st->u.fr.items[i]));
            runList(st->u.fr.body);
        }
        return lastStatus_;
    }
    case ZST_WHILE: {
        for (;;) {
            runList(st->u.wh.cond);
            if (lastStatus_ != 0)
                break;
            runList(st->u.wh.body);
        }
        return 0;
    }
    case ZST_FUNC:
        registerFunction(st);
        return 0;
    case ZST_CASE:
        return runCase(st->u.cs.word, st->u.cs.arms);
    default:
        return 0;
    }
}

int Interpreter::runList(ZashList *list) {
    if (!list || list->npipes == 0)
        return 0;
    int st = runPipeline(list->pipes[0]);
    lastStatus_ = st;
    for (int i = 1; i < list->npipes; i++) {
        const int sep = (list->ops && i - 1 < list->npipes - 1) ? list->ops[i - 1] : ZASH_SEP_SEMI;
        if (sep == ZASH_SEP_AND) {
            if (lastStatus_ != 0)
                break;
        } else if (sep == ZASH_SEP_OR) {
            if (lastStatus_ == 0)
                break;
        }
        st = runPipeline(list->pipes[i]);
        lastStatus_ = st;
    }
    return lastStatus_;
}

int Interpreter::runCase(const char *word, ZashCaseArm *arms) {
    std::string w = expandWord(word);
    for (ZashCaseArm *a = arms; a; a = a->next) {
        for (int i = 0; i < a->npatterns; i++) {
            if (fnmatch(a->patterns[i], w.c_str(), 0) == 0)
                return runList(a->body);
        }
    }
    return 0;
}

static int apply_redirs(ZashCmd *cmd, bool forChild) {
    (void)forChild;
    for (int i = 0; i < cmd->nredirs; i++) {
        ZashRedir *r = cmd->redirs[i];
        int fl = (r->op == 2) ? O_RDONLY : (r->op == 1 ? O_WRONLY | O_CREAT | O_APPEND : O_WRONLY | O_CREAT | O_TRUNC);
        int fd = open(r->path, fl, 0666);
        if (fd < 0)
            return -1;
        if (dup2(fd, r->op == 2 ? 0 : 1) < 0) {
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

void Interpreter::syncEnvToProcess() const {
#if defined(__unix__) || defined(__APPLE__)
    for (const auto &p : env_)
        setenv(p.first.c_str(), p.second.c_str(), 1);
#endif
}

int Interpreter::runCmd(ZashCmd *cmd, bool inChild) {
    if (!cmd)
        return 0;
    for (int i = 0; i < cmd->nassigns; i++) {
        std::string k, v;
        split_assign(cmd->assigns[i], k, v);
        setVar(k, expandWord(v.c_str()));
    }
    if (cmd->argc == 0) {
        lastStatus_ = 0;
        return 0;
    }

    std::string cmd0 = expandWord(cmd->argv[0]);
    const bool skipAliasRecursive =
        !aliasExpandStack_.empty() && cmd0 == aliasExpandStack_.back();
    auto ait = skipAliasRecursive ? aliases_.end() : aliases_.find(cmd0);
    if (ait != aliases_.end()) {
        std::string line = ait->second;
        for (int i = 1; i < cmd->argc; i++) {
            line.push_back(' ');
            appendShellWord(line, cmd->argv[i]);
        }
        for (int i = 0; i < cmd->nredirs; i++) {
            ZashRedir *r = cmd->redirs[i];
            line.push_back(' ');
            if (r->op == 0)
                line += ">";
            else if (r->op == 1)
                line += ">>";
            else
                line += "<";
            line.push_back(' ');
            appendShellWord(line, r->path);
        }
        ZashProgram *sub = nullptr;
        const int pr = zash_parse_line(line.c_str(), &sub);
        if (pr != 0 || !sub) {
            if (writeErr) {
                const char *detail = zash_parse_last_error();
                std::string msg = "zash: alias expansion: ";
                if (pr < 0)
                    msg += "out of memory";
                else if (detail && detail[0])
                    msg += detail;
                else
                    msg += "parse error";
                msg += "\r\n";
                writeErr(msg);
            }
            if (sub)
                zash_free_program(sub);
            lastStatus_ = 1;
            return 1;
        }
        ++aliasDepth_;
        if (aliasDepth_ > kAliasMaxDepth) {
            if (writeErr)
                writeErr("zash: alias expansion: recursion limit exceeded\r\n");
            --aliasDepth_;
            zash_free_program(sub);
            lastStatus_ = 1;
            return 1;
        }
        aliasExpandStack_.push_back(cmd0);
        int st = 0;
        for (ZashStmt *s = sub->first; s; s = s->next)
            st = runStmt(s);
        aliasExpandStack_.pop_back();
        zash_free_program(sub);
        --aliasDepth_;
        lastStatus_ = st;
        return st;
    }

    auto itb = builtins_.find(cmd0);
    if (itb != builtins_.end()) {
        std::vector<char *> av;
        av.push_back(strdup(cmd0.c_str()));
        for (int i = 1; i < cmd->argc; i++)
            av.push_back(strdup(expandWord(cmd->argv[i]).c_str()));
        av.push_back(nullptr);
        if (inChild) {
            if (apply_redirs(cmd, true) < 0)
                _exit(1);
        }
        int r = itb->second(static_cast<int>(av.size()) - 1, av.data(), *this);
        for (size_t i = 0; i < av.size(); i++)
            free(av[i]);
        if (inChild)
            _exit(r);
        lastStatus_ = r;
        return r;
    }

    auto itf = functions_.find(cmd0);
    if (itf != functions_.end() && itf->second) {
        if (inChild) {
            /* function body in forked pipeline segment */
            localStack_.emplace_back();
            for (int i = 1; i < cmd->argc; i++)
                localStack_.back()[std::to_string(i)] = expandWord(cmd->argv[i]);
            ZashProgram *prog = itf->second;
            for (ZashStmt *s = prog->first; s; s = s->next)
                runStmt(s);
            localStack_.pop_back();
            _exit(lastStatus_);
        }
        localStack_.emplace_back();
        for (int i = 1; i < cmd->argc; i++)
            localStack_.back()[std::to_string(i)] = expandWord(cmd->argv[i]);
        ZashProgram *prog = itf->second;
        for (ZashStmt *s = prog->first; s; s = s->next)
            runStmt(s);
        localStack_.pop_back();
        return lastStatus_;
    }

#if defined(__unix__) || defined(__APPLE__)
    if (inChild) {
        if (apply_redirs(cmd, true) < 0)
            _exit(127);
        syncEnvToProcess();
        std::vector<char *> av;
        for (int i = 0; i < cmd->argc; i++)
            av.push_back(strdup(expandWord(cmd->argv[i]).c_str()));
        av.push_back(nullptr);
        execvp(av[0], av.data());
        reportExecvpFailed(av[0]);
        _exit(127);
    }

    int master = -1;
    pid_t pid = forkpty(&master, nullptr, nullptr, nullptr);
    if (pid < 0) {
        const int e = errno;
        lastStatus_ = 1;
        if (writeErr) {
            std::string msg = "zash: forkpty: ";
            msg += strerror(e);
            msg += "\r\n";
            writeErr(msg);
        }
        return 1;
    }
    if (pid == 0) {
        if (apply_redirs(cmd, true) < 0)
            _exit(127);
        syncEnvToProcess();
        std::vector<char *> av;
        for (int i = 0; i < cmd->argc; i++)
            av.push_back(strdup(expandWord(cmd->argv[i]).c_str()));
        av.push_back(nullptr);
        execvp(av[0], av.data());
        reportExecvpFailed(av[0]);
        _exit(127);
    }
    lastStatus_ = relayPtySession(master, pid);
    return lastStatus_;
#else
    lastStatus_ = 1;
    return 1;
#endif
}

int Interpreter::runPipeline(ZashPipeline *pl) {
    if (!pl || pl->ncmds == 0)
        return 0;
    if (pl->ncmds == 1)
        return runCmd(pl->cmds[0], false);

#if defined(__unix__) || defined(__APPLE__)
    int prev = -1;
    std::vector<pid_t> kids;
    const int n = pl->ncmds;

    for (int i = 0; i < n - 1; i++) {
        int p[2];
        if (pipe(p) < 0) {
            lastStatus_ = 1;
            if (prev >= 0)
                close(prev);
            for (pid_t k : kids) {
                int s = 0;
                waitpid(k, &s, 0);
            }
            return 1;
        }
        pid_t pid = fork();
        if (pid < 0) {
            close(p[0]);
            close(p[1]);
            if (prev >= 0)
                close(prev);
            lastStatus_ = 1;
            for (pid_t k : kids) {
                int s = 0;
                waitpid(k, &s, 0);
            }
            return 1;
        }
        if (pid == 0) {
            if (prev >= 0) {
                dup2(prev, 0);
                close(prev);
            }
            dup2(p[1], 1);
            close(p[1]);
            close(p[0]);
            runCmd(pl->cmds[i], true);
            _exit(lastStatus_);
        }
        kids.push_back(pid);
        if (prev >= 0)
            close(prev);
        close(p[1]);
        prev = p[0];
    }

    int master = -1;
    pid_t lastPid = forkpty(&master, nullptr, nullptr, nullptr);
    if (lastPid < 0) {
        const int e = errno;
        if (prev >= 0)
            close(prev);
        lastStatus_ = 1;
        if (writeErr) {
            std::string msg = "zash: forkpty: ";
            msg += strerror(e);
            msg += "\r\n";
            writeErr(msg);
        }
        for (pid_t k : kids) {
            int s = 0;
            waitpid(k, &s, 0);
        }
        return 1;
    }
    if (lastPid == 0) {
        if (prev >= 0) {
            dup2(prev, 0);
            close(prev);
        }
        runCmd(pl->cmds[n - 1], true);
        _exit(lastStatus_);
    }
    if (prev >= 0)
        close(prev);
    lastStatus_ = relayPtySession(master, lastPid);
    for (pid_t k : kids) {
        int s = 0;
        waitpid(k, &s, 0);
        (void)s;
    }
    return lastStatus_;
#else
    return 1;
#endif
}

int Interpreter::runProgram(ZashProgram *prog) {
    if (!prog)
        return 0;
    int st = 0;
    for (ZashStmt *s = prog->first; s; s = s->next)
        st = runStmt(s);
    zash_free_program(prog);
    return st;
}

} // namespace os::zash
