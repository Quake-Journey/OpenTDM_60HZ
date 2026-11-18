/* opentdm-60hz: ballistic trajectories of toss/bounce entities (TZ 5.A).
 *
 * Each scenario spawns a real game entity (fire_grenade, fire_grenade2,
 * Drop_Item), fixes only its launch velocity (the random parts), and prints
 * one TRJ line at every 100 ms mark since the entity's own birth:
 *
 *   TRJ <tag> <t_ms> <x> <y> <z> <vx> <vy> <vz> <onground> <inuse>
 *
 * The comparison is done outside (tools/harness/compare_ballistics.py):
 * every rate against the 10 Hz run of the same scenario, the oracle. The
 * scenarios themselves only check that the entity exists and that the fuse
 * does not end the run. Box geometry: floor top z = 0, walls added here.
 *
 * b_phase_<k>: the same floor bounce spawned k world steps into a 100 ms
 * period (k < steps per 100 ms), to show the arc does not depend on the
 * birth phase. */

#define B_FUSE      30.0f   /* seconds: no explosion during a run */
#define B_SECONDS   3

static edict_t *b_owner;

static edict_t *b_find_new(int before, const char *classname)
{
    for (int n = before; n < ge->num_edicts; n++) {
        edict_t *e = EDICT(n);
        if (e->inuse && e->classname && !strcmp(e->classname, classname))
            return e;
    }
    for (int n = 1; n < ge->num_edicts; n++) {      /* a reused slot */
        edict_t *e = EDICT(n);
        if (e->inuse && e->classname && !strcmp(e->classname, classname) && e->owner == b_owner)
            return e;
    }
    return NULL;
}

static void b_scene(void)
{
    h_scene_base();
    /* wall facing -x at x = 600, corner wall facing -y at y = 1600 */
    h_add_world(600, -4096, 0, 664, 4096, 1024);
    h_add_world(-4096, 1600, 0, 600, 1664, 1024);
    h_start(h_ents);
    b_owner = h_join(0, "thrower");
}

static void b_trace(const char *tag, edict_t *e, int seconds)
{
    int per = h_rate / 10, marks = seconds * 10;
    for (int k = 0; k <= marks; k++) {
        if (k)
            for (int i = 0; i < per; i++) h_frame();
        int in = e->inuse;
        printf("TRJ %s %d %.4f %.4f %.4f %.4f %.4f %.4f %d %d\n", tag, k * 100,
               in ? e->s.origin[0] : 0, in ? e->s.origin[1] : 0, in ? e->s.origin[2] : 0,
               in ? e->velocity[0] : 0, in ? e->velocity[1] : 0, in ? e->velocity[2] : 0,
               in && e->groundentity != NULL, in);
    }
}

static edict_t *b_grenade(const vec3_t start, const vec3_t vel, int hand)
{
    int before = ge->num_edicts;
    vec3_t aim = {1, 0, 0};
    if (hand)
        fire_grenade2(b_owner, (float *)start, aim, 125, 600, B_FUSE, 165, false);
    else
        fire_grenade(b_owner, (float *)start, aim, 120, 600, B_FUSE, 160);
    edict_t *g = b_find_new(before, hand ? "hgrenade" : "grenade");
    CHECK(g != NULL, "%s spawned", hand ? "hand grenade" : "grenade");
    if (!g) return NULL;
    VectorCopy(vel, g->velocity);
    gi.linkentity(g);
    return g;
}

static void b_run(const char *tag, const vec3_t start, const vec3_t vel, int hand, int phase)
{
    b_scene();
    for (int i = 0; i < phase; i++) h_frame();
    edict_t *g = b_grenade(start, vel, hand);
    if (!g) return;
    b_trace(tag, g, B_SECONDS);
    CHECK(g->inuse, "%s still there after %d s (no fuse)", tag, B_SECONDS);
}

static void sc_b_free(void)
{
    const vec3_t s = {0, 1000, 10000}, v = {400, 0, 300};
    b_run("free", s, v, 0, 0);
}

static void sc_b_floor(void)
{
    const vec3_t s = {0, 1000, 40}, v = {300, 0, 250};
    b_run("floor", s, v, 0, 0);
}

static void sc_b_wall(void)
{
    const vec3_t s = {300, 1000, 90}, v = {500, 0, 180};
    b_run("wall", s, v, 0, 0);
}

/* the arc of b_wall from z 60 meets the wall and the floor in the same
 * 100 ms move, both at its very end (x 600, z 0): which plane is hit first
 * is decided by float rounding, at any rate. Reported, not a pass/fail. */
static void sc_b_edge(void)
{
    const vec3_t s = {300, 1000, 60}, v = {500, 0, 180};
    b_run("edge", s, v, 0, 0);
}

