"""Negative controls: each control undoes one part of the 60 Hz fixes in a
copy of the module (build_harness_win.py --replace) and must turn its parity
check red (compare_parity.py --expect-differ against the original module at
10 Hz) or fail its d_ scenario. A control that stays green means the check
would not see that part broken.

  window       the client's 10 Hz frame runs every world frame (no FRAMESYNC gate)
  windowclock  a weapon step from ClientThink sees the frame's own game clock
  grid         weapon projectiles fly in real time (no G_GridProjectile)
  freeze       a timeout freezes the clock on the next frame, resumes off the grid
  respawn      the respawn delay counts from the frame of the death
  matchstart   the match countdown counts from the frame of "ready"
  display      the display ignores the client's frame divisor
  angles       a grid projectile's angles are not carried between the grid steps
  itemtimers   item and quad timers count from the pickup frame
  pueffects    power-up effects are chosen on every world frame
  exitlevel    a failed map change leaves the frame counters off the grid
  quadpick     a dropped player quad: the rest counted from the pickup frame
  quadthr      the quad drop at death: "more than 1 s left" from the death frame
  dropitem     a dropped item turns touchable 1 s after the drop frame
  killrule     "kill" 5 s after the respawn counted from the command frame
  voteend      a time limit cut below the time played ends on the vote frame
  deadfx       a dead player keeps its effects until the grid frame
  deadblend    a dead player keeps its power-up tint until the grid frame
  otvote       a time limit cut in overtime / sudden death compared with the frame
  hurtgrid     a hurt zone counts its hits and sound from the frame of the touch
  multiwait    a trigger_multiple re-arms "wait" after the frame of the touch
  usedelay     a delayed target fires "delay" after the frame of the touch
  drown        the water effects (air, drowning) run every world frame
  jumppad      the jump pad sound repeats 1.5 s after the frame of the touch
  freezethink  a think due on the frozen grid frame runs on the first frame after the timeout
  freezetoss   a thing in flight loses only the frozen frame of a timeout, not its tick

usage: python negative_controls.py REF_DIR --mod-src DIR --game-build DIR --engine-build DIR
           --work DIR [--out DIR] [--only a,b] [--mingw DIR] [--jobs 4]
REF_DIR: <runs>/orig/harness-engine-10 of the original. Writes OUT/<name>/ and OUT/summary.json.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PY = sys.executable
NL = chr(10)

CONTROLS = {
    'window': ({'p_client.c': [('\tif (!FRAMESYNC)' + NL + '\t\treturn;' + NL + NL + '\tclient = ent->client;' + NL,
                                '\tclient = ent->client;' + NL)]}, 'w_timing'),
    'windowclock': ({'p_weapon.c': [('\tlevel.framenum = G_WindowFrame (framenum);' + NL, '')]}, 'w_timing|w_special'),
    'grid': ({'g_weapon.c': [('\tG_GridProjectile (bolt);' + NL, ''), ('\t\tG_GridProjectile (grenade);' + NL, ''),
                             ('\tG_GridProjectile (grenade);' + NL, ''), ('\tG_GridProjectile (rocket);' + NL, ''),
                             ('\tG_GridProjectile (bfg);' + NL, '')]}, 'f_rocket_jump|r_ext|w_timing'),
    'freeze': ({'g_main.c': [('\telse if (FRAMESYNC)' + NL + '\t{' + NL, '\telse' + NL + '\t{' + NL)],
                'g_tdm_core.c': [('\t\telse if (remaining <= 0 && FRAMESYNC)' + NL, '\t\telse if (remaining == 0)' + NL)]},
               'w_special'),
    'respawn': ({'p_client.c': [('G_WindowFrame (level.framenum) + SECS_TO_FRAMES(g_respawn_time->value)',
                                 'level.framenum + SECS_TO_FRAMES(g_respawn_time->value)')]}, 'w_special'),
    'matchstart': ({'g_tdm_core.c': [('G_WindowFrame (level.framenum) + SECS_TO_FRAMES(g_match_countdown->value)',
                                      'level.framenum + SECS_TO_FRAMES(g_match_countdown->value)')]}, 'w_countdown'),
    'display': ({'g_main.c': [('\tf1 = (float)(j + d < div ? j + d : div) / div;' + NL,
                               '\tf1 = (float)(j + 1 < div ? j + 1 : div) / div;' + NL)]}, 'd_customize'),
    'angles': ({'g_main.c': [('\t\ttemp->angles[i] = ent->grid_from_angles[i] + da * f1;' + NL, '')]}, 'd_customize'),
    'itemtimers': ({'g_items.c': [('\tent->nextthink = G_WindowFrame (level.framenum) + SECS_TO_FRAMES(delay);' + NL,
                                   '\tent->nextthink = level.framenum + SECS_TO_FRAMES(delay);' + NL),
                                  ('\t\tent->client->quad_framenum = G_WindowFrame (level.framenum) + timeout;',
                                   '\t\tent->client->quad_framenum = level.framenum + timeout;')]}, 'i_items'),
    'pueffects': ({'p_view.c': [('\tif (!FRAMESYNC)' + NL + '\t\treturn;' + NL + NL + '\tent->s.effects = 0;' + NL,
                                 '\tent->s.effects = 0;' + NL)]}, 'i_items'),
    'exitlevel': ({'g_main.c': [('\t\t\t\tlevel.realframenum++;' + NL + '\t\t\t\tlevel.framenum++;' + NL + '\t\t\t\treturn;' + NL,
                                 '\t\t\t\treturn;' + NL)]}, 'd_exitlevel'),
    'quadpick': ({'g_items.c': [('(ent->nextthink - G_WindowFrame (level.framenum))', '(ent->nextthink - level.framenum)')]},
                 'i_drops', 'WD qpick'),
    'quadthr': ({'p_client.c': [('(G_WindowFrame (level.framenum) + SECS_TO_FRAMES(1))', '(level.framenum + SECS_TO_FRAMES(1))')]},
                'i_drops', 'WD qthr'),
    'dropitem': ({'g_items.c': [('\tdropped->nextthink = G_WindowFrame (level.framenum) + SECS_TO_FRAMES(1);',
                                 '\tdropped->nextthink = level.framenum + SECS_TO_FRAMES(1);')]}, 'i_drops', 'WD drop'),
    'killrule': ({'g_cmds.c': [('(G_WindowFrame (level.framenum) - ent->client->respawn_framenum)',
                                '(level.framenum - ent->client->respawn_framenum)')]}, 'i_drops', 'WD kill5'),
    'voteend': ({'g_tdm_vote.c': [('\t\t\tif (level.match_end_framenum < G_WindowFrame (level.framenum))' + NL +
                                   '\t\t\t\tlevel.match_end_framenum = G_WindowFrame (level.framenum);' + NL,
                                   '\t\t\tif (level.match_end_framenum < level.framenum)' + NL +
                                   '\t\t\t\tlevel.match_end_framenum = level.framenum;' + NL)]},
                'i_vote0|i_vote1|i_vote2|i_vote3|i_vote4|i_vote5', 'WV'),
    'deadfx': ({'p_view.c': [('\tif (ent->health <= 0 || tdm_match_status == MM_SCOREBOARD)' + NL + '\t{' + NL +
                              '\t\tent->s.effects = 0;' + NL + '\t\tent->s.renderfx = 0;' + NL + '\t\treturn;' + NL + '\t}' + NL,
                              '')]}, 'd_death_fx'),
    'deadblend': ({'p_view.c': [('\t\tent->client->powerup_blend[3] = 0;' + NL, '')]}, 'd_death_fx'),
    'otvote': ({'g_tdm_vote.c': [('(level.match_end_framenum > G_WindowFrame (level.framenum) && (tdm_match_status',
                                  '(level.match_end_framenum > level.framenum && (tdm_match_status')]},
               'i_ot0|i_ot1|i_ot2|i_ot3|i_ot4|i_ot5', 'WT'),
    'hurtgrid': ({'g_trigger.c': [('\twf = G_WindowFrame (level.framenum);' + NL, '\twf = level.framenum;' + NL)]},
                 'i_world', 'WH hurt'),
    'multiwait': ({'g_trigger.c': [('ent->nextthink = G_WindowFrame (level.framenum) + G_GridFrames (ent->wait);',
                                    'ent->nextthink = level.framenum + SECS_TO_FRAMES(ent->wait);')]}, 'i_world', 'WH multi'),
    'usedelay': ({'g_utils.c': [('t->nextthink = G_WindowFrame (level.framenum) + G_GridFrames (ent->delay);',
                                 't->nextthink = level.framenum + SECS_TO_FRAMES(ent->delay);')]}, 'i_world', 'WH delay'),
    'drown': ({'p_view.c': [('\tif (!FRAMESYNC)' + NL + '\t\treturn;' + NL + NL + '\twaterlevel = current_player->waterlevel;',
                             '\twaterlevel = current_player->waterlevel;')]}, 'i_world', 'WH drown'),
    'jumppad': ({'g_trigger.c': [('if (other->fly_sound_debounce_framenum < G_WindowFrame (level.framenum))',
                                  'if (other->fly_sound_debounce_framenum < level.framenum)'),
                                 ('other->fly_sound_debounce_framenum = G_WindowFrame (level.framenum) + SECS_TO_FRAMES(1.5f);',
                                  'other->fly_sound_debounce_framenum = level.framenum + SECS_TO_FRAMES(1.5f);')]},
                'i_world', 'WH slide'),
    'freezethink': ({'g_phys.c': [('\t\tif (ent->nextthink == level.framenum)' + NL + '\t\t\tent->nextthink += div;' + NL, '')]},
                    'i_tpause', 'WM tthink'),
    'freezetoss': ({'g_phys.c': [('\t\t\tent->toss_resume_hold = div - 1;' + NL, '\t\t\t;' + NL)]}, 'i_tpause', 'WM tfly'),
}


def run(cmd):
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, creationflags=0x08000000)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('ref')
    ap.add_argument('--mod-src', required=True, help='module sources of the game build')
    ap.add_argument('--game-build', required=True)
    ap.add_argument('--engine-build', required=True)
    ap.add_argument('--work', required=True, help='folder for the control builds')
    ap.add_argument('--out', required=True, help='folder for the control runs')
    ap.add_argument('--only')
    ap.add_argument('--mingw')
    ap.add_argument('--ninja')
    ap.add_argument('--jobs', type=int, default=4)
    a = ap.parse_args(argv)
    ref, out, mod = a.ref, Path(a.out), Path(a.mod_src)
    tool_args = ['--mingw', a.mingw] if a.mingw else []
    build_args = tool_args + (['--ninja', a.ninja] if a.ninja else [])
    summary = {}
    if a.only and (out / 'summary.json').exists():
        summary = json.loads((out / 'summary.json').read_text(encoding='utf-8'))
    for name, spec in CONTROLS.items():
        if a.only and name not in a.only.split(','):
            continue
        files, scen = spec[0], spec[1]
        prefix = spec[2] if len(spec) > 2 else None
        d = out / name
        d.mkdir(parents=True, exist_ok=True)
        repl = []
        for fname, edits in files.items():
            t = (mod / fname).read_bytes().decode('latin-1').replace('\r' + NL, NL)
            for old, new in edits:
                assert t.count(old) == 1, (name, fname, old[:60])
                t = t.replace(old, new)
            (d / fname).write_bytes(t.encode('latin-1'))
            repl += ['--replace', f'{fname}={d / fname}']
        bdir = Path(a.work) / f'ctl-{name}'
        b = run([PY, HERE / 'build_harness_win.py', '--variant', 'engine', '--game-build', a.game_build,
                 '--engine-build', a.engine_build, '--mod-src', mod, '--out', bdir, '--local-base']
                + build_args + repl)
        if b.returncode:
            summary[name] = {'build_failed': (b.stdout + b.stderr)[-1500:]}
            print(name, 'BUILD FAILED')
            continue
        exe = bdir / 'harness-engine.exe'
        run([PY, HERE / 'run_harness_win.py', '--exe', exe, '--rates', '30,60', '--only',
             f'^({scen})$', '--out', d / 'run', '--jobs', str(a.jobs), '--timeout', '1800'] + tool_args)
        if scen.startswith('d_'):
            fails = sum(t.read_text(errors='replace').count('  FAIL') for t in (d / 'run').rglob(scen + '.txt'))
            summary[name] = {'red': fails > 0, 'fail_lines': fails}
        else:
            c = run([PY, HERE / 'compare_parity.py', ref, d / 'run', '--expect-differ', '--show', '0',
                     '--json', d / 'parity.json'])
            tail = c.stdout.strip().splitlines()
            rep = json.loads((d / 'parity.json').read_text(encoding='utf-8'))
            own = scen.split('|')
            diffs = [x['case'] for per in rep.values() for s, v in per.items() if s in own for x in v['diffs']]
            hits = [x for x in diffs if not prefix or x.startswith(prefix)]
            summary[name] = {'red': len(hits) > 0, 'differing_cases': len(diffs), 'of_the_kind': len(hits),
                             'examples': sorted(set(hits))[:6],
                             'per_scenario': [l for l in tail if '/' in l and 'missing' in l and any(s in l for s in own)]}
        print(name, 'RED (ok)' if summary[name].get('red') else 'GREEN (control failed)',
              summary[name].get('of_the_kind', summary[name].get('fail_lines', '')), flush=True)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'summary.json').write_text(json.dumps(summary, indent=1), encoding='utf-8')
    red = sum(1 for v in summary.values() if v.get('red'))
    print(f'{red} of {len(summary)} controls red')
    return 0 if summary and red == len(summary) else 1


if __name__ == '__main__':
    sys.exit(main())
