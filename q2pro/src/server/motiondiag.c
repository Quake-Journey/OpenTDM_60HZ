/*
Copyright (C) 2026 opentdm-60hz

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/*
Movement delivery diagnostics (sv_motiondiag), the server half of the
remote player motion checks (TZ 5.H). Default off: the hooks test the
switch only (no clock reads, memory or file access).

On, for every spawned client (its slot and connection generation:
connect time + slot; a reconnect, a map or a rate change starts anew), a
bounded ring of the last MD_RING events:

  P  a received move packet: arrival time, usercmds run from it (new,
     backup after a loss, lastcmd repeated for a loss without backups),
     commands refused by the time budget, msec of the commands run
  O  a world step after which the client's authoritative origin differs
     from the previous step (Pmove results reaching the world), with the
     new origin
  S  (observer slot only, sv_motiondiag_observer) a frame actually written
     and transmitted to the observer: per other player entity in it, the
     frame number, the step and the origin as packed for the wire (1/8)
  V  the same frame: the observer's own view origin (chase target)
  R  the same frame: a projectile in it (blaster, rocket, grenade,
     hyperblaster, BFG effects) with its origin as packed for the wire
  Q  the same projectile: its old_origin as packed for the wire

  sv_motiondiag 1             start (all clients), 0 stops, keeps the data
  sv_motiondiag_observer N    record the frames sent to client slot N
                              (-1 none, the default; -2 every client)
  motiondiag_status           per client, one rcon packet (<= 900 bytes):
                              packet intervals med/p95/max, commands per
                              packet, backup/repeat/refused, origin change
                              intervals med/p95/max, share of steps without
                              a change while the client sent movement
  motiondiag_dump <name>      CSV of every event to <gamedir>/<name>.csv,
                              also the last events of a client that has
                              left (kept until its slot is taken again)

Times are the server's own clock (Sys_Microseconds); they are not
comparable with a client's clock without an alignment of the two. No
chat, rcon or passwords are recorded.
*/

#include "server.h"

#define MD_RING     8192
#define MD_SUMMARY  900

typedef struct {
    uint64_t    us;
    uint8_t     kind;               // 'P', 'O', 'S' or 'V'
    uint8_t     fresh, backup, repeat, refused;
    uint16_t    msec;
    uint32_t    step;               // sv.framenum at the event
    int16_t     ent;                // O/S/V: entity number
    uint32_t    frame;              // S/V: client frame number sent
    int32_t     org[3];             // O/S/V: origin in 1/8 units
} md_event_t;

typedef struct {
    md_event_t  *ring;
    unsigned    head, count;
    uint64_t    overflow;
    time_t      connect_time;       // connection generation
    int         spawncount, rate;
    vec3_t      origin;
    bool        have_origin;
    bool        moving_input;       // last packet carried movement keys
    unsigned    still_steps, input_steps;
} md_client_t;

static cvar_t       *sv_motiondiag;
static cvar_t       *sv_motiondiag_observer;
static md_client_t  md[MAX_CLIENTS];

static int          pace_before[PACE_MAX];
static unsigned     msec_before;

static void md_reset(md_client_t *m, client_t *cl)
{
    m->head = m->count = 0;
    m->overflow = 0;
    m->connect_time = cl ? cl->connect_time : 0;
    m->spawncount = sv.spawncount;
    m->rate = SV_FRAMERATE;
    m->have_origin = false;
    m->moving_input = false;
    m->still_steps = m->input_steps = 0;
}

static md_client_t *md_slot(client_t *cl)
{
    md_client_t *m = &md[cl->number];

    if (!m->ring)
        m->ring = Z_Malloc(sizeof(*m->ring) * MD_RING);
    if (m->connect_time != cl->connect_time || m->spawncount != sv.spawncount || m->rate != SV_FRAMERATE)
        md_reset(m, cl);
    return m;
}

