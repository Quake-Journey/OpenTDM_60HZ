"""Build the tickrate-tests API3 harness natively for Windows x64.

Same compiler, defines, optimization and float model as the released pair:

* box variant (harness-box.exe): the module objects ARE the objects of the
  DLL build (<game-build>/gamex86_64.dll.p), linked into one executable with
  harness.c compiled by the same command. Only g_tdm_curl.c is compiled again
  without HAVE_CURL (no HTTP/DNS in tests; the file has no timing or physics
  code).
* engine variant (harness-engine.exe): every module source compiled by its
  DLL command plus `-include namespace.h` (renames the helpers the engine
  defines too; codegen otherwise identical), linked with the q2proded objects
  of the engine build and engine.c (the engine collision fixture), as
  tickrate-tests/mods/api3/build.py does on Linux.

This is not a test of the DLL file itself: the short start of the release
loads the real gamex86_64.dll in the real q2proded64.exe.

usage: python build_harness_win.py --mingw DIR --engine-build DIR --game-build DIR --mod-src DIR --out DIR
           [--variant box|engine|both] [--local-base] [--harness-src FILE] [--replace NAME=COPY ...]
"""
import argparse
import json
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / 'tools'))
from build_win64 import find_mingw, find_ninja, mingw_tool, tool_env  # noqa: E402

CREATE_NO_WINDOW = 0x08000000


def run(cmd, cwd=None, env=None):
    r = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env, capture_output=True, text=True,
                       creationflags=CREATE_NO_WINDOW)
    if r.returncode:
        print(' '.join(map(str, cmd)))
        print(r.stdout + r.stderr)
        raise SystemExit('build failed')
    return r.stdout


def split(cmd):
    # meson double-quotes every argument on Windows: inside quotes POSIX
    # shlex keeps path backslashes and turns \" into ", as the shell does
    return shlex.split(cmd, posix=True)


