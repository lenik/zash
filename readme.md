# zash

`zash` is a bash-like interactive shell: pipelines, `if`/`then`/`fi`, functions, `alias`, and a configurable `PS1` prompt. It is built as a shared library (`libzash`) plus a small `zash` driver that reads lines, parses them with flex/bison, and runs them through the interpreter.

## Build

Dependencies: a C and C++17 toolchain, Meson, Ninja, Bison, Flex, and `pkg-config`.

```sh
meson setup build
ninja -C build
```

Run the shell:

```sh
./build/zash
./build/zash -c 'echo hello'
```

Install system-wide (optional):

```sh
meson install -C build
```

## Debian package

From the repository root, with `debhelper`, `meson`, and build dependencies installed:

```sh
dpkg-buildpackage -us -uc -b
```

This produces `.deb` packages under the parent directory using the files in `debian/`.

## Layout

| Path | Purpose |
|------|---------|
| `src/` | Parser, interpreter, builtins |
| `man/zash.1` | Manual page |
| `data/bash-completion/zash` | Bash completion for `zash` |

## License

See `debian/copyright` for packaging metadata; add a top-level `LICENSE` if you need a single project license.
