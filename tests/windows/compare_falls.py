"""Falling damage parity: 30/60 Hz against 10 Hz.

Reads the FALL lines of f_fall_parity (engine harness, one usercmd stream for
every rate) from <dir>/harness-engine-<rate>/f_fall_parity.txt and requires
every 30 and 60 Hz line to equal the 10 Hz line of the same case and phase:
health lost, armor lost, health drops, fallshort / footstep events, fall and
pain sounds, the usercmd of landing. Writes <dir>/fall_parity.json.

usage: python compare_falls.py DIR [--expect-differ]
  --expect-differ   a negative control: the rates must NOT all match
"""
import argparse
import json
import sys
from pathlib import Path


def load(d, rate):
    rows = {}
    for line in (Path(d) / f'harness-engine-{rate}' / 'f_fall_parity.txt').read_text(errors='replace').splitlines():
        if line.startswith('FALL '):
            parts = line.split()
            rows[(parts[1], parts[2])] = ' '.join(parts[3:])
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('dir')
    ap.add_argument('--expect-differ', action='store_true')
    a = ap.parse_args(argv)
    base = load(a.dir, 10)
    res = {'cases': len(base), 'rates': {}}
    for rate in (30, 60):
        other = load(a.dir, rate)
        diff = [{'case': k[0], 'phase': k[1], '10': base[k], str(rate): other.get(k)}
                for k in sorted(base) if other.get(k) != base[k]]
        missing = sorted(set(other) ^ set(base))
        res['rates'][rate] = {'lines': len(other), 'differ': len(diff), 'missing': missing, 'first_diffs': diff[:12]}
    same = all(r['differ'] == 0 and not r['missing'] and r['lines'] == len(base) for r in res['rates'].values())
    res['all_equal_10hz'] = same
    res['pass'] = (not same) if a.expect_differ else (same and len(base) > 0)
    (Path(a.dir) / 'fall_parity.json').write_text(json.dumps(res, indent=1), encoding='utf-8')
    print(json.dumps({'cases': res['cases'], 'all_equal_10hz': same,
                      'differ': {r: v['differ'] for r, v in res['rates'].items()}, 'pass': res['pass']}))
    for r, v in res['rates'].items():
        for d in v['first_diffs'][:4]:
            print(r, d)
    return 0 if res['pass'] else 1


if __name__ == '__main__':
    sys.exit(main())
