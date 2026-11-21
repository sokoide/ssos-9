#!/bin/zsh
# H2: FD1 real-floppy `load` + `run` smoke test on the MonitorSocket MPX68K
# fork (oss/MPX68K @ 4fc9e85). This re-runs the manual procedure recorded in
# tests/README.md ("MPX68K FD1 automated smoke test (H2)") as a scripted
# runner with per-gate PASS/FAIL output and PNG+OCR evidence.
#
# Why MPX68K and not XEiJ: XEiJ cannot swap floppy media reliably while the
# guest is running, and H2 needs live MOUNTFDD/EJECTFDD of drive 1. The MPX68K
# fork exposes PAUSE / MOUNTFDD <drive 0-1> <image> / EJECTFDD <drive> /
# RESUME lines on the ~/mpx68k_monitor.sock Unix socket.
#
# Prerequisites (this script checks but never modifies them):
#   - /Applications/MPX68K.app already running, Human68k booted from FD0
#     (HUMAN302.xdf), sitting at the A: prompt. FD1 mounts as C:.
#   - ~/mpx68k_monitor.sock present (monitorSocketEnabled=true in
#     ~/Library/Preferences/NANKIN.X68000.plist; the plist itself is never
#     read or written by this script).
#   - The terminal holds macOS Accessibility permission (keystrokes) and
#     Screen Recording permission (screencapture), and MPX68K stays the
#     frontmost application for the whole run.
#   - Build artifacts: ssos/build/export/ssos9.x,
#     tests/target/build/console.s9m, apps/edit/build/edit.s9m, and the
#     template tests/target/build/console.xdf.
#   - python3 and mtools (mcopy via prepare_console_image.py); fsck.fat is
#     optional and only used for the post-run integrity check.
#
# Run from the repository root:  tools/mpx68k-h2.sh
#
# A dedicated image is created at build/mpx68k-h2/console-h2.xdf
# (tests/target/build/console.xdf is copied, never modified) and mounted in
# drive 1 for the whole run. Evidence PNGs with their OCR text land in
# build/mpx68k-h2/results-<timestamp>/. Gates:
#   G1  Human68k A: prompt visible; keywitch -r
#   G2  PAUSE / MOUNTFDD 1 <image> / RESUME over the monitor socket
#   G3  dir c: lists CONSOLE.S9M, EDIT.S9M, SS9CON.X
#   G4  c: + ss9con.x boots SSOS-9 (banner + prompt)
#   G5  M2: load 1 edit.s9m -> Module loaded. (3028 B = 3-cluster chain,
#       read to the end and CRC-verified by the loader at registration)
#   G6  load 1 console.s9m -> Module loaded.
#   G7  run console; key a reports KEY: 0061
#   G8  key q: PID 2 exited with status 0
#   G9  M3 negative: load 1 nosuch.s9m -> "Unable to load module: file or
#       cluster chain." and the prompt returns (no hang)
#   G10 M3 no-media: EJECTFDD 1, then load 1 console.s9m -> "Unable to load
#       module: disk or FAT12." and the prompt returns (no hang)
#   G11 exit -> SSOS-9 terminated. -> Human68k A: prompt
#
# Screen text comes from a small Swift + Vision helper written to /tmp and
# run with `swift`. If `swift` is missing, or the screen cannot be captured,
# the script saves the evidence PNG and exits 2 (UNVERIFIED) instead of
# reporting a failure: no gate can be judged without screen text, and an
# environment gap must not be mistaken for a test failure.
#
# Exit codes: 0 = PASS (all gates), 1 = FAIL (a gate or integrity check
# failed), 2 = UNVERIFIED (screen or OCR unavailable), 3 = ENVIRONMENT
# (prerequisites missing; the test did not run). Cleanup (EJECTFDD 1,
# image/template SHA-256 checks, fsck.fat -n) runs from the EXIT trap on
# every path.
set -u

