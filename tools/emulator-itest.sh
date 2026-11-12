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

wait_screen() { # wait_screen <pattern> <timeout-seconds>; screen text in $s
    local pat=$1 tmo=$2 waited=0
    while (( waited < tmo )); do
        sleep 3
        waited=$((waited + 3))
        s=$(screen)
        print -r -- "$s" | grep -Fq -- "$pat" && return 0
    done
    return 1
}

xeij_stop() {
    pkill -f XEiJ.jar
    local i=0
    while pgrep -f XEiJ.jar >/dev/null && (( i < 15 )); do
        sleep 1
        i=$((i + 1))
    done
    pgrep -f XEiJ.jar >/dev/null || return 0
    pkill -9 -f XEiJ.jar
    sleep 2
    pgrep -f XEiJ.jar >/dev/null && { print -u2 'FAIL: cannot stop XEiJ'; return 1; }
    return 0
}
xeij_start() {
    "$HOME/.local/bin/xeij.sh"
    local i=0
    while ! [[ -p "$PIPE" ]] && (( i < 30 )); do
        sleep 1
        i=$((i + 1))
    done
    [[ -p "$PIPE" ]] || { print -u2 'FAIL: control pipe did not come back'; return 1; }
}

a3_cleanup() {
    local result=$1
    [[ -n $A3_BACKUP ]] || return $result
    print '== A3 restore XEiJ configuration =='
    if ! xeij_stop; then
        print -u2 -- "FAIL: a3: XEiJ still running; backup retained at $A3_BACKUP"
        return 1
    fi
    if cp -p "$A3_BACKUP" "$HOME/XEiJ.ini"; then
        rm -f "$A3_BACKUP"
        A3_BACKUP=''
    else
        print -u2 -- "FAIL: a3: restore failed; backup retained at $A3_BACKUP"
        return 1
    fi
    xeij_start || result=1
    return $result
}

run_a3() {
    local backup
    [[ -f "$HOME/tmp/ssos9.xdf" ]] || fail 'a3: missing ~/tmp/ssos9.xdf'
    [[ -f "$HOME/XEiJ.ini" ]] || fail 'a3: missing ~/XEiJ.ini'
    backup=$(mktemp "${TMPDIR%/}/ssos9-xeij-ini.XXXXXXXX") || fail 'a3: cannot create backup'
    if ! cp -p "$HOME/XEiJ.ini" "$backup"; then
        rm -f "$backup"
        fail 'a3: cannot back up ~/XEiJ.ini'
    fi
    A3_BACKUP=$backup
    print '== A3 insert ssos9.xdf in FD0 (ini fd0 + restart) =='
    xeij_stop || fail 'a3: cannot stop XEiJ'
    sed -i '' '/^fd0=/d' "$HOME/XEiJ.ini" || fail 'a3: cannot remove fd0'
    printf 'fd0=%s\n' "$HOME/tmp/ssos9.xdf" >> "$HOME/XEiJ.ini" || fail 'a3: cannot set fd0'

    xeij_start || fail 'a3: cannot start XEiJ'
    print '== A3 wait for XDF IPL boot =='
    wait_screen 'SSOS-9: minimal X68000 CLI' 60 || fail 'a3: XDF boot banner missing' "$s"
    sleep 2
    s=$(screen)
    require 'a3 prompt' "$s" '>'

    print '== A3 exit must be rejected (PID 1) =='
    type_cmd 'exit'
    sleep 2
    s=$(screen)
    if ! print -r -- "$s" | grep -Fq 'Cannot exit the initial SSOS-9 process (PID 1).'; then
        fail 'a3: exit was not rejected' "$s"
    fi
    print -r -- "$s" | grep -F 'Cannot exit the initial SSOS-9 process (PID 1).'
    s2=$(screen)
    require 'a3 prompt alive' "$s2" '>'

    trap - EXIT HUP INT TERM
    a3_cleanup 0 || fail 'a3: configuration restore failed'
    wait_screen 'A:\>' 90 || fail 'a3: Human68k did not come back' "$s"
    print 'PASS a3: configuration restored and Human68k returned'
    print 'PASS a3'
}

case ${1:-} in
    a2) run_a2 ;;
    a3)
        A3_BACKUP=''
        trap 'a3_cleanup $?; exit $?' EXIT
        trap 'exit 129' HUP
        trap 'exit 130' INT
        trap 'exit 143' TERM
        run_a3 ;;
    b1) run_b1 ;;
    *) fail 'usage: tools/emulator-itest.sh a2|a3|b1' ;;
esac
