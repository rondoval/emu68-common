#!/bin/sh
# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
# emu68-common's test suite (README.md): the common test harness's own tests.
#
#   run.sh host [pytest options]    the emu68test package on recorded inputs, the kv and logic
#                                   libraries and the check tools under vamos, the code gates;
#                                   no Amiga
#
# The first run creates tests/.venv from testing/requirements.txt.
set -e
here=$(cd "$(dirname "$0")" && pwd)
venv="$here/.venv"
if [ ! -x "$venv/bin/python" ]; then
    python3 -m venv "$venv"
    "$venv/bin/pip" install -q -r "$here/../testing/requirements.txt"
fi
tier=${1:-host}
[ $# -gt 0 ] && shift
cd "$here"
case "$tier" in
    host)
        exec "$venv/bin/python" -m pytest unit logic gates "$@"
        ;;
    *)
        echo "usage: run.sh host [pytest options]" >&2
        exit 20
        ;;
esac
