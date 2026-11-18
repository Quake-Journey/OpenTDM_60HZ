"""The comparison stand on Windows: the released module against the ORIGINAL module at 10 Hz.

    python tests/windows/run_all.py --mingw C:/mingw64 [--controls] [--jobs 4] [--reuse-build]
                                    [--reference DIR] [--build-dir build]

1. builds the engine and the module as tools/build_win64.py does (or reuses that build: --reuse-build);
2. builds the reference: the original OpenTDM module, by default the opentdm/ folder of this repository's
   first commit (git needed), or --reference DIR;
3. builds the API3 harnesses: the reference (engine variant, the parity scenarios only) and the released
   module (engine and box variants);
4. runs: the reference at 10 Hz (parity scenarios); the released module, engine variant at 10/30/60 Hz,
   box variant at 10/20/30/40/50/60 Hz, scheduler variants at 60 Hz;
5. compares: the parity case lines with the reference at 10 Hz, the falls at 30/60 Hz with 10 Hz;
6. with --controls: the negative controls (each one must turn its check red).

Results: <build>/runs/ (summary.json, one text file per scenario). Exit 0 when everything passed.
"""
import argparse
import io
import json
import shutil
import subprocess
import sys
import tarfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / 'tools'))
import build_win64 as bw  # noqa: E402

PY = sys.executable
# the scenarios that both the original and the released module run (parity, rocket jumps)
PAR = ('^(f_rocket_jump|w_timing|w_special|w_countdown|r_ext|w_moving|i_items|i_drops'
       '|i_vote0|i_vote1|i_vote2|i_vote3|i_vote4|i_vote5|i_ot0|i_ot1|i_ot2|i_ot3|i_ot4|i_ot5|i_world|i_tpause)$')