static void sc_b_corner(void)
{
    const vec3_t s = {300, 1300, 80}, v = {450, 450, 220};
    b_run("corner", s, v, 0, 0);
}

static void sc_b_low(void)
{
    const vec3_t s = {0, 1000, 8}, v = {200, 0, 40};
    b_run("low", s, v, 0, 0);
}

static void sc_b_hand(void)
{
    const vec3_t s = {0, 1000, 60}, v = {350, -120, 260};
    b_run("hand", s, v, 1, 0);
}

static void b_phase(int k)
{
    char tag[32];
    const vec3_t s = {0, 1000, 40}, v = {300, 0, 250};
    if (k >= h_rate / 10) {             /* no such phase at this rate: same as phase 0 */
        k = 0;
    }
    snprintf(tag, sizeof(tag), "floor");
    b_run(tag, s, v, 0, k);
}

static void sc_b_phase1(void) { b_phase(1); }
static void sc_b_phase3(void) { b_phase(3); }
static void sc_b_phase5(void) { b_phase(5); }

/* a weapon dropped by a player standing at the spawn, facing +x */
static void sc_b_drop(void)
{
    b_scene();
    h_give(b_owner, "rocket launcher", 0);
    h_frames_n(2);
    int before = ge->num_edicts;
    edict_t *d = Drop_Item(b_owner, FindItem("Rocket Launcher"));
    (void)before;
    CHECK(d != NULL && d->inuse, "rocket launcher dropped");
    if (!d) return;
    d->nextthink = level.framenum + SECS_TO_FRAMES(B_FUSE);     /* no item expiry */
    b_trace("drop", d, 2);
}

/* A body queue slot is reused without G_FreeEdict's memset (reserved
 * slots): CopyToBodyQue must start its toss segment anew. Unit check of
 * that reset (the only mod-private fields this file reads). */
void CopyToBodyQue(edict_t *ent);
static void sc_b_body_reuse(void)
{
    b_scene();
    edict_t *body = &g_edicts[game.maxclients + level.body_que + 1];
    body->toss_step = 3;
    body->toss_hold = true;
    CopyToBodyQue(b_owner);
    CHECK(body->toss_step == 0 && !body->toss_hold, "reused body: toss segment reset (step %d, hold %d)",
          body->toss_step, body->toss_hold);
}

/* opentdm-60hz TZ 5.B: the temporary view effects of a fall or of damage
 * (pitch/roll kick and the view height dip) settle during a timeout, while
 * the game clock stands still. Based on Codex's codex_fall_kick_pause probe. */
static void p_view_settles(int damage)
{
    edict_t *a, *b;
    h_tdm_pair(&a, &b);
    if (!h_tdm_start(a, b)) return;
    if (damage)
        a->health = a->max_health = 999;    /* real damage, no death */
    else
        a->flags |= FL_GODMODE;
    h_frames_n(h_rate);
    float height = a->client->ps.viewoffset[2];         /* standing, settled */
    if (damage) {
        vec3_t dir = {1, 0, 0};
        vec3_t point = {a->s.origin[0] + 64, a->s.origin[1] + 32, a->s.origin[2]};   /* hit from the front-left */
        T_Damage(a, b, b, dir, point, vec3_origin, 50, 50, 0, MOD_ROCKET);
        /* the knockback's velocity would add its own (frozen) view pitch */
        VectorClear(a->velocity); VectorClear(a->client->oldvelocity);
        h_frame();
        VectorClear(a->velocity); VectorClear(a->client->oldvelocity);
        h_real_phase(0); h_frame();
    } else {
        /* the fall is detected on the 100 ms grid (C1): land on a grid step */
        h_real_phase(0);
        a->velocity[2] = -900; h_frame();
        VectorClear(a->velocity); VectorClear(a->client->oldvelocity);
        h_real_phase(0); h_frame();
    }
    float kick0 = a->client->ps.kick_angles[PITCH], roll0 = a->client->ps.kick_angles[ROLL];
    float dip0 = height - a->client->ps.viewoffset[2];
    int frame0 = level.framenum;
    h_client_command(a, "time");
    CHECK(tdm_match_status == MM_TIMEOUT, "timeout entered");
#ifdef HARNESS_LOCAL_BASE
    /* candidate 4: the game clock freezes on the next grid frame, as on the next tick at 10 Hz */
    while (!level.timeout_frozen) h_frame();
    frame0 = level.framenum;
#endif
    h_frames_n(2 * h_rate);
    float kick = a->client->ps.kick_angles[PITCH], roll = a->client->ps.kick_angles[ROLL];
    float dip = height - a->client->ps.viewoffset[2];
    printf("OBS %s rate=%d kick %.4f->%.4f roll %.4f->%.4f dip %.4f->%.4f\n", damage ? "damage" : "fall",
           h_rate, kick0, kick, roll0, roll, dip0, dip);
    CHECK(fabsf(kick0) > 0.01f || fabsf(roll0) > 0.01f || fabsf(dip0) > 0.01f, "the %s made a view effect",
          damage ? "damage" : "fall");
    CHECK(fabsf(kick) < 0.01f && fabsf(roll) < 0.01f, "kick back to neutral during the timeout");
    CHECK(fabsf(dip) < 0.01f, "view height back to normal during the timeout (dip %.4f)", dip);
    CHECK(level.framenum == frame0, "game clock stood still (%d -> %d)", frame0, level.framenum);
}

