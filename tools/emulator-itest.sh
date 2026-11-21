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
#   c4  F1/F2/F3 select mux windows and retain output
#   c5  console module KEY: reporting over real IOCS input
#   d1  Human68k-format HDD create/read/replace in A:\TEST
#   d2  edit saves and reloads a new file in A:\TEST
#   d3  on-target assembler output matches the host build
#   d4  ld links, run fallback executes, image matches host golden
#   e1  mux three-window edit -> asm x3 -> ld -> run
#   e2  linker rejects an undefined symbol and writes no output
#   e3  builtin hello real-PIC launch
#   f1  background builtins (help&, ps&, mem&) interleave cleanly and reap
#   f2  background module output continues across mux window switches
#   g1  five spawn/kill/wait cycles return the arena to baseline
#   h3  Ctrl-C interrupts only the active mux window's foreground
#   h4  sread boundaries: empty file ok, >256 bytes rejected
#   h5  XDF IPL boot + smount/sread + PID 1 protection
#   h6  25 spawn/kill/wait cycles return the arena to baseline
# Storage tests (c5, d1..d4) place fixtures in A:\TEST only and reuse the
# already-copied module images; run B/C1-4 tests without disk writes.
set -u

PIPE="${TMPDIR%/}/XEiJControl"
XEI_J_STAGE=${SSOS9_XEIJ_STAGE:-"$HOME/tmp"}
XEI_J_INI=${SSOS9_XEIJ_INI:-"$HOME/XEiJ.ini"}
XEI_J_LAUNCHER=${SSOS9_XEIJ_LAUNCHER:-"$HOME/.local/bin/xeij.sh"}
XEI_J_LOCK="/tmp/ssos9-xeij-itest-$UID.lock"
TRANSPORT="${0:A:h}/xeij-transport.py"
typeset -a held_keys=()

transport() { python3 "$TRANSPORT" "$@"; }
send_control() {
    local command=$1 key_name=${1#* }
    if [[ "$command" == 'presskey '* ]]; then
        held_keys+=("$key_name")
    fi
    transport send "$PIPE" "$command" 2 || environment_fail "control command failed: $command"
    if [[ "$command" == 'releasekey '* ]]; then
        held_keys=("${(@)held_keys:#$key_name}")
    fi
}
release_held_keys() {
    local key_name result=0
    for key_name in "${held_keys[@]}"; do
        transport send "$PIPE" "releasekey $key_name" 0.5 || result=1
    done
    return "$result"
}
finish_case() {
    local result=$1
    trap - EXIT
    # A caught signal is reset on exec, so a restarted JVM still honors TERM.
    trap ':' HUP INT TERM
    release_held_keys || { (( result != 0 )) || result=1; }
    if [[ -n "$ini_backup" ]]; then
        restore_xeij_ini || { (( result != 0 )) || result=3; }
    fi
    exit "$result"
}


environment_fail() { print -u2 -- "ENVIRONMENT: $1"; exit 3; }
fail() { print -u2 -- "FAIL: $1"; [[ $# -lt 2 ]] || print -u2 -r -- "$2"; exit 1; }
copy_screen() {
    transport run 4 osascript - "$XEI_J_PID" >/dev/null <<'APPLESCRIPT'
on run argv
    set targetPID to item 1 of argv as integer
    tell application "System Events"
        set targetProcess to first application process whose unix id is targetPID
        set frontmost of targetProcess to true
        if not (frontmost of targetProcess) then error "XEiJ is not frontmost"
        keystroke "c" using {option down, shift down}
    end tell
end run
APPLESCRIPT
}
key() { send_control "typekey $1"; sleep 0.1; }
# Real host key event to the frontmost XEiJ (macOS key code). The control
# pipe has no key names for the cursor or DEL keys, so c6 routes those
# through System Events instead; typing itself keeps using the pipe.
host_keycode() {
    osascript \
        -e 'tell application "System Events" to set frontmost of first process whose unix id is '"$XEI_J_PID"' to true' \
        -e "tell application \"System Events\" to key code $1" > /dev/null ||
        environment_fail "host key event failed: $1"
    sleep 0.25
}
# XEiJ's instant typekey drops some keycodes (yen, underline); hold them
# just under the keyboard auto-repeat delay so exactly one is delivered.
hold_key() {
    send_control "presskey $1"
    sleep 0.15
    send_control "releasekey $1"
    sleep 0.1
}
shift_key() { # shifted glyph: hold shift, tap the key, release
    send_control "presskey shift"
    sleep 0.12
    send_control "typekey $1"
    sleep 0.12
    send_control "releasekey shift"
    sleep 0.1
}
simple_key_name() {
    case "$1" in
        ' ') REPLY=space ;;
        '-') REPLY=minus ;;
        ':') REPLY=colon ;;
        '.') REPLY=period ;;
        ',') REPLY=comma ;;
        '/') REPLY=slash ;;
        ';') REPLY=semicolon ;;
        $'\n') REPLY=return ;;
        [a-zA-Z0-9]) REPLY=$1 ;;
        *) return 1 ;;
    esac
}
type_char() {
    if simple_key_name "$1"; then
        key "$REPLY"
        return
    fi
    case "$1" in
        '&') shift_key 6 ;;
        '"') shift_key 2 ;;
        '#') shift_key 3 ;;
        '%') shift_key 5 ;;
        '(') shift_key 8 ;;
        ')') shift_key 9 ;;
        '_') hold_key underline ;;
        '\') hold_key yen ;;
        *) key "$1" ;;
    esac
}
type_text() {
    local c
    local -a keys=()
    for c in ${(s::)1}; do
        if ! simple_key_name "$c"; then
            for c in ${(s::)1}; do type_char "$c"; done
            return
        fi
        keys+=("$REPLY")
    done
    transport keys "$PIPE" "${keys[@]}" ||
        environment_fail "control input failed while typing: $1"
}
type_cmd() {
    type_text "$1"
    key return
}