def reference_source(arg, into: Path) -> Path:
    if arg:
        src = Path(arg).resolve()
    else:
        root = subprocess.run(['git', '-C', str(REPO), 'rev-list', '--max-parents=0', 'HEAD'],
                              capture_output=True, text=True)
        if root.returncode or not root.stdout.strip():
            raise SystemExit('no git history here: pass --reference DIR (the original OpenTDM module)')
        first = root.stdout.split()[-1]
        tar = subprocess.run(['git', '-C', str(REPO), '-c', 'core.autocrlf=false', 'archive', first, 'opentdm'],
                             capture_output=True)
        if tar.returncode:
            raise SystemExit(f'git archive {first} opentdm failed: {tar.stderr.decode(errors="replace")}')
        shutil.rmtree(into, ignore_errors=True)
        into.mkdir(parents=True)
        with tarfile.open(fileobj=io.BytesIO(tar.stdout)) as t:
            try:
                t.extractall(into, filter='data')
            except TypeError:       # Python without extraction filters
                t.extractall(into)
        src = into / 'opentdm'
        print(f'reference: opentdm/ of the first commit {first[:7]}')
    if not (src / 'g_main.c').is_file():
        raise SystemExit(f'{src} is not an OpenTDM source folder')
    return src


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--mingw')
    ap.add_argument('--ninja')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--build-dir', type=Path, default=REPO / 'build')
    ap.add_argument('--reference', help='the original OpenTDM module sources (default: opentdm/ of the first commit)')
    ap.add_argument('--reuse-build', action='store_true', help='reuse <build>/obj/engine and <build>/obj/opentdm')
    ap.add_argument('--controls', action='store_true', help='also run the negative controls')
    a = ap.parse_args(argv)
    tc = bw.Toolchain(a.mingw, a.ninja, a.build_dir)
    b = tc.build
    runs, work = b / 'runs', b / 'harness'
    shutil.rmtree(runs, ignore_errors=True)
    runs.mkdir(parents=True)
    tool_args = ['--mingw', str(tc.mingw), '--ninja', str(tc.ninja)]
    log = {'started': time.strftime('%Y-%m-%d %H:%M:%S')}

    def step(name, cmd):
        t0 = time.time()
        r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, creationflags=0x08000000)
        (runs / f'{name}.log').write_text(r.stdout + r.stderr, encoding='utf-8')
        tail = (r.stdout + r.stderr).strip().splitlines()[-1:] if (r.stdout + r.stderr).strip() else []
        log[name] = {'exit': r.returncode, 'tail': tail, 'seconds': round(time.time() - t0, 1)}
        print(f'{name}: exit {r.returncode} {tail[0][:150] if tail else ""}', flush=True)
        return r.returncode

    eng_b, mod_b, mod_src = b / 'obj' / 'engine', b / 'obj' / 'opentdm', b / 'src' / 'opentdm-local'
    if not (a.reuse_build and (eng_b / 'q2proded.exe').is_file() and (mod_b / 'gamex86_64.dll').is_file()):
        bw.build_engine(tc, a.jobs)
        bw.build_module(tc, a.jobs)
    ref = reference_source(a.reference, b / 'src' / '_reference_archive')
    ref_src = bw.stage(ref, b / 'src' / 'opentdm-base', 'r1~opentdm-original')
    ref_b = b / 'obj' / 'opentdm-base'
    bw.meson_build(tc, ref_src, ref_b, bw.GAME_OPTS, 'gamex86_64.dll', a.jobs)

    rc = 0
    rc |= step('make_orig_src', [PY, HERE / 'make_rj_orig.py', '--out', work / 'orig-src'])
    rc |= step('build_orig', [PY, HERE / 'build_harness_win.py', '--variant', 'engine', '--engine-build', eng_b,
                              '--game-build', ref_b, '--mod-src', ref_src, '--out', work / 'orig', '--local-base',
                              '--harness-src', work / 'orig-src' / 'harness_rj.c'] + tool_args)
    rc |= step('build_cand', [PY, HERE / 'build_harness_win.py', '--variant', 'both', '--engine-build', eng_b,
                              '--game-build', mod_b, '--mod-src', mod_src, '--out', work / 'cand', '--local-base']
               + tool_args)
    if rc:
        raise SystemExit('a harness did not build: see ' + str(runs))
    j = ['--jobs', str(a.jobs), '--timeout', '1800', '--mingw', str(tc.mingw)]
    rc |= step('orig_10', [PY, HERE / 'run_harness_win.py', '--exe', work / 'orig' / 'harness-engine.exe',
                           '--rates', '10', '--only', PAR, '--out', runs / 'orig'] + j)
    rc |= step('cand_engine', [PY, HERE / 'run_harness_win.py', '--exe', work / 'cand' / 'harness-engine.exe',
                               '--rates', '10,30,60', '--out', runs / 'engine'] + j)
    rc |= step('cand_box', [PY, HERE / 'run_harness_win.py', '--exe', work / 'cand' / 'harness-box.exe',
                            '--rates', '10,20,30,40,50,60', '--out', runs / 'box'] + j)
    rc |= step('cand_sched', [PY, HERE / 'run_harness_win.py', '--exe', work / 'cand' / 'harness-engine.exe',
                              '--rates', '60', '--sched', '10,30,batch', '--out', runs / 'sched'] + j)
    rc |= step('falls', [PY, HERE / 'compare_falls.py', runs / 'engine'])
    rc |= step('parity', [PY, HERE / 'compare_parity.py', runs / 'orig' / 'harness-engine-10', runs / 'engine',
                          '--json', runs / 'parity.json'])
    if a.controls:
        rc |= step('controls', [PY, HERE / 'negative_controls.py', runs / 'orig' / 'harness-engine-10',
                                '--mod-src', mod_src, '--game-build', mod_b, '--engine-build', eng_b,
                                '--work', work, '--out', runs / 'controls', '--jobs', str(a.jobs)] + tool_args)
    log['passed'] = rc == 0
    (runs / 'summary.json').write_text(json.dumps(log, indent=1), encoding='utf-8')
    print('ALL PASSED' if rc == 0 else 'SOMETHING FAILED', '-', runs / 'summary.json')
    return 0 if rc == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