static void sc_p_fall_timeout(void) { p_view_settles(0); }
static void sc_p_damage_timeout(void) { p_view_settles(1); }

#ifdef HARNESS_LOCAL_BASE
/* opentdm-60hz review 1, applicability of q2trek 33ea415 ("Keep a timeout
 * called in sudden death"): a timeout called in sudden death (g_tie_mode 2)
 * or in overtime (g_tie_mode 1, the profile's default) holds until the caller
 * resumes, then the match goes on in the same state. The local base tests the
 * deadline with "remaining == 0", so a passed deadline does not end the
 * match again while the game clock stands still. */
static void m_tie_timeout(int mode)
{
    const char *what = mode == 2 ? "sudden death" : "overtime";
    int want = mode == 2 ? MM_SUDDEN_DEATH : MM_OVERTIME;
    edict_t *a, *b;
    imp_cvar_set("g_tie_mode", mode == 2 ? "2" : "1");
    h_tdm_pair(&a, &b);
    if (!h_tdm_start(a, b)) { imp_cvar_set("g_tie_mode", "1"); return; }
    a->flags |= FL_GODMODE;
    b->flags |= FL_GODMODE;
    level.match_end_framenum = level.framenum + h_rate;    /* 1 s left at 0:0 */
    for (int i = 0; i < 3 * h_rate && tdm_match_status != want; i++) h_frame();
    CHECK(tdm_match_status == want, "tie at the deadline: %s (status %d)", what, tdm_match_status);
    h_frames_n(h_rate);
    h_client_command(a, "time");
    CHECK(tdm_match_status == MM_TIMEOUT, "timeout entered in %s (status %d)", what, tdm_match_status);
    h_frames_n(3 * h_rate);
    CHECK(tdm_match_status == MM_TIMEOUT, "%s: the timeout holds 3 s later (status %d)", what, tdm_match_status);
    h_client_command(a, "time");
    for (int i = 0; i < 15 * h_rate && tdm_match_status == MM_TIMEOUT; i++) h_frame();
    CHECK(tdm_match_status == want, "resumed into %s (status %d)", what, tdm_match_status);
    imp_cvar_set("g_tie_mode", "1");
}

static void sc_m_sd_timeout(void) { m_tie_timeout(2); }
static void sc_m_ot_timeout(void) { m_tie_timeout(1); }

/* applicability of q2trek b68d74c ("Go to the map an admin or a vote
 * picked"): changemap and a passed map vote set level.nextmap and call
 * EndDMLevel, which checks DF_SAME_LEVEL and sv_maplist first. With the
 * profile (dmflags 2, no sv_maplist) the picked map wins (CHECK); with
 * DF_SAME_LEVEL the old order keeps the current map (OBS only: an existing
 * OpenTDM behaviour, not a tick-rate effect, not ported). */
void EndDMLevel (void);
static void sc_m_picked_map(void)
{
    h_scene_base();
    h_start(h_ents);
    h_frames_n(2);
    imp_cvar_set("sv_maplist", "");
    imp_cvar_set("dmflags", "2");
    Q_strncpy(level.nextmap, "q2dm8", sizeof(level.nextmap) - 1);
    EndDMLevel();
    const char *got = level.changemap ? level.changemap : "(none)";
    CHECK(!strcmp(got, "q2dm8"), "profile dmflags 2, no sv_maplist: next map is the picked q2dm8 (got %s)", got);
    level.intermissionframe = 0;
    level.changemap = NULL;
    imp_cvar_set("dmflags", "34");      /* 2 | DF_SAME_LEVEL */
    Q_strncpy(level.nextmap, "q2dm8", sizeof(level.nextmap) - 1);
    EndDMLevel();
    printf("OBS dmflags 34 (DF_SAME_LEVEL): picked q2dm8, next map %s (map %s)\n",
           level.changemap ? level.changemap : "(none)", level.mapname);
    imp_cvar_set("dmflags", "0");
}
#define H_SCENARIOS_M \
    { "m_sd_timeout", sc_m_sd_timeout }, \
    { "m_ot_timeout", sc_m_ot_timeout }, \
    { "m_picked_map", sc_m_picked_map },
