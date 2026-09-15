#!/usr/bin/env python3
# Copyright 2025 | Jake Rose
#
# This file is part of project eclipse-os
# See readme.md for full license details.
#
# Is the sculpture on the wall actually reachable from Home Assistant?
#
#   tools/check-ha.py            check
#   tools/check-ha.py --tidy     also clear stale discovery from older builds
#
# Run it after every flash of a relic that talks to HA. It reads secrets.h
# and, from this machine, does what HA does: logs into the broker, reads the
# device's discovery configs, and drives every command topic they advertise,
# expecting the relic to echo the new state back. It exits non-zero on the
# first thing HA would show as broken.
#
# It exists because this went wrong twice without a sound: a light whose
# discovery config named topics the firmware never subscribed to (unavailable
# in HA for months, while the Mode select beside it worked), and a rewired
# board flashed from a branch that had the old pin (a dark strip, log clean).
# Neither was visible from the serial monitor. Both are visible from here.
#
# Stdlib only - the MQTT it needs is small, and this has to run on any machine
# with the repo on it.

import argparse, json, re, socket, struct, sys, time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# ---- the little MQTT client ------------------------------------------------

def _str(x):
    b = x.encode(); return struct.pack('>H', len(b)) + b

def _len(n):
    out = b''
    while True:
        d, n = n % 128, n // 128
        out += bytes([d | (0x80 if n else 0)])
        if not n: return out

class Mqtt:
    """Just enough of MQTT 3.1.1 to log in, subscribe, publish, and listen."""
    RC = {0: 'accepted', 1: 'bad protocol', 2: 'bad client id', 3: 'server unavailable',
          4: 'bad username/password', 5: 'not authorized'}

    def __init__(self, host, port, user, password, client_id):
        self.sk = socket.create_connection((host, port), timeout=4)
        var = _str('MQTT') + b'\x04' + bytes([0xC2]) + struct.pack('>H', 30)
        pay = _str(client_id) + _str(user) + _str(password)
        self.sk.sendall(b'\x10' + _len(len(var + pay)) + var + pay)
        ack = self.sk.recv(4)
        self.rc = ack[3] if len(ack) == 4 and ack[0] == 0x20 else -1
        self.buf = b''
        self.sk.settimeout(0.3)

    def subscribe(self, *filters):
        body = struct.pack('>H', 1) + b''.join(_str(f) + b'\x00' for f in filters)
        self.sk.sendall(b'\x82' + _len(len(body)) + body)

    def publish(self, topic, payload, retain=False):
        body = _str(topic) + payload.encode()
        self.sk.sendall(bytes([0x30 | retain]) + _len(len(body)) + body)

    def listen(self, seconds, until=None, value=None):
        """Every PUBLISH heard in the window, last payload per topic. With
        `until`, returns as soon as that topic has been heard - carrying
        `value`, if one is given, so a late echo of the previous command
        does not pass for the answer to this one."""
        got, end = {}, time.time() + seconds
        def done():
            return until in got and (value is None or got[until] == value)
        while time.time() < end and not done():
            try: self.buf += self.sk.recv(65536)
            except socket.timeout: continue
            while len(self.buf) >= 2:
                mult, n, i = 1, 0, 1
                while True:
                    d = self.buf[i]; n += (d & 127) * mult; mult *= 128; i += 1
                    if not d & 128: break
                if len(self.buf) < i + n: break
                typ, pkt, self.buf = self.buf[0] >> 4, self.buf[i:i + n], self.buf[i + n:]
                if typ == 3:
                    tl = struct.unpack('>H', pkt[:2])[0]
                    got[pkt[2:2 + tl].decode()] = pkt[2 + tl:].decode(errors='replace')
        return got

    def close(self):
        self.sk.close()

# ---- the check -------------------------------------------------------------