def strip_output(argv):
    out, skip = [], 0
    for arg in argv:
        if skip:
            skip -= 1
        elif arg in ('-MQ', '-MF', '-o', '-c'):
            skip = 1
        elif arg != '-MD':
            out.append(arg)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--mingw', help='MinGW-w64 folder (or MINGW_DIR)')
    ap.add_argument('--ninja')
    ap.add_argument('--variant', choices=('box', 'engine', 'both'), default='both')
    ap.add_argument('--engine-build', required=True, help='Meson build folder of q2proded.exe')
    ap.add_argument('--game-build', required=True, help='Meson build folder of the module')
    ap.add_argument('--mod-src', required=True, help='module source folder of that build')
    ap.add_argument('--api3', default=str(REPO / 'tests' / 'tickrate-tests' / 'mods' / 'api3'))
    ap.add_argument('--out', required=True, help='output folder')
    ap.add_argument('--local-base', action='store_true',
                    help='module is the local OpenTDM base: HARNESS_LOCAL_BASE + harness_local.h')
    ap.add_argument('--harness-src', help='harness source instead of api3/harness.c (make_rj_orig.py)')
    ap.add_argument('--replace', action='append', default=[],
                    help='NAME=COPY: compile COPY instead of module file NAME (negative controls)')
    a = ap.parse_args(argv)
    mingw = find_mingw(a.mingw)
    ninja = find_ninja(a.ninja)
    env = tool_env(mingw, ninja)
    objdump = mingw_tool(mingw, 'objdump.exe', 'x86_64-w64-mingw32-objdump.exe')
    objcopy = mingw_tool(mingw, 'objcopy.exe', 'x86_64-w64-mingw32-objcopy.exe')
    nm = mingw_tool(mingw, 'nm.exe', 'x86_64-w64-mingw32-nm.exe')
    replace = dict(r.split('=', 1) for r in a.replace)
    game = Path(a.game_build).resolve()
    engine = Path(a.engine_build).resolve()
    api3 = Path(a.api3).resolve()
    mod_src = Path(a.mod_src).resolve()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)

    cmds = {Path(e['file']).name: e for e in json.loads((game / 'compile_commands.json').read_text())
            if (Path(e['directory']) / e['file']).resolve().parent == mod_src}
    assert 'g_main.c' in cmds and 'g_tdm_curl.c' in cmds, sorted(cmds)

    def dll_cmd(name):
        e = cmds[name]
        # strip_output drops -o/-MQ/-MF and `-c SOURCE`
        return strip_output(split(e['command'])), e['directory'], e['file']

    # a config.h without HAVE_CURL for the no-curl g_tdm_curl.c
    nocurl_inc = out / 'nocurl-config'
    if (game / 'config.h').exists():
        nocurl_inc.mkdir(parents=True, exist_ok=True)
        lines = (game / 'config.h').read_text().splitlines()
        (nocurl_inc / 'config.h').write_text('\n'.join(l for l in lines if 'HAVE_CURL' not in l) + '\n')

    def compile_to(name, obj, extra=(), drop=()):
        argv, cwd, src = dll_cmd(name)
        argv = [x for x in argv if x not in drop]
        if name in replace:
            # the copy's quoted includes are the module's headers
            src = replace[name]
            argv = argv[:1] + ['-iquote', str(mod_src)] + argv[1:]
        if drop and (nocurl_inc / 'config.h').exists():
            argv = argv[:1] + ['-I' + str(nocurl_inc)] + argv[1:]
        run(argv + list(extra) + ['-c', src, '-o', str(obj)], cwd=cwd, env=env)
        return str(obj)

    nocurl = ('-DHAVE_CURL', '-DCURL_STATICLIB')
    harness_defs = ['-Wno-incompatible-pointer-types', '-DHARNESS_OPENTDM', '-DHARNESS_MOD="opentdm"',
                    '-I' + str(api3), '-include', str(HERE / 'harness_compat.h')]
    if a.local_base:
        harness_defs += ['-DHARNESS_LOCAL_BASE', '-include', str(HERE / 'harness_local.h')]

    def harness_obj(obj, extra):
        argv, cwd, _ = dll_cmd('g_main.c')
        argv = [x for x in argv if x not in nocurl]
        src = a.harness_src or str(api3 / 'harness.c')
        run(argv + extra + harness_defs + ['-c', src, '-o', str(obj)], cwd=cwd, env=env)
        return str(obj)

    gcc = dll_cmd('g_main.c')[0][0]
    built = []

    if a.variant in ('box', 'both'):
        box = out / 'box'
        box.mkdir(exist_ok=True)
        objs = sorted(str(p) for p in (game / 'gamex86_64.dll.p').glob('*.obj')
                      if p.name != 'g_tdm_curl.c.obj' and p.name[:-4] not in replace)
        objs.append(compile_to('g_tdm_curl.c', box / 'g_tdm_curl-nocurl.o', drop=nocurl))
        for name in replace:
            if name != 'g_tdm_curl.c':
                objs.append(compile_to(name, box / (Path(name).stem + '-replaced.o')))
        h = harness_obj(box / 'harness.o', [])
        exe = out / 'harness-box.exe'
        run([gcc, '-o', str(exe), h] + objs + ['-static', '-static-libgcc', '-lws2_32', '-lm'], env=env)
        if 'MessageBox' in run([objdump, '-p', str(exe)]):
            exe.unlink()
            raise SystemExit('harness-box.exe imports a MessageBox function: refused')
        built.append(f'{exe} ({len(objs)} module objects from the DLL build, no MessageBox import)')

    if a.variant in ('engine', 'both'):
        eng = out / 'engine'
        eng.mkdir(exist_ok=True)
        ns = ['-I' + str(api3), '-include', str(HERE / 'namespace_win.h')]

        with ThreadPoolExecutor(max_workers=4) as pool:
            objs = list(pool.map(lambda n: compile_to(n, eng / (Path(n).stem + '.o'), ns,
                                                      nocurl if n == 'g_tdm_curl.c' else ()),
                                 sorted(cmds)))
        h = harness_obj(eng / 'harness.o', ns + ['-DHARNESS_ENGINE'])

        link = run([ninja, '-C', str(engine), '-t', 'commands', 'q2proded.exe'], env=env).strip().splitlines()[-1]
        link = split(link)
        eobjs = [str(engine / x) for x in link if x.endswith('.obj')]
        # q2proded's main (windows/system.c) renamed, as engine/scheduler and
        # api3/build.py do with src/unix/system.c on Linux
        system = next(o for o in eobjs if o.endswith('src_windows_system.c.obj'))
        renamed = eng / 'system-main-renamed.o'
        run([objcopy, '--redefine-sym', 'main=q2pro_real_main', system, str(renamed)])
        eobjs[eobjs.index(system)] = str(renamed)
        # no modal windows from a test process: every engine object that
        # imports MessageBoxA gets its import redirected to the stub in
        # win_compat.c (stderr + exit code instead of a dialog)
        for i, o in enumerate(eobjs):
            syms = run([nm, o])
            if '__imp_MessageBoxA' in syms:
                quiet = eng / (Path(o).stem + '-nobox.o')
                run([objcopy, '--redefine-sym', '__imp_MessageBoxA=__imp_harness_MessageBoxA', o, str(quiet)])
                eobjs[i] = str(quiet)
        libs = [x for x in link if x.startswith(('-l', '-Wl,', '-static')) or x.endswith('.a')]
        libs = [x if not x.endswith('.a') or Path(x).is_absolute() else str(engine / x) for x in libs]
        ecmd = next(e for e in json.loads((engine / 'compile_commands.json').read_text())
                    if e['file'].replace('\\', '/').endswith('src/server/main.c'))
        eflags = [x for x in strip_output(split(ecmd['command']))
                  if x not in ('-fvisibility=hidden', '-Wmissing-prototypes')]
        run(eflags + ['-include', str(HERE / 'win_compat.h'), '-c', str(api3 / 'engine.c'),
                      '-o', str(eng / 'engine.o')], cwd=ecmd['directory'], env=env)
        run(eflags + ['-c', str(HERE / 'win_compat.c'), '-o', str(eng / 'win_compat.o')],
            cwd=ecmd['directory'], env=env)
        exe = out / 'harness-engine.exe'
        run([gcc, '-o', str(exe), h, str(eng / 'engine.o'), str(eng / 'win_compat.o')] + objs + eobjs + libs
            + ['-lws2_32', '-lwinmm', '-lm'], cwd=str(engine), env=env)
        if 'MessageBox' in run([objdump, '-p', str(exe)]):
            exe.unlink()
            raise SystemExit('harness-engine.exe still imports a MessageBox function: refused')
        built.append(f'{exe} ({len(objs)} module objects, {len(eobjs)} engine objects, no MessageBox import)')

    print('\n'.join(built))
    return 0


if __name__ == '__main__':
    sys.exit(main())
