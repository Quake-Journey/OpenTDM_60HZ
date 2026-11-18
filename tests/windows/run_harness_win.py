"""Run the Windows API3 harness: every scenario, one process each.

usage: python run_harness_win.py --exe harness-box.exe --rates 10,30,60
           [--sched tick,30,10,batch] [--only REGEX] [--out DIR] [--jobs 4] [--mingw DIR]

Writes <out>/<exe-stem>-<rate>[-<sched>]/<scenario>.txt and a summary.json;
prints one line per FAIL and a total. Exit 1 if anything failed.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / 'tools'))
from build_win64 import find_mingw, mingw_tool  # noqa: E402

CREATE_NO_WINDOW = 0x08000000


def no_error_dialogs():
    """Children inherit the error mode: no Windows Error Reporting / critical
    error dialogs from a crashing test process (the harness binaries also
    carry no MessageBox import, see build_harness_win.py)."""
    import ctypes
    SEM_FAILCRITICALERRORS, SEM_NOGPFAULTERRORBOX, SEM_NOOPENFILEERRORBOX = 0x1, 0x2, 0x8000
    ctypes.windll.kernel32.SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX)


def run_one(exe, scenario, env, timeout):
    t0 = time.time()
    try:
        r = subprocess.run([str(exe), scenario], env=env, capture_output=True, text=True, timeout=timeout,
                           creationflags=CREATE_NO_WINDOW)
        code, text = r.returncode, r.stdout + r.stderr
    except subprocess.TimeoutExpired as e:
        code, text = 'timeout', (e.stdout or b'').decode('latin-1') if isinstance(e.stdout, bytes) else (e.stdout or '')
    return scenario, code, text, time.time() - t0


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', required=True)
    ap.add_argument('--rates', default='10,30,60')
    ap.add_argument('--sched', default='')
    ap.add_argument('--only', default='')
    ap.add_argument('--out', default='')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--timeout', type=float, default=120)
    ap.add_argument('--mingw', help='MinGW-w64 folder (or MINGW_DIR): its objdump checks the executable')
    a = ap.parse_args(argv)
    exe = Path(a.exe).resolve()
    out = Path(a.out) if a.out else exe.parent / 'results'
    objdump = mingw_tool(find_mingw(a.mingw), 'objdump.exe', 'x86_64-w64-mingw32-objdump.exe')
    if 'MessageBox' in subprocess.run([str(objdump), '-p', str(exe)], capture_output=True,
                                      text=True, creationflags=CREATE_NO_WINDOW).stdout:
        sys.exit(f'{exe} imports a MessageBox function: not run')
    no_error_dialogs()
    # the engine fixture writes its BSP under TMPDIR
    tmp = exe.parent / 'tmp'
    tmp.mkdir(exist_ok=True)
    total = {'pass': 0, 'fail': 0, 'runs': []}
    for rate in a.rates.split(','):
        for sched in (a.sched.split(',') if a.sched else ['']):
            env = dict(os.environ, HARNESS_RATE=rate, TMPDIR=str(tmp))
            env.pop('HARNESS_SCHED', None)
            if sched:
                env['HARNESS_SCHED'] = sched
            names = subprocess.run([str(exe), 'list'], env=env, capture_output=True, text=True,
                                   creationflags=CREATE_NO_WINDOW).stdout.split()
            if a.only:
                names = [n for n in names if re.search(a.only, n)]
            tag = f'{exe.stem}-{rate}' + (f'-{sched}' if sched else '')
            d = out / tag
            d.mkdir(parents=True, exist_ok=True)
            with ThreadPoolExecutor(max_workers=a.jobs) as pool:
                results = list(pool.map(lambda n: run_one(exe, n, env, a.timeout), names))
            fails = []
            for name, code, text, dt in results:
                (d / f'{name}.txt').write_text(text, encoding='utf-8')
                if code != 0:
                    fails.append(name)
            total['pass'] += len(names) - len(fails)
            total['fail'] += len(fails)
            total['runs'].append({'tag': tag, 'scenarios': len(names), 'failed': fails})
            print(f'{tag}: {len(names)} scenarios, failed {len(fails)}' + (f': {" ".join(fails)}' if fails else ''))
    (out / f'summary-{exe.stem}.json').write_text(json.dumps(total, indent=1), encoding='utf-8')
    print(f'total pass {total["pass"]} fail {total["fail"]}')
    return 1 if total['fail'] else 0


if __name__ == '__main__':
    sys.exit(main())