ROOT=${0:A:h:h}
IMAGE_DIR="$ROOT/build/mpx68k-h2"
TEMPLATE="$ROOT/tests/target/build/console.xdf"
IMAGE="$IMAGE_DIR/console-h2.xdf"
LAUNCHER="$ROOT/ssos/build/export/ssos9.x"
CONSOLE="$ROOT/tests/target/build/console.s9m"
EDIT="$ROOT/apps/edit/build/edit.s9m"
PREP="$ROOT/tests/target/prepare_console_image.py"
MONITOR_SOCK="$HOME/mpx68k_monitor.sock"
APP=MPX68K
GATE_TOTAL=11
KEY_PACE=0.05
SAFE_CHAR_RE='^[a-zA-Z0-9 .:_-]$'
image_sha='' template_sha='' app_pid='' win_rect='' results_dir=''
ocr_helper='' gates_failed=''
integer mounted=0
typeset -a gates_passed=()
s=''   # OCR text of the last snap

environment_fail() { print -u2 -- "ENVIRONMENT: $1"; exit 3 }
gate_pass() {
    gates_passed+=("$1")
    print "PASS $1: $2"
}
gate_fail() { # gate_fail <id> <why> [screen-text]; reports and stops.
    gates_failed=$1
    print "FAIL $1: $2"
    print -u2 -- "FAIL: $1: $2"
    [[ $# -lt 3 ]] || print -u2 -r -- "$3"
    exit 1
}

monitor_cmd() { # monitor_cmd <LINE...>; one line over the monitor socket.
    # Pure: returns nonzero on failure so the EXIT trap can degrade to WARN.
    python3 - "$MONITOR_SOCK" "$@" <<'PY'
import socket, sys
path = sys.argv[1]
line = " ".join(sys.argv[2:])
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(5)
try:
    s.connect(path)
    s.sendall((line + "\n").encode())
    s.settimeout(1.0)
    out = b""
    try:
        while True:
            data = s.recv(4096)
            if not data:
                break
            out += data
    except socket.timeout:
        pass
    if out:
        sys.stderr.write(out.decode(errors="replace").strip() + "\n")
finally:
    s.close()
PY
}
mount_floppy() {
    monitor_cmd PAUSE && monitor_cmd "MOUNTFDD 1 $IMAGE" && monitor_cmd RESUME ||
        return 1
    mounted=1
}
eject_floppy() {
    (( mounted )) || return 0
    monitor_cmd PAUSE && monitor_cmd "EJECTFDD 1" && monitor_cmd RESUME ||
        return 1
    mounted=0
}
sha256_of() { shasum -a 256 "$1" 2>/dev/null | awk '{print $1}' }

window_rect() { # front the MPX68K window; print "x,y,w,h" for screencapture
    osascript - "$app_pid" <<'APPLESCRIPT'
on run argv
    set targetPID to item 1 of argv as integer
    tell application "System Events"
        set targetProcess to first application process whose unix id is targetPID
        set frontmost of targetProcess to true
        if not (frontmost of targetProcess) then error "MPX68K is not frontmost"
        tell targetProcess
            set p to position of window 1
            set z to size of window 1
        end tell
    end tell
    return ((item 1 of p) as text) & "," & ((item 2 of p) as text) & "," & ((item 1 of z) as text) & "," & ((item 2 of z) as text)
end run
APPLESCRIPT
}

write_ocr_helper() {
    local dir
    dir=$(mktemp -d /tmp/ssos9-h2.XXXXXXXX) || return 1
    ocr_helper="$dir/ocr.swift"
    cat > "$ocr_helper" <<'SWIFT'
import CoreGraphics
import Foundation
import ImageIO
import Vision

// usage: ocr <png>; prints recognized text lines top-to-bottom.
// exit 0 = text printed (possibly empty), nonzero = OCR unavailable.
let args = CommandLine.arguments
if args.count < 2 {
    FileHandle.standardError.write("usage: ocr <png>\n".data(using: .utf8)!)
    exit(3)
}
let url = URL(fileURLWithPath: args[1])
guard let src = CGImageSourceCreateWithURL(url as CFURL, nil),
      let cg = CGImageSourceCreateImageAtIndex(src, 0, nil) else {
    FileHandle.standardError.write("OCR_ERROR: cannot read image\n".data(using: .utf8)!)
    exit(3)
}
let request = VNRecognizeTextRequest()
request.recognitionLevel = .accurate
request.usesLanguageCorrection = false
request.recognitionLanguages = ["en-US"]
let handler = VNImageRequestHandler(cgImage: cg, options: [:])
do { try handler.perform([request]) } catch {
    FileHandle.standardError.write("OCR_ERROR: \(error)\n".data(using: .utf8)!)
    exit(3)
}
let observations = (request.results ?? []).sorted {
    $0.boundingBox.origin.y > $1.boundingBox.origin.y
}
print(observations.compactMap { $0.topCandidates(1).first?.string }
    .joined(separator: "\n"))
SWIFT
}

snap() { # snap <stem>; PNG + OCR text into results dir, text into $s. An
    # unavailable screen or OCR ends the run as UNVERIFIED (exit 2): gates
    # cannot be judged blind, and an environment gap must not read as FAIL.
    local stem=$1 png="$results_dir/$stem.png" text
    if ! screencapture -x -R"$win_rect" "$png" || [[ ! -s "$png" ]]; then
        print "UNVERIFIED: screencapture failed; evidence PNG missing ($stem)"
        print -u2 -- "UNVERIFIED: screencapture failed (Screen Recording permission?)"
        exit 2
    fi
    if ! text=$(swift "$ocr_helper" "$png" 2>>"$IMAGE_DIR/ocr-stderr.log"); then
        print "UNVERIFIED: OCR unavailable; evidence PNG at $png"
        print -u2 -- "UNVERIFIED: swift/Vision OCR failed (see $IMAGE_DIR/ocr-stderr.log)"
        exit 2
    fi
    printf '%s\n' "$text" > "${png:r}.txt"
    s=$text
}
wait_screen() { # wait_screen <regex> <timeout-seconds> <stem>; text in $s
    local pat=$1 deadline=$(( SECONDS + $2 )) stem=$3 gap=0.5
    while (( SECONDS < deadline )); do
        snap "$stem"
        print -r -- "$s" | grep -Eqi -- "$pat" && return 0
        sleep $gap
        (( gap < 2 )) && gap=$(( gap * 2 ))
    done
    return 1
}

type_text() {
    local c
    for c in ${(s::)1}; do
        if ! [[ "$c" =~ $SAFE_CHAR_RE ]]; then
            environment_fail "unsupported character in typed text: [$c]"
        fi
        osascript -e "tell application \"System Events\" to keystroke \"$c\"" \
            >/dev/null ||
            environment_fail 'keystroke failed (Accessibility permission?)'
        sleep "$KEY_PACE"
    done
}
key_return() {
    osascript -e 'tell application "System Events" to key code 36' >/dev/null \
        || environment_fail 'keystroke failed (Accessibility permission?)'
    sleep 0.1
}
type_cmd() {
    type_text "$1"
    key_return
}

finish() { # EXIT trap: eject, integrity checks, one-line summary.
    local result=$1
    trap - EXIT
    if (( mounted )); then
        eject_floppy || {
            print -u2 'WARN: EJECTFDD 1 failed; eject drive 1 manually'
            (( result == 2 )) && result=1
        }
    fi
    if [[ -n "$image_sha" ]]; then
        local now_template now_image
        now_image=$(sha256_of "$IMAGE")
        [[ "$now_image" == "$image_sha" ]] || {
            print -u2 'FAIL: dedicated image changed during the run'
            print -u2 "  before: $image_sha"
            print -u2 "  after:  $now_image"
            result=1
        }
        now_template=$(sha256_of "$TEMPLATE")
        [[ "$now_template" == "$template_sha" ]] || {
            print -u2 'FAIL: template console.xdf changed during the run'
            result=1
        }
        if (( ${+commands[fsck.fat]} )) && ! fsck.fat -n "$IMAGE" >/dev/null 2>&1; then
            print -u2 'FAIL: fsck.fat -n reported errors on the dedicated image'
            result=1
        fi
    fi
    local label
    case $result in
        0) label=PASS ;;
        1) label=FAIL ;;
        2) label=UNVERIFIED ;;
        *) label=ENVIRONMENT ;;
    esac
    print "SUMMARY: H2 result=$label gates=${#gates_passed}/$GATE_TOTAL failed_gate=${gates_failed:-none} image=build/mpx68k-h2/console-h2.xdf sha256=${image_sha:-n/a} evidence=${results_dir:-n/a}"
    exit "$result"
}

