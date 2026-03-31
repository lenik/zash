project('zash', 'c', 'cpp', version: '1.0.0', default_options: ['warning_level=0', 'cpp_std=c++17'])

c = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')
bison = find_program('bison', required: true)
flex = find_program('flex', required: true)

zash_bison = custom_target(
    'zash_bison',
    input: 'src/zash.y',
    output: ['zash.tab.c', 'zash.tab.h'],
    command: [bison, '-d', '-o', '@OUTPUT0@', '@INPUT@'],
)

zash_flex = custom_target(
    'zash_flex',
    input: 'src/zash.lex',
    output: 'lex.yy.c',
    command: [flex, '-o', '@OUTPUT0@', '@INPUT@'],
    depends: zash_bison,
)

zash_sources = files(
    'src/builtins/builtins.cpp',
    'src/zash_alloc.c',
    'src/zash_driver.c',
    'src/zash_interpreter.cpp',
)

zash_lib = library(
    'zash',
    zash_sources,
    zash_bison,
    zash_flex,
    include_directories: include_directories('src'),
    install: true,
)

zash_dep = declare_dependency(
    link_with: zash_lib,
    include_directories: include_directories('src'),
)

executable(
    'zash',
    'src/main.cpp',
    dependencies: zash_dep,
    install: true,
)

install_man('man/zash.1')

install_data(
    'zash.bash',
    rename: 'zash',
    install_dir: get_option('datadir') / 'bash-completion' / 'completions',
)

# ---- pkg-config ----
pkgconfig = import('pkgconfig')
pkgconfig.generate(
    zash_lib,
    name: 'omnishell',
    description: 'omnishell',
    version: meson.project_version(),
    subdirs: 'omnishell',
    requires: ['bas-c', 'bas-cpp', 'bas-ui', 'boost', 'openssl', 'libcurl'],
)

# ---- Headers for libbas-c package (includedir/bas-c) ----
find_other_cmd = run_command(
    'find',
    '.',
    '-name', '*.h',
    '-o',
    '-name', '*.hpp',
    '-o',
    '-type', 'd',
    '-o',
    '-print',
    check: true,
)
other_files = find_other_cmd.stdout().strip().split('\n')

other_list = []
foreach f : other_files
    other_list += f.substring(4)
endforeach

install_subdir(
    'src',
    install_dir: get_option('includedir') / 'zash',
    exclude_files: other_list,
    strip_directory: true,
)
