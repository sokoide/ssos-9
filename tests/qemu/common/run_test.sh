#!/bin/sh
# QEMU tests park after printing their verdict, so timeout (124) is expected.
# Optional environment: EXPECT — a substring the output must additionally
# contain, for suites whose per-case expected output is stronger than the
# generic final-OK check (tests/asm uses this).
set -u
seconds=$1
shift
log_dir=${SS_TEST_LOG_DIR:-../../build/test-logs}
mkdir -p "$log_dir" || exit 1
output=$(mktemp "$log_dir/${SS_TEST_CASE:-case}.XXXXXX") || exit 1
echo "QEMU log: $output"

if timeout --foreground "$seconds" "$@" >"$output" 2>&1; then
    status=0
else
    status=$?
fi
cat "$output"
if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
    echo "QEMU exited with status $status" >&2
    exit 1
fi
# Exact final-verdict check: the last non-empty line of guest output (after
# dropping QEMU's own termination notice) must be the verdict line. The SSOS
# QEMU tests print "OK" or "OK <detail>"; the pure-assembly samples print
# "tNN: OK". A FAIL verdict or any other trailing line fails the case, and
# OK-looking output earlier in the log does not count.
verdict=$(grep -v -e '^qemu-system-m68k: terminating on signal' "$output" \
    | sed -e '/^[[:space:]]*$/d' -e 's/[[:space:]]*$//' | tail -n 1)
case $verdict in
    OK|OK\ *)
        ;;
    FAIL*)
        echo "QEMU test reported FAIL: $verdict" >&2
        exit 1
        ;;
    t[0-9][0-9]:\ OK|"before ISR after OK")
        ;;
    *)
        echo "QEMU test did not end with an OK verdict (last line: ${verdict:-none})" >&2
        exit 1
        ;;
esac
if [ -n "${EXPECT:-}" ] && ! grep -Fq -- "$EXPECT" "$output"; then
    echo "Expected output fragment not found: $EXPECT" >&2
    exit 1
fi
