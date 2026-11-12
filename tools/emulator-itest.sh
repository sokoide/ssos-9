#!/bin/zsh
# XEiJ integration tests. See docs/emulator-integration-test-plan.md.
#   a2  repeated XEiJ startup/return exercise
#   a3  XDF IPL boot and PID 1 exit rejection
#   b1  Timer D tick rate measurement (200 Hz nominal)
#   d1  Human68k-format HDD create/read/replace in A:\TEST
#   e3  builtin hello real-PIC launch
set -u

PIPE="${TMPDIR%/}/XEiJControl"

fail() { print -u2 -- "FAIL: $1"; [[ $# -lt 2 ]] || print -u2 -r -- "$2"; exit 1; }
frontmost() {
    osascript -e 'tell application "System Events" to set frontmost of process "java" to true' >/dev/null || fail 'cannot activate XEiJ'
}
key() { printf 'typekey %s\n' "$1" > "$PIPE" || fail 'cannot write control pipe'; sleep 0.1; }
# XEiJ's instant typekey drops the yen keycode; hold it just under the
# keyboard auto-repeat delay so exactly one backslash is delivered.
yen_key() {
    printf 'presskey yen\n' > "$PIPE" || fail 'cannot write control pipe'
    sleep 0.15
    printf 'releasekey yen\n' > "$PIPE" || fail 'cannot write control pipe'
    sleep 0.1
}
type_char() {
    case "$1" in
        ' ') key space ;;
        '-') key minus ;;
        ':') key colon ;;
        '.') key period ;;
        ',') key comma ;;
        '/') key slash ;;
        '\') yen_key ;;
        $'\n') key return ;;
        *) key "$1" ;;
    esac
}
type_cmd() {
    local c
    frontmost
    for c in ${(s::)1}; do type_char "$c"; done
    key return
}
screen() {
    local out tries=0
    frontmost
    printf '%s' 'SSOS9_SCREEN_COPY_PENDING' | pbcopy
    osascript -e 'tell application "System Events" to keystroke "c" using {option down, shift down}' >/dev/null || fail 'screen copy failed'
    sleep 1.2
    out=$(pbpaste)
    while [[ "$out" == 'SSOS9_SCREEN_COPY_PENDING' && $tries -lt 3 ]]; do
        osascript -e 'tell application "System Events" to keystroke "c" using {option down, shift down}' >/dev/null
        sleep $((1 + tries))
        out=$(pbpaste)
        tries=$((tries + 1))
    done
    [[ "$out" == 'SSOS9_SCREEN_COPY_PENDING' ]] && fail 'screen copy did not complete'
    print -r -- "$out"
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

boot_ssos() { # from the Human68k prompt; banner screen in $s
    type_cmd 'keywitch -r'
    sleep 1
    type_cmd 'cls'
    type_cmd 'f:ssos9'
    wait_screen 'SSOS-9: minimal X68000 CLI' 30 || return 1
    wait_screen '> ' 15 || return 1
    sleep 1
    s=$(screen)
    return 0
}

d1_names() { # normalize Human68k dir rows ("name +ext", no dot) to
    # lowercase name.ext tokens; stored case differs by creator
    print -r -- "$1" | tr '\r' '\n' | grep -oE '^[A-Za-z0-9_]{1,18} +[A-Za-z0-9]{1,3} ' |
        awk '{print tolower($1) "." tolower($2)}' | sort -u
}
d1_screen_line() { # d1_screen_line <screen text> <exact line>; the screen
    # copy pads every line to the text screen width, so allow trailing blanks
    print -r -- "$1" | tr '\r' '\n' | grep -Eq -- "^${2}[[:space:]]*$"
}
d1_wait_line() { # d1_wait_line <exact line> <timeout-seconds>; screen in $s
    local tmo=$2 waited=0
    while (( waited < tmo )); do
        sleep 3
        waited=$((waited + 3))
        s=$(screen)
        d1_screen_line "$s" "$1" && return 0
    done
    return 1
}
d1_wait_row() { # d1_wait_row <name> <ext> <timeout>; dir row in $s, any case
    local tmo=$3 waited=0
    while (( waited < tmo )); do
        sleep 3
        waited=$((waited + 3))
        s=$(screen)
        print -r -- "$s" | tr '\r' '\n' | grep -Eiq "^${1}[ ]+${2}" && return 0
    done
    return 1
}

run_d1() {
    local base fix upper before after target id s2
    [[ -f "$HOME/tmp/ssos9.x" ]] || fail 'd1: missing ~/tmp/ssos9.x (run make install)'
    base="d1t$(printf '%05x' $(( $(date +%s) % 1048576 )))"
    fix="D1F$(printf '%05X' $(( ($(date +%s) + 7) % 1048576 )))"
    upper=${(U)base}
    print "d1 test file: $upper.TXT  fixture: $fix.TXT"
    print "d1 ssos9.x sha256: $(shasum -a 256 "$HOME/tmp/ssos9.x" | awk '{print $1}')"

    s=$(screen)
    require 'd1 initial prompt' "$s" 'A:\>'
    key return # flush any pending unentered line from earlier activity
    type_cmd 'keywitch -r'
    sleep 1

    # Fixture: a unique file copied into A:\TEST by Human68k itself. The name
    # is uppercase because SSOS-9's path lookup compares the uppercased 8.3
    # name against the stored entry bytes; Human68k preserves the case it was
    # given. Reading the fixture back through each smount candidate identifies
    # A:'s SCSI target read-only before any SSOS-9 write. Writes stay inside
    # A:\TEST.
    printf 'D1FIXTURE' > "$HOME/tmp/$fix.TXT" || fail 'd1: cannot write fixture to ~/tmp (F:)'
    type_cmd 'dir a:\test'
    wait_screen 'A:\>' 8 || fail 'd1: dir a:\test did not complete' "$s"
    if print -r -- "$s" | grep -Eq 'ディレクトリ.*見つかり|irectory not found'; then
        print '== D1 create A:\TEST =='
        type_cmd 'mkdir a:\test'
        sleep 1
    fi
    type_cmd "copy f:$fix.TXT a:\\test\\$fix.TXT"
    sleep 1
    type_cmd 'cls'
    type_cmd 'dir a:\test'
    # Match the dir row "name +ext" in the lowercased display so command
    # echoes cannot match.
    d1_wait_row "${fix:l}" 'txt' 12 || fail "d1: fixture $fix.TXT missing in A:\\TEST" "$s"
    before=$(d1_names "$s")
    type_cmd "type a:\\test\\$fix.TXT"
    wait_screen 'D1FIXTURE' 8 || fail 'd1: Human68k fixture content mismatch' "$s"

    print '== D1 boot SSOS-9 =='
    boot_ssos || fail 'd1: ssos9 boot failed'
    require 'd1 banner' "$s" 'SSOS-9: minimal X68000 CLI'

    print '== D1 identify A: SCSI target (read-only) =='
    target=''
    for ((id = 0; id <= 7; id++)); do
        type_cmd "smount $id"
        wait_screen 'FAT16 mounted.' 9 || continue
        type_cmd "sread test/$fix.TXT"
        if wait_screen 'D1FIXTURE' 9; then
            target=$id
            break
        fi
        print "d1: SCSI ID $id has a Human68k volume that is not A:; restarting SSOS-9"
        type_cmd 'exit'
        sleep 3
        boot_ssos || fail 'd1: ssos9 reboot failed'
    done
    [[ -n $target ]] || fail 'd1: cannot identify A: SCSI target; no writes performed' "$s"
    print "d1: A: identified as SCSI ID $target"

    print '== D1 create/read/replace/read =='
    # Results are asserted through the following sread line so stale
    # "File written." text from an earlier command cannot pass the check.
    type_cmd "swrite test/$base.txt hello"
    type_cmd "sread test/$base.txt"
    d1_wait_line 'hello' 15 || fail 'd1: create + first read did not return hello' "$s"
    type_cmd "sreplace test/$base.txt world"
    type_cmd "sread test/$base.txt"
    d1_wait_line 'world' 15 || fail 'd1: replace + second read did not return world' "$s"
    print 'PASS d1: create/read/replace/read inside SSOS-9'

    print '== D1 verify from Human68k after return =='
    type_cmd 'exit'
    wait_screen 'SSOS-9 terminated.' 15 || fail 'd1: SSOS-9 did not terminate' "$s"
    require 'd1 return' "$s" 'A:\>'
    # Human68k caches directory sectors in its track buffer, so entries that
    # SSOS-9 wrote behind the cache stay invisible to dir/type until the cache
    # is dropped. Reboot the guest via the pipe's reset command and verify on
    # the fresh Human68k.
    printf 'reset\n' > "$PIPE" || fail 'd1: cannot write reset to control pipe'
    wait_screen 'HIOCS' 30 || fail 'd1: guest did not start rebooting' "$s"
    wait_screen 'A:\>' 150 || fail 'd1: Human68k did not come back' "$s"
    sleep 3
    type_cmd 'keywitch -r'
    sleep 1
    type_cmd 'cls'
    type_cmd 'dir a:\test'
    # SSOS-9 stores the 8.3 name uppercase; Human68k keeps its own case.
    d1_wait_row "$base" 'txt' 12 || fail 'd1: new file missing in A:\TEST after reset' "$s"
    print -r -- "$s" | tr '\r' '\n' | grep -Eiq "^${fix}[ ]+txt" ||
        fail 'd1: fixture missing in A:\TEST after reset' "$s"
    after=$(d1_names "$s" | grep -Eiv "^$base\.")
    [[ "$after" == "$before" ]] || fail 'd1: A:\TEST listing changed beyond the new file' \
        "before: $before"$'\n'"after: $after"
    type_cmd "type a:\\test\\$upper.TXT"
    d1_wait_line 'world' 12 || fail 'd1: Human68k does not see world' "$s"
    print -r -- "$s" | grep -Fq 'hello' && fail 'd1: old content still visible after replace' "$s"
    print 'PASS d1: Human68k sees world and the A:\TEST listing is unchanged'
    print 'PASS d1'
    return 0
}

run_e3() {
    s=$(screen)
    require 'e3 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'e3: ssos9 boot failed'
    require 'e3 banner' "$s" 'SSOS-9: minimal X68000 CLI'

    print '== E3 run builtin hello =='
    type_cmd 'run hello'
    wait_screen 'exited with status 0' 20 || fail 'e3: exit status not 0' "$s"
    require 'e3 hello output' "$s" 'Hello'
    print -r -- "$s" | awk '/PID [0-9]+ exited with status 0/ { exited=1; next } exited && /^> / { found=1 } END { exit !found }' || fail 'e3: shell prompt missing after exit' "$s"
    print -r -- "$s" | grep -E 'Hello|PID [0-9]+ exited with status 0|^> '
    print 'PASS e3: hello printed and exited 0'

    print '== E3 trace shows scheduler events =='
    type_cmd 'trace'
    sleep 2
    s=$(screen)
    print -r -- "$s" | grep -Eq 'switch task=[0-9]+ other=[0-9]+ value=000000[0-9A-F]{2}' || fail 'e3: no Timer D switch in trace' "$s"
    print -r -- "$s" | grep -E 'switch task=[0-9]+ other=[0-9]+ value=000000[0-9A-F]{2}'
    print 'PASS e3: trace has a Timer D switch'

    type_cmd 'exit'
    sleep 3
    s=$(screen)
    require 'e3 exit' "$s" 'SSOS-9 terminated.'
    require 'e3 return' "$s" 'A:\>'
    print 'PASS e3'
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
    d1) run_d1 ;;
    e3) run_e3 ;;
    *) fail 'usage: tools/emulator-itest.sh a2|a3|b1|d1|e3' ;;
esac
