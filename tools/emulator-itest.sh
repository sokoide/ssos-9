#!/bin/zsh
# XEiJ integration tests. See docs/emulator-integration-test-plan.md.
#   a2  repeated XEiJ startup/return exercise
#   b1  Timer D tick rate measurement (200 Hz nominal)
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
ticks_of() {
    local v=$(print -r -- "$1" | grep -o 'ticks=[0-9A-F]\{8\}' | tail -1)
    [[ -n $v ]] || fail 'b1: no ticks= on screen' "$1"
    print -- $((16#${v#ticks=}))
}

run_a2() {
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
}

run_b1() {
    s=$(screen)
    require 'b1 initial prompt' "$s" 'A:\>'
    type_cmd 'keywitch -r'
    sleep 1
    type_cmd 'cls'
    type_cmd 'f:ssos9'
    sleep 3
    s=$(screen)
    require 'b1 startup' "$s" 'SSOS-9: minimal X68000 CLI'

    # Measure ticks across a wall-clock window. Both samples are taken the
    # same delay after their `debug` typing, so the typing/copy overhead
    # cancels and ticks delta ~= 200 Hz x elapsed seconds.
    type_cmd 'debug'
    sleep 1
    t1=$(date +%s)
    s=$(screen)
    require 'b1 debug1' "$s" 'ticks='
    n1=$(ticks_of "$s")

    sleep 6
    type_cmd 'debug'
    sleep 1
    t2=$(date +%s)
    s=$(screen)
    require 'b1 debug2' "$s" 'ticks='
    n2=$(ticks_of "$s")

    elapsed=$((t2 - t1))
    (( elapsed >= 1 )) || elapsed=1
    expected=$((elapsed * 200))
    tol=$((expected / 5))
    delta=$((n2 - n1))
    print "b1 ticks delta=$delta expected~$expected +/- $tol over ${elapsed}s"
    if (( delta >= expected - tol && delta <= expected + tol )); then
        print 'PASS b1: tick rate within 200 Hz +/- 20%'
    else
        fail 'b1: tick rate out of range' "delta=$delta expected=$expected +/-$tol"
    fi

    type_cmd 'exit'
    sleep 3
    s=$(screen)
    require 'b1 exit' "$s" 'SSOS-9 terminated.'
    print 'PASS b1'
}

case ${1:-} in
    a2) run_a2 ;;
    b1) run_b1 ;;
    *) fail 'usage: tools/emulator-itest.sh a2|b1' ;;
esac