#else
#define H_SCENARIOS_M
#endif

#if defined HARNESS_LOCAL_BASE && defined HARNESS_ENGINE
/* opentdm-60hz review 2 (C1): falling damage keeps the 10 Hz contract.
 * Engine Pmove and collision. One usercmd stream for every rate: six usercmds
 * per 100 ms (17 17 16 17 17 16 msec); the world ticks after 60 / rate of
 * them, and the 100 ms grid of a 30 or 60 Hz world ends on the same usercmd
 * as a 10 Hz tick. Every case runs at landing phases k = 0..5 (the drop
 * starts with usercmd k of a 100 ms window). Printed per case and phase
 * (FALL lines): health lost, health drops, fallshort and footstep events,
 * fall and pain sounds, usercmds to the first landing. compare_falls.py
 * checks that 30 and 60 Hz print the 10 Hz lines exactly; the CHECKs here
 * only check that each case did what it is for. */
static const int f_msec[6] = { 17, 17, 16, 17, 17, 16 };
static int f_c, f_g, f_drops, f_last_hp, f_air, f_land;
static edict_t *f_p;
static short f_pitch;      /* view pitch of the f_ usercmds (0: level) */

static void f_cmd(int upmove, int buttons)
{
    usercmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.msec = (byte)f_msec[f_c % 6];
    cmd.upmove = (short)upmove;
    cmd.angles[PITCH] = f_pitch;
    cmd.buttons = (byte)buttons;
    h_pmove_self = f_p;
    ge->ClientThink(f_p, &cmd);
    if (!f_p->groundentity)
        f_air = 1;
    else if (f_air && f_land < 0)
        f_land = f_c;
    if (++f_c % f_g == 0) {
        h_frame();
        if (f_p->health < f_last_hp) f_drops++;
        f_last_hp = f_p->health;
    }
}

/* the next world tick is on the 100 ms grid; the usercmd counter restarts so
 * that the grid ticks end on usercmds 5, 11, 17, ... at every rate */
static void f_align(void)
{
    f_g = 60 / h_rate;
    h_real_phase(0);
    f_c = 6 - f_g;
}

enum { F_NONE, F_JUMP, F_TELEPORT, F_KILL, F_TIMEOUT };

/* one drop from `drop` units above the floor at x; returns the health lost */
static int f_case(const char *name, int k, float x, float drop, int what)
{
    for (int i = 0; i < 60; i++) f_cmd(0, 0);          /* settle (also a respawn) */
    f_p->health = 100;
    f_last_hp = 100;
    while (f_c % 6 != k) f_cmd(0, 0);
    int s0 = h_nsounds, falls0 = h_falls, steps0 = h_steps, start = f_c, ev = 0;
    int armor0 = f_p->client->ps.stats[STAT_ARMOR];
    f_drops = 0;
    f_air = 0;
    f_land = -1;
    h_place(f_p, x, 0, 24 + drop);
    int n = what == F_TIMEOUT ? 1200 : 240;
    for (int i = 0; i < n; i++) {
        /* events on the 100 ms grid, 0.2..0.3 s into the drop */
        if (!ev && i >= 12 && f_c % 6 == 0) {
            ev = f_c;
            if (what == F_TELEPORT) h_place(f_p, x, 300, 24);     /* onto the floor, at rest */
            if (what == F_KILL) h_client_command(f_p, "kill");
            if (what == F_TIMEOUT) h_client_command(f_p, "time");
        }
        /* resume: the game unfreezes one world tick after the countdown
         * (100 ms at 10 Hz, 17 ms at 60 Hz). The call is placed 6 - 60/rate
         * usercmds later so that the unfreezing tick ends on the same usercmd
         * at every rate: the fall subsystem is compared, not the match clock
         * (that tick difference is reported separately, inventory C2). */
        if (what == F_TIMEOUT && ev && f_c == ev + 180 + 6 - f_g) h_client_command(f_p, "time");
        int attack = what == F_KILL && ev && f_c >= ev + 90 && f_c < ev + 96;                /* respawn */
        f_cmd(what == F_JUMP ? 400 : 0, attack ? BUTTON_ATTACK : 0);
    }
    int lost = 100 - f_p->health;
    printf("FALL %s k=%d lost=%d armor=%d drops=%d fallshort=%d steps=%d fallsnd=%d painsnd=%d land=%d\n", name, k,
           lost, armor0 - f_p->client->ps.stats[STAT_ARMOR], f_drops, h_falls - falls0, h_steps - steps0,
           h_count_prefix("*fall", s0), h_count_prefix("*pain", s0), f_land < 0 ? -1 : f_land - start);
    return lost;
}

