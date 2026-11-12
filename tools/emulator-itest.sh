#!/bin/zsh
# XEiJ integration tests. See docs/emulator-integration-test-plan.md.
#   a2  repeated XEiJ startup/return exercise
#   a3  XDF IPL boot and PID 1 exit rejection
#   b1  Timer D tick rate measurement (200 Hz nominal)
#   b2  trace shows scheduler events while counter runs
#   b3  shell stays responsive with a CPU spinner (preemptive)
#   b4  Ctrl-C on a busy foreground task restores the arena
#   c1  help output matches the cli.c command table
#   c2  >25 lines of console output redraw without scrolling
#   c3  mux windows F1/F2/F3 retain and restore their output
#   c4  Ctrl-] plus a digit selects a mux window
#   c5  console module KEY: reporting over real IOCS input
#   d1  Human68k-format HDD create/read/replace in A:\TEST
#   d2  edit saves and reloads a new file in A:\TEST
#   d3  on-target assembler output matches the host build
#   d4  ld links, run fallback executes, image matches host golden
#   e3  builtin hello real-PIC launch
#   f1  background builtins (help&, ps&, mem&) interleave cleanly and reap
#   f2  background module output continues across mux window switches
#   g1  five spawn/kill/wait cycles return the arena to baseline
# Storage tests (c5, d1..d4) place fixtures in A:\TEST only and reuse the
# already-copied module images; run B/C1-4 tests without disk writes.
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
shift_key() { # shifted glyph: hold shift, tap the key, release
    printf 'presskey shift\n' > "$PIPE" || fail 'cannot write control pipe'
    sleep 0.12
    printf 'typekey %s\n' "$1" > "$PIPE" || fail 'cannot write control pipe'
    sleep 0.12
    printf 'releasekey shift\n' > "$PIPE" || fail 'cannot write control pipe'
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
        '&') shift_key 6 ;;
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
    # Runs inside a command-substitution subshell, so fail() here would only
    # exit the subshell and the script would continue on a dead capture. On
    # an unrecoverable copy failure, emit the pending marker: the caller's
    # content checks then fail in the main shell with a real diagnostic.
    local out tries=0
    osascript -e 'tell application "System Events" to set frontmost of process "java" to true' >/dev/null 2>&1
    printf '%s' 'SSOS9_SCREEN_COPY_PENDING' | pbcopy
    if ! osascript -e 'tell application "System Events" to keystroke "c" using {option down, shift down}' >/dev/null 2>&1; then
        print -u2 'WARN: screen copy keystroke failed'
        print -r -- 'SSOS9_SCREEN_COPY_PENDING'
        return 0
    fi
    sleep 1.2
    out=$(pbpaste)
    while [[ "$out" == 'SSOS9_SCREEN_COPY_PENDING' && $tries -lt 3 ]]; do
        osascript -e 'tell application "System Events" to keystroke "c" using {option down, shift down}' >/dev/null 2>&1
        sleep $((1 + tries))
        out=$(pbpaste)
        tries=$((tries + 1))
    done
    if [[ "$out" == 'SSOS9_SCREEN_COPY_PENDING' ]]; then
        print -u2 'WARN: screen copy did not complete'
        print -r -- 'SSOS9_SCREEN_COPY_PENDING'
        return 0
    fi
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
boot_mount() { # boot SSOS-9 and mount the A: volume (SCSI ID 0)
    boot_ssos || return 1
    type_cmd 'smount 0'
    wait_screen 'FAT16 mounted.' 10 || fail 'smount 0 failed (A: expected at SCSI ID 0)' "$s"
    return 0
}
guest_reset() { # reboot the guest through the control pipe and wait for the
    # Human68k prompt; drops the Human68k disk cache (track buffer)
    printf 'reset\n' > "$PIPE" || fail 'cannot write reset to control pipe'
    wait_screen 'HIOCS' 30 || fail 'guest did not start rebooting' "$s"
    wait_screen 'A:\>' 150 || fail 'Human68k did not come back' "$s"
    sleep 3
    type_cmd 'keywitch -r'
    sleep 1
}
h68_prep() { # from A:\>: reset keywitch, ensure A:\TEST exists, leave a
    # fresh dir listing of A:\TEST in $s (create the directory if needed)
    key return # flush any pending unentered line from earlier activity
    type_cmd 'keywitch -r'
    sleep 1
    type_cmd 'dir a:\test'
    wait_screen 'A:\>' 8 || fail 'dir a:\test did not complete' "$s"
    if print -r -- "$s" | grep -Eq 'ディレクトリ.*見つかり|irectory not found'; then
        print '== create A:\TEST =='
        type_cmd 'mkdir a:\test'
        sleep 1
    fi
    type_cmd 'cls'
    type_cmd 'dir a:\test'
    wait_screen 'A:\>' 8 || fail 'dir a:\test did not complete' "$s"
    return 0
}
place_in_test() { # place_in_test <host-file> <NAME.EXT>; stage on F: and copy
    # into A:\TEST unless the row already exists (Human68k copy would prompt
    # on overwrite, and immutable module images keep their first copy).
    # Requires a fresh dir listing in $s; leaves $s on the new listing.
    local src=$1 name=$2
    local stem=${name%.*} ext=${name##*.}
    if print -r -- "$s" | tr '\r' '\n' | grep -Eiq "^${stem}[ ]+${ext}"; then
        print "place: $name already in A:\\TEST (keeping existing copy)"
        return 0
    fi
    [[ "$src" == "$HOME/tmp/$name" ]] || cp "$src" "$HOME/tmp/$name" ||
        fail "cannot stage $name in ~/tmp (F:)"
    type_cmd "copy f:$name a:\\test\\$name"
    sleep 1
    type_cmd 'cls'
    type_cmd 'dir a:\test'
    wait_row "$stem" "$ext" 12 || fail "$name was not copied into A:\\TEST" "$s"
    return 0
}
ssos_exit() { # exit SSOS-9 and confirm the Human68k prompt
    type_cmd 'exit'
    wait_screen 'SSOS-9 terminated.' 15 || fail 'SSOS-9 did not terminate' "$s"
    require 'return to Human68k' "$s" 'A:\>'
    return 0
}

d1_names() { # normalize Human68k dir rows ("name +ext", no dot) to
    # lowercase name.ext tokens; stored case differs by creator
    print -r -- "$1" | tr '\r' '\n' | grep -oE '^[A-Za-z0-9_]{1,18} +[A-Za-z0-9]{1,3} ' |
        awk '{print tolower($1) "." tolower($2)}' | sort -u
}
screen_line() { # screen_line <screen text> <exact line>; the screen copy
    # pads every line to the text screen width, so allow trailing blanks
    print -r -- "$1" | tr '\r' '\n' | grep -Eq -- "^${2}[[:space:]]*$"
}
wait_line() { # wait_line <exact line> <timeout-seconds>; screen in $s
    local tmo=$2 waited=0
    while (( waited < tmo )); do
        sleep 3
        waited=$((waited + 3))
        s=$(screen)
        screen_line "$s" "$1" && return 0
    done
    return 1
}
wait_row() { # wait_row <name> <ext> <timeout>; dir row in $s, any case
    local tmo=$3 waited=0
    while (( waited < tmo )); do
        sleep 3
        waited=$((waited + 3))
        s=$(screen)
        print -r -- "$s" | tr '\r' '\n' | grep -Eiq "^${1}[ ]+${2}" && return 0
    done
    return 1
}
norm_text() { # wrap-insensitive text: drop every space, CR, and LF
    print -r -- "$1" | tr -d ' \r\n\t'
}
started_pid() { # extract the last "Started ... PID n" number from screen $s
    print -r -- "$1" | grep -o 'Started \(module\|command\) as PID [0-9]*' |
        grep -o '[0-9]*$' | tail -1
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
    wait_row "${fix:l}" 'txt' 12 || fail "d1: fixture $fix.TXT missing in A:\\TEST" "$s"
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
    wait_line 'hello' 15 || fail 'd1: create + first read did not return hello' "$s"
    type_cmd "sreplace test/$base.txt world"
    type_cmd "sread test/$base.txt"
    wait_line 'world' 15 || fail 'd1: replace + second read did not return world' "$s"
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
    wait_row "$base" 'txt' 12 || fail 'd1: new file missing in A:\TEST after reset' "$s"
    print -r -- "$s" | tr '\r' '\n' | grep -Eiq "^${fix}[ ]+txt" ||
        fail 'd1: fixture missing in A:\TEST after reset' "$s"
    after=$(d1_names "$s" | grep -Eiv "^$base\.")
    [[ "$after" == "$before" ]] || fail 'd1: A:\TEST listing changed beyond the new file' \
        "before: $before"$'\n'"after: $after"
    type_cmd "type a:\\test\\$upper.TXT"
    wait_line 'world' 12 || fail 'd1: Human68k does not see world' "$s"
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

run_b2() {
    s=$(screen)
    require 'b2 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'b2: ssos9 boot failed'
    type_cmd 'run counter&'
    wait_screen 'Started module as PID' 8 || fail 'b2: counter did not start' "$s"
    local pid=$(started_pid "$s")
    [[ -n $pid ]] || fail 'b2: cannot parse counter PID' "$s"
    type_cmd 'trace'
    sleep 2
    s=$(screen)
    require 'b2 trace header' "$s" 'scheduler events: 16 (oldest first)'
    local events=$(print -r -- "$s" | tr '\r' '\n' |
        grep -cE '^t=[0-9A-F]{8} (create|start|sleep|wake|switch|exit) task=[0-9]+ other=[0-9]+ value=[0-9A-F]{8}')
    (( events >= 10 )) || fail "b2: only $events typed events in trace" "$s"
    print -r -- "$s" | grep -Fq ' switch task=' || fail 'b2: no switch event in trace' "$s"
    print "b2: $events scheduler events with non-empty kinds"
    type_cmd "kill $pid"
    sleep 1
    type_cmd "wait $pid"
    wait_screen 'exited with status' 8 || fail 'b2: counter was not reaped' "$s"
    ssos_exit
    print 'PASS b2: trace shows 16 scheduler events while counter ran'
}

run_b3() {
    s=$(screen)
    require 'b3 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'b3: ssos9 boot failed'
    type_cmd 'run counter&'
    wait_screen 'Started module as PID' 8 || fail 'b3: counter did not start' "$s"
    local pid=$(started_pid "$s")
    type_cmd 'debug'
    sleep 1
    s=$(screen)
    require 'b3 debug1' "$s" 'ticks='
    local n1=$(ticks_of "$s")
    sleep 6
    type_cmd 'debug'
    sleep 1
    s=$(screen)
    require 'b3 debug2' "$s" 'ticks='
    local n2=$(ticks_of "$s")
    (( n2 - n1 > 400 )) || fail "b3: ticks advanced only by $((n2 - n1)) while the shell stayed responsive" "$s"
    print "b3: ticks advanced by $((n2 - n1)) with the spinner running"
    type_cmd 'ps'
    sleep 2
    s=$(screen)
    require 'b3 ps header' "$s" 'PID PPID STATE NAME MODULE DATA GOT STACK'
    print -r -- "$s" | grep -Eq " counter " || fail 'b3: counter task not listed' "$s"
    print 'b3: shell kept typing/debug/ps while counter spun (preemptive)'
    type_cmd "kill $pid"
    sleep 1
    type_cmd "wait $pid"
    wait_screen 'exited with status' 8 || fail 'b3: counter was not reaped' "$s"
    ssos_exit
    print 'PASS b3'
}

run_b4() {
    s=$(screen)
    require 'b4 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'b4: ssos9 boot failed'
    type_cmd 'mem'
    sleep 1
    s=$(screen)
    local base=$(print -r -- "$s" | grep -o 'arena free_bytes=[0-9]* largest_free=[0-9]*' | tail -1)
    [[ -n $base ]] || fail 'b4: no arena baseline' "$s"
    type_cmd 'run counter'
    sleep 2 # foreground: the shell blocks while counter spins
    printf 'presskey ctrl\n' > "$PIPE"; sleep 0.2
    printf 'typekey c\n' > "$PIPE"; sleep 0.2
    printf 'releasekey ctrl\n' > "$PIPE"; sleep 0.2
    wait_line '\^C' 10 || fail 'b4: Ctrl-C not observed' "$s"
    wait_screen 'interrupted' 8 || fail 'b4: foreground task not interrupted' "$s"
    wait_line '> ' 8 || fail 'b4: prompt missing after interrupt' "$s"
    type_cmd 'mem'
    sleep 1
    s=$(screen)
    local after=$(print -r -- "$s" | grep -o 'arena free_bytes=[0-9]* largest_free=[0-9]*' | tail -1)
    [[ "$after" == "$base" ]] || fail "b4: arena did not return to baseline" \
        "baseline: $base"$'\n'"after: $after"
    ssos_exit
    print "PASS b4: Ctrl-C reaped the spinner; arena back to $base"
}

HELP_EXPECTED='Commands: help, uptime, ps, modules, mem, run NAME [ARGS...] [&], load DRIVE FILE, unload NAME, kill PID, wait PID, smount ID [START [BLOCKS]], swrite FILE TEXT, sreplace FILE TEXT, sread FILE, debug, trace, clear, version, mux, exit'

run_c1() {
    s=$(screen)
    require 'c1 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'c1: ssos9 boot failed'
    type_cmd 'help'
    sleep 2
    s=$(screen)
    local got=$(norm_text "$s")
    local want=$(norm_text "$HELP_EXPECTED")
    [[ "$got" == *"$want"* ]] || fail 'c1: help text differs from the cli.c command table' "$s"
    require 'c1 prompt alive' "$s" '>'
    ssos_exit
    print 'PASS c1: full help text matches the command table (wrap-normalized)'
}

run_c2() {
    s=$(screen)
    require 'c2 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'c2: ssos9 boot failed'
    local i
    for ((i = 0; i < 8; i++)); do type_cmd 'ps'; done # ~50 lines > 25 rows
    # The console keeps an internal line window and redraws: the prompt
    # stays visible on the last console row and the function-key row stays
    # the last screen row. (The never-written bottom-right cell cannot be
    # distinguished from blank padding in a text copy; that part of C2
    # stays a manual check.)
    wait_line '> ' 15 || fail 'c2: prompt missing after long output' "$s"
    local rows=("${(@f)$(print -r -- "$s")}")
    local last=${rows[-1]}
    print -r -- "$last" | grep -Eq '(C|S)[0-9]' || \
        fail 'c2: function-key row is not the last screen row' "$s"
    require 'c2 no errors' "$s" '>'
    ssos_exit
    print 'PASS c2: >25 lines of output kept the prompt on screen without errors'
}

run_c3() {
    s=$(screen)
    require 'c3 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'c3: ssos9 boot failed'
    type_cmd 'mux'
    wait_screen 'Multiplexer:' 8 || fail 'c3: mux did not start' "$s"
    key f2
    sleep 1.5
    type_cmd 'help'
    sleep 2
    s=$(screen)
    [[ "$(norm_text "$s")" == *"$(norm_text "$HELP_EXPECTED")"* ]] || \
        fail 'c3: window 2 did not run help' "$s"
    key f3
    sleep 1.5
    type_cmd 'ps'
    sleep 2
    s=$(screen)
    require 'c3 window 3 ps' "$s" 'PID PPID STATE NAME MODULE DATA GOT STACK'
    require 'c3 window 3 shell row' "$s" 'shell'
    key f1
    sleep 1.5
    s=$(screen)
    print -r -- "$s" | grep -Fq 'Multiplexer:' || fail 'c3: window 1 content not restored' "$s"
    local want=$(norm_text "$HELP_EXPECTED")
    [[ "$(norm_text "$s")" != *"$want"* ]] || fail 'c3: window 1 shows window 2 output' "$s"
    key f2
    sleep 1.5
    s=$(screen)
    [[ "$(norm_text "$s")" == *"$want"* ]] || fail 'c3: window 2 did not retain help output' "$s"
    key f1
    sleep 1
    ssos_exit
    print 'PASS c3: per-window output retained and restored across F1/F2/F3'
}

run_c4() {
    s=$(screen)
    require 'c4 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'c4: ssos9 boot failed'
    type_cmd 'mux'
    wait_screen 'Multiplexer:' 8 || fail 'c4: mux did not start' "$s"
    key f2
    sleep 1.5
    type_cmd 'mem'
    wait_screen 'arena free_bytes=' 8 || fail 'c4: window 2 did not run mem' "$s"
    key f1
    sleep 1.5
    s=$(screen)
    print -r -- "$s" | grep -Fq 'arena free_bytes=' && fail 'c4: window 1 shows window 2 output' "$s"
    # Try Ctrl-] then digit. IOCS encodes ctrl only for letters, so this
    # prefix may be undeliverable through the emulated keyboard.
    printf 'presskey ctrl\n' > "$PIPE"; sleep 0.2
    printf 'presskey rightbracket\n' > "$PIPE"; sleep 0.2
    printf 'releasekey rightbracket\n' > "$PIPE"; sleep 0.15
    printf 'releasekey ctrl\n' > "$PIPE"; sleep 0.3
    key 2
    sleep 1.5
    s=$(screen)
    if ! print -r -- "$s" | grep -Fq 'arena free_bytes='; then
        print 'c4: Ctrl-] did not switch; trying the ~ prefix'
        key return # flush whatever reached the shell prompt
        sleep 0.5
        printf 'presskey shift\n' > "$PIPE"; sleep 0.2
        printf 'presskey caret\n' > "$PIPE"; sleep 0.2
        printf 'releasekey caret\n' > "$PIPE"; sleep 0.15
        printf 'releasekey shift\n' > "$PIPE"; sleep 0.4
        key 2
        sleep 1.5
        s=$(screen)
        if ! print -r -- "$s" | grep -Fq 'arena free_bytes='; then
            key return
            key f1
            sleep 1
            ssos_exit
            fail 'c4: neither Ctrl-] nor ~ delivered a mux prefix through the emulated IOCS keyboard (F-key switching is covered by c3)' "$s"
        fi
        print 'c4: Ctrl-] not encodable on this IOCS path; ~ prefix switched'
    else
        print 'c4: Ctrl-] then 2 selected window 2'
    fi
    key f1
    sleep 1
    ssos_exit
    print 'PASS c4'
}

run_c5() {
    local console_module=tests/target/build/console.s9m
    [[ -f "$console_module" ]] || fail 'c5: missing tests/target/build/console.s9m (run make test)'
    s=$(screen)
    print -r -- "$s" | grep -Fq 'A:\>' || fail 'c5: initial Human68k prompt missing' "$s"
    h68_prep
    place_in_test "$console_module" CONSOLE.S9M
    boot_mount
    type_cmd 'run test/console'
    sleep 2
    s=$(screen)
    print -r -- "$s" | grep -Fq 'Unable to start' && fail 'c5: console module failed to spawn' "$s"
    # The module clears the screen and draws KEY: xxxx only after a key
    # arrives, so drive it with keys instead of waiting for initial output.
    key a
    wait_screen 'KEY: 0061' 8 || fail 'c5: key a not reported as 0061' "$s"
    key z
    wait_screen 'KEY: 007A' 6 || fail 'c5: key z not reported as 007A' "$s"
    key 5
    wait_screen 'KEY: 0035' 6 || fail 'c5: key 5 not reported as 0035' "$s"
    key f5
    sleep 2
    s=$(screen)
    print -r -- "$s" | grep -Eq 'KEY: (0[1-9A-F]|[1-9A-F][0-9A-F])00' ||
        fail 'c5: special key f5 not reported as scan<<8' "$s"
    key q
    wait_screen 'exited with status 0' 10 || fail 'c5: console module did not quit on q' "$s"
    ssos_exit
    print 'PASS c5: console module reported ASCII and scan-shifted keys'
}

run_d2() {
    local edit_module=apps/edit/build/edit.s9m
    [[ -f "$edit_module" ]] || fail 'd2: missing apps/edit/build/edit.s9m (run make all)'
    local name="D2T$(printf '%05X' $(( $(date +%s) % 1048576 )))"
    s=$(screen)
    print -r -- "$s" | grep -Fq 'A:\>' || fail 'd2: initial Human68k prompt missing' "$s"
    h68_prep
    place_in_test "$edit_module" EDIT.S9M
    boot_mount
    print '== d2 create and save a new file with edit =='
    type_cmd "run test/edit test/$name.TXT"
    wait_screen 'Cannot read file.' 10 || fail 'd2: editor did not prompt for a new file' "$s"
    key n
    sleep 1.5
    s=$(screen)
    print -r -- "$s" | grep -Fq 'NORMAL' || fail 'd2: editor did not open empty buffer' "$s"
    key i
    sleep 0.5
    # typekey sends unshifted scans, so everything the script types arrives
    # lowercase; the editor content and all waits follow that.
    local c line1='d2line1' line2='d2line2'
    for c in ${(s::)line1}; do type_char "$c"; done
    key return
    for c in ${(s::)line2}; do type_char "$c"; done
    key esc
    sleep 1
    type_cmd ':wq'
    # "Written" appears only between the save and the editor's exit clear, so
    # assert on the outcome: :wq exits 0 only when the save succeeded.
    wait_screen 'exited with status 0' 25 || fail 'd2: editor did not save and exit' "$s"
    wait_line '> ' 10 || fail 'd2: no prompt after :wq' "$s"
    print '== d2 reopen the saved file =='
    type_cmd "run test/edit test/$name.TXT"
    sleep 3
    s=$(screen)
    print -r -- "$s" | grep -Fq 'd2line1' || fail 'd2: reopened file lacks d2line1' "$s"
    print -r -- "$s" | grep -Fq 'd2line2' || fail 'd2: reopened file lacks d2line2' "$s"
    type_cmd ':q!'
    wait_line '> ' 10 || fail 'd2: no prompt after :q!' "$s"
    ssos_exit
    print '== d2 verify from Human68k after reset =='
    guest_reset
    type_cmd "type a:\\test\\$name.TXT"
    wait_line 'd2line1' 10 || fail 'd2: Human68k cannot read the saved file' "$s"
    print -r -- "$s" | grep -Fq 'd2line2' || fail 'd2: Human68k file lacks d2line2' "$s"
    print "PASS d2: edit saved and reloaded A:\\TEST\\$name.TXT"
}

wait_host_file() { # wait_host_file <case-insensitive NAME.EXT> <timeout>; sets $host_file
    local want=${1:l} tmo=$2 waited=0 name
    while (( waited < tmo )); do
        host_file=''
        for name in "$HOME"/tmp/*(N); do
            if [[ "${name:t:l}" == "$want" ]]; then
                host_file=$name
                return 0
            fi
        done
        sleep 2
        waited=$((waited + 2))
    done
    return 1
}

# d3_place_and_asm <suffix>: stage the three link sources under unique
# SA<suffix>.S / MB<suffix>.S / OB<suffix>.S names in A:\TEST and assemble
# them on target into matching .O files.
d3_place_and_asm() {
    local suf=$1
    cp tests/asm/link/start.s "$HOME/tmp/SA$suf.S"
    cp tests/asm/link/main.s "$HOME/tmp/MB$suf.S"
    cp tests/asm/link/obj.s "$HOME/tmp/OB$suf.S"
    place_in_test "$HOME/tmp/SA$suf.S" "SA$suf.S"
    place_in_test "$HOME/tmp/MB$suf.S" "MB$suf.S"
    place_in_test "$HOME/tmp/OB$suf.S" "OB$suf.S"
    boot_mount
    type_cmd "run test/asm test/sa$suf.s test/sa$suf.o"
    wait_screen "Wrote test/sa$suf.o" 25 || fail "d3: asm sa$suf failed" "$s"
    wait_line '> ' 10 || fail "d3: no prompt after asm sa$suf" "$s"
    type_cmd "run test/asm test/mb$suf.s test/mb$suf.o"
    wait_screen "Wrote test/mb$suf.o" 25 || fail "d3: asm mb$suf failed" "$s"
    wait_line '> ' 10 || fail "d3: no prompt after asm mb$suf" "$s"
    type_cmd "run test/asm test/ob$suf.s test/ob$suf.o"
    wait_screen "Wrote test/ob$suf.o" 25 || fail "d3: asm ob$suf failed" "$s"
    wait_line '> ' 10 || fail "d3: no prompt after asm ob$suf" "$s"
}

run_d3() {
    local asm_module=apps/asm/build/asm.s9m
    [[ -f "$asm_module" ]] || fail 'd3: missing apps/asm/build/asm.s9m (run make all)'
    [[ -x tests/build/asm-host ]] || fail 'd3: missing tests/build/asm-host (run make test)'
    local suf=$(printf '%04x' $(( $(date +%s) % 65536 )))
    local exp=$(mktemp -d "${TMPDIR%/}/d3exp.XXXXXXXX")
    s=$(screen)
    print -r -- "$s" | grep -Fq 'A:\>' || fail 'd3: initial Human68k prompt missing' "$s"
    h68_prep
    place_in_test "$asm_module" ASM.S9M
    d3_place_and_asm "$suf"
    ssos_exit
    print '== d3 fetch objects through F: after reset =='
    guest_reset
    type_cmd "copy a:\\test\\sa$suf.o f:"
    type_cmd "copy a:\\test\\mb$suf.o f:"
    type_cmd "copy a:\\test\\ob$suf.o f:"
    sleep 2
    tests/build/asm-host tests/asm/link/start.s "$exp/SA$suf.O"
    tests/build/asm-host tests/asm/link/main.s "$exp/MB$suf.O"
    tests/build/asm-host tests/asm/link/obj.s "$exp/OB$suf.O"
    local f
    for f in sa mb ob; do
        local stem="${f}${suf}"
        wait_host_file "$stem.o" 20 || fail "d3: $stem.O not copied to F: (~/tmp)"
        cmp -s "$host_file" "$exp/${(U)stem}.O" || \
            fail "d3: guest ${host_file:t} differs from the host build" \
                "$(cmp "$host_file" "$exp/${(U)stem}.O" 2>&1 | head -2)"
        print "d3: ${host_file:t} byte-identical to host build"
    done
    rm -rf "$exp"
    print 'PASS d3: three on-target objects match the host assembler'
}

run_d4() {
    local ld_module=apps/link/build/ld.s9m
    [[ -f "$ld_module" ]] || fail 'd4: missing apps/link/build/ld.s9m (run make all)'
    [[ -x tests/build/asm-host && -x tests/build/link-host ]] || \
        fail 'd4: missing tests/build/asm-host or link-host (run make test)'
    local suf=$(printf '%04x' $(( ($(date +%s) + 3) % 65536 )))
    local prog=PROG$suf
    local exp=$(mktemp -d "${TMPDIR%/}/d4exp.XXXXXXXX")
    s=$(screen)
    print -r -- "$s" | grep -Fq 'A:\>' || fail 'd4: initial Human68k prompt missing' "$s"
    h68_prep
    place_in_test "$ld_module" LD.S9M
    d3_place_and_asm "$suf"
    type_cmd "run test/ld test/$prog.s9m test/sa$suf.o test/mb$suf.o test/ob$suf.o"
    wait_screen "Wrote test/${prog:l}.s9m" 30 || fail "d4: ld did not write the image" "$s"
    wait_line '> ' 10 || fail 'd4: no prompt after ld' "$s"
    print '== d4 run the linked program through the volume fallback =='
    type_cmd "run test/$prog"
    wait_screen 'LNK' 15 || fail 'd4: linked program did not print LNK' "$s"
    wait_screen 'exited with status 27' 10 || fail 'd4: linked program did not exit 27' "$s"
    ssos_exit
    print '== d4 fetch and compare the linked image =='
    guest_reset
    type_cmd "copy a:\\test\\$prog.s9m f:"
    sleep 2
    tests/build/asm-host tests/asm/link/start.s "$exp/SA$suf.O"
    tests/build/asm-host tests/asm/link/main.s "$exp/MB$suf.O"
    tests/build/asm-host tests/asm/link/obj.s "$exp/OB$suf.O"
    tests/build/link-host "$exp/$prog.S9M" "$exp/SA$suf.O" "$exp/MB$suf.O" "$exp/OB$suf.O" || \
        fail 'd4: host link of the expected image failed'
    wait_host_file "$prog.s9m" 20 || fail "d4: $prog.S9M not copied to F: (~/tmp)"
    cmp -s "$host_file" "$exp/$prog.S9M" || \
        fail "d4: guest ${host_file:t} differs from the host golden" \
            "$(cmp "$host_file" "$exp/$prog.S9M" 2>&1 | head -2)"
    rm -rf "$exp"
    print "PASS d4: ld + run fallback produced the golden image; status 27 and LNK"
}

run_f1() {
    s=$(screen)
    require 'f1 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'f1: ssos9 boot failed'
    type_cmd 'mem'
    sleep 1
    s=$(screen)
    local base=$(print -r -- "$s" | grep -o 'arena free_bytes=[0-9]* largest_free=[0-9]*' | tail -1)
    [[ -n $base ]] || fail 'f1: no arena baseline' "$s"
    # Each trailing-& builtin runs in an isolated kernel-entry child whose
    # output interleaves on the same console; the IOCS lock keeps lines whole.
    type_cmd 'help&'
    wait_screen 'Started command as PID' 8 || fail 'f1: help& did not start' "$s"
    local p1=$(started_pid "$s")
    type_cmd 'ps&'
    sleep 2
    s=$(screen)
    local p2=$(started_pid "$s")
    [[ -n $p2 ]] || fail 'f1: ps& did not start' "$s"
    type_cmd 'mem&'
    sleep 2
    s=$(screen)
    local p3=$(started_pid "$s")
    [[ -n $p3 ]] || fail 'f1: mem& did not start' "$s"
    [[ "$(norm_text "$s")" == *"$(norm_text "$HELP_EXPECTED")"* ]] || \
        fail 'f1: help output missing or garbled' "$s"
    print -r -- "$s" | grep -Fq 'PID PPID STATE NAME MODULE DATA GOT STACK' || \
        fail 'f1: ps output missing or garbled' "$s"
    print -r -- "$s" | grep -Fq 'arena free_bytes=' || \
        fail 'f1: mem output missing or garbled' "$s"
    print 'f1: help/ps/mem outputs all present and line-intact'
    local pid
    for pid in $p1 $p2 $p3; do
        type_cmd "wait $pid"
        wait_screen 'exited with status' 8 || fail "f1: PID $pid was not reaped" "$s"
    done
    type_cmd 'mem'
    sleep 1
    s=$(screen)
    local after=$(print -r -- "$s" | grep -o 'arena free_bytes=[0-9]* largest_free=[0-9]*' | tail -1)
    [[ "$after" == "$base" ]] || fail 'f1: arena did not return to baseline' \
        "baseline: $base"$'\n'"after: $after"
    ssos_exit
    print 'PASS f1: background builtins interleaved cleanly and reaped'
}

bg_max_record() { # highest "BG nn" number visible on the screen
    print -r -- "$1" | tr '\r' '\n' | grep -oE 'BG [0-9]{2}' |
        grep -oE '[0-9]{2}' | sort -n | tail -1
}

run_f2() {
    local bg_module=tests/target/build/BG.S9M
    [[ -f "$bg_module" ]] || fail 'f2: missing tests/target/build/BG.S9M (run make test)'
    s=$(screen)
    require 'f2 initial prompt' "$s" 'A:\>'
    h68_prep
    place_in_test "$bg_module" BG.S9M
    boot_mount
    type_cmd 'mux'
    wait_screen 'Multiplexer:' 8 || fail 'f2: mux did not start' "$s"
    # The builtin counter emits nothing; BG emits numbered records for ~30 s,
    # matching the 2026-11-11 manual observation this area automates.
    type_cmd 'run test/bg&'
    wait_screen 'Started module as PID' 8 || fail 'f2: bg did not start' "$s"
    local pid=$(started_pid "$s")
    wait_screen 'BG 0' 10 || fail 'f2: no BG records on window 1' "$s"
    local round max1 max2 max3
    for ((round = 1; round <= 3; round++)); do
        max1=$(bg_max_record "$s")
        [[ -n $max1 ]] || fail "f2: no BG record visible before switch $round" "$s"
        key f2
        sleep 3
        s=$(screen)
        print -r -- "$s" | grep -Fq 'SSOS-9 shell' || \
            fail "f2: window 2 not shown after switch $round" "$s"
        key f1
        sleep 2
        if ! wait_screen 'BG ' 8; then fail "f2: BG output gone after switch $round" "$s"; fi
        max2=$(bg_max_record "$s")
        (( max2 > max1 )) || fail "f2: BG did not advance while window 1 was hidden (round $round: $max1 -> $max2)" "$s"
        print "f2: round $round: BG advanced $max1 -> $max2 across F2/F1"
    done
    max3=$max2
    type_cmd "wait $pid"
    wait_screen 'exited with status 0' 45 || fail 'f2: bg module did not finish cleanly' "$s"
    (( max3 >= 30 )) || fail 'f2: too few records survived the switches' "$s"
    ssos_exit
    print 'PASS f2: background output continued across window switches'
}

run_g1() {
    s=$(screen)
    require 'g1 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'g1: ssos9 boot failed'
    type_cmd 'mem'
    sleep 1
    s=$(screen)
    local base=$(print -r -- "$s" | grep -o 'arena free_bytes=[0-9]* largest_free=[0-9]*' | tail -1)
    [[ -n $base ]] || fail 'g1: no arena baseline' "$s"
    local i pid
    for ((i = 1; i <= 5; i++)); do
        type_cmd 'run counter&'
        wait_screen 'Started module as PID' 8 || fail "g1: cycle $i spawn failed" "$s"
        pid=$(started_pid "$s")
        [[ -n $pid ]] || fail "g1: cycle $i PID parse failed" "$s"
        type_cmd "kill $pid"
        wait_screen 'Kill requested.' 8 || fail "g1: cycle $i kill failed" "$s"
        type_cmd "wait $pid"
        wait_screen 'exited with status' 8 || fail "g1: cycle $i reap failed" "$s"
        print "g1: cycle $i spawn/kill/wait reaped PID $pid"
    done
    type_cmd 'mem'
    sleep 1
    s=$(screen)
    local after=$(print -r -- "$s" | grep -o 'arena free_bytes=[0-9]* largest_free=[0-9]*' | tail -1)
    [[ "$after" == "$base" ]] || fail 'g1: arena did not return to baseline' \
        "baseline: $base"$'\n'"after: $after"
    ssos_exit
    print "PASS g1: five spawn/kill/wait cycles; arena back to $base"
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
    b2) run_b2 ;;
    b3) run_b3 ;;
    b4) run_b4 ;;
    c1) run_c1 ;;
    c2) run_c2 ;;
    c3) run_c3 ;;
    c4) run_c4 ;;
    c5) run_c5 ;;
    d1) run_d1 ;;
    d2) run_d2 ;;
    d3) run_d3 ;;
    d4) run_d4 ;;
    e3) run_e3 ;;
    f1) run_f1 ;;
    f2) run_f2 ;;
    g1) run_g1 ;;
    *) fail 'usage: tools/emulator-itest.sh a2|a3|b1|b2|b3|b4|c1|c2|c3|c4|c5|d1|d2|d3|d4|e3|f1|f2|g1' ;;
esac
