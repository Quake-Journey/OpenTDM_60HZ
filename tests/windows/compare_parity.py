"""Parity: compare the case lines of the parity scenarios with the ORIGINAL
module at 10 Hz.

A case line is "<key>:<events>" (W/WS/WC/RX/... lines of scenarios_parity.h,
RJ lines of f_rocket_jump). The reference is a run_harness_win.py output
directory of the original module at 10 Hz; every other directory/rate must
print the same lines. Prints per scenario and rate: equal/total, missing,
and the first differences (key, reference, got).

usage: python compare_parity.py REF_DIR RUN_DIR [RUN_DIR ...] [--json OUT] [--show N] [--expect-differ]
  REF_DIR  e.g. build/runs/orig/harness-engine-10
  RUN_DIR  e.g. build/runs/engine (its harness-engine-<rate> subdirectories are read)
exit 0: all equal (or, with --expect-differ, at least one difference)
"""
import argparse
import json
import re
import sys
from pathlib import Path

# the case key: kind, name(s) and phase; the rest (apex, events) is the value
LINE = re.compile(r'^((?:W \S+ \S+|WS \S+|WC \S+|WI \S+|WD \S+|WV|WT \S+|WH \S+|WM \S+|PX \S+|PB \S+|RX \S+|RJ \S+) k=\d+)(.*)$')


def lines(d):
    out = {}
    for f in sorted(Path(d).glob('*.txt')):
        for line in f.read_text(errors='replace').splitlines():
            m = LINE.match(line)
            if m:
                out[(f.stem, m.group(1))] = m.group(2)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('ref')
    ap.add_argument('runs', nargs='+')
    ap.add_argument('--json')
    ap.add_argument('--show', type=int, default=3)
    ap.add_argument('--expect-differ', action='store_true')
    a = ap.parse_args(argv)
    ref = lines(a.ref)
    if not ref:
        sys.exit(f'no case lines in {a.ref}')
    report, total_diff, total_lines = {}, 0, {}
    for run in a.runs:
        for d in sorted(Path(run).glob('harness-engine-*')):
            got = lines(d)
            per = {}
            for (scen, key), val in ref.items():
                s = per.setdefault(scen, {'equal': 0, 'total': 0, 'missing': 0, 'diffs': []})
                s['total'] += 1
                if (scen, key) not in got:
                    s['missing'] += 1
                elif got[(scen, key)] == val:
                    s['equal'] += 1
                elif len(s['diffs']) < 50:
                    s['diffs'].append({'case': key, 'ref': val, 'got': got[(scen, key)]})
            name = f'{Path(run).name}/{d.name}'
            report[name] = per
            total_lines[name] = (sum(s['equal'] for s in per.values()), sum(s['total'] for s in per.values()))
            for scen, s in per.items():
                bad = s['total'] - s['equal']
                total_diff += bad
                print(f'{name:40s} {scen:14s} {s["equal"]:4d}/{s["total"]:<4d} missing {s["missing"]}')
                for x in s['diffs'][:a.show]:
                    print(f'    {x["case"]}\n      ref:{x["ref"][:300]}\n      got:{x["got"][:300]}')
    for name, (eq, tot) in total_lines.items():
        print(f'{name}: {eq}/{tot} case lines equal to the original at 10 Hz')
    if a.json:
        Path(a.json).write_text(json.dumps(report, indent=1), encoding='utf-8')
    ok = total_diff > 0 if a.expect_differ else total_diff == 0
    print('ALL EQUAL' if total_diff == 0 else f'{total_diff} case lines differ')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
