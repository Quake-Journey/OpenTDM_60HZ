/*
Copyright (C) 2026 opentdm-60hz

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

/*
World step pacing diagnostics of the dedicated server (sv_tickdiag).

Off (default): SV_Frame only tests the switch, no clock reads, no memory,
no file access. On: a bounded ring of the last TD_RING world steps (120 s at
60 Hz): high-resolution start time of each step and the scheduler debt it ran
with. Percentiles are computed when asked (tickdiag_status), never per step.
A map change or a different world rate starts a new window: rates are never
mixed in one distribution. Turning it off keeps the last summary readable.

  sv_tickdiag 1     start a new window
  sv_tickdiag 0     stop, keep the summary
  tickdiag_status   one-packet summary (<= 900 ASCII bytes), also via rcon
  tickdiag_reset    clear the window, keep collecting
  tickdiag_dump [name]  write the window's step times to <gamedir>/<name>.csv

Definitions:
  interval   time between the starts of two consecutive world steps
  late       scheduler debt at the step: credit beyond one step, in ms
             (how far behind its deadline the step ran, 1 ms resolution)
  catch-up   a step that ran in a call made right after the previous step
             without waiting (debt of a further step was already there);
             a series is a run of consecutive catch-up steps
  wakes      SV_Frame calls of the window; idle wakes ran no world step
             (network packet or an early timer wake)
  game, sent time from the step start to the end of the game frame and to
             the end of SV_SendClientMessages (the step's own work)
*/

#include "server.h"

#define TD_RING         (120 * 60)
#define TD_SUMMARY_MAX  900

typedef struct {
    uint64_t    us;         // step start, microseconds of the process clock
    uint16_t    late;       // scheduler debt in 1/16 ms
    uint8_t     catchup;
    uint32_t    game_us;    // step start -> game frame done
    uint32_t    sent_us;    // step start -> frames sent to the clients
} td_step_t;

static cvar_t   *sv_tickdiag;

static struct {
    td_step_t   *ring;
    unsigned    head, count;        // next write index, valid samples
    uint64_t    steps, overflow;    // steps of the window, samples overwritten
    uint64_t    wakes, idle_wakes;
    uint64_t    first_us;
    unsigned    series, series_len, series_max;
    unsigned    residual_resets;
    int         rate, spawncount;
    bool        prev_catchup_due;
    char        last[TD_SUMMARY_MAX + 1];
} td;