static void sc_f_fall_parity(void)
{
    if (60 % h_rate) {
        printf("OBS f_fall_parity: 60 usercmds per second do not divide into %d Hz ticks\n", h_rate);
        return;
    }
    h_scene_base();
    h_add_water(-2000, -200, 0, -1800, 200, 12);       /* waterlevel 1 */
    h_add_water(-2400, -200, 0, -2200, 200, 40);       /* waterlevel 2 */
    h_add_water(-2800, -200, 0, -2600, 200, 120);      /* waterlevel 3 */
    h_ent("{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"300 0 24\"\n\"angle\" \"0\"\n}\n");
    h_start(h_ents);
    f_p = h_join(0, "p1");
    edict_t *b = h_join(1, "p2");
    h_frames_n(3 * h_rate);
    /* every case runs in one long match: the timeout needs a match, and an
     * idle player of a long warmup leaves the team */
    imp_cvar_set("g_match_time", "3600");
    if (!h_tdm_start(f_p, b)) return;
    f_align();
    for (int k = 0; k < 6; k++) {
        f_case("timeout", k, -600, 400, F_TIMEOUT);
        CHECK(tdm_match_status == MM_PLAYING, "timeout k=%d: the match resumed (status %d)", k, tdm_match_status);
    }

    /* the CHECKs: landed, and the health lost in a sane range (armor takes
     * part of the fall); the 10 Hz equality is compare_falls.py's */
    static const struct { const char *name; float drop; int min, max; } heights[] = {
        { "h80", 80, 0, 0 }, { "h96", 96, 0, 0 }, { "h104", 104, 0, 0 },
        { "h180", 180, 0, 0 }, { "h188", 188, 0, 0 }, { "h196", 196, 0, 0 },
        { "h260", 260, 0, 10 }, { "h400", 400, 1, 25 },
    };
    for (int h = 0; h < (int)(sizeof(heights) / sizeof(heights[0])); h++)
        for (int k = 0; k < 6; k++) {
            int lost = f_case(heights[h].name, k, -600, heights[h].drop, F_NONE);
            CHECK(f_land >= 0 && lost >= heights[h].min && lost <= heights[h].max,
                  "%s k=%d: landed (%d), health lost %d in [%d, %d]", heights[h].name, k, f_land, lost,
                  heights[h].min, heights[h].max);
        }
    for (int k = 0; k < 6; k++) {
        f_case("water1", k, -1900, 400, F_NONE);
        f_case("water2", k, -2300, 400, F_NONE);
        int lost3 = f_case("water3", k, -2700, 400, F_NONE);
        CHECK(lost3 == 0, "water3 k=%d: no falling damage completely underwater (%d)", k, lost3);
        imp_cvar_set("dmflags", "8");                   /* DF_NO_FALLING */
        int lostn = f_case("nofalling", k, -600, 400, F_NONE);
        imp_cvar_set("dmflags", "0");
        CHECK(lostn == 0, "DF_NO_FALLING k=%d: no falling damage (%d)", k, lostn);
        f_case("jump", k, -600, 260, F_JUMP);
        f_case("teleport", k, -600, 400, F_TELEPORT);
        f_case("kill", k, -600, 400, F_KILL);
        CHECK(f_p->health > 0, "kill k=%d: respawned (health %d)", k, f_p->health);
    }
}

/* opentdm-60hz, PO local test 17.11: "rocket jumps are lower at 60 Hz".
 * The rocket jump with the engine Pmove and collision, in the usercmd stream
 * of f_fall_parity (six usercmds per 100 ms at every rate). The player stands
 * on the floor, looks straight down, the rocket launcher is ready; jump is
 * held for 100 ms from usercmd k of a 100 ms window, attack for 100 ms from
 * `delay` usercmds after the jump (0: in the same usercmd). RJ lines per case
 * and phase: usercmds from the jump to the rocket's spawn (fire) and to its
 * explosion, the origin height above standing and the vertical speed right
 * after the explosion, the apex height, the health lost. The 10 Hz lines are
 * the reference: the world rate must not change them. */
static edict_t *r_rocket(void)
{
    for (int n = game.maxclients + 1; n < ge->num_edicts; n++) {
        edict_t *e = &g_edicts[n];
        if (e->inuse && e->owner == f_p && e->classname && !strcmp(e->classname, "rocket"))
            return e;
    }
    return NULL;
}