# Preflight: MPX68K, monitor socket, tools, build artifacts.
typeset -a mpids=(${(f)"$(pgrep -f "$APP.app/Contents/MacOS/$APP( |\$)")"})
(( ${#mpids} >= 1 )) || environment_fail "$APP is not running (start it, boot Human68k from FD0, wait for A:)"
(( ${#mpids} == 1 )) || environment_fail "multiple $APP processes match; refusing to guess the window"
app_pid=${mpids[1]}
[[ -S "$MONITOR_SOCK" ]] || environment_fail "monitor socket missing: $MONITOR_SOCK (monitorSocketEnabled)"
(( ${+commands[python3]} )) || environment_fail 'python3 not found'
(( ${+commands[mcopy]} )) || environment_fail 'mtools (mcopy) not found'
(( ${+commands[screencapture]} && ${+commands[osascript]} )) ||
    environment_fail 'screencapture/osascript not found'
for file in "$TEMPLATE" "$LAUNCHER" "$CONSOLE" "$EDIT" "$PREP"; do
    [[ -f "$file" ]] || environment_fail "missing build artifact: $file"
done

trap 'finish $?' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir -p "$IMAGE_DIR"
results_dir="$IMAGE_DIR/results-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$results_dir" || environment_fail 'cannot create results directory'

print '== H2 build dedicated image =='
python3 "$PREP" "$TEMPLATE" "$IMAGE" "$LAUNCHER" "$CONSOLE" --module "$EDIT" ||
    environment_fail 'prepare_console_image.py failed'
image_sha=$(sha256_of "$IMAGE")
template_sha=$(sha256_of "$TEMPLATE")
[[ -n "$image_sha" ]] || environment_fail 'cannot hash the dedicated image'
print "h2: template sha256=$template_sha (tests/target/build/console.xdf)"
print "h2: image    sha256=$image_sha (build/mpx68k-h2/console-h2.xdf)"
print "h2: launcher sha256=$(sha256_of "$LAUNCHER")"
print "h2: console  sha256=$(sha256_of "$CONSOLE")"
print "h2: edit     sha256=$(sha256_of "$EDIT")"
write_ocr_helper || environment_fail 'cannot write the OCR helper'

win_rect=$(window_rect) || environment_fail 'cannot read the MPX68K window bounds'
print "h2: window rect=$win_rect pid=$app_pid"

print '== H2 G1: Human68k prompt =='
# A line that is just the prompt: "A:¥>" with the yen possibly misread and
# trailing OCR blanks tolerated.
wait_screen '^a:.{0,3}[[:space:]]*$' 30 01-prompt ||
    gate_fail G1 'Human68k A: prompt not found' "$s"
gate_pass G1 'Human68k A: prompt visible'
type_cmd 'keywitch -r'
sleep 1

print '== H2 G2: mount dedicated image in drive 1 =='
mount_floppy || environment_fail 'monitor socket command failed'
gate_pass G2 'PAUSE/MOUNTFDD 1/RESUME sent (dir c: verifies the effect)'

print '== H2 G3: dir c: =='
type_cmd 'dir c:'
wait_screen '^[[:space:]]*console[[:space:]]*s9m' 15 03-dir ||
    gate_fail G3 'CONSOLE.S9M row missing in dir c:' "$s"
print -r -- "$s" | grep -Eqi '^[[:space:]]*edit[[:space:]]*s9m' ||
    gate_fail G3 'EDIT.S9M row missing in dir c:' "$s"
print -r -- "$s" | grep -Eqi '^[[:space:]]*ss9con[[:space:]]*x' ||
    gate_fail G3 'SS9CON.X row missing in dir c:' "$s"
gate_pass G3 'dir c: lists CONSOLE.S9M, EDIT.S9M, SS9CON.X'

print '== H2 G4: boot SSOS-9 =='
type_cmd 'c:'
sleep 0.5
type_cmd 'ss9con.x'
wait_screen 'minimal X68000 CLI' 30 04-banner ||
    gate_fail G4 'SSOS-9 banner missing' "$s"
wait_screen '^[[:space:]]*> ' 15 04-prompt ||
    gate_fail G4 'SSOS-9 prompt missing after banner' "$s"
gate_pass G4 'SSOS-9 banner and prompt up'

print '== H2 G5 (M2): load edit.s9m (3-cluster chain) =='
type_cmd 'load 1 edit.s9m'
wait_screen 'module loaded' 15 05-load-edit ||
    gate_fail G5 'edit.s9m did not report Module loaded.' "$s"
gate_pass G5 'M2: load 1 edit.s9m -> Module loaded. (chain + CRC verified)'

print '== H2 G6: load console.s9m =='
type_cmd 'load 1 console.s9m'
wait_screen 'module loaded' 15 06-load-console ||
    gate_fail G6 'console.s9m did not report Module loaded.' "$s"
gate_pass G6 'load 1 console.s9m -> Module loaded.'

print '== H2 G7: run console, key a =='
type_cmd 'run console'
sleep 1
type_text 'a'
wait_screen '0061' 10 07-key-a ||
    gate_fail G7 'key a did not report KEY: 0061' "$s"
gate_pass G7 'KEY: 0061 after key a'

print '== H2 G8: key q exits with status 0 =='
type_text 'q'
wait_screen 'exited with status 0' 10 08-q ||
    gate_fail G8 'console did not exit on q' "$s"
print -r -- "$s" | grep -Eqi 'pid 2 exited with status 0' ||
    gate_fail G8 'expected PID 2 exited with status 0' "$s"
gate_pass G8 'PID 2 exited with status 0'

print '== H2 G9 (M3 negative): load missing file =='
type_cmd 'load 1 nosuch.s9m'
wait_screen 'unable to load module' 15 09-nosuch ||
    gate_fail G9 'missing file did not produce the cli.c load error' "$s"
print -r -- "$s" | grep -Eqi 'file or cluster chain' ||
    gate_fail G9 'expected wording: Unable to load module: file or cluster chain.' "$s"
wait_screen '^[[:space:]]*> ' 10 09b-nosuch-prompt ||
    gate_fail G9 'prompt did not return after the failed load' "$s"
gate_pass G9 'M3: Unable to load module: file or cluster chain. and prompt back'

print '== H2 G10 (M3 no-media): load after EJECTFDD 1 =='
eject_floppy || environment_fail 'monitor socket command failed (eject)'
type_cmd 'load 1 console.s9m'
wait_screen 'unable to load module' 15 10-nomedia ||
    gate_fail G10 'missing media did not produce the cli.c load error' "$s"
print -r -- "$s" | grep -Eqi 'disk or FAT12' ||
    gate_fail G10 'expected wording: Unable to load module: disk or FAT12.' "$s"
wait_screen '^[[:space:]]*> ' 10 10b-nomedia-prompt ||
    gate_fail G10 'prompt did not return after the no-media load' "$s"
gate_pass G10 'M3: Unable to load module: disk or FAT12. and prompt back'

print '== H2 G11: exit =='
type_cmd 'exit'
wait_screen 'SSOS-9 terminated' 15 11-exit ||
    gate_fail G11 'SSOS-9 did not terminate' "$s"
wait_screen '^a:.{0,3}[[:space:]]*$' 15 11b-h68 ||
    gate_fail G11 'Human68k A: prompt missing after exit' "$s"
gate_pass G11 'SSOS-9 terminated. and Human68k A: prompt back'