type_checked_cmd() {
    local command=$1 echo_line deadline
    type_text "$command"
    deadline=$((SECONDS + 3))
    while true; do
        snap
        echo_line=$(print -r -- "$s" | awk '/^> / { line=$0 } END { sub(/[[:space:]]+$/, "", line); print line }')
        if [[ "$echo_line" == "> $command" ]]; then
            key return
            return
        fi
        # The screen may lag behind the FIFO while the guest drains its keys.
        [[ "> $command" == "$echo_line"* ]] && (( SECONDS < deadline )) || break
    done
    print -u2 -r -- "$s"
    environment_fail "guest input differs before Enter: expected [$command], saw [$echo_line]"
}
screen() {
    # Runs inside a command-substitution subshell, so fail() here would only
    # exit the subshell and the script would continue on a dead capture. On
    # an unrecoverable copy failure, emit the pending marker: the caller's
    # content checks then fail in the main shell with a real diagnostic.
    local out tries=0 marker="SSOS9_SCREEN_COPY_PENDING-$RANDOM-$RANDOM"
    if ! printf '%s' "$marker" | transport run 3 pbcopy; then
        print -r -- 'SSOS9_SCREEN_COPY_PENDING'
        return 0
    fi
    if ! copy_screen && { sleep 0.3; ! copy_screen; }; then
        print -u2 'WARN: screen copy keystroke failed'
        print -r -- 'SSOS9_SCREEN_COPY_PENDING'
        return 0
    fi
    # Adaptive clipboard poll (S2): most copies land in well under a second,
    # so check early (0.4 s) and poll at 0.3 s before falling back to the
    # slower retry keystrokes below.
    sleep 0.4
    out=$(transport run 3 pbpaste) || out=$marker
    local quick=0
    while [[ ( -z "$out" || "$out" == "$marker" ) && $quick -lt 3 ]]; do
        sleep 0.3
        quick=$((quick + 1))
        out=$(transport run 3 pbpaste) || out=$marker
    done
    while [[ ( -z "$out" || "$out" == "$marker" ) && $tries -lt 3 ]]; do
        # Re-assert focus before each retry: a stolen focus sends the copy
        # keystroke to another app and the marker would never clear.
        copy_screen || break
        sleep $((1 + tries))
        out=$(transport run 3 pbpaste) || out=$marker
        tries=$((tries + 1))
    done
    if [[ -z "$out" || "$out" == "$marker" ]]; then
        print -u2 'WARN: screen copy did not complete'
        print -r -- 'SSOS9_SCREEN_COPY_PENDING'
        return 0
    fi
    print -r -- "$out"
}
snap() { # snap [var]; capture the screen into $s (default) or $var. screen()
    # runs in a command-substitution subshell where fail() cannot stop the
    # script, so the dead-capture marker is checked here in the main shell.
    local var=${1:-s} out
    out=$(screen)
    if [[ "$out" == 'SSOS9_SCREEN_COPY_PENDING' ]]; then
        environment_fail 'screen copy did not complete (XEiJ or clipboard unresponsive)'
    fi
    if [[ -n ${SSOS9_XEIJ_SCREEN_LOG:-} ]]; then
        print -r -- "$out" > "$SSOS9_XEIJ_SCREEN_LOG" ||
            environment_fail 'cannot write screen evidence'
    fi
    typeset -g "$var"="$out"
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
wait_screen() { # wait_screen <pattern> <timeout-seconds>; screen text in $s.
    # Adaptive cadence (S3): check right away (screen() settles ~0.4 s),
    # then back off 0.5 -> 1 -> 2 s. Most waits match on the first or second
    # capture, so the old fixed 3 s poll dominated the run time.
    local pat=$1 deadline=$(( SECONDS + $2 )) gap=0.5
    while (( SECONDS < deadline )); do
        snap
        print -r -- "$s" | grep -Fq -- "$pat" && return 0
        sleep $gap
        (( gap < 2 )) && gap=$(( gap * 2 ))
    done
    return 1
}
xeij_pids() {
    local processes
    processes=$(ps -axww -o pid= -o command=) || return 1
    print -r -- "$processes" | awk -v ini="${1:-$XEI_J_INI}" '
        index($0, " -jar ") && index($0, "/XEiJ.jar ") {
            option = " -ini " ini
            start = index($0, option)
            if (ini == "*" || (start && substr($0, start + length(option)) ~ /^($| )/))
                print $1
        }'
}
xeij_stop() {
    local pids i
    pids=$(xeij_pids) || return 1
    [[ -n "$pids" ]] || return 0
    [[ "$pids" == "$XEI_J_PID" && "$(xeij_pids '*')" == "$XEI_J_PID" ]] || return 1
    kill -TERM "$XEI_J_PID" || return 1
    for ((i = 0; i < 15; i++)); do
        pids=$(xeij_pids) || return 1
        [[ -z "$pids" ]] && return 0
        sleep 1
    done
    print -u2 'FAIL: XEiJ did not stop'
    return 1
}
xeij_start() {
    local running
    running=$(xeij_pids '*') || return 1
    [[ -z "$running" ]] || return 1
    if [[ -n ${SSOS9_XEIJ_JAR:-} && -n ${SSOS9_XEIJ_CGROM:-} ]]; then
        transport launch "$XEI_J_INI.log" java -jar "$SSOS9_XEIJ_JAR" \
            -cgrom "$SSOS9_XEIJ_CGROM" -ini "$XEI_J_INI" || return 1
    else
        transport run 10 "$XEI_J_LAUNCHER" "$XEI_J_INI" || return 1
    fi
    local i candidate
    for ((i = 0; i < 30; i++)); do
        candidate=$(xeij_pids) || return 1
        if [[ "$candidate" == <-> ]]; then
            XEI_J_PID=$candidate
            if [[ -p "$PIPE" ]] && transport probe "$PIPE" 0.2 2>/dev/null; then
                return 0
            fi
        fi
        sleep 1
    done
    print -u2 'FAIL: XEiJ process or control pipe did not come back'
    return 1
}

human68k_mfp_snapshot() {
    type_cmd 'cls'
    type_cmd 'f:mfpstate'
    wait_screen 'MFPSTATE=' 10 || fail 'Human68k MFP snapshot failed' "$s"
    # The MFPSTATE line (8 byte fields + 7 dword fields) exceeds the
    # 80-column text screen, so the copy wraps it. Strip whitespace and row
    # breaks first: the wrapped halves join back into the exact-length
    # byte/dword pattern below. Padding only trails the value, so no
    # captured byte can be lost or reordered.
    MFP_SNAPSHOT=$(print -r -- "$s" | tr -d ' \r\n\t' |
        grep -Eo 'MFPSTATE=([0-9A-F]{2}/){8}([0-9A-F]{8}/){6}[0-9A-F]{8}')
    [[ -n "$MFP_SNAPSHOT" ]] || fail 'invalid MFP snapshot' "$s"
    if ! screen_line "$s" 'A:\\>'; then
        wait_line 'A:\\>' 8 || fail 'Human68k did not return after MFP snapshot' "$s"
    fi
}

run_a2() {
    snap
    require 'initial prompt' "$s" 'A:\>'
    type_cmd 'keywitch -r'
    sleep 1

    local before=''
    if [[ ${SSOS9_XEIJ_MFP_CHECK:-0} == 1 ]]; then
        [[ -f "$XEI_J_STAGE/mfpstate.x" ]] || environment_fail 'MFP probe missing from HFS stage'
        human68k_mfp_snapshot
        before=$MFP_SNAPSHOT
        print "A2 baseline $before"
    fi
    for ((i = 1; i <= 5; i++)); do
        print "== A2 iteration $i =="
        type_cmd 'cls'
        type_cmd 'f:ssos9'
        wait_screen 'SSOS-9: minimal X68000 CLI' 30 ||
            fail "iteration $i: startup failed" "$s"
        if ! screen_line "$s" '> '; then
            wait_line '> ' 8 || fail "iteration $i: SSOS-9 prompt missing" "$s"
        fi
        type_cmd 'debug'
        wait_screen 'MFP IERB/IMRB/TCDCR/TDDR=' 8 ||
            fail "iteration $i: no MFP diagnostics" "$s"
        print -r -- "$s" | grep -F 'MFP IERB/IMRB/TCDCR/TDDR='
        # B5-lite: the Timer D interrupt stays consistently enabled (mask
        # 3E3E) across cycles. Register values before launch and after exit
        # are not observable from Human68k, so behavior after return (the
        # dir check below) remains the restoration evidence.
        local mfp=$(print -r -- "$s" |
            grep -o 'MFP IERB/IMRB/TCDCR/TDDR=[0-9A-F]*' | grep -o '[0-9A-F]*$')
        [[ $mfp == 3E3E* ]] || fail "iteration $i: Timer D mask changed: $mfp" "$s"
        type_cmd 'exit'
        wait_screen 'SSOS-9 terminated.' 15 || fail "iteration $i: exit failed" "$s"
        wait_line 'A:\\>' 8 || fail "iteration $i: Human68k did not return" "$s"
        type_cmd 'cls'
        type_cmd 'dir a:'
        wait_screen '<dir>' 8 || fail "iteration $i: DOS listing missing" "$s"
        if ! screen_line "$s" 'A:\\>'; then
            wait_line 'A:\\>' 8 || fail "iteration $i: DOS prompt missing" "$s"
        fi
        if [[ -n "$before" ]]; then
            human68k_mfp_snapshot
            [[ "$MFP_SNAPSHOT" == "$before" ]] ||
                fail "iteration $i: MFP state changed ($before -> $MFP_SNAPSHOT)" "$s"
            print "A2 restored $MFP_SNAPSHOT"
        fi
        print "PASS A2 iteration $i"
    done
    print 'PASS A2: 5 iterations'
}

restore_xeij_ini() {
    print '== Restore XEiJ configuration =='
    if ! xeij_stop; then
        print -u2 -- "ENVIRONMENT: XEiJ still running; backup retained at $ini_backup"
        return 1
    fi
    if ! mv -f "$ini_backup" "$XEI_J_INI"; then
        print -u2 -- "ENVIRONMENT: restore failed; backup retained at $ini_backup"
        return 1
    fi
    ini_backup=''
    xeij_start
}

restart_with_floppy() {
    local unit=$1 disk=$2 backup
    [[ -f "$disk" && -f "$XEI_J_INI" ]] ||
        environment_fail "missing floppy image or XEiJ configuration: $disk"
    backup=$(mktemp "$XEI_J_INI.ssos9.XXXXXXXX") || environment_fail 'cannot allocate XEiJ backup'
    if ! cp -p "$XEI_J_INI" "$backup"; then
        rm -f "$backup"
        environment_fail 'cannot back up XEiJ configuration'
    fi
    ini_backup=$backup
    xeij_stop || environment_fail 'cannot stop XEiJ'
    sed -i '' "/^fd${unit}=/d" "$XEI_J_INI" &&
        printf 'fd%s=%s\n' "$unit" "$disk" >> "$XEI_J_INI" ||
        environment_fail 'cannot attach floppy in XEiJ configuration'
    xeij_start || environment_fail 'cannot start XEiJ'
}

run_a3() {
    print '== A3 insert ssos9.xdf in FD0 =='
    restart_with_floppy 0 "$XEI_J_STAGE/ssos9.xdf"
    print '== A3 wait for XDF IPL boot =='
    wait_screen 'SSOS-9: minimal X68000 CLI' 60 || fail 'a3: XDF boot banner missing' "$s"
    wait_line '> ' 15 || fail 'a3: no CLI prompt after IPL boot' "$s"

    print '== A3 exit must be rejected (PID 1) =='
    type_cmd 'exit'
    wait_screen 'Cannot exit the initial SSOS-9 process (PID 1).' 10 ||
        fail 'a3: exit was not rejected' "$s"
    type_cmd 'version'
    wait_screen 'SSOS-9 CLI preemptive minimum' 8 ||
        fail 'a3: CLI did not accept a command after refusing exit' "$s"

    restore_xeij_ini || environment_fail 'a3: configuration restore failed'
    wait_screen 'A:\>' 90 || fail 'a3: Human68k did not come back' "$s"
    print 'PASS a3: configuration restored and Human68k returned'
    print 'PASS a3'
}

run_b1() {
    snap
    require 'b1 initial prompt' "$s" 'A:\>'
    type_cmd 'keywitch -r'
    sleep 1
    type_cmd 'cls'
    type_cmd 'f:ssos9'
    sleep 3
    snap
    require 'b1 startup' "$s" 'SSOS-9: minimal X68000 CLI'

    # Measure ticks across a wall-clock window. Both samples are taken the
    # same delay after their `debug` typing, so the typing/copy overhead
    # cancels and ticks delta ~= 200 Hz x elapsed seconds.
    type_cmd 'debug'
    sleep 1
    t1=$(transport monotonic) || environment_fail 'cannot sample monotonic clock'
    snap
    require 'b1 debug1' "$s" 'ticks='
    n1=$(ticks_of "$s")

    sleep 6
    type_cmd 'debug'
    sleep 1
    t2=$(transport monotonic) || environment_fail 'cannot sample monotonic clock'
    snap
    require 'b1 debug2' "$s" 'ticks='
    n2=$(ticks_of "$s")

    local elapsed=$((t2 - t1))
    local -i expected=$((elapsed * 200.0))
    local tol=$((expected / 5))
    local delta=$(((n2 - n1) & 0xffffffff))
    printf 'b1 ticks delta=%d expected~%d +/- %d over %.3fs\n' "$delta" "$expected" "$tol" "$elapsed"
    if (( delta >= expected - tol && delta <= expected + tol )); then
        print 'PASS b1: tick rate within 200 Hz +/- 20%'
    else
        fail 'b1: tick rate out of range' "delta=$delta expected=$expected +/-$tol"
    fi

    type_cmd 'exit'
    sleep 3
    snap
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
    snap
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
    send_control "reset"
    wait_screen 'HIOCS' 30 || fail 'guest did not start rebooting' "$s"
    wait_screen 'A:\>' 150 || fail 'Human68k did not come back' "$s"
    sleep 2 # AUTOEXPORT tools can still print just after the prompt
    type_cmd 'keywitch -r'
    sleep 1
}
h68_prep() { # from A:\>: reset keywitch, ensure A:\TEST exists, leave a
    # fresh dir listing of A:\TEST in $s (create the directory if needed)
    key return # flush any pending unentered line from earlier activity
    type_cmd 'keywitch -r'
    sleep 1
    # Relative paths (dir/mkdir test) keep backslashes out of the typed
    # stream: the held yen key can double under auto-repeat races.
    type_cmd 'cls'
    type_cmd 'dir test'
    wait_line 'A:\\>' 8 || fail 'dir test did not complete' "$s"
    if print -r -- "$s" | grep -Eq 'ディレクトリ.*見つかり|irectory not found'; then
        print '== create A:\TEST =='
        type_cmd 'mkdir test'
        sleep 1
        type_cmd 'cls'
        type_cmd 'dir test'
        wait_screen 'A:\>' 8 || fail 'dir test did not complete' "$s"
    else
        # The first listing is already fresh; only clear the screen noise.
        type_cmd 'cls'
    fi
    return 0
}
place_in_test() { # place_in_test <host-file> <NAME.EXT>; stage on F: and copy
    # into A:\TEST. Existing files are replaced only on a disposable profile.
    # Requires a fresh dir listing in $s; leaves $s on the new listing.
    local src=$1 name=$2
    local stem=${name%.*} ext=${name##*.}
    if print -r -- "$s" | tr '\r' '\n' | grep -Eiq "^${stem}[ ]+${ext}"; then
        if [[ ! -f "${XEI_J_INI:h}/profile.json" ]]; then
            print "place: $name already in A:\\TEST (keeping existing copy)"
            return 0
        fi
        # Existing files on a disposable clone can be from older builds.
        h68_del "$name"
        h68_absent "$stem" "$ext" 12
    fi
    # macOS HFS+/APFS ignore name case, so a string compare misses the
    # staged file when only the case differs; compare content instead.
    cmp -s "$src" "$XEI_J_STAGE/$name" || cp "$src" "$XEI_J_STAGE/$name" ||
        fail "cannot stage $name in ~/tmp (F:)"
    type_cmd 'cd test'
    type_cmd "copy f:$name $name"
    sleep 1
    type_cmd 'cls'
    type_cmd 'dir'
    wait_row "$stem" "$ext" 12 || fail "$name was not copied into A:\\TEST" "$s"
    type_cmd 'cd ..'
    return 0
}
ssos_exit() { # exit SSOS-9 and confirm the Human68k prompt
    type_cmd 'exit'
    wait_screen 'SSOS-9 terminated.' 15 || fail 'SSOS-9 did not terminate' "$s"
    if ! screen_line "$s" 'A:\\>'; then
        wait_line 'A:\\>' 8 || fail 'Human68k prompt did not return' "$s"
    fi
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
    local deadline=$(( SECONDS + $2 )) gap=0.5
    while (( SECONDS < deadline )); do
        snap
        screen_line "$s" "$1" && return 0
        sleep $gap
        (( gap < 2 )) && gap=$(( gap * 2 ))
    done
    return 1
}
wait_row() { # wait_row <name> <ext> <timeout>; dir row in $s, any case
    local deadline=$(( SECONDS + $3 )) gap=0.5
    while (( SECONDS < deadline )); do
        snap
        print -r -- "$s" | tr '\r' '\n' | grep -Eiq "^${1}[ ]+${2}" && return 0
        sleep $gap
        (( gap < 2 )) && gap=$(( gap * 2 ))
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

wait_new_pid() { # wait_new_pid <last-pid> <timeout>; replies in $REPLY_PID with
    # a strictly newer "Started ... PID n". With the adaptive first-check, a
    # stale frame can still hold the PREVIOUS cycle's Started line; parsing it
    # would kill the wrong (already reaped) PID and leak the real process.
    local last=$1 deadline=$(( SECONDS + $2 )) gap=0.5 candidate
    REPLY_PID=''
    while (( SECONDS < deadline )); do
        snap
        candidate=$(started_pid "$s")
        if [[ -n $candidate && $candidate -gt $last ]]; then
            REPLY_PID=$candidate
            return 0
        fi
        sleep $gap
        (( gap < 2 )) && gap=$(( gap * 2 ))
    done
    return 1
}
type_editor_lines() { # type_editor_lines <text>; types the lines into the
    # editor in insert mode, one return key per line. Long streams lose
    # their tail when typed at full speed, so pace the characters and let
    # the editor drain every few lines.
    local line c n=0
    for line in ${(f)1}; do
        for c in ${(s::)line}; do
            type_char "$c"
            sleep 0.05
        done
        key return
        sleep 0.3
        if (( (++n % 6) == 0 )); then sleep 1; fi
    done
}
h68_del() { # h68_del <NAME.EXT>...; delete exact files in A:\TEST on
    # Human68k (exact names never trigger the wildcard <Y/N> prompt)
    local n
    type_cmd 'cd test'
    wait_line 'A:\\[Tt][Ee][Ss][Tt]>' 8 ||
        fail 'refusing to delete outside A:\TEST' "$s"
    for n in "$@"; do
        type_cmd "del ${(L)n}"
        sleep 1
    done
    type_cmd 'cd ..'
    return 0
}
h68_absent() { # h68_absent <name> <ext> <timeout>; poll the A:\TEST listing
    # until the row is gone. A single check races a stale screen copy (the
    # Option-Shift-C keystroke can be lost, leaving the previous frame on the
    # clipboard), so keep refreshing the listing.
    local name=${1:l} ext=${2:l} tmo=$3 waited=0
    type_cmd 'cd test'
    sleep 0.5
    while (( waited < tmo )); do
        sleep 2
        waited=$((waited + 2))
        type_cmd 'cls'
        type_cmd 'dir'
        sleep 1
        snap
        if ! print -r -- "$s" | tr '\r' '\n' | grep -Eiq "^${name}[ ]+${ext}"; then
            type_cmd 'cd ..'
            wait_screen 'A:\>' 10 || fail 'return to A:\ after checking file absence failed' "$s"
            return 0
        fi
    done
    type_cmd 'cd ..'
    fail "$name.$ext still exists in A:\\TEST after ${tmo}s" "$s"
}

wait_command_result() {
    local command=$1 expected=$2 deadline=$((SECONDS + $3)) gap=0.5
    while (( SECONDS < deadline )); do
        snap
        if print -r -- "$s" | awk -v command="> $command" -v expected="$expected" '
            { sub(/[[:space:]]+$/, "") }
            $0 == command { seen = 1; matched = 0; complete = 0; next }
            seen && $0 == expected { matched = 1 }
            seen && $0 == ">" && matched { complete = 1 }
            END { exit !complete }'
        then
            return 0
        fi
        sleep $gap
        (( gap < 2 )) && gap=$(( gap * 2 ))
    done
    return 1
}

run_d1() {
    local base fix upper before after target id s2
    [[ -f "$XEI_J_STAGE/ssos9.x" ]] || fail 'd1: missing ~/tmp/ssos9.x (run make install)'
    base="d1t$(printf '%05x' $(( $(date +%s) % 1048576 )))"
    fix="D1F$(printf '%05X' $(( ($(date +%s) + 7) % 1048576 )))"
    upper=${(U)base}
    print "d1 test file: $upper.TXT  fixture: $fix.TXT"
    print "d1 ssos9.x sha256: $(shasum -a 256 "$XEI_J_STAGE/ssos9.x" | awk '{print $1}')"

    snap
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
    printf 'D1FIXTURE' > "$XEI_J_STAGE/$fix.TXT" || fail 'd1: cannot write fixture to ~/tmp (F:)'
    h68_prep
    type_cmd 'cd test'
    type_cmd "copy f:$fix.TXT $fix.TXT"
    sleep 1
    type_cmd 'cls'
    type_cmd 'dir'
    # Match the dir row "name +ext" in the lowercased display so command
    # echoes cannot match.
    wait_row "${fix:l}" 'txt' 12 || fail "d1: fixture $fix.TXT missing in A:\\TEST" "$s"
    before=$(d1_names "$s")
    type_cmd "type $fix.TXT"
    wait_screen 'D1FIXTURE' 8 || fail 'd1: Human68k fixture content mismatch' "$s"
    type_cmd 'cd ..'

    print '== D1 boot SSOS-9 =='
    boot_ssos || fail 'd1: ssos9 boot failed'
    require 'd1 banner' "$s" 'SSOS-9: minimal X68000 CLI'

    if [[ -f "${XEI_J_INI:h}/profile.json" ]] &&
            ! grep -Eiq '^(sc|si|se)6=.+$' "$XEI_J_INI"; then
        type_cmd 'smount 6'
        wait_command_result 'smount 6' 'Unable to mount FAT16.' 15 ||
            fail 'd1: unconnected SCSI target was not rejected' "$s"
        print 'PASS d1: unconnected SCSI target rejected'
    fi

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
    local command="swrite test/$base.txt hello"
    type_cmd "$command"
    wait_command_result "$command" 'File written.' 30 ||
        fail 'd1: create did not report success and return to prompt' "$s"
    command="sread test/$base.txt"
    type_cmd "$command"
    wait_command_result "$command" hello 15 || fail 'd1: first read did not return hello' "$s"
    command="sreplace test/$base.txt world"
    type_cmd "$command"
    wait_command_result "$command" 'File written.' 30 ||
        fail 'd1: replace did not report success and return to prompt' "$s"
    command="sread test/$base.txt"
    type_cmd "$command"
    wait_command_result "$command" world 15 || fail 'd1: second read did not return world' "$s"
    print 'PASS d1: create/read/replace/read inside SSOS-9'

    print '== D1 verify from Human68k after return =='
    type_cmd 'exit'
    wait_screen 'SSOS-9 terminated.' 15 || fail 'd1: SSOS-9 did not terminate' "$s"
    require 'd1 return' "$s" 'A:\>'
    # Human68k caches directory sectors in its track buffer, so entries that
    # SSOS-9 wrote behind the cache stay invisible to dir/type until the cache
    # is dropped. Reboot the guest via the pipe's reset command and verify on
    # the fresh Human68k.
    send_control "reset"
    wait_screen 'HIOCS' 30 || fail 'd1: guest did not start rebooting' "$s"
    wait_screen 'A:\>' 150 || fail 'd1: Human68k did not come back' "$s"
    sleep 3
    type_cmd 'keywitch -r'
    sleep 1
    type_cmd 'cls'
    type_cmd 'cd test'
    type_cmd 'dir'
    # SSOS-9 stores the 8.3 name uppercase; Human68k keeps its own case.
    wait_row "$base" 'txt' 12 || fail 'd1: new file missing in A:\TEST after reset' "$s"
    print -r -- "$s" | tr '\r' '\n' | grep -Eiq "^${fix}[ ]+txt" ||
        fail 'd1: fixture missing in A:\TEST after reset' "$s"
    after=$(d1_names "$s" | grep -Eiv "^$base\.")
    [[ "$after" == "$before" ]] || fail 'd1: A:\TEST listing changed beyond the new file' \
        "before: $before"$'\n'"after: $after"
    type_cmd "type $upper.TXT"
    wait_line 'world' 12 || fail 'd1: Human68k does not see world' "$s"
    type_cmd 'cd ..'
    print -r -- "$s" | grep -Fq 'hello' && fail 'd1: old content still visible after replace' "$s"
    wait_line 'A:\\>' 10 || fail 'd1: Human68k did not return to the root directory' "$s"
    print 'PASS d1: Human68k sees world and the A:\TEST listing is unchanged'
    print 'PASS d1'
    return 0
}

run_e3() {
    snap
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
    snap
    print -r -- "$s" | grep -Eq 'switch task=[0-9]+ other=[0-9]+ value=000000[0-9A-F]{2}' || fail 'e3: no Timer D switch in trace' "$s"
    print -r -- "$s" | grep -E 'switch task=[0-9]+ other=[0-9]+ value=000000[0-9A-F]{2}'
    print 'PASS e3: trace has a Timer D switch'

    type_cmd 'exit'
    sleep 3
    snap
    require 'e3 exit' "$s" 'SSOS-9 terminated.'
    require 'e3 return' "$s" 'A:\>'
    print 'PASS e3'
}

run_b2() {
    snap
    require 'b2 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'b2: ssos9 boot failed'
    type_cmd 'run counter&'
    wait_screen 'Started module as PID' 8 || fail 'b2: counter did not start' "$s"
    local pid=$(started_pid "$s")
    [[ -n $pid ]] || fail 'b2: cannot parse counter PID' "$s"
    type_cmd 'trace'
    # The 64-entry trace scrolls the header off the physical text screen.
    # Inspect the visible tail and require the command to have completed.
    wait_line '> ' 10 || fail 'b2: trace did not return to the prompt' "$s"
    local events=$(print -r -- "$s" | tr '\r' '\n' |
        grep -cE '^t=[0-9A-F]{8} (create|start|sleep|wake|switch|exit) task=[0-9]+ other=[0-9]+ value=[0-9A-F]{8}')
    (( events >= 10 )) || fail "b2: only $events typed events in trace" "$s"
    print -r -- "$s" | grep -Fq ' switch task=' || fail 'b2: no switch event in trace' "$s"
    print "b2: $events scheduler events with non-empty kinds"
    type_cmd "kill $pid"
    wait_screen 'Kill requested.' 8 || fail "kill PID $pid was not accepted" "$s"
    type_cmd "wait $pid"
    wait_screen "PID $pid exited with status " 8 || fail 'b2: counter was not reaped' "$s"
    ssos_exit
    print 'PASS b2: trace shows scheduler events while counter ran'
}

run_b3() {
    snap
    require 'b3 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'b3: ssos9 boot failed'
    type_cmd 'run counter&'
    wait_screen 'Started module as PID' 8 || fail 'b3: counter did not start' "$s"
    local pid=$(started_pid "$s")
    type_cmd 'debug'
    sleep 1
    snap
    require 'b3 debug1' "$s" 'ticks='
    local n1=$(ticks_of "$s")
    sleep 6
    type_cmd 'debug'
    sleep 1
    snap
    require 'b3 debug2' "$s" 'ticks='
    local n2=$(ticks_of "$s")
    (( n2 - n1 > 400 )) || fail "b3: ticks advanced only by $((n2 - n1)) while the shell stayed responsive" "$s"
    print "b3: ticks advanced by $((n2 - n1)) with the spinner running"
    type_cmd 'ps'
    wait_screen 'PID PPID STATE NAME MODULE DATA GOT STACK' 8 ||
        fail 'b3: task table did not appear' "$s"
    if ! screen_line "$s" '> '; then
        wait_line '> ' 8 || fail 'b3: task table command did not complete' "$s"
    fi
    print -r -- "$s" | grep -Eq " counter " || fail 'b3: counter task not listed' "$s"
    print 'b3: shell kept typing/debug/ps while counter spun (preemptive)'
    type_checked_cmd "kill $pid"
    wait_screen 'Kill requested.' 8 || fail "kill PID $pid was not accepted" "$s"
    type_checked_cmd "wait $pid"
    wait_screen "PID $pid exited with status " 8 || fail 'b3: counter was not reaped' "$s"
    ssos_exit
    print 'PASS b3'
}

arena_after() {
    print -r -- "$1" | awk -v marker="$2" '
        index($0, marker) { seen = 1 }
        seen && match($0, /arena free_bytes=[0-9]+ largest_free=[0-9]+/) {
            value = substr($0, RSTART, RLENGTH)
        }
        END { if (value != "") print value }'
}

wait_arena_after() {
    local marker=$1 deadline=$((SECONDS + $2)) gap=0.5
    while (( SECONDS < deadline )); do
        snap
        REPLY_ARENA=$(arena_after "$s" "$marker")
        [[ -z "$REPLY_ARENA" ]] || return 0
        sleep $gap
        (( gap < 2 )) && gap=$(( gap * 2 ))
    done
    return 1
}

run_b4() {
    snap
    require 'b4 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'b4: ssos9 boot failed'
    type_cmd 'mem'
    sleep 1
    snap
    local base=$(print -r -- "$s" | grep -o 'arena free_bytes=[0-9]* largest_free=[0-9]*' | tail -1)
    [[ -n $base ]] || fail 'b4: no arena baseline' "$s"
    type_cmd 'run counter'
    sleep 2 # foreground: the shell blocks while counter spins
    send_control "presskey ctrl"; sleep 0.2
    send_control "typekey c"; sleep 0.2
    send_control "releasekey ctrl"; sleep 0.2
    wait_line '\^C' 10 || fail 'b4: Ctrl-C not observed' "$s"
    wait_screen 'interrupted' 8 || fail 'b4: foreground task not interrupted' "$s"
    wait_line '> ' 8 || fail 'b4: prompt missing after interrupt' "$s"
    type_cmd 'mem'
    wait_arena_after 'interrupted' 8 || fail 'b4: no arena sample after interruption' "$s"
    [[ "$REPLY_ARENA" == "$base" ]] || fail 'b4: arena did not return to baseline' \
        "baseline: $base"$'\n'"after: $REPLY_ARENA"
    ssos_exit
    print "PASS b4: Ctrl-C reaped the spinner; arena back to $base"
}

HELP_EXPECTED='Commands: help, uptime, ps, modules, mem, run NAME [ARGS...] [&], load DRIVE FILE, unload NAME, kill PID, wait PID, smount ID [START [BLOCKS]], swrite FILE TEXT, sreplace FILE TEXT, sread FILE, debug, trace, clear, version, mux, exit'

run_c1() {
    snap
    require 'c1 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'c1: ssos9 boot failed'
    type_cmd 'help'
    sleep 2
    snap
    local got=$(norm_text "$s")
    local want=$(norm_text "$HELP_EXPECTED")
    [[ "$got" == *"$want"* ]] || fail 'c1: help text differs from the cli.c command table' "$s"
    # uptime/modules/clear had never run in any area; spot-check them here
    # so every command in the help table has at least one real-IOCS pass.
    type_cmd 'uptime'
    wait_screen 'worker wakes: ' 8 || fail 'c1: uptime header missing' "$s"
    print -r -- "$s" | grep -Eq 'worker wakes: [0-9]+' ||
        fail 'c1: uptime count missing' "$s"
    type_cmd 'modules'
    wait_screen 'REFS' 8 || fail 'c1: modules header missing' "$s"
    # The builtin registry image is counter; hello is resolved by the run
    # fallback, not registered as a shared image.
    require 'c1 modules builtin counter row' "$s" 'counter builtin'
    type_cmd 'clear'
    sleep 1
    snap
    print -r -- "$s" | grep -Fq 'Commands:' &&
        fail 'c1: clear left the previous output on screen' "$s"
    type_cmd 'version'
    wait_screen 'SSOS-9 CLI preemptive minimum' 8 ||
        fail 'c1: version missing after clear' "$s"
    require 'c1 prompt alive' "$s" '>'
    ssos_exit
    print 'PASS c1: help/uptime/modules/clear/version match the command table'
}

run_c6() {
    # E2 emulator half: BS/cursor/DEL key delivery through real IOCS and
    # the line-editor echo (the editor logic itself is unit-covered, and
    # the 0x08/0x7f branches also run through the real shell_loop in QEMU
    # t15). Findings 2026-11-21: typekey bs delivers; the control pipe has
    # no key name for cursor keys or DEL (instant typekey del and held
    # presskey/releasekey del reached nothing), but real host key events
    # deliver them — measured codes: left 0x3B00, right 0x3D00, DEL
    # 0x3700 — and the shell editor consumes them since the same day.
    snap
    require 'c6 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'c6: ssos9 boot failed'
    # "helq" + BS deletes the q; "p" then completes "help".
    type_text 'helq'
    key bs
    type_text 'p'
    key return
    sleep 2
    snap
    local want=$(norm_text "$HELP_EXPECTED")
    [[ "$(norm_text "$s")" == *"$want"* ]] ||
        fail 'c6: BS did not delete the trailing character' "$s"
    # Double deletion mid-word: "versioz" + BS + BS + "on" -> "version".
    type_text 'versioz'
    key bs
    key bs
    type_text 'on'
    key return
    sleep 2
    snap
    print -r -- "$s" | grep -Fq 'SSOS-9 CLI preemptive minimum' ||
        fail 'c6: double BS did not edit the line into version' "$s"
    # Clear the preceding version result so only this edit can satisfy the check.
    type_cmd 'clear'
    local clear_deadline=$(( SECONDS + 8 ))
    while true; do
        snap
        if screen_line "$s" '> ' &&
            ! print -r -- "$s" | grep -Fq 'SSOS-9 CLI preemptive minimum'; then
            break
        fi
        (( SECONDS < clear_deadline )) ||
            fail 'c6: clear did not return to a fresh prompt' "$s"
        sleep 0.25
    done
    # Wait for the FIFO input to reach the guest before sending host events.
    type_text 'version'
    wait_line '> version' 8 || fail 'c6: initial version echo missing' "$s"
    # LEFT twice then RIGHT puts the cursor before the final n.
    host_keycode 123
    host_keycode 123
    host_keycode 124
    type_text 'z'
    wait_line '> versiozn' 8 || fail 'c6: cursor movement or insertion echo differs' "$s"
    host_keycode 117
    wait_line '> versioz' 8 || fail 'c6: forward delete echo differs' "$s"
    key bs
    type_text 'n'
    wait_line '> version' 8 || fail 'c6: final version echo differs' "$s"
    key return
    wait_command_result 'version' 'SSOS-9 CLI preemptive minimum' 10 ||
        fail 'c6: cursor keys and DEL did not edit the line' "$s"
    # The mux caps its width at 80 columns. Include the two prompt
    # columns to place z at the right edge and x on the next row.
    type_cmd 'mux'
    wait_screen 'Multiplexer:' 8 || fail 'c6: mux did not start' "$s"
    type_cmd 'clear'
    local wrap_prefix
    wrap_prefix=$(printf 'version%70sz' '')
    type_text "${wrap_prefix}x"
    wait_line '> '"$wrap_prefix" 10 || fail 'c6: wrapped first row differs' "$s"
    wait_line 'x' 8 || fail 'c6: wrapped second row differs' "$s"
    host_keycode 123
    host_keycode 123
    host_keycode 124
    host_keycode 117
    key bs
    local edit_deadline=$(( SECONDS + 8 ))
    while true; do
        snap
        if screen_line "$s" '> version' &&
            ! screen_line "$s" 'x' && ! screen_line "$s" '> '"$wrap_prefix"; then
            break
        fi
        (( SECONDS < edit_deadline )) ||
            fail 'c6: wrap-boundary edit did not erase both cells' "$s"
        sleep 0.25
    done
    key return
    wait_command_result 'version' 'SSOS-9 CLI preemptive minimum' 10 ||
        fail 'c6: wrapped command did not execute' "$s"
    require 'c6 prompt alive' "$s" '>'
    ssos_exit
    print 'PASS c6: BS, left/right cursor keys, and DEL, including mux line wrapping'
}

run_c2() {
    snap
    require 'c2 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'c2: ssos9 boot failed'
    local i
    for ((i = 0; i < 9; i++)); do type_cmd 'ps'; done # reaches the 31st console row
    # The console keeps an internal line window and redraws: the prompt
    # stays visible on the last console row and the function-key row stays
    # the last screen row. (The never-written bottom-right cell cannot be
    # distinguished from blank padding in a text copy; that part of C2
    # stays a manual check.)
    wait_line '> ' 15 || fail 'c2: prompt missing after long output' "$s"
    # wait_line matches a mid-burst frame while the guest is still draining
    # the typed ps burst; settle until the prompt reaches the last console
    # row before asserting on the captured rows.
    local rows settled=0 deadline=$(( SECONDS + 15 ))
    while (( SECONDS < deadline )); do
        snap
        rows=("${(@f)$(print -r -- "$s")}")
        if print -r -- "${rows[-2]}" | grep -Fq '> '; then
            settled=1
            break
        fi
        sleep 0.5
    done
    (( settled )) || fail 'c2: prompt did not settle on the last console row' "$s"
    local last=${rows[-1]}
    local prompt_row=${rows[-2]}
    print -r -- "$prompt_row" | grep -Fq '> ' || \
        fail 'c2: prompt is not on the last console row' "$s"
    print -r -- "$last" | grep -Eq '(C|S)[0-9]' || \
        fail 'c2: function-key row is not the last screen row' "$s"
    require 'c2 no errors' "$s" '>'
    ssos_exit
    print 'PASS c2: prompt and function-key row remained visible at 31-row depth'
}

run_c3() {
    snap
    require 'c3 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'c3: ssos9 boot failed'
    type_cmd 'mux'
    wait_screen 'Multiplexer:' 8 || fail 'c3: mux did not start' "$s"
    key f2
    sleep 1.5
    type_cmd 'help'
    sleep 2
    snap
    [[ "$(norm_text "$s")" == *"$(norm_text "$HELP_EXPECTED")"* ]] || \
        fail 'c3: window 2 did not run help' "$s"
    key f3
    sleep 1.5
    type_cmd 'ps'
    sleep 2
    snap
    require 'c3 window 3 ps' "$s" 'PID PPID STATE NAME MODULE DATA GOT STACK'
    require 'c3 window 3 shell row' "$s" 'shell'
    key f1
    sleep 1.5
    snap
    print -r -- "$s" | grep -Fq 'Multiplexer:' || fail 'c3: window 1 content not restored' "$s"
    local want=$(norm_text "$HELP_EXPECTED")
    [[ "$(norm_text "$s")" != *"$want"* ]] || fail 'c3: window 1 shows window 2 output' "$s"
    key f2
    sleep 1.5
    snap
    [[ "$(norm_text "$s")" == *"$want"* ]] || fail 'c3: window 2 did not retain help output' "$s"
    key f1
    sleep 1
    ssos_exit
    print 'PASS c3: per-window output retained and restored across F1/F2/F3'
}

run_c4() {
    snap
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
    snap
    print -r -- "$s" | grep -Fq 'arena free_bytes=' && fail 'c4: window 1 shows window 2 output' "$s"
    # Use the supported XEiJ path: function keys select mux windows directly.
    key f3
    sleep 1.5
    type_cmd 'ps'
    wait_screen 'PID PPID STATE NAME MODULE DATA GOT STACK' 8 || fail 'c4: window 3 did not run ps' "$s"
    key f2
    sleep 1.5
    snap
    print -r -- "$s" | grep -Fq 'arena free_bytes=' || fail 'c4: window 2 output was not retained' "$s"
    key f1
    sleep 1
    ssos_exit
    print 'PASS c4: F1/F2/F3 select the intended mux windows and retain output'
}

run_c5() {
    local console_module=tests/target/build/console.s9m
    [[ -f "$console_module" ]] || fail 'c5: missing tests/target/build/console.s9m (run make test)'
    snap
    print -r -- "$s" | grep -Fq 'A:\>' || fail 'c5: initial Human68k prompt missing' "$s"
    h68_prep
    place_in_test "$console_module" CONSOLE.S9M
    boot_mount
    type_cmd 'run test/console'
    sleep 2
    snap
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
    snap
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
    snap
    print -r -- "$s" | grep -Fq 'A:\>' || fail 'd2: initial Human68k prompt missing' "$s"
    h68_prep
    place_in_test "$edit_module" EDIT.S9M
    boot_mount
    print '== d2 create and save a new file with edit =='
    type_cmd "run test/edit test/$name.TXT"
    wait_screen 'Cannot read file.' 10 || fail 'd2: editor did not prompt for a new file' "$s"
    key n
    sleep 1.5
    snap
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
    snap
    print -r -- "$s" | grep -Fq 'd2line1' || fail 'd2: reopened file lacks d2line1' "$s"
    print -r -- "$s" | grep -Fq 'd2line2' || fail 'd2: reopened file lacks d2line2' "$s"
    type_cmd ':q!'
    wait_line '> ' 10 || fail 'd2: no prompt after :q!' "$s"
    ssos_exit
    print '== d2 verify from Human68k after reset =='
    guest_reset
    type_cmd 'cd test'
    type_cmd "type $name.TXT"
    wait_line 'd2line1' 10 || fail 'd2: Human68k cannot read the saved file' "$s"
    print -r -- "$s" | grep -Fq 'd2line2' || fail 'd2: Human68k file lacks d2line2' "$s"
    type_cmd 'cd ..'
    h68_del "${(U)name}.TXT"
    h68_absent "$name" 'txt' 12
    print "PASS d2: edit saved and reloaded A:\\TEST\\$name.TXT"
}

wait_host_file() { # wait_host_file <case-insensitive NAME.EXT> <timeout>; sets $host_file
    local want=${1:l} tmo=$2 waited=0 name
    while (( waited < tmo )); do
        host_file=''
        for name in "$XEI_J_STAGE"/*(N); do
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
    cp tests/asm/link/start.s "$XEI_J_STAGE/SA$suf.S"
    cp tests/asm/link/main.s "$XEI_J_STAGE/MB$suf.S"
    cp tests/asm/link/obj.s "$XEI_J_STAGE/OB$suf.S"
    place_in_test "$XEI_J_STAGE/SA$suf.S" "SA$suf.S"
    place_in_test "$XEI_J_STAGE/MB$suf.S" "MB$suf.S"
    place_in_test "$XEI_J_STAGE/OB$suf.S" "OB$suf.S"
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
    snap
    print -r -- "$s" | grep -Fq 'A:\>' || fail 'd3: initial Human68k prompt missing' "$s"
    h68_prep
    place_in_test "$asm_module" ASM.S9M
    d3_place_and_asm "$suf"
    ssos_exit
    print '== d3 fetch objects through F: after reset =='
    guest_reset
    type_cmd 'cd test'
    type_cmd "copy sa$suf.o f:"
    type_cmd "copy mb$suf.o f:"
    type_cmd "copy ob$suf.o f:"
    sleep 2
    type_cmd 'cd ..'
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
    h68_del "SA${(U)suf}.S" "MB${(U)suf}.S" "OB${(U)suf}.S" \
        "SA${(U)suf}.O" "MB${(U)suf}.O" "OB${(U)suf}.O"
    h68_absent "sa$suf" 'o' 12
    h68_absent "ob$suf" 'o' 12
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
    snap
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
    type_cmd 'cd test'
    type_cmd "copy $prog.s9m f:"
    sleep 2
    type_cmd 'cd ..'
    tests/build/asm-host tests/asm/link/start.s "$exp/SA$suf.O"
    tests/build/asm-host tests/asm/link/main.s "$exp/MB$suf.O"
    tests/build/asm-host tests/asm/link/obj.s "$exp/OB$suf.O"
    # The guest types the output name unshifted (lowercase), and ld embeds
    # the module name in the image, so the golden must use the same case.
    tests/build/link-host "$exp/${prog:l}.s9m" "$exp/SA$suf.O" "$exp/MB$suf.O" "$exp/OB$suf.O" || \
        fail 'd4: host link of the expected image failed'
    wait_host_file "$prog.s9m" 20 || fail "d4: $prog.S9M not copied to F: (~/tmp)"
    cmp -s "$host_file" "$exp/${prog:l}.s9m" || \
        fail "d4: guest ${host_file:t} differs from the host golden" \
            "$(cmp "$host_file" "$exp/${prog:l}.s9m" 2>&1 | head -2)"
    rm -rf "$exp"
    h68_del "SA${(U)suf}.S" "MB${(U)suf}.S" "OB${(U)suf}.S" \
        "SA${(U)suf}.O" "MB${(U)suf}.O" "OB${(U)suf}.O" "${(U)prog}.S9M"
    h68_absent "sa$suf" 'o' 12
    h68_absent "$prog" 's9m' 12
    print "PASS d4: ld + run fallback produced the golden image; status 27 and LNK"
}

run_f1() {
    snap
    require 'f1 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'f1: ssos9 boot failed'
    type_cmd 'mem'
    sleep 1
    snap
    local base=$(print -r -- "$s" | grep -o 'arena free_bytes=[0-9]* largest_free=[0-9]*' | tail -1)
    [[ -n $base ]] || fail 'f1: no arena baseline' "$s"
    # Each trailing-& builtin runs in an isolated kernel-entry child whose
    # output interleaves on the same console; the IOCS lock keeps lines whole.
    type_cmd 'help&'
    wait_new_pid 0 8 || fail 'f1: help& did not start' "$s"
    local p1=$REPLY_PID
    type_cmd 'ps&'
    wait_new_pid $p1 8 || fail 'f1: ps& did not start' "$s"
    local p2=$REPLY_PID
    type_cmd 'mem&'
    wait_new_pid $p2 8 || fail 'f1: mem& did not start' "$s"
    local p3=$REPLY_PID
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
        wait_screen "PID $pid exited with status " 8 || fail "f1: PID $pid was not reaped" "$s"
    done
    type_cmd 'mem'
    sleep 1
    snap
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
    snap
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
        snap
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
    wait_line "PID $pid exited with status 0" 45 || fail 'f2: bg module did not finish cleanly' "$s"
    (( max3 >= 30 )) || fail 'f2: too few records survived the switches' "$s"
    ssos_exit
    print 'PASS f2: background output continued across window switches'
}

run_g1() {
    snap
    require 'g1 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'g1: ssos9 boot failed'
    type_cmd 'mem'
    sleep 1
    snap
    local base=$(print -r -- "$s" | grep -o 'arena free_bytes=[0-9]* largest_free=[0-9]*' | tail -1)
    [[ -n $base ]] || fail 'g1: no arena baseline' "$s"
    local i pid last=0
    for ((i = 1; i <= 5; i++)); do
        type_cmd 'run counter&'
        wait_new_pid $last 8 || fail "g1: cycle $i spawn failed" "$s"
        pid=$REPLY_PID
        [[ -n $pid ]] || fail "g1: cycle $i PID parse failed" "$s"
        last=$pid
        type_cmd "kill $pid"
        wait_screen 'Kill requested.' 8 || fail "g1: cycle $i kill failed" "$s"
        type_cmd "wait $pid"
        wait_screen "PID $pid exited with status " 8 || fail "g1: cycle $i reap failed" "$s"
        print "g1: cycle $i spawn/kill/wait reaped PID $pid"
    done
    type_cmd 'mem'
    sleep 1
    snap
    local after=$(print -r -- "$s" | grep -o 'arena free_bytes=[0-9]* largest_free=[0-9]*' | tail -1)
    [[ "$after" == "$base" ]] || fail 'g1: arena did not return to baseline' \
        "baseline: $base"$'\n'"after: $after"
    ssos_exit
    print "PASS g1: five spawn/kill/wait cycles; arena back to $base"
}

# Simplified t14-style sources typed through the editor (lowercase, no
# comments): START entry calls main and exits with its status, MAIN prints
# "lnk" and returns base(7) + factor(10) doubled by helper = 27, OBJ holds
# helper and factor. The assembler/linker are case-insensitive on mnemonics
# and symbols, so the typed lowercase form links and runs identically.
E1_START='.text
.globl _start
.extern main
_start:
bsr.w main
move.l %d0, %d1
moveq #4, %d0
trap #0'

E1_OBJ='.text
.globl helper
helper:
add.l factor(%a5), %d0
rts
.data
.globl factor
factor:
.dc.l 10'

E1_MAIN='.text
.globl main
.extern helper
.extern factor
main:
move.l base_val(%a5), %d0
add.l factor(%a5), %d0
bsr.w helper
move.l %d0, result(%a5)
lea msg(%pc), %a0
move.l %a0, wt_buf(%a5)
lea wt(%a5), %a1
move.l %a1, %d1
moveq #35, %d0
trap #0
move.l result(%a5), %d0
rts
msg:
.ascii "lnk\n"
.data
base_val:
.dc.l 7
wt:
.dc.l 1
wt_buf:
.dc.l 0
wt_len:
.dc.l 4
.bss
result:
.space 4'

run_e1() {
    local asm_module=apps/asm/build/asm.s9m ld_module=apps/link/build/ld.s9m
    [[ -f "$asm_module" && -f "$ld_module" ]] || fail 'e1: missing asm/ld module images (run make all)'
    local suf=$(printf '%04x' $(( $(date +%s) % 65536 )))
    local srcs="e1s$suf" main="e1m$suf" obj="e1o$suf" prog="e1p$suf"
    local base after
    snap
    print -r -- "$s" | grep -Fq 'A:\>' || fail 'e1: initial Human68k prompt missing' "$s"
    h68_prep
    place_in_test "$asm_module" ASM.S9M
    place_in_test "$ld_module" LD.S9M
    boot_mount
    type_cmd 'mux'
    wait_screen 'Multiplexer:' 8 || fail 'e1: mux did not start' "$s"
    key f1
    sleep 1.5
    type_cmd 'mem' # baseline after the mux shells exist, so the comparison
    wait_screen 'arena free_bytes=' 8 || fail 'e1: no arena baseline' "$s"
    snap
    base=$(print -r -- "$s" | grep -o 'arena free_bytes=[0-9]* largest_free=[0-9]*' | tail -1)
    [[ -n $base ]] || fail 'e1: cannot parse arena baseline' "$s"

    print '== E1 window 1: create three sources with edit =='
    local stem text
    for stem in $srcs $main $obj; do
        case $stem in
            ${srcs}) text=$E1_START ;;
            ${main}) text=$E1_MAIN ;;
            ${obj}) text=$E1_OBJ ;;
        esac
        type_cmd "run test/edit test/$stem.s"
        wait_screen 'Cannot read file.' 12 || fail "e1: editor did not prompt for $stem.s" "$s"
        key n
        sleep 1.5
        key i
        sleep 0.5
        type_editor_lines "$text"
        key esc
        sleep 1
        type_cmd ':wq'
        wait_screen 'exited with status 0' 30 || fail "e1: editor did not save $stem.s" "$s"
        wait_line '> ' 10 || fail "e1: no prompt after saving $stem.s" "$s"
        print "e1: created $stem.s via edit"
    done

    print '== E1 window 2: asm x3 + ld =='
    key f2
    sleep 1.5
    type_cmd "run test/asm test/$srcs.s test/$srcs.o"
    wait_screen "Wrote test/$srcs.o" 30 || fail "e1: asm $srcs failed" "$s"
    wait_line '> ' 10 || fail "e1: no prompt after asm $srcs" "$s"
    type_cmd "run test/asm test/$main.s test/$main.o"
    wait_screen "Wrote test/$main.o" 30 || fail "e1: asm $main failed" "$s"
    wait_line '> ' 10 || fail "e1: no prompt after asm $main" "$s"
    type_cmd "run test/asm test/$obj.s test/$obj.o"
    wait_screen "Wrote test/$obj.o" 30 || fail "e1: asm $obj failed" "$s"
    wait_line '> ' 10 || fail "e1: no prompt after asm $obj" "$s"
    type_cmd "run test/ld test/$prog.s9m test/$srcs.o test/$main.o test/$obj.o"
    wait_screen "Wrote test/$prog.s9m" 30 || fail 'e1: ld did not write the image' "$s"
    wait_line '> ' 10 || fail 'e1: no prompt after ld' "$s"
    print 'e1: assembled and linked on window 2'

    print '== E1 window 3: run the linked program =='
    key f3
    sleep 1.5
    type_cmd "run test/$prog"
    wait_screen 'lnk' 15 || fail 'e1: linked program did not print lnk' "$s"
    wait_screen 'exited with status 27' 10 || fail 'e1: program did not exit 27' "$s"
    print 'e1: program reaped with status 27 and lnk output'

    print '== E1 window 1: unload the registered images, then compare =='
    key f1
    sleep 1.5
    # Foreground `run` resolves through the disk fallback and keeps the
    # registered image (edit/asm/ld plus the linked program) after the
    # process exits; unload them so the arena is comparable again.
    local mod
    for mod in edit asm ld $prog; do
        type_cmd "unload $mod"
        wait_screen 'Module unloaded.' 8 || fail "e1: unload $mod failed" "$s"
        sleep 0.3
    done
    type_cmd 'mem'
    sleep 1
    snap
    after=$(print -r -- "$s" | grep -o 'arena free_bytes=[0-9]* largest_free=[0-9]*' | tail -1)
    [[ "$after" == "$base" ]] || fail 'e1: arena did not return to baseline' \
        "baseline: $base"$'\n'"after: $after"
    ssos_exit
    print '== E1 cleanup on Human68k =='
    guest_reset
    h68_del "${(U)srcs}.S" "${(U)main}.S" "${(U)obj}.S" \
        "${(U)srcs}.O" "${(U)main}.O" "${(U)obj}.O" "${(U)prog}.S9M"
    h68_absent "${srcs}" 's' 12
    h68_absent "${main}" 'o' 12
    print "PASS e1: edit->asm->ld->run across mux windows; lnk, status 27, arena $base"
}

run_e2() {
    local asm_module=apps/asm/build/asm.s9m ld_module=apps/link/build/ld.s9m
    [[ -f "$asm_module" && -f "$ld_module" ]] || fail 'e2: missing asm/ld module images (run make all)'
    local suf=$(printf '%04x' $(( ($(date +%s) + 11) % 65536 )))
    local bad="e2b$suf" prog="e2p$suf"
    snap
    print -r -- "$s" | grep -Fq 'A:\>' || fail 'e2: initial Human68k prompt missing' "$s"
    h68_prep
    place_in_test "$asm_module" ASM.S9M
    place_in_test "$ld_module" LD.S9M
    cp tests/asm/link/badmain.s "$XEI_J_STAGE/$bad.S"
    place_in_test "$XEI_J_STAGE/$bad.S" "${(U)bad}.S"
    boot_mount
    type_cmd "run test/asm test/$bad.s test/$bad.o"
    wait_screen "Wrote test/$bad.o" 30 || fail 'e2: asm of the negative sample failed' "$s"
    wait_line '> ' 10 || fail 'e2: no prompt after asm' "$s"
    print '== E2 ld must reject the undefined symbol =='
    type_cmd "run test/ld test/$prog.s9m test/$bad.o"
    wait_screen 'Undefined symbol missing' 20 || fail 'e2: undefined symbol not reported' "$s"
    wait_screen 'exited with status 1' 10 || fail 'e2: ld did not exit nonzero' "$s"
    wait_line '> ' 10 || fail 'e2: no prompt after the failed ld' "$s"
    print 'e2: ld reported Undefined symbol missing and exited 1'
    ssos_exit
    print '== E2 verify the output file was not created =='
    guest_reset
    h68_absent "$prog" 's9m' 12
    h68_del "${(U)bad}.S" "${(U)bad}.O"
    h68_absent "$bad" 'o' 12
    print 'PASS e2: undefined symbol rejected, no output file, artifacts cleaned'
}

run_h3() {
    snap
    require 'h3 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'h3: ssos9 boot failed'
    type_cmd 'mux'
    wait_screen 'Multiplexer:' 8 || fail 'h3: mux did not start' "$s"
    key f2
    sleep 1.5
    type_cmd 'run counter' # foreground on window 2: its shell blocks
    sleep 2
    key f1
    sleep 1.5
    print '== H3 Ctrl-C on the active window only =='
    send_control "presskey ctrl"; sleep 0.2
    send_control "typekey c"; sleep 0.2
    send_control "releasekey ctrl"; sleep 0.5
    wait_screen '^C' 8 || fail 'h3: window 1 did not consume the Ctrl-C' "$s"
    print -r -- "$s" | grep -Fq 'interrupted' &&
        fail 'h3: Ctrl-C leaked to the hidden window' "$s"
    key f2
    sleep 1.5
    snap
    print -r -- "$s" | grep -Fq 'interrupted' &&
        fail 'h3: window 2 foreground was interrupted while hidden' "$s"
    print 'h3: hidden foreground survived; interrupting it on its window'
    send_control "presskey ctrl"; sleep 0.2
    send_control "typekey c"; sleep 0.2
    send_control "releasekey ctrl"; sleep 0.5
    wait_screen 'interrupted' 8 || fail 'h3: window 2 foreground not interrupted' "$s"
    wait_line '> ' 8 || fail 'h3: no prompt after interrupt' "$s"
    key f1
    sleep 1
    ssos_exit
    print 'PASS h3: Ctrl-C routes to the active window only'
}

run_h4() {
    local edit_module=apps/edit/build/edit.s9m
    [[ -f "$edit_module" ]] || fail 'h4: missing apps/edit/build/edit.s9m (run make all)'
    local id=$(printf '%05x' $(( $(date +%s) % 1048576 )))
    local empty="h4a$id" big="h4b$id"
    local bigline='xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx'
    snap
    require 'h4 initial prompt' "$s" 'A:\>'
    h68_prep
    place_in_test "$edit_module" EDIT.S9M
    boot_mount
    print '== H4 empty file: sread returns 0 bytes without error =='
    type_cmd "run test/edit test/$empty.TXT"
    wait_screen 'Cannot read file.' 10 || fail 'h4: editor did not prompt' "$s"
    key n
    sleep 1.5
    type_cmd ':wq' # save the empty buffer
    wait_screen 'exited with status 0' 25 || fail 'h4: empty save failed' "$s"
    wait_line '> ' 10 || fail 'h4: no prompt after :wq' "$s"
    type_cmd "sread test/$empty.TXT"
    # The typed extension arrives lowercase; match the stem only.
    wait_screen "sread test/$empty" 8 || fail 'h4: sread echo missing' "$s"
    sleep 1
    snap
    print -r -- "$s" | grep -Fq 'Unable to read' &&
        fail 'h4: empty file read reported an error' "$s"
    print 'h4: empty file read clean'
    print '== H4 280+ byte file: sread rejects beyond the 256-byte cap =='
    type_cmd "run test/edit test/$big.TXT"
    wait_screen 'Cannot read file.' 10 || fail 'h4: editor did not prompt (big)' "$s"
    key n
    sleep 1.5
    key i
    sleep 0.5
    local n
    for ((n = 0; n < 7; n++)); do
        type_editor_lines "$bigline"
    done
    key esc
    sleep 1
    type_cmd ':wq'
    wait_screen 'exited with status 0' 30 || fail 'h4: big save failed' "$s"
    wait_line '> ' 10 || fail 'h4: no prompt after big :wq' "$s"
    type_cmd "sread test/$big.TXT"
    wait_screen 'Unable to read file (maximum 256 bytes).' 10 ||
        fail 'h4: oversized read not rejected' "$s"
    wait_line '> ' 8 || fail 'h4: no prompt after rejection' "$s"
    ssos_exit
    guest_reset
    h68_del "H4A${(U)id}.TXT" "H4B${(U)id}.TXT"
    h68_absent "$empty" 'txt' 12
    h68_absent "$big" 'txt' 12
    print 'PASS h4: sread empty ok, >256 rejected, artifacts cleaned'
}

run_h5() {
    # XDF-IPL boot (no Human68k underneath) + the SCSI volume: place a
    # fixture in A:\TEST from Human68k first, then boot the XDF image via
    # ini fd0 (STD boots FD when inserted) and read the fixture with smount.
    [[ -f "$XEI_J_STAGE/ssos9.xdf" ]] || fail 'h5: missing ~/tmp/ssos9.xdf (run make install)'
    local fix="H5F$(printf '%05X' $(( $(date +%s) % 1048576 )))"
    [[ -f "$XEI_J_INI" ]] || fail 'h5: missing ~/XEiJ.ini'
    snap
    require 'h5 initial prompt' "$s" 'A:\>'
    printf 'H5FIXTURE' > "$XEI_J_STAGE/$fix.TXT"
    h68_prep
    place_in_test "$XEI_J_STAGE/$fix.TXT" "$fix.TXT"
    print '== H5 insert ssos9.xdf in FD0 and boot the XDF image =='
    restart_with_floppy 0 "$XEI_J_STAGE/ssos9.xdf"
    wait_screen 'SSOS-9: minimal X68000 CLI' 90 || fail 'h5: XDF boot banner missing' "$s"
    wait_line '> ' 15 || fail 'h5: no CLI prompt after IPL boot' "$s"
    print '== H5 mount the HDD and read the fixture =='
    type_cmd 'smount 0'
    wait_screen 'FAT16 mounted.' 15 || fail 'h5: smount 0 failed under XDF boot' "$s"
    type_cmd "sread test/$fix.TXT"
    wait_screen 'H5FIXTURE' 10 || fail 'h5: fixture not readable under XDF boot' "$s"
    type_cmd 'exit'
    wait_screen 'Cannot exit the initial SSOS-9 process (PID 1).' 10 ||
        fail 'h5: PID 1 exit not rejected under XDF boot' "$s"
    type_cmd 'version'
    wait_screen 'SSOS-9 CLI preemptive minimum' 8 ||
        fail 'h5: CLI did not accept a command after refusing exit' "$s"
    restore_xeij_ini || environment_fail 'h5: configuration restore failed'
    wait_screen 'A:\>' 150 || fail 'h5: Human68k did not come back' "$s"
    h68_del "$fix.TXT"
    h68_absent "$fix" 'txt' 12
    print 'PASS h5: XDF boot + smount + sread + PID 1 protection'
}

run_h6() {
    # Soak: 25 spawn/kill/wait cycles must return the arena to baseline.
    snap
    require 'h6 initial prompt' "$s" 'A:\>'
    boot_ssos || fail 'h6: ssos9 boot failed'
    type_cmd 'mem'
    sleep 1
    snap
    local base=$(print -r -- "$s" | grep -o 'arena free_bytes=[0-9]* largest_free=[0-9]*' | tail -1)
    [[ -n $base ]] || fail 'h6: no arena baseline' "$s"
    local i pid last=0
    for ((i = 1; i <= 25; i++)); do
        type_cmd 'run counter&'
        wait_new_pid $last 8 || fail "h6: cycle $i spawn failed" "$s"
        pid=$REPLY_PID
        [[ -n $pid ]] || fail "h6: cycle $i PID parse failed" "$s"
        last=$pid
        type_cmd "kill $pid"
        wait_screen 'Kill requested.' 8 || fail "h6: cycle $i kill failed" "$s"
        type_cmd "wait $pid"
        wait_screen "PID $pid exited with status " 8 || fail "h6: cycle $i reap failed" "$s"
        type_cmd 'mem'
        wait_arena_after "PID $pid exited with status " 8 ||
            fail "h6: cycle $i missing arena sample after reap" "$s"
        [[ "$REPLY_ARENA" == "$base" ]] ||
            fail "h6: cycle $i arena changed ($base -> $REPLY_ARENA)" "$s"
        if (( i % 5 == 0 )); then print "h6: $i cycles done"; fi
    done
    ssos_exit
    print "PASS h6: 25 cycles; arena back to $base"
}

# Host tests source the transport and cleanup functions without running a case.
[[ $ZSH_EVAL_CONTEXT == toplevel ]] || return 0

if [[ ${1:-} != --locked ]]; then
    exec python3 "$TRANSPORT" lock "$XEI_J_LOCK" zsh "$0" --locked "$@"
fi
shift
ini_backup=''
trap 'finish_case $?' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

XEI_J_PID=$(xeij_pids) || environment_fail 'cannot enumerate XEiJ processes'
[[ "$XEI_J_PID" == <-> ]] || environment_fail "expected one XEiJ process using $XEI_J_INI"
[[ "$(xeij_pids '*')" == "$XEI_J_PID" ]] || environment_fail 'multiple XEiJ processes share the control pipe'

backup_files=("$XEI_J_INI".ssos9.????????(N))
(( ${#backup_files} == 0 )) ||
    environment_fail "interrupted XEiJ configuration change; retained backup: ${backup_files[*]}"

case ${1:-} in
    a2) run_a2 ;;
    a3) run_a3 ;;
    b1) run_b1 ;;
    b2) run_b2 ;;
    b3) run_b3 ;;
    b4) run_b4 ;;
    c1) run_c1 ;;
    c2) run_c2 ;;
    c3) run_c3 ;;
    c4) run_c4 ;;
    c5) run_c5 ;;
    c6) run_c6 ;;
    d1) run_d1 ;;
    d2) run_d2 ;;
    d3) run_d3 ;;
    d4) run_d4 ;;
    e1) run_e1 ;;
    e2) run_e2 ;;
    e3) run_e3 ;;
    f1) run_f1 ;;
    f2) run_f2 ;;
    g1) run_g1 ;;
    h3) run_h3 ;;
    h5) run_h5 ;;
    h6) run_h6 ;;
    h4) run_h4 ;;
    *) fail 'usage: tools/emulator-itest.sh a2|a3|b1|b2|b3|b4|c1|c2|c3|c4|c5|d1|d2|d3|d4|e1|e2|e3|f1|f2|g1|h3|h4|h5|h6' ;;
esac