static float r_case(const char *name, int k, int delay)
{
    h_place(f_p, -600, 0, 24);
    for (int i = 0; i < 90; i++) f_cmd(0, 0);          /* settle on the floor, weapon idle */
    f_p->health = 999;
    h_give(f_p, "rockets", 10);
    while (f_c % 6 != k) f_cmd(0, 0);
    int start = f_c, spawn = -1, expl = -1, hp0 = f_p->health;
    float z0 = f_p->s.origin[2], zx = 0, vzx = 0, apex = 0;
    for (int i = 0; i < 150; i++) {
        int c = f_c - start;
        int attack = c >= delay && c < delay + 6;
        f_cmd(c < 6 ? 400 : 0, attack ? BUTTON_ATTACK : 0);
        edict_t *r = r_rocket();
        if (r && spawn < 0) spawn = c;
        if (!r && spawn >= 0 && expl < 0) {
            expl = c;
            zx = f_p->s.origin[2] - z0;
            vzx = f_p->velocity[2];
        }
        if (f_p->s.origin[2] - z0 > apex) apex = f_p->s.origin[2] - z0;
    }
    printf("RJ %s k=%d fire=%d expl=%d z=%.1f vz=%.0f apex=%.1f lost=%d\n", name, k, spawn, expl, zx, vzx, apex,
           hp0 - f_p->health);
    return apex;
}

static void sc_f_rocket_jump(void)
{
    if (60 % h_rate) {
        printf("OBS f_rocket_jump: 60 usercmds per second do not divide into %d Hz ticks\n", h_rate);
        return;
    }
    h_scene_base();
    h_ent("{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"300 0 24\"\n\"angle\" \"0\"\n}\n");
    h_start(h_ents);
    f_p = h_join(0, "p1");
    edict_t *b = h_join(1, "p2");
    h_frames_n(3 * h_rate);
    imp_cvar_set("g_match_time", "3600");
    if (!h_tdm_start(f_p, b)) return;
    b->flags |= FL_GODMODE;
    h_give(f_p, "rocket launcher", 0);
    h_give(f_p, "rockets", 50);
    h_use(f_p, "rocket launcher");
    f_pitch = ANGLE2SHORT(89);
    f_align();
    static const struct { const char *name; int delay; } v[] = { { "same", 0 }, { "after1", 1 }, { "after3", 3 } };
    for (int j = 0; j < 3; j++)
        for (int k = 0; k < 6; k++) {
            float apex = r_case(v[j].name, k, v[j].delay);
            CHECK(apex > 60, "%s k=%d: a rocket jump (apex %.1f)", v[j].name, k, apex);
        }
    f_pitch = 0;
}
#include "scenarios_parity.h"
#define H_SCENARIOS_F \
    { "f_fall_parity", sc_f_fall_parity }, \
    { "f_rocket_jump", sc_f_rocket_jump }, \
    H_SCENARIOS_PARITY \
    { "d_customize", sc_d_customize }, \
    { "d_exitlevel", sc_d_exitlevel }, \
    { "d_death_fx", sc_d_death_fx },

/* opentdm-60hz candidate 4 (R5): the display of grid projectiles to clients
 * faster than 10 Hz. The module's CustomizeEntityToClient is called as the
 * engine calls it (after each world frame) with a stub of the engine's
 * OPENTDM60_FRAMEDIV_V1 for d = 1, 2, 3, 6 world frames per client frame.
 * Checked: only that callback is exported; no extension, no callback; a
 * projectile is hidden before its first grid step; d >= 6 (a 10 Hz client)
 * gets the grid state; otherwise origin = from + (to - from) * min(j + d, 6) / 6
 * and old_origin = ... * min(j, 6) / 6 (j: world frames since the step); for
 * d = 1 the origin of each frame is the old_origin of the next one, also
 * across a step (no jump); a rocket, a bolt and a grenade. */
typedef struct { int (*ClientFrameDiv)(edict_t *ent); } d_fdiv_t;
typedef struct {
    uint32 apiversion, structsize;
    void *local_sound, *get_configstring, *clip, *inVIS;
    void *(*GetExtension)(const char *name);
    void *TagRealloc;
} d_import_t;
typedef struct {
    uint32 apiversion, structsize;
    void *(*GetExtension)(const char *name);
    qboolean (*CanSave)(void);
    void (*PrepFrame)(void);
    void (*RestartFilesystem)(void);
    qboolean (*Customize)(edict_t *client, edict_t *ent, entity_state_t *temp);
    qboolean (*Visible)(edict_t *client, edict_t *ent);
} d_export_t;
const d_export_t *GetGameAPIEx(const d_import_t *import);

static int d_div;
static int d_framediv(edict_t *e) { (void)e; return d_div; }
static const d_fdiv_t d_fdiv = { d_framediv };
static void *d_getext(const char *name) { return !strcmp(name, "OPENTDM60_FRAMEDIV_V1") ? (void *)&d_fdiv : NULL; }

static edict_t *d_find(const char *classname)
{
    for (int n = game.maxclients + 1; n < ge->num_edicts; n++) {
        edict_t *e = &g_edicts[n];
        if (e->inuse && e->owner == f_p && e->classname && !strcmp(e->classname, classname)) return e;
    }
    return NULL;
}