static md_event_t *md_push(md_client_t *m, int kind)
{
    md_event_t *e = &m->ring[m->head];

    memset(e, 0, sizeof(*e));
    e->us = Sys_Microseconds();
    e->kind = kind;
    e->step = sv.framenum;
    m->head = (m->head + 1) % MD_RING;
    if (m->count < MD_RING)
        m->count++;
    else
        m->overflow++;
    return e;
}

static void md_free(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        Z_Freep(&md[i].ring);
    memset(md, 0, sizeof(md));
}

static void sv_motiondiag_changed(cvar_t *self)
{
    if (self->integer) {
        md_free();
        Com_Printf("motiondiag: collecting (sv_motiondiag 0 stops, motiondiag_status reads)\n");
    } else {
        Com_Printf("motiondiag: stopped, data kept for motiondiag_status / motiondiag_dump\n");
    }
}

// before a move message of sv_client is executed
void SV_MotionDiagMoveBegin(void)
{
    if (!sv_motiondiag->integer)
        return;
    memcpy(pace_before, sv_client->pace, sizeof(pace_before));
    msec_before = sv_client->cmd_msec_used;

}

// after it: `fresh` usercmds were new in this packet, `buttons_move` tells
// whether the newest one had movement input
void SV_MotionDiagMoveEnd(int fresh, bool buttons_move)
{
    if (!sv_motiondiag->integer || !sv_client || sv_client->state != cs_spawned)
        return;

    md_client_t *m = md_slot(sv_client);
    md_event_t *e = md_push(m, 'P');
    int run = sv_client->pace[PACE_CMDS] - pace_before[PACE_CMDS];
    int dups = sv_client->pace[PACE_DUPS] - pace_before[PACE_DUPS];
    int refused = sv_client->pace[PACE_REJECTS] - pace_before[PACE_REJECTS];

    // a counter reset of the 10 s window between the two reads
    if (run < 0 || dups < 0 || refused < 0) {
        run = fresh;
        dups = refused = 0;
    }
    e->fresh = min(fresh, 255);
    e->backup = min(max(run - fresh, 0), 255);
    e->repeat = min(dups, 255);
    e->refused = min(refused, 255);
    e->msec = min((unsigned)(sv_client->cmd_msec_used - msec_before), 65535u);
    m->moving_input = buttons_move;
}

// after every world step
void SV_MotionDiagStep(void)
{
    client_t *cl;

    if (!sv_motiondiag->integer)
        return;

    FOR_EACH_CLIENT(cl) {
        if (cl->state != cs_spawned || !cl->edict)
            continue;
        md_client_t *m = md_slot(cl);
        const float *org = cl->edict->s.origin;
        bool changed = !m->have_origin || !VectorCompare(org, m->origin);

        if (m->moving_input) {
            m->input_steps++;
            if (!changed)
                m->still_steps++;
        }
        if (changed && m->have_origin) {
            md_event_t *e = md_push(m, 'O');
            e->ent = cl->number + 1;
            for (int k = 0; k < 3; k++)
                e->org[k] = COORD2SHORT(org[k]);
        }
        VectorCopy(org, m->origin);
        m->have_origin = true;
    }
}

