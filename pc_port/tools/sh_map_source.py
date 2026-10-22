#!/usr/bin/env python3
"""Readers for the map sources (src/maps/mapN_sNN): map names and indices,
MAP_POINTS / MAP_EVENTS initializers, message counts. Used by
gen_remix_data.py; kept from the old randomizer's generator when that mode
was removed.
"""

import os
import re
import sys
import glob

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# The 15 lines every map inherits from include/maps/shared/map_msg_common.h.
COMMON_MSG_COUNT = 15


def top_level_braces(text):
    """Split a brace-initializer body into its top-level {...} blocks."""
    out, depth, start = [], 0, None
    for i, c in enumerate(text):
        if c == '{':
            if depth == 0:
                start = i
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                out.append(text[start:i + 1])
    return out


def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    return re.sub(r'//[^\n]*', '', s)


def field(block, name):
    m = re.search(r'\.' + name + r'\s*=\s*([^,\n}]+)', block)
    return m.group(1).strip() if m else None


def map_points(m):
    """Return each map point's C initializer body, verbatim, so we can re-emit it."""
    path = os.path.join(REPO, 'src', 'maps', m, 'map_points.h')
    if not os.path.exists(path):
        return []
    return [strip_comments(b) for b in top_level_braces(open(path).read())]


def map_events(m):
    files = glob.glob(os.path.join(REPO, 'src', 'maps', m, '*events_data.c'))
    if not files:
        return []
    t = open(files[0]).read()
    body = t[t.index('{') + 1: t.rindex('}')]
    return [strip_comments(b) for b in top_level_braces(body)]


def msg_count(m):
    """Count MAP_MESSAGES[] entries: the 15 shared ones plus the map's own strings."""
    for f in glob.glob(os.path.join(REPO, 'src', 'maps', m, '*.c')):
        t = open(f, errors='ignore').read()
        i = t.find('MAP_MESSAGES[]')
        if i < 0:
            continue
        j = t.index('{', i)
        depth, k = 0, j
        while k < len(t):
            if t[k] == '{':
                depth += 1
            elif t[k] == '}':
                depth -= 1
                if depth == 0:
                    break
            k += 1
        body = t[j + 1:k]
        has_common = 'map_msg_common.h' in body
        # Count top-level commas outside string literals. Adjacent-concatenated
        # parts ("a" "b") and multi-line literals then count as ONE entry, which a
        # line-based count would get wrong -- and an overcount here would make the
        # old randomizer copy pointers from past the end of MAP_MESSAGES[].
        body = re.sub(r'^\s*#include[^\n]*$', '', body, flags=re.M)
        own, depth, i, in_str = 0, 0, 0, False
        while i < len(body):
            c = body[i]
            if in_str:
                if c == '\\':
                    i += 2
                    continue
                if c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
            elif c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
            elif c == ',' and depth == 0:
                own += 1
            i += 1
        # A trailing entry without a comma still counts.
        if re.search(r'"[^"]*"\s*$', body.strip()):
            own += 1
        return (COMMON_MSG_COUNT if has_common else 0) + own
    return 0


def is_origin(rec):
    x = field(rec, 'positionX') or ''
    z = field(rec, 'positionZ') or ''
    num = lambda s: re.search(r'Q12\(\s*(-?[\d.]+)f?\s*\)', s)
    mx, mz = num(x), num(z)
    return bool(mx and mz and float(mx.group(1)) == 0.0 and float(mz.group(1)) == 0.0)


def door_func_mask(m):
    """Which of mapEventFuncs[0]/[1] are the shared jammed/locked door handlers."""
    path = os.path.join(REPO, 'src', 'maps', m, m + '_header.c')
    if not os.path.exists(path):
        return 0
    t = strip_comments(open(path).read())
    mm = re.search(r'g_MapEventFuncs\[\][^=]*=\s*\{(.*?)\n\};', t, re.S)
    if not mm:
        return 0
    ents = [e.strip() for e in mm.group(1).split(',') if e.strip()]
    mask = 0
    if len(ents) > 0 and ents[0] == 'MapEvent_DoorJammed':
        mask |= 1
    if len(ents) > 1 and ents[1] == 'MapEvent_DoorLocked':
        mask |= 2
    return mask


MAP_NAMES = []
for n in range(8):
    for s in range(7):
        d = os.path.join(REPO, 'src', 'maps', 'map%d_s%02d' % (n, s))
        if os.path.isdir(d):
            MAP_NAMES.append('map%d_s%02d' % (n, s))

IDX = {name: i for i, name in enumerate(MAP_NAMES)}


def map_idx_of(token):
    """`MapIdx_MAP2_S04` -> index."""
    if not token or not token.startswith('MapIdx_'):
        return None
    return IDX.get(token[len('MapIdx_'):].lower())


def bgm_value(token):
    """The BGM track a room transition switches to. On a `SysState_LoadRoom` row the
    `mapIdx` field is reused as a BGM track index, and several maps spell that index
    with the `MapIdx_*` enum whose numeric value happens to be the track they want."""
    token = (token or '0').strip()
    if token.isdigit():
        return token
    idx = map_idx_of(token)
    if idx is not None:
        return '%d /* %s */' % (idx, token)
    return '0'
