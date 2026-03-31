# bash completion for zash(1)

_zash() {
    local cur="${COMP_WORDS[COMP_CWORD]}"
    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W '-h --help -c' -- "$cur") )
    fi
} &&
    complete -F _zash zash
