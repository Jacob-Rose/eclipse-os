#!/bin/sh
# The python suite, runnable from anywhere.
#
#   ./test.sh                 all of it
#   ./test.sh Viewer          only names containing "Viewer" - a class, a test,
#                             or any substring of either
#   ./test.sh Viewer Midi     either of them
#   ./test.sh -v              anything starting with a dash goes to unittest
#
# Reach for a name while you are working. A full run drives the real executable
# a few hundred times and opens a few dozen windows, so it is minutes; one
# class is seconds. Run the whole thing before you commit, not before you look.
#
# The windows a run opens announce themselves as "Eclipse-dmx-tests" so a
# window manager can put them somewhere out of the way rather than on top of
# what you are doing - see python/tests/harness.py, and the readme for the
# hyprland rules.
set -e

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

set -- $(for arg in "$@"; do
    case "$arg" in
        -*) printf '%s ' "$arg" ;;
        *)  printf -- '-k %s ' "$arg" ;;
    esac
done)

exec python -m unittest discover -s "$here/python/tests" "$@"
