"""Windows x64 build of the OpenTDM 60 Hz server pair: q2proded64.exe and opentdm/gamex86_64.dll.

    python tools/build_win64.py --mingw C:/mingw64 [--ninja PATH] [--jobs 4]
                                [--build-dir build] [--out out] [--compare-with DIR] [--build-time release|now|N]

Needs MinGW-w64 GCC (x86_64-w64-mingw32), Python 3.10+ with Meson (pip install meson==1.10.2), Ninja, and
network access on the first run: Meson downloads curl 8.15.0 and zlib-ng 2.2.4 and checks their SHA-256
(q2pro/subprojects/*.wrap, opentdm/subprojects/*.wrap).

The sources are copied to <build>/src/q2pro and <build>/src/opentdm-local and built in <build>/obj/engine and
<build>/obj/opentdm. The binaries keep the relative path of a few zlib-ng source files (assertion texts);
with this layout it is the path of the accepted build, so the result can be compared with the released files
byte for byte, the PE time stamps aside (--compare-with). The version strings come from tools/release.json, and
so does the build time the compiler writes into __DATE__/__TIME__ (SOURCE_DATE_EPOCH; --build-time now for the
current time).

Result: <out>/q2proded64.exe, <out>/opentdm/gamex86_64.dll, <out>/manifest.json.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import sysconfig
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
RELEASE = json.loads((REPO / 'tools' / 'release.json').read_text(encoding='utf-8'))
MESON = [sys.executable, '-m', 'mesonbuild.mesonmain']
CREATE_NO_WINDOW = 0x08000000 if os.name == 'nt' else 0

# the options of the accepted build (the dedicated server links zlib only)
ENGINE_OPTS = ['-Dbuildtype=release', '-Dwrap_mode=forcefallback', '-Dc_link_args=-static',
               '-Dvariable-fps=true', '-Dgame-new-api=false', '-Dpacketdup-hack=true', '-Dmvd-server=true',
               '-Dzlib=enabled',
               '-Dlibpng=disabled', '-Dlibjpeg=disabled', '-Dlibcurl=disabled', '-Dopenal=disabled',
               '-Davcodec=disabled', '-Dsdl2=disabled', '-Dtests=false']
GAME_OPTS = ['-Dbuildtype=release', '-Doptimization=2', '-Dwrap_mode=forcefallback']


def run(cmd, cwd=None, env=None, quiet=False):
    r = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env, capture_output=True, text=True,
                       encoding='utf-8', errors='replace', creationflags=CREATE_NO_WINDOW)
    if r.returncode:
        print(r.stdout[-4000:] + r.stderr[-4000:])
        raise SystemExit(f'failed ({r.returncode}): {" ".join(str(c) for c in cmd[:6])}')
    if not quiet:
        tail = (r.stdout or '').strip().splitlines()[-1:]
        if tail:
            print('   ', tail[0][:200])
    return r.stdout


def sha256(p: Path) -> str:
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


# --- toolchain -----------------------------------------------------------------------------------------------

def find_mingw(arg: str | None) -> Path:
    cands = [Path(x) for x in (arg, os.environ.get('MINGW_DIR')) if x]
    gcc = shutil.which('x86_64-w64-mingw32-gcc')
    if gcc:
        cands.append(Path(gcc).resolve().parent.parent)
    for c in cands:
        if (c / 'bin' / 'x86_64-w64-mingw32-gcc.exe').is_file():
            return c.resolve()
    raise SystemExit('MinGW-w64 GCC not found: pass --mingw DIR (the folder that holds '
                     'bin\\x86_64-w64-mingw32-gcc.exe) or set MINGW_DIR')


def mingw_tool(mingw: Path, *names: str) -> Path:
    for n in names:
        if (mingw / 'bin' / n).is_file():
            return mingw / 'bin' / n
    raise SystemExit(f'none of {names} in {mingw / "bin"}')


def find_ninja(arg: str | None) -> Path:
    cands = [arg, os.environ.get('NINJA'), shutil.which('ninja')]
    for scheme in ('nt', 'nt_user'):
        try:
            cands.append(str(Path(sysconfig.get_path('scripts', scheme)) / 'ninja.exe'))
        except KeyError:
            pass
    for c in cands:
        if c and Path(c).is_file():
            return Path(c).resolve()
    raise SystemExit('Ninja not found: pass --ninja PATH, put ninja.exe in PATH or pip install ninja')


def tool_env(mingw: Path, ninja: Path, build_time: str | int | None = None) -> dict:
    """PATH with Ninja and MinGW first. build_time: 'release' (the recorded time of the released build, so
    __DATE__ / __TIME__ come out the same), 'now', or seconds since 1970 (UTC) for SOURCE_DATE_EPOCH."""
    env = dict(os.environ)
    env['PATH'] = os.pathsep.join([str(ninja.parent), str(mingw / 'bin'), env.get('PATH', '')])
    env['NINJA'] = str(ninja)
    env.pop('SOURCE_DATE_EPOCH', None)
    if build_time == 'release':
        env['SOURCE_DATE_EPOCH'] = str(RELEASE['build_time']['source_date_epoch'])
    elif build_time not in (None, 'now'):
        env['SOURCE_DATE_EPOCH'] = str(int(build_time))
    return env


def write_cross(mingw: Path, path: Path) -> Path:
    def p(x: Path) -> str:
        return x.as_posix()
    text = ('; generated by tools/build_win64.py\n[binaries]\n'
            f"c = '{p(mingw_tool(mingw, 'x86_64-w64-mingw32-gcc.exe'))}'\n"
            f"ar = '{p(mingw_tool(mingw, 'x86_64-w64-mingw32-gcc-ar.exe', 'gcc-ar.exe', 'ar.exe'))}'\n"
            f"strip = '{p(mingw_tool(mingw, 'strip.exe', 'x86_64-w64-mingw32-strip.exe'))}'\n"
            f"windres = '{p(mingw_tool(mingw, 'windres.exe', 'x86_64-w64-mingw32-windres.exe'))}'\n\n"
            "[host_machine]\nsystem = 'windows'\ncpu_family = 'x86_64'\ncpu = 'x86_64'\nendian = 'little'\n")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8', newline='\n')
    return path


class Toolchain:
    def __init__(self, mingw: str | None = None, ninja: str | None = None, build_dir: Path | None = None,
                 build_time: str | int | None = 'release'):
        self.mingw = find_mingw(mingw)
        self.ninja = find_ninja(ninja)
        self.build_time = build_time
        self.env = tool_env(self.mingw, self.ninja, build_time)
        self.build = Path(build_dir or REPO / 'build').resolve()
        self.cross = write_cross(self.mingw, self.build / 'cross-win64.txt')
        self.objdump = mingw_tool(self.mingw, 'objdump.exe', 'x86_64-w64-mingw32-objdump.exe')
        self.objcopy = mingw_tool(self.mingw, 'objcopy.exe', 'x86_64-w64-mingw32-objcopy.exe')
        self.nm = mingw_tool(self.mingw, 'nm.exe', 'x86_64-w64-mingw32-nm.exe')
        self.gcc = mingw_tool(self.mingw, 'x86_64-w64-mingw32-gcc.exe')

    def versions(self) -> dict:
        def first(cmd):
            return run(cmd, env=self.env, quiet=True).strip().splitlines()[0]
        return {'gcc': first([self.gcc, '--version']),
                'ld': first([mingw_tool(self.mingw, 'ld.exe', 'x86_64-w64-mingw32-ld.exe'), '--version']),
                'meson': first(MESON + ['--version']),
                'ninja': first([self.ninja, '--version']),
                'python': sys.version.split()[0]}


# --- build ---------------------------------------------------------------------------------------------------

def stage(src: Path, dst: Path, version: str) -> Path:
    """A copy of src at dst (the downloaded package cache of an earlier run is kept) with its VERSION file."""
    cache = dst / 'subprojects' / 'packagecache'
    keep = dst.parent / f'.{dst.name}.packagecache'
    if cache.is_dir():
        shutil.rmtree(keep, ignore_errors=True)
        shutil.move(str(cache), str(keep))
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns('.git'))
    if keep.is_dir():
        (dst / 'subprojects').mkdir(exist_ok=True)
        shutil.rmtree(dst / 'subprojects' / 'packagecache', ignore_errors=True)
        shutil.move(str(keep), str(cache))
    (dst / 'VERSION').write_text(version + '\n', encoding='utf-8', newline='\n')
    return dst


def meson_build(tc: Toolchain, src: Path, bdir: Path, opts: list, target: str, jobs: int) -> Path:
    shutil.rmtree(bdir, ignore_errors=True)
    bdir.parent.mkdir(parents=True, exist_ok=True)
    print(f'meson setup {bdir.name} ({src.name})')
    run(MESON + ['setup', bdir, src, '--cross-file', tc.cross] + opts, cwd=src, env=tc.env)
    print(f'ninja {target}')
    run([tc.ninja, '-C', bdir, '-j', str(jobs), target], env=tc.env)
    return bdir / target


def build_engine(tc: Toolchain, jobs: int) -> Path:
    src = stage(REPO / 'q2pro', tc.build / 'src' / 'q2pro', RELEASE['engine']['version'])
    return meson_build(tc, src, tc.build / 'obj' / 'engine', ENGINE_OPTS, 'q2proded.exe', jobs)


def build_module(tc: Toolchain, jobs: int) -> Path:
    src = stage(REPO / 'opentdm', tc.build / 'src' / 'opentdm-local', RELEASE['module']['version'])
    return meson_build(tc, src, tc.build / 'obj' / 'opentdm', GAME_OPTS, 'gamex86_64.dll', jobs)


# --- comparison with the accepted files ----------------------------------------------------------------------

def pe_time_fields(data: bytes) -> dict:
    """Byte ranges of the PE fields that hold the link time (or derive from the bytes): name -> (offset, size)."""
    e = struct.unpack_from('<I', data, 0x3C)[0]
    if data[e:e + 4] != b'PE\0\0':
        raise ValueError('not a PE file')
    coff = e + 4
    nsec, = struct.unpack_from('<H', data, coff + 2)
    optsize, = struct.unpack_from('<H', data, coff + 16)
    opt = coff + 20
    magic, = struct.unpack_from('<H', data, opt)
    fields = {'COFF header TimeDateStamp': (coff + 4, 4), 'optional header CheckSum': (opt + 64, 4)}
    dirs = opt + (112 if magic == 0x20B else 96)
    secs = []
    for i in range(nsec):
        s = opt + optsize + 40 * i
        vsize, vaddr, rawsize, rawptr = struct.unpack_from('<IIII', data, s + 8)
        secs.append((vaddr, max(vsize, rawsize), rawptr))

    def off(rva):
        for vaddr, size, rawptr in secs:
            if vaddr <= rva < vaddr + size:
                return rawptr + rva - vaddr
        return None

    for idx, name in ((0, 'export directory TimeDateStamp'), (2, 'resource directory TimeDateStamp')):
        rva, size = struct.unpack_from('<II', data, dirs + 8 * idx)
        o = off(rva) if rva and size else None
        if o is not None:
            fields[name] = (o + 4, 4)
    rva, size = struct.unpack_from('<II', data, dirs + 8 * 6)
    o = off(rva) if rva and size else None
    if o is not None:
        for k in range(size // 28):
            fields[f'debug directory {k} TimeDateStamp'] = (o + 28 * k + 4, 4)
    return fields


def compare_pe(built: Path, accepted: Path) -> dict:
    a, b = built.read_bytes(), accepted.read_bytes()
    res = {'built_sha256': hashlib.sha256(a).hexdigest(), 'accepted_sha256': hashlib.sha256(b).hexdigest()}
    res['identical'] = a == b
    if res['identical']:
        res['verdict'] = 'identical'
        return res
    if len(a) != len(b):
        res['verdict'] = f'different size ({len(a)} / {len(b)} bytes)'
        return res
    fields = pe_time_fields(b)
    # the __DATE__ / __TIME__ texts ("Nov 18 2026" and "Nov 18 2026, 17:07:41") at the same place in both files
    stamp = re.compile(rb'[A-Z][a-z]{2} [ 0-9][0-9] [0-9]{4}(?:, [0-9]{2}:[0-9]{2}:[0-9]{2})?')
    for m in stamp.finditer(b):
        if stamp.fullmatch(a[m.start():m.end()]):
            fields[f'build date/time text @{m.start():#x}'] = (m.start(), m.end() - m.start())
    explained, other = {}, []
    for i in (i for i in range(len(a)) if a[i] != b[i]):
        name = next((n for n, (o, s) in fields.items() if o <= i < o + s), None)
        if name:
            explained[name] = explained.get(name, 0) + 1
        else:
            other.append(i)
    res['differing_fields'] = sorted(explained)
    res['unexplained_bytes'] = len(other)
    res['first_unexplained_offsets'] = [hex(i) for i in other[:8]]
    res['verdict'] = ('identical except ' + ', '.join(sorted(explained))) if not other else \
        f'{len(other)} bytes differ outside the PE time fields'
    return res


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--mingw', help='MinGW-w64 folder (holds bin/x86_64-w64-mingw32-gcc.exe); or MINGW_DIR')
    ap.add_argument('--ninja', help='ninja.exe (default: PATH, NINJA, the pip ninja package)')
    ap.add_argument('--jobs', type=int, default=4, help='compiler jobs (default 4)')
    ap.add_argument('--build-dir', type=Path, default=REPO / 'build')
    ap.add_argument('--out', type=Path, default=REPO / 'out')
    ap.add_argument('--compare-with', type=Path,
                    help='a folder with the released q2proded64.exe and opentdm/gamex86_64.dll to compare with')
    ap.add_argument('--build-time', default='release',
                    help="the time the compiler writes into __DATE__/__TIME__: 'release' (default, the recorded "
                         "time of the released build), 'now', or seconds since 1970 UTC (SOURCE_DATE_EPOCH)")
    a = ap.parse_args(argv)
    tc = Toolchain(a.mingw, a.ninja, a.build_dir, a.build_time)
    versions = tc.versions()
    for k, v in versions.items():
        print(f'{k}: {v}')
    print(f'SOURCE_DATE_EPOCH: {tc.env.get("SOURCE_DATE_EPOCH", "not set (the current time)")}')
    exe = build_engine(tc, a.jobs)
    dll = build_module(tc, a.jobs)
    out = a.out.resolve()
    (out / 'opentdm').mkdir(parents=True, exist_ok=True)
    shutil.copyfile(exe, out / 'q2proded64.exe')
    shutil.copyfile(dll, out / 'opentdm' / 'gamex86_64.dll')
    files = {'q2proded64.exe': (out / 'q2proded64.exe', 'engine'),
             'opentdm/gamex86_64.dll': (out / 'opentdm' / 'gamex86_64.dll', 'module')}
    manifest = {'release': RELEASE['release'], 'tools': versions,
                'source_date_epoch': tc.env.get('SOURCE_DATE_EPOCH'),
                'engine_options': ENGINE_OPTS, 'module_options': GAME_OPTS, 'files': {}}
    for name, (path, part) in files.items():
        h = sha256(path)
        entry = {'version': RELEASE[part]['version'], 'sha256': h, 'size': path.stat().st_size,
                 'accepted_sha256': RELEASE[part]['accepted_sha256'],
                 'same_as_accepted': h == RELEASE[part]['accepted_sha256']}
        if a.compare_with:
            entry['comparison'] = compare_pe(path, a.compare_with / name)
        manifest['files'][name] = entry
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=1, ensure_ascii=False), encoding='utf-8')
    print()
    for name, e in manifest['files'].items():
        print(f'{name}: {e["sha256"]}')
        print(f'    accepted {e["accepted_sha256"]} -> {"same" if e["same_as_accepted"] else "not the same bytes"}')
        if 'comparison' in e:
            print(f'    compared with {a.compare_with / name}: {e["comparison"]["verdict"]}')
    print(f'manifest: {out / "manifest.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