def read_secrets():
    cfg = {}
    for line in (REPO / 'secrets.h').read_text().splitlines():
        m = re.match(r'\s*#define\s+(\w+)\s+("([^"]*)"|\S+)', line)
        if m: cfg[m.group(1)] = m.group(3) if m.group(3) is not None else m.group(2)
    return cfg

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--tidy', action='store_true',
                    help='clear retained discovery configs left by older builds of this device')
    ap.add_argument('--host', help='broker to use instead of MQTT_SERVER (an IP, to bypass mDNS)')
    args = ap.parse_args()

    failures = 0
    def check(what, ok, detail=''):
        nonlocal failures
        print(f"{'ok  ' if ok else 'FAIL'} {what}" + (f"  ({detail})" if detail else ''))
        if not ok: failures += 1

    cfg = read_secrets()
    for key in ('WIFI_SSID', 'WIFI_PASSWORD', 'MQTT_SERVER', 'MQTT_USERNAME', 'MQTT_PASSWORD'):
        check(f"secrets.h {key} is not the template's",
              cfg.get(key, '') and not re.search(r'Your|your_|192\.168\.1\.100', cfg[key]))
    host = args.host or cfg['MQTT_SERVER']
    device = cfg['HA_DEVICE_ID']

    # -- the broker, as the relic sees it
    try:
        ip = socket.gethostbyname(host)
        check(f"{host} resolves", True, ip)
    except OSError as e:
        check(f"{host} resolves", False, str(e)); return 2
    try:
        m = Mqtt(ip, int(cfg['MQTT_PORT']), cfg['MQTT_USERNAME'], cfg['MQTT_PASSWORD'], 'eclipse-check-ha')
    except OSError as e:
        check(f"broker {ip}:{cfg['MQTT_PORT']} reachable", False, str(e)); return 2
    check(f"broker {ip}:{cfg['MQTT_PORT']} reachable", True)
    check(f"login as {cfg['MQTT_USERNAME']!r}", m.rc == 0, Mqtt.RC.get(m.rc, m.rc))
    if m.rc != 0: return 2

    # -- what HA has been told
    prefix = cfg.get('HA_DISCOVERY_PREFIX', 'homeassistant')
    m.subscribe(f'{prefix}/#')
    retained = m.listen(2.5)
    mine = {t: json.loads(p) for t, p in retained.items() if f'/{device}/' in t and p}
    check(f"device {device!r} has discovery configs on the broker", bool(mine), f'{len(mine)} entities')

    # the same unique_id under another device id: HA keeps whichever it saw
    # first and logs the other as a duplicate. A previous build of this board.
    ids = {c.get('unique_id') for c in mine.values()}
    stale = [t for t, p in retained.items() if p and f'/{device}/' not in t
             and json.loads(p).get('unique_id') in ids]
    check("no stale discovery from an older build of this device", not stale,
          '; '.join(stale) or '')
    if stale and args.tidy:
        for t in stale:
            m.publish(t, '', retain=True); print(f"     cleared {t}")
        for t, c in mine.items():
            m.publish(t, json.dumps(c), retain=True)
        print("     re-sent this device's configs so HA re-reads them")

    # -- what the relic answers
    m.subscribe('eclipse/#')
    live = m.listen(1.5)
    restore = []   # (command topic, payload) to put the look back afterwards
    for t, c in sorted(mine.items()):
        name = c.get('name', t)
        avail = c.get('availability_topic')
        if avail:
            check(f"{name}: availability is online", live.get(avail) == 'online', f'{avail} = {live.get(avail)!r}')

        # drive every command the config advertises and expect its state echo
        drives = []
        if 'effect_command_topic' in c and c.get('effect_list'):
            eff = c['effect_list']
            drives += [(c['effect_command_topic'], e, c['effect_state_topic'], e) for e in eff[:2]]
        if 'brightness_command_topic' in c:
            drives.append((c['brightness_command_topic'], '255', c['brightness_state_topic'], None))
        if 'options' in c and 'command_topic' in c:      # a select
            drives += [(c['command_topic'], o, c['state_topic'], o) for o in c['options'][:2]]
        elif 'command_topic' in c:                      # a light / switch
            drives += [(c['command_topic'], 'OFF', c['state_topic'], 'OFF'),
                       (c['command_topic'], 'ON', c['state_topic'], 'ON')]
        for cmd, payload, state, expect in drives:
            if state in live and not any(r[0] == cmd for r in restore):
                restore.append((cmd, live[state]))
            m.publish(cmd, payload)
            # the relic answers within a tick or two; the ceiling is for a
            # board mid-way through a blocking WiFi retry
            got = m.listen(6.0, until=state, value=expect).get(state)
            ok = (got == expect) if expect is not None else got is not None
            check(f"{name}: {cmd.split('/')[-2] if cmd.endswith('/set') and cmd.count('/') > 2 else 'command'} <- {payload!r} echoes {state.split('/')[-1]}",
                  ok, f'got {got!r}')

    # leave the wall as we found it
    for cmd, payload in restore:
        m.publish(cmd, payload); time.sleep(0.2)

    m.close()
    print()
    print("HA CHECK PASS" if failures == 0 else f"HA CHECK FAILURES {failures}")
    return 0 if failures == 0 else 1

if __name__ == '__main__':
    sys.exit(main())