// after a frame was built, written and transmitted to `client`
void SV_MotionDiagSent(client_t *client)
{
    if (!sv_motiondiag->integer ||
        (client->number != sv_motiondiag_observer->integer && sv_motiondiag_observer->integer != -2) ||
        client->state != cs_spawned || !client->entities || !client->num_entities)
        return;

    md_client_t *m = md_slot(client);
    const client_frame_t *frame = &client->frames[client->framenum & UPDATE_MASK];
    md_event_t *e = md_push(m, 'V');

    e->ent = frame->clientNum + 1;
    e->frame = client->framenum;
    for (int k = 0; k < 3; k++)
        e->org[k] = frame->ps.pmove.origin[k];
    for (int i = 0; i < frame->num_entities; i++) {
        const entity_packed_t *s = &client->entities[(frame->first_entity + i) & (client->num_entities - 1)];
        if (s->number > sv_maxclients->integer) {
            if (!(s->effects & (EF_BLASTER | EF_ROCKET | EF_GRENADE | EF_HYPERBLASTER | EF_BFG)))
                continue;
            e = md_push(m, 'R');
            e->ent = s->number;
            e->frame = client->framenum;
            for (int k = 0; k < 3; k++)
                e->org[k] = s->origin[k];
            e = md_push(m, 'Q');
            e->ent = s->number;
            e->frame = client->framenum;
            for (int k = 0; k < 3; k++)
                e->org[k] = s->old_origin[k];
            continue;
        }
        if (s->number < 1 || s->number == client->number + 1)
            continue;
        e = md_push(m, 'S');
        e->ent = s->number;
        e->frame = client->framenum;
        for (int k = 0; k < 3; k++)
            e->org[k] = s->origin[k];
    }
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

#define MD_AT(m, i) (m)->ring[((m)->head + MD_RING - (m)->count + (i)) % MD_RING]

// intervals between consecutive events of a kind, in 0.1 ms: med/p95/max
static int md_intervals(const md_client_t *m, int kind, uint64_t *out3)
{
    uint64_t *v = Z_Malloc(sizeof(*v) * (m->count + 1));
    uint64_t last = 0;
    int n = 0;

    for (unsigned i = 0; i < m->count; i++) {
        const md_event_t *e = &MD_AT(m, i);
        if (e->kind != kind)
            continue;
        if (last)
            v[n++] = (e->us - last) / 100;
        last = e->us;
    }
    if (n) {
        qsort(v, n, sizeof(*v), cmp_u64);
        out3[0] = v[n / 2];
        out3[1] = v[(n * 95 + 99) / 100 - 1];
        out3[2] = v[n - 1];
    }
    Z_Free(v);
    return n;
}

static void SV_MotionDiagStatus_f(void)
{
    char buf[MD_SUMMARY + 1];
    size_t len = 0;
    client_t *cl;

    len += Q_scnprintf(buf + len, sizeof(buf) - len, "motiondiag %s, %d Hz world (intervals ms med/p95/max)\n",
                       sv_motiondiag->integer ? "on" : "off", SV_FRAMERATE);
    FOR_EACH_CLIENT(cl) {
        md_client_t *m = &md[cl->number];
        uint64_t p[3] = {0}, o[3] = {0};
        unsigned fresh = 0, backup = 0, repeat = 0, refused = 0, packets = 0;

        if (!m->ring || !m->count)
            continue;
        for (unsigned i = 0; i < m->count; i++) {
            const md_event_t *e = &MD_AT(m, i);
            if (e->kind != 'P')
                continue;
            packets++;
            fresh += e->fresh;
            backup += e->backup;
            repeat += e->repeat;
            refused += e->refused;
        }
        int np = md_intervals(m, 'P', p);
        int no = md_intervals(m, 'O', o);
        len += Q_scnprintf(buf + len, sizeof(buf) - len,
                           "%d %.10s pkt %d %.1f/%.1f/%.1f cmd/pkt %.2f bk %u rp %u rf %u | org %d %.1f/%.1f/%.1f still %u/%u\n",
                           cl->number, cl->name, packets, p[0] / 10.0, p[1] / 10.0, p[2] / 10.0,
                           packets ? (double)fresh / packets : 0.0, backup, repeat, refused,
                           no, o[0] / 10.0, o[1] / 10.0, o[2] / 10.0, m->still_steps, m->input_steps);
        (void)np;
        if (len >= sizeof(buf) - 1)
            break;
        if (cl->number != sv_motiondiag_observer->integer)
            continue;
        // frames sent to the observer, and per player entity in them the
        // intervals between sent origin changes and the frames repeating it
        uint64_t v[3] = {0};
        int nv = md_intervals(m, 'V', v);
        len += Q_scnprintf(buf + len, sizeof(buf) - len, "  sent frames %d %.1f/%.1f/%.1f\n",
                           nv + 1, v[0] / 10.0, v[1] / 10.0, v[2] / 10.0);
        for (int ent = 1; ent <= sv_maxclients->integer && len < sizeof(buf) - 1; ent++) {
            uint64_t *iv = Z_Malloc(sizeof(*iv) * (m->count + 1));
            uint64_t last_us = 0;
            int32_t last[3] = {0};
            int n = 0, frames = 0, same = 0;
            for (unsigned i = 0; i < m->count; i++) {
                const md_event_t *e = &MD_AT(m, i);
                if (e->kind != 'S' || e->ent != ent)
                    continue;
                if (frames && !memcmp(e->org, last, sizeof(last))) {
                    same++;
                } else {
                    if (last_us)
                        iv[n++] = (e->us - last_us) / 100;
                    last_us = e->us;
                }
                memcpy(last, e->org, sizeof(last));
                frames++;
            }
            if (frames) {
                qsort(iv, n, sizeof(*iv), cmp_u64);
                len += Q_scnprintf(buf + len, sizeof(buf) - len, "  ent %d in %d frames, changes %d %.1f/%.1f/%.1f, repeated %d\n",
                                   ent, frames, n, n ? iv[n / 2] / 10.0 : 0.0, n ? iv[(n * 95 + 99) / 100 - 1] / 10.0 : 0.0,
                                   n ? iv[n - 1] / 10.0 : 0.0, same);
            }
            Z_Free(iv);
        }
    }
    Com_Printf("%s", buf);
}

static void SV_MotionDiagDump_f(void)
{
    char path[MAX_OSPATH];
    qhandle_t f;
    client_t *cl;

    if (Cmd_Argc() < 2 || !COM_IsPath(Cmd_Argv(1))) {
        Com_Printf("Usage: %s <name>\n", Cmd_Argv(0));
        return;
    }
    Q_snprintf(path, sizeof(path), "%s.csv", Cmd_Argv(1));
    if (FS_OpenFile(path, &f, FS_MODE_WRITE | FS_FLAG_TEXT) < 0 || !f) {
        Com_Printf("motiondiag: couldn't open %s\n", path);
        return;
    }
    FS_FPrintf(f, "# world %d Hz, observer slot %d; origins in world units\n"
               "slot,kind,us,step,fresh,backup,repeat,refused,msec,ent,frame,x,y,z\n",
               SV_FRAMERATE, sv_motiondiag_observer->integer);
    // every slot with rows, also of a client that has left: its ring is kept
    // until the slot is taken again (a test player's last minute is dumped
    // after the player left). With the game stopped (killserver, a dropped
    // map) the client pool is freed and every client has left (Fable 18.11 E3).
    int slots = svs.client_pool ? svs.maxclients : MAX_CLIENTS;
    for (int n = 0; n < slots && n < MAX_CLIENTS; n++) {
        md_client_t *m = &md[n];
        if (!m->ring || !m->count)
            continue;
        cl = svs.client_pool ? &svs.client_pool[n] : NULL;
        if (!cl || cl->state < cs_connected || cl->connect_time != m->connect_time)
            FS_FPrintf(f, "# slot %d: the client has left\n", n);
        for (unsigned i = 0; i < m->count; i++) {
            const md_event_t *e = &MD_AT(m, i);
            FS_FPrintf(f, "%d,%c,%"PRIu64",%u,%u,%u,%u,%u,%u,%d,%u,%.3f,%.3f,%.3f\n", n, e->kind, e->us,
                       e->step, e->fresh, e->backup, e->repeat, e->refused, e->msec, e->ent, e->frame,
                       SHORT2COORD(e->org[0]), SHORT2COORD(e->org[1]), SHORT2COORD(e->org[2]));
        }
    }
    FS_CloseFile(f);
    Com_Printf("motiondiag: wrote %s\n", path);
}

void SV_MotionDiagInit(void)
{
    sv_motiondiag = Cvar_Get("sv_motiondiag", "0", 0);
    sv_motiondiag->changed = sv_motiondiag_changed;
    sv_motiondiag_observer = Cvar_Get("sv_motiondiag_observer", "-1", 0);
    Cmd_AddCommand("motiondiag_status", SV_MotionDiagStatus_f);
    Cmd_AddCommand("motiondiag_dump", SV_MotionDiagDump_f);
}
