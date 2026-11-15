#!/bin/sh
set -eu
runner=$(cd "$(dirname "$0")" && pwd)/qemu/common/run_test.sh
SS_TEST_LOG_DIR=$(mktemp -d "${TMPDIR:-/tmp}/ssos-verdict.XXXXXX")
export SS_TEST_LOG_DIR
trap 'rm -rf "$SS_TEST_LOG_DIR"' EXIT HUP INT TERM
for value in 'OK' 'OK detail' 't01: OK' 'before ISR after OK'; do
    sh "$runner" 1 sh -c 'printf "%s\n" "$1"' sh "$value" >/dev/null
done
for value in 'FAIL OK' 'FAIL' 'anything OK' 'OKAY' 'bad'; do
    if sh "$runner" 1 sh -c 'printf "%s\n" "$1"' sh "$value" >/dev/null 2>&1; then
        echo "incorrectly accepted: $value" >&2
        exit 1
    fi
done
