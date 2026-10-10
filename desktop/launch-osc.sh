#!/usr/bin/env bash
# Copyright 2026 | Jake Rose
#
# This file is part of project eclipse-os
# See readme.md for full license details.
#
# afterglow's OSC mode on this machine: the tower's stage in the viewer,
# taking OSC on 7000 through the same map the scanner-pi opens - see
# config/afterglow_osc.json. Anything after is the viewer's: --live puts it
# on whatever is plugged in here.
#
#   ./launch-osc.sh
#   python3 ~/Documents/osc-host/osc_host.py --host 127.0.0.1 --port 7000

set -euo pipefail

cd "$(dirname "$0")"

[ -x build/eclipse-dmx ] || ./build.sh

PYTHONPATH=python exec python3 -m eclipse_dmx view config/afterglow_osc.json "$@"
