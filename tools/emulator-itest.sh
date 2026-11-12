#!/bin/zsh
# XEiJ integration test A2. See docs/emulator-remote-operation.md.
set -u

PIPE="${TMPDIR%/}/XEiJControl"
fail() { print -u2 -- "FAIL: $1"; [[ $# -lt 2 ]] || print -u2 -r -- "$2"; exit 1; }
frontmost() {
    osascript -e 'tell application "System Events" to set frontmost of process "java" to true' >/dev/null || fail 'cannot activate XEiJ'
}
key() { printf 'typekey %s\n' "$1" > "$PIPE" || fail 'cannot write control pipe'; sleep 0.1; }
type_cmd() {
    local c
    frontmost
    for c in ${(s::)1}; do
        case "$c" in
            ' ') key space ;;
            '-') key minus ;;
            ':') key colon ;;
            *) key "$c" ;;
        esac
    done
    key return
}
screen() {
    frontmost
    printf '%s' 'SSOS9_SCREEN_COPY_PENDING' | pbcopy
    osascript -e 'tell application "System Events" to keystroke "c" using {option down, shift down}' >/dev/null || fail 'screen copy failed'
    sleep 0.8
    pbpaste
}
require() {
    print -r -- "$2" | grep -Fq -- "$3" || fail "$1: missing $3" "$2"
    print -r -- "$2" | grep -Eiq '\$004[fF]|error|エラー' && fail "$1: error on screen" "$2"
    return 0
}

[[ ${1:-} == a2 ]] || fail 'usage: tools/emulator-itest.sh a2'
[[ -f "$HOME/tmp/ssos9.x" ]] || fail 'missing ~/tmp/ssos9.x'
pgrep -f XEiJ.jar >/dev/null || fail 'XEiJ is not running'
[[ -p "$PIPE" ]] || fail "missing FIFO: $PIPE"
s=$(screen)
require 'initial prompt' "$s" 'A:\>'
type_cmd 'keywitch -r'
sleep 1

for ((i = 1; i <= 5; i++)); do
    print "== A2 iteration $i =="
    type_cmd 'cls'
    type_cmd 'f:ssos9'
    sleep 3
    s=$(screen)
    require "iteration $i startup" "$s" 'SSOS-9: minimal X68000 CLI'
    type_cmd 'debug'
    sleep 1
    s=$(screen)
    require "iteration $i debug" "$s" 'MFP IERB/IMRB/TCDCR/TDDR='
    print -r -- "$s" | grep -F 'MFP IERB/IMRB/TCDCR/TDDR='
    type_cmd 'exit'
    sleep 3
    s=$(screen)
    require "iteration $i exit" "$s" 'SSOS-9 terminated.'
    require "iteration $i return" "$s" 'A:\>'
    type_cmd 'dir a:'
    sleep 1
    s=$(screen)
    require "iteration $i DOS listing" "$s" '<dir>'
    require "iteration $i DOS prompt" "$s" 'A:\>'
    print "PASS A2 iteration $i"
done
print 'PASS A2: 5 iterations'