static int d_near(const vec3_t a, const vec3_t b) { return fabsf(a[0] - b[0]) < 0.01f && fabsf(a[1] - b[1]) < 0.01f && fabsf(a[2] - b[2]) < 0.01f; }

static void d_follow(const d_export_t *ex, int weapon, const char *classname, int *checks, int *bad, int *turning)
{
    vec3_t last_d1;
    int have_last = 0, seen = 0, hidden_ok = 1;
    f_p->health = 1000;
    p_arm(weapon);
    p_settle(P_WX, 0, 24, 240);
    p_begin(5000 + weapon);
    for (int c = 0; c < 240; c++) {
        p_cmd(0, 0, c == 1 ? BUTTON_ATTACK : 0);
        if (f_c % f_g) continue;                        /* only right after a world frame */
        edict_t *r = d_find(classname);
        if (!r) { have_last = 0; continue; }
        seen = 1;
        entity_state_t st;
        if (r->grid_frame < 0) {
            d_div = 1;
            if (!(r->svflags & SVF_NOCLIENT) || ex->Customize(f_p, r, &st)) hidden_ok = 0;
            continue;
        }
        int j = level.realframenum - 1 - r->grid_frame;
        static const int ds[] = { 1, 2, 3, 6 };
        for (int i = 0; i < 4; i++) {
            d_div = ds[i];
            qboolean ok = ex->Customize(f_p, r, &st);
            (*checks)++;
            if (ds[i] >= h_div) { if (ok) (*bad)++; continue; }
            float f1 = (float)(j + ds[i] < h_div ? j + ds[i] : h_div) / h_div, f0 = (float)(j < h_div ? j : h_div) / h_div;
            vec3_t o, oo;
            for (int k = 0; k < 3; k++) {
                o[k] = r->grid_from[k] + (r->s.origin[k] - r->grid_from[k]) * f1;
                oo[k] = r->grid_from[k] + (r->s.origin[k] - r->grid_from[k]) * f0;
            }
            vec3_t ang;
            for (int k = 0; k < 3; k++) {
                float da = r->s.angles[k] - r->grid_from_angles[k];
                da -= 360.0f * floorf((da + 180.0f) / 360.0f);
                ang[k] = r->grid_from_angles[k] + da * f1;
            }
            if (!ok || !d_near(st.origin, o) || !d_near(st.old_origin, oo) || !d_near(st.angles, ang)) (*bad)++;
            if (ds[i] == 1 && r->s.angles[0] != r->grid_from_angles[0]) (*turning)++;
            if (ds[i] == 1) {
                if (have_last && !d_near(last_d1, st.old_origin)) (*bad)++;
                VectorCopy(st.origin, last_d1);
                have_last = 1;
            }
        }
    }
    p_on = 0;
    CHECK(seen, "%s: the projectile existed", classname);
    CHECK(hidden_ok, "%s: hidden and not customized before its first grid step", classname);
}

static void sc_d_customize(void)
{
    p_scene();
    if (!p_match()) return;
    d_import_t none = { 3, sizeof(d_import_t), NULL, NULL, NULL, NULL, NULL, NULL };
    const d_export_t *ex = GetGameAPIEx(&none);
    CHECK(ex && ex->apiversion == 3 && !ex->Customize, "no engine extension: no customize callback");
    d_import_t imp = { 3, sizeof(d_import_t), NULL, NULL, NULL, NULL, d_getext, NULL };
    ex = GetGameAPIEx(&imp);
    CHECK(ex && ex->apiversion == 3 && ex->structsize == sizeof(d_export_t) && ex->Customize && !ex->GetExtension &&
          !ex->CanSave && !ex->PrepFrame && !ex->RestartFilesystem && !ex->Visible,
          "extended API 3: only CustomizeEntityToClient");
    if (!ex || !ex->Customize) return;
    int checks = 0, bad = 0, turning = 0;
    d_follow(ex, 7, "rocket", &checks, &bad, &turning);
    d_follow(ex, 0, "bolt", &checks, &bad, &turning);
    d_follow(ex, 5, "grenade", &checks, &bad, &turning);
    printf("OBS d_customize rate=%d: %d callback checks, %d wrong, %d with a turning grenade\n", h_rate, checks, bad, turning);
    CHECK(checks > 0 && bad == 0, "display fractions and angles, 10 Hz clients and d = 1 continuity (%d checks, %d wrong)", checks, bad);
    CHECK(h_div == 1 || turning > 0, "a turning projectile was checked (%d)", turning);
}

/* Z7 (Fable 18.11): a map change that fails (ExitLevel, then the "not found"
 * branch) must keep the module's frame counters in step with the engine's:
 * the grid frames stay the engine's key frames (10 Hz clients, MVD). */
