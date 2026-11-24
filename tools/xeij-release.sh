#!/bin/zsh
# Run the release suite on a fresh clone of the Human68k test environment.
set -u

if (( $# < 2 )); then
    print -u2 'usage: xeij-release.sh SOURCE_INI CASE...'
    exit 3
fi
source_ini=$1
shift
for artifact in "${SSOS9_XEIJ_JAR:-}" "${SSOS9_XEIJ_CGROM:-}"; do
    if [[ "$artifact" != /* || ! -f "$artifact" ]]; then
        print -u2 'Set SSOS9_XEIJ_JAR and SSOS9_XEIJ_CGROM to absolute file paths.'
        exit 3
    fi
done
[[ ${SSOS9_XEIJ_JAR:t} == XEiJ.jar ]] || {
    print -u2 'The XEiJ process detector requires the JAR filename XEiJ.jar.'
    exit 3
}

mkdir -p build/xeij/profiles || exit 3
profile_root=$(mktemp -d "${PWD:A}/build/xeij/profiles/release.XXXXXXXX") || exit 3
export SSOS9_XEIJ_INI="$profile_root/run/XEiJ.ini"
export SSOS9_XEIJ_STAGE="$profile_root/run/hfs"
source "${0:A:h}/emulator-itest.sh" || exit 3

running=$(xeij_pids '*') || environment_fail 'cannot enumerate XEiJ'
[[ -z "$running" ]] || environment_fail 'close the running XEiJ before cloning its media'
python3 tools/xeij-test-profile.py "$source_ini" ssos/build/export "$profile_root/run" ||
    environment_fail 'could not prepare the disposable profile'
cp build/xeij/mfpstate.x "$SSOS9_XEIJ_STAGE/" || environment_fail 'could not stage the MFP probe'

finish_release() {
    local result=$1
    trap - EXIT
    local pids
    if ! pids=$(xeij_pids); then
        environment_fail 'cannot locate the disposable XEiJ for cleanup'
    fi
    if [[ -n "$pids" ]]; then
        XEI_J_PID=$pids
        xeij_stop || environment_fail 'could not stop the disposable XEiJ'
    fi
    exit "$result"
}
trap 'finish_release $?' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
xeij_start || environment_fail 'could not start disposable XEiJ'
deadline=$(( SECONDS + 45 ))
until screen_line "$(screen)" 'A:\>'; do
    (( SECONDS < deadline )) ||
        environment_fail 'disposable XEiJ did not reach the Human68k A: prompt'
    sleep 1
done
python3 tools/xeij-suite.py --require-profile --mfp-check "$@"