static void td_clear(void)
{
    td.head = td.count = 0;
    td.steps = td.overflow = td.wakes = td.idle_wakes = 0;
    td.first_us = 0;
    td.series = td.series_len = td.series_max = 0;
    td.residual_resets = 0;
    td.prev_catchup_due = false;
    td.rate = 0;
    td.spawncount = 0;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static uint64_t pct(const uint64_t *v, size_t n, unsigned p)
{
    size_t i = (n * p + 99) / 100;      // nearest-rank
    return v[i ? i - 1 : 0];
}

#define TD_AT(i)    td.ring[(td.head + TD_RING - td.count + (i)) % TD_RING]

static void td_summary(char *out, size_t size, const char *state)
{
    if (!td.ring || td.count < 2) {
        Q_snprintf(out, size, "tickdiag %s: no samples%s\n", state,
                   td.ring ? " yet" : (td.last[0] ? " (last window below)" : ""));
        return;
    }

    size_t n = td.count - 1;
    uint64_t *iv = Z_Malloc(sizeof(*iv) * n);
    uint64_t *lt = Z_Malloc(sizeof(*lt) * td.count);
    uint64_t *gw = Z_Malloc(sizeof(*gw) * td.count);
    uint64_t *sw = Z_Malloc(sizeof(*sw) * td.count);
    uint64_t nominal = 1000000 / td.rate;       // us
    size_t over15 = 0, within2 = 0;

    for (size_t i = 0; i < n; i++) {
        iv[i] = TD_AT(i + 1).us - TD_AT(i).us;
        if (iv[i] * 2 > nominal * 3)
            over15++;
        if (iv[i] + 2000 >= nominal && iv[i] <= nominal + 2000)
            within2++;
    }
    for (size_t i = 0; i < td.count; i++) {
        lt[i] = TD_AT(i).late;
        gw[i] = TD_AT(i).game_us;
        sw[i] = TD_AT(i).sent_us;
    }

    qsort(iv, n, sizeof(*iv), cmp_u64);
    qsort(lt, td.count, sizeof(*lt), cmp_u64);
    qsort(gw, td.count, sizeof(*gw), cmp_u64);
    qsort(sw, td.count, sizeof(*sw), cmp_u64);

    double secs = (TD_AT(td.count - 1).us - td.first_us) * 1e-6;

#define MS(x)   ((x) * 1e-3)
#define LMS(x)  ((x) / 16.0)
    Q_snprintf(out, size,
               "tickdiag %s: %d Hz, window %.1f s, steps %"PRIu64" (rate %.2f/s), "
               "kept %u%s\n"
               "interval ms: med %.2f p95 %.2f p99 %.2f max %.2f, nominal %.2f\n"
               "within +-2 ms %.2f%%, >1.5x nominal %.2f%% (%zu)\n"
               "late ms: p95 %.2f max %.2f; catch-up series %u, longest %u\n"
               "work ms: game p95 %.2f max %.2f; sent p95 %.2f max %.2f\n"
               "wakes %"PRIu64", idle %"PRIu64"; residual resets %u\n",
               state, td.rate, secs, td.steps, secs > 0 ? (td.steps - 1) / secs : 0.0,
               td.count, td.overflow ? va(" (oldest %"PRIu64" dropped)", td.overflow) : "",
               MS(pct(iv, n, 50)), MS(pct(iv, n, 95)), MS(pct(iv, n, 99)), MS(iv[n - 1]), MS(nominal),
               100.0 * within2 / n, 100.0 * over15 / n, over15,
               LMS(pct(lt, td.count, 95)), LMS(lt[td.count - 1]), td.series, td.series_max,
               MS(pct(gw, td.count, 95)), MS(gw[td.count - 1]), MS(pct(sw, td.count, 95)), MS(sw[td.count - 1]),
               td.wakes, td.idle_wakes, td.residual_resets);
#undef MS
#undef LMS

    Z_Free(iv);
    Z_Free(lt);
    Z_Free(gw);
    Z_Free(sw);
}

static void td_start(void)
{
    if (!td.ring)
        td.ring = Z_Malloc(sizeof(*td.ring) * TD_RING);
    td_clear();
    Com_Printf("tickdiag: collecting (sv_tickdiag 0 stops, tickdiag_status reads)\n");
}

static void td_stop(void)
{
    if (!td.ring)
        return;
    td_summary(td.last, sizeof(td.last), "stopped");
    Z_Freep(&td.ring);
    Com_Printf("tickdiag: stopped, summary kept for tickdiag_status\n");
}

static void sv_tickdiag_changed(cvar_t *self)
{
    if (self->integer)
        td_start();
    else
        td_stop();
}

/*
Called once per SV_Frame call, before any world step. `stepping` tells
whether this call will run a world step.
*/
void SV_TickDiagWake(bool stepping)
{
    if (!sv_tickdiag->integer || !td.ring)
        return;
    td.wakes++;
    if (!stepping)
        td.idle_wakes++;
}

/*
Called at the start of a world step with the scheduler credit before the
step is paid for (FRAME_CREDIT units, see utils.h).
*/
void SV_TickDiagStep(uint64_t credit)
{
    if (!sv_tickdiag->integer || !td.ring)
        return;

    // a new map or world rate is a new window
    if (td.rate != SV_FRAMERATE || td.spawncount != sv.spawncount) {
        bool had = td.steps > 0;
        td_clear();
        td.rate = SV_FRAMERATE;
        td.spawncount = sv.spawncount;
        if (had)
            Com_Printf("tickdiag: new map or rate, new window\n");
    }

    uint64_t now = Sys_Microseconds();
    uint64_t debt = credit > FRAME_CREDIT ? credit - FRAME_CREDIT : 0;     // ms * rate
    uint64_t late16 = debt * 16 / SV_FRAMERATE;

    td_step_t *s = &td.ring[td.head];
    s->us = now;
    s->late = late16 > UINT16_MAX ? UINT16_MAX : late16;
    s->catchup = td.prev_catchup_due;

    if (s->catchup) {
        if (!td.series_len++)
            td.series++;
        td.series_max = max(td.series_max, td.series_len);
    } else {
        td.series_len = 0;
    }

    // after this step is paid, is another one already due?
    td.prev_catchup_due = credit >= 2 * FRAME_CREDIT;

    if (!td.steps)
        td.first_us = now;
    td.steps++;
    td.head = (td.head + 1) % TD_RING;
    if (td.count < TD_RING)
        td.count++;
    else
        td.overflow++;
}

/*
Called after the game frame (sent false) and after the frames were sent
to the clients (sent true) of the step SV_TickDiagStep recorded last.
*/
void SV_TickDiagStepDone(bool sent)
{
    if (!sv_tickdiag->integer || !td.ring || !td.count)
        return;
    td_step_t *s = &td.ring[(td.head + TD_RING - 1) % TD_RING];
    uint64_t d = Sys_Microseconds() - s->us;
    if (d > UINT32_MAX)
        d = UINT32_MAX;
    if (sent)
        s->sent_us = d;
    else
        s->game_us = d;
}

void SV_TickDiagResidualReset(void)
{
    if (sv_tickdiag->integer && td.ring)
        td.residual_resets++;
}

static void SV_TickDiagStatus_f(void)
{
    char buf[TD_SUMMARY_MAX + 1];

    if (td.ring) {
        td_summary(buf, sizeof(buf), "on");
        Com_Printf("%s", buf);
    } else if (td.last[0]) {
        Com_Printf("tickdiag off; last window:\n%s", td.last);
    } else {
        Com_Printf("tickdiag off, no window collected (set sv_tickdiag 1)\n");
    }
}

static void SV_TickDiagReset_f(void)
{
    if (!td.ring) {
        Com_Printf("tickdiag is off\n");
        return;
    }
    td_clear();
    Com_Printf("tickdiag: window cleared\n");
}

static void SV_TickDiagDump_f(void)
{
    char path[MAX_OSPATH];
    qhandle_t f;

    if (!td.ring || td.count < 2) {
        Com_Printf("tickdiag: nothing to dump (collect with sv_tickdiag 1 first)\n");
        return;
    }
    if (Cmd_Argc() > 1) {
        if (!COM_IsPath(Cmd_Argv(1))) {
            Com_Printf("tickdiag: bad file name\n");
            return;
        }
        Q_snprintf(path, sizeof(path), "%s.csv", Cmd_Argv(1));
    } else {
        Q_snprintf(path, sizeof(path), "tickdiag-%d.csv", Sys_Milliseconds());
    }
    if (FS_OpenFile(path, &f, FS_MODE_WRITE | FS_FLAG_TEXT) < 0 || !f) {
        Com_Printf("tickdiag: couldn't open %s for writing\n", path);
        return;
    }
    FS_FPrintf(f, "# rate %d Hz\nstep_us,interval_us,late_ms,catchup,game_us,sent_us\n", td.rate);
    for (unsigned i = 0; i < td.count; i++) {
        const td_step_t *s = &TD_AT(i);
        uint64_t iv = i ? s->us - TD_AT(i - 1).us : 0;
        FS_FPrintf(f, "%"PRIu64",%"PRIu64",%.3f,%d,%u,%u\n", s->us - td.first_us, iv, s->late / 16.0, s->catchup,
                   s->game_us, s->sent_us);
    }
    FS_CloseFile(f);
    Com_Printf("tickdiag: wrote %u steps to %s\n", td.count, path);
}

void SV_TickDiagInit(void)
{
    sv_tickdiag = Cvar_Get("sv_tickdiag", "0", 0);
    sv_tickdiag->changed = sv_tickdiag_changed;
    if (sv_tickdiag->integer)
        td_start();

    Cmd_AddCommand("tickdiag_status", SV_TickDiagStatus_f);
    Cmd_AddCommand("tickdiag_reset", SV_TickDiagReset_f);
    Cmd_AddCommand("tickdiag_dump", SV_TickDiagDump_f);
}
