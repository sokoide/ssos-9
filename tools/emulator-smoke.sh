#!/bin/zsh
# Emulator smoke test for SSOS-9 through XEiJ (docs/emulator-remote-operation.md).
#
# Drives the running XEiJ instance: sends keys to the guest through the XEiJ
# control pipe (paste and control pipe must be ON, `pastepipe=on` in
# ~/XEiJ.ini), copies the text screen with Option-Shift-C, and reads it back
# through pbpaste. Checks, in order:
#   T1  Human68k prompt (A:\>) is visible
#   T2  `keywitch -r` then `f:ssos9` boots the SSOS-9 standalone CLI
#   T3  `mem` prints arena statistics
#   T4  `exit` returns to Human68k
#   T5  `dir a:` works after SSOS-9 returns (integration-plan case A1)
#
# Notes
# - The control pipe FIFO is at ${TMPDIR}/XEiJControl (java.io.tmpdir), not
#   /tmp as the XEiJ docs say (that path is for WSL).
# - The guest may run a keyswitch remap driver (keywitch) that turns the
#   X68000 `:` into `'`; the script resets it with `keywitch -r` first.
# - `typekey` presses all keys then releases all, which garbles multi-key
#   commands, so the script sends one key per command.
# - Requires: accessibility/keystroke permission for the calling terminal,
#   XEiJ running with the HFS drive mapped to ~/tmp (hf0 in ~/XEiJ.ini), and
#   ssos9.x installed in ~/tmp (make install).
# - T5 expects at least one directory entry visible in the A: listing.
#
# Usage: tools/emulator-smoke.sh
set -u

XEIJ_PROCESS="java"
TMPDIR_HOST="${HOME}/tmp"
PIPE="${TMPDIR%/}/XEiJControl"

pass=0
fail=0

die() { echo "emulator-smoke: $*" >&2; exit 2; }

frontmost() {
    osascript -e "tell application \"System Events\" to set frontmost of process \"$XEIJ_PROCESS\" to true" >/dev/null || die "cannot bring XEiJ to front"
}

# Send one X68000 key by name through the control pipe.
key() {
    printf 'typekey %s\n' "$1" > "$PIPE" || die "cannot write $PIPE (paste and control pipe ON?)"
    sleep 0.1
}

# Type a guest command: one key per command so keys cannot collide.
type_cmd() {
    frontmost
    local -a chars
    chars=(${(s::)1})
    local k
    for k in "${chars[@]}"; do
        case "$k" in
            ' ') key space ;;
            '-') key minus ;;
            ':') key colon ;;
            *) key "$k" ;;
        esac
    done
    key return
}

# Copy the XEiJ text screen (Option-Shift-C) and return it via pbpaste.
screen() {
    frontmost
    osascript -e 'tell application "System Events" to keystroke "c" using {option down, shift down}' >/dev/null
    sleep 0.8
    pbpaste
}

check() { # check <name> <screen-text> <pattern>
    if printf '%s' "$2" | grep -q "$3"; then
        echo "PASS $1"
        pass=$((pass + 1))
    else
        echo "FAIL $1 (pattern '$3' not on screen)"
        fail=$((fail + 1))
    fi
}

[ -f "$TMPDIR_HOST/ssos9.x" ] || die "$TMPDIR_HOST/ssos9.x missing; run 'make install'"
pgrep -f "XEiJ.jar" >/dev/null || die "XEiJ is not running"
[ -p "$PIPE" ] || die "control pipe $PIPE missing; set pastepipe=on in ~/XEiJ.ini and restart XEiJ"

echo "== T1 Human68k prompt =="
s1=$(screen)
check "T1 Human68k prompt" "$s1" 'A:\\>'

echo "== T2 launch f:ssos9 =="
type_cmd "keywitch -r"
sleep 1
type_cmd "f:ssos9"
sleep 3
s2=$(screen)
check "T2 SSOS-9 CLI banner" "$s2" 'SSOS-9'

echo "== T3 mem =="
type_cmd "mem"
sleep 1
s3=$(screen)
check "T3 mem arena stats" "$s3" 'arena free_bytes='

echo "== T4 exit to Human68k =="
type_cmd "exit"
sleep 3
s4=$(screen)
check "T4 Human68k return" "$s4" 'A:\\>'

echo "== T5 Human68k command after return (A1) =="
type_cmd "dir a:"
sleep 1
s5=$(screen)
check "T5 Human68k directory listing" "$s5" '<dir>'
check "T5 Human68k prompt after dir a:" "$s5" 'A:\\>'

echo "== result: $pass passed, $fail failed =="
[ "$fail" -eq 0 ]