static void sc_d_exitlevel(void)
{
    h_scene_base();
    h_ent("{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"300 0 24\"\n\"angle\" \"0\"\n}\n");
    h_start(h_ents);
    edict_t *a = h_join(0, "p1");
    (void)a;
    h_frames_n(3 * h_rate);
    int d0 = level.realframenum - (int)h_frames, g0 = level.realframenum - level.framenum;
    level.changemap = "q2dm8";
    level.exitintermission = 1;
    h_frames_n(3 * h_div);
    CHECK(!level.exitintermission, "the failed map change was handled (exitintermission %d)", level.exitintermission);
    CHECK(level.realframenum - (int)h_frames == d0, "real frame counter in step with the engine frames (%d -> %d)",
          d0, level.realframenum - (int)h_frames);
    CHECK((level.realframenum - level.framenum) % h_div == g0 % h_div, "game and real phases unchanged");
}

/* Rework 2 (Fable 18.11, D2): the effects of a living player are chosen on
 * the grid frames and held between them; a player killed between two grid
 * frames shows none from the next world frame on, as at 10 Hz (no gib trail
 * on the head thrown off, no quad glow on the body, no power-up tint). The
 * target has a quad; the shooter's railgun with a quad gibs it in 6 phases. */
static void sc_d_death_fx(void)
{
    if (60 % h_rate) { printf("OBS d_death_fx: 60 usercmds per second do not divide into %d Hz ticks\n", h_rate); return; }
    p_scene();
    if (!p_match()) return;
    imp_cvar_set("dmflags", "1024");        /* DF_FORCE_RESPAWN: the target is back for the next phase */
    int alive = 0, dead = 0, bad_alive = 0, bad_dead = 0, gibbed = 0;
    for (int k = 0; k < 6; k++) {
        for (int i = 0; i < 600 && p_tgt->deadflag; i++) p_cmd(0, 0, 0);
        p_place_target(P_TX, 0, 0);
        p_tgt->health = 100;
        h_give(p_tgt, "quad damage", 0);
        h_client_command(p_tgt, "use quad damage");
        f_p->health = 1000;
        p_arm(9);
        h_give(f_p, "quad damage", 0);
        h_client_command(f_p, "use quad damage");
        p_yaw = 0;
        p_settle(P_WX, 0, 24, 120);
        p_tgt->health = 100;
        p_begin(9500 + k);
        int was_dead = 0;
        for (int c = 0; c < 60; c++) {
            unsigned fr = (unsigned)h_frames;
            p_cmd(0, 0, c == k ? BUTTON_ATTACK : 0);
            if ((unsigned)h_frames == fr) continue;     /* no world frame after this usercmd */
            if (p_tgt->health > 0 && !was_dead) {
                alive++;
                if (!(p_tgt->s.effects & EF_QUAD) || p_tgt->client->powerup_blend[3] <= 0) bad_alive++;
            } else if (p_tgt->deadflag || p_tgt->health <= 0) {
                was_dead = 1;
                dead++;
                if (p_tgt->s.effects & (EF_GIB | EF_QUAD | EF_PENT | EF_COLOR_SHELL)) bad_dead++;
                if (p_tgt->client->powerup_blend[3] > 0) bad_dead++;
            }
        }
        gibbed += p_tgt->health < -40;
        p_end("DF k=%d", k);
    }
    printf("OBS d_death_fx rate=%d: %d frames alive with a quad, %d frames dead (%d gibbed of 6)\n", h_rate, alive, dead,
           gibbed);
    CHECK(gibbed == 6, "the railgun with a quad gibbed the target in every phase (%d)", gibbed);
    CHECK((alive > 0 || h_div == 1) && bad_alive == 0, "a living target keeps its quad glow and tint between grid frames (%d of %d wrong)",
          bad_alive, alive);
    CHECK(dead > 0 && bad_dead == 0, "a dead target shows no effects and no tint in any frame (%d wrong in %d frames)",
          bad_dead, dead);
}
#else
#define H_SCENARIOS_F
#endif

#define H_SCENARIOS_60HZ \
    H_SCENARIOS_M \
    H_SCENARIOS_F \
    { "p_fall_timeout", sc_p_fall_timeout }, \
    { "p_damage_timeout", sc_p_damage_timeout }, \
    { "b_body_reuse", sc_b_body_reuse }, \
    { "b_free", sc_b_free }, \
    { "b_floor", sc_b_floor }, \
    { "b_wall", sc_b_wall }, \
    { "b_edge", sc_b_edge }, \
    { "b_corner", sc_b_corner }, \
    { "b_low", sc_b_low }, \
    { "b_hand", sc_b_hand }, \
    { "b_phase1", sc_b_phase1 }, \
    { "b_phase3", sc_b_phase3 }, \
    { "b_phase5", sc_b_phase5 }, \
    { "b_drop", sc_b_drop },
