#ifndef SCENARIOS_PARITY_H
#define SCENARIOS_PARITY_H
/*
 * opentdm-60hz candidate 4: weapon actions and projectiles against the
 * ORIGINAL module at 10 Hz (PO 17.11: no weapon fires later or earlier than
 * on the normal server; rocket jumps as there). Engine Pmove and collision,
 * the usercmd stream of f_fall_parity (six usercmds per 100 ms at every
 * rate, the 100 ms grid ending on usercmds 5, 11, ...).
 *
 * Every case starts on a 100 ms window boundary; its actions are placed at
 * usercmd offsets (k = 0..5: the phase within the window). Events are
 * registered where they happen, in slots: 2c = inside the ClientThink of
 * usercmd c, 2c+1 = inside the world frame after it (slot -1: frames before
 * the first usercmd of a batch window). Events:
 *   +K / -K   creation (first link) / removal (unlink) of a projectile of the
 *             shooter, with its origin: B bolt, G grenade, H hand grenade,
 *             R rocket, F BFG ball
 *   Mn        n muzzle flashes of the shooter      A-n  ammo spent
 *   S-n(v)    shooter health change, its velocity after (impulse)
 *   T-n       target health change                 FIGHT  "Fight!"
 *   DIE/RESP  shooter death / respawn
 * The line of a case must be the same at 10, 30 and 60 Hz and the same as
 * the original module's at 10 Hz (compare_parity.py). Shared by the full
 * harness and the reduced harness of the original tree (make_rj_orig.py):
 * only fields and functions both trees have.
 */

void init_genrand(uint32 s);

static char p_out[16384];
static int p_len, p_on, p_slot, p_c0, p_batch;
static edict_t *p_tgt;
static int p_tgt_fwd;                   /* target forwardmove while it runs (0: still, no usercmds) */
static short p_yaw, p_tgt_yaw;
static float p_z0, p_apex;
static int p_m0, p_a0, p_hs0, p_ht0, p_bp0, p_dead0;
/* HARNESS_RNG_TRACE=n: after every slot of the n-th case, draw one number
 * from the module generator and print it (both builds draw at the same
 * points: the first different number is the first slot where the module
 * itself drew differently) */
static int p_case_no, p_trace;
static void (*p_seg_hook)(void);      /* extra per-slot events of a scenario */

static void p_put(const char *fmt, ...)
{
    if (p_len > (int)sizeof(p_out) - 128) return;
    va_list ap; va_start(ap, fmt);
    p_len += vsnprintf(p_out + p_len, sizeof(p_out) - p_len, fmt, ap);
    va_end(ap);
}

static const char *p_kind(const edict_t *e)
{
    if (!e->classname) return NULL;
    if (!strcmp(e->classname, "bolt")) return "B";
    if (!strcmp(e->classname, "grenade")) return "G";
    if (!strcmp(e->classname, "hgrenade")) return "H";
    if (!strcmp(e->classname, "rocket")) return "R";
    if (!strcmp(e->classname, "bfg blast")) return "F";
    return NULL;
}

static void p_link(edict_t *e)
{
    const char *k = p_kind(e);
    if (p_on && k && e->owner == f_p)
        p_put(" +%s@%d(%.1f %.1f %.1f)", k, p_slot, e->s.origin[0], e->s.origin[1], e->s.origin[2]);
}

static void p_unlink(edict_t *e)
{
    const char *k = p_kind(e);
    if (p_on && k && e->owner == f_p)
        p_put(" -%s@%d(%.1f %.1f %.1f)", k, p_slot, e->s.origin[0], e->s.origin[1], e->s.origin[2]);
}

static int p_ammo_total(void)
{
    int n = 0;
    for (int i = 0; i < game.num_items; i++)
        if (itemlist[i].flags & IT_AMMO) n += f_p->client->inventory[i];
    return n;
}

static void p_seg_begin(void)
{
    p_m0 = h_nmsgs;
    p_a0 = p_ammo_total();
    p_hs0 = f_p->health;
    p_ht0 = p_tgt ? p_tgt->health : 0;
    p_bp0 = h_nbprints;
    p_dead0 = f_p->deadflag;
}

static void p_seg_end(void)
{
    if (!p_on) return;
    int m = 0;
    for (int i = p_m0; i < h_nmsgs; i++)
        m += h_msgs[i].svc == H_SVC_MUZZLEFLASH && h_msgs[i].entnum == NUM(f_p);
    if (m) p_put(" M%d@%d", m, p_slot);
    int a = p_ammo_total() - p_a0;
    if (a) p_put(" A%+d@%d", a, p_slot);
    if (!p_dead0 && f_p->deadflag) p_put(" DIE@%d", p_slot);
    if (p_dead0 && !f_p->deadflag) p_put(" RESP@%d", p_slot);
    else if (f_p->health != p_hs0)
        p_put(" S%+d@%d(%.0f %.0f %.0f)", f_p->health - p_hs0, p_slot, f_p->velocity[0], f_p->velocity[1],
              f_p->velocity[2]);
    if (p_tgt && p_tgt->health != p_ht0) p_put(" T%+d@%d", p_tgt->health - p_ht0, p_slot);
    for (int i = p_bp0; i < h_nbprints; i++)
        if (strstr(h_bprints[i].text, "Fight!")) p_put(" FIGHT@%d", p_slot);
    if (p_seg_hook) p_seg_hook();
    float z = f_p->s.origin[2] - p_z0;
    if (z > p_apex) p_apex = z;
    if (p_trace && (!(p_slot & 1) || ((p_slot - 1) / 2 + 1) % 6 == 0))   /* slots every rate has */
        printf("  rng %d %08x\n", p_slot, (unsigned)genrand_int32());
}

static void p_think(edict_t *who, int fwd, int up, int buttons, short yaw)
{
    usercmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.msec = (byte)f_msec[f_c % 6];
    cmd.forwardmove = (short)fwd;
    cmd.upmove = (short)up;
    cmd.buttons = (byte)buttons;
    cmd.angles[PITCH] = who == f_p ? f_pitch : 0;
    cmd.angles[YAW] = yaw;
    h_pmove_self = who;
    ge->ClientThink(who, &cmd);
}

/* one usercmd of the shooter (and of a running target), then the world
 * frame if the usercmd ends one */
static void p_run(int fwd, int up, int buttons)
{
    p_slot = 2 * (f_c - p_c0);
    p_seg_begin();
    p_think(f_p, fwd, up, buttons, p_yaw);
    if (p_tgt && p_tgt_fwd) p_think(p_tgt, p_tgt_fwd, 0, 0, p_tgt_yaw);
    p_seg_end();
    f_c++;
}

static void p_frame(void)
{
    p_seg_begin();
    h_frame();
    p_seg_end();
}

typedef struct { int fwd, up, buttons; } pcmd_t;
static pcmd_t p_q[6];
static int p_nq;

/* p_batch: the usercmds of a 100 ms window arrive together right before its
 * grid frame (bunched packets): the window's other world frames run first */
static void p_cmd(int fwd, int up, int buttons)
{
    if (!p_batch || f_g == 6) {
        p_run(fwd, up, buttons);
        if (f_c % f_g == 0) { p_slot++; p_frame(); }
        return;
    }
    p_q[p_nq++] = (pcmd_t){ fwd, up, buttons };
    if ((f_c + p_nq) % 6) return;
    p_slot = 2 * (f_c - p_c0) - 1;
    for (int i = 0; i < 6 / f_g - 1; i++) p_frame();
    for (int i = 0; i < p_nq; i++) p_run(p_q[i].fwd, p_q[i].up, p_q[i].buttons);
    p_nq = 0;
    p_slot = 2 * (f_c - 1 - p_c0) + 1;
    p_frame();
}

/* start a case on the next 100 ms window boundary: event slots count from it */
static void p_begin(unsigned seed)
{
    while (f_c % 6) p_cmd(0, 0, 0);
    init_genrand(seed);
    /* the harness logs start empty in every case (h_msgs filled up within
     * w_timing: later muzzle flashes were not registered) */
    h_nmsgs = 0;
    h_nbprints = 0;
    h_nsounds = 0;
    p_case_no++;
    p_trace = getenv("HARNESS_RNG_TRACE") && atoi(getenv("HARNESS_RNG_TRACE")) == p_case_no;
    p_c0 = f_c;
    p_len = 0;
    p_out[0] = 0;
    p_apex = 0;
    p_z0 = f_p->s.origin[2];
    p_on = 1;
}

static void p_end(const char *fmt, ...)
{
    p_on = 0;
    char head[256];
    va_list ap; va_start(ap, fmt); vsnprintf(head, sizeof(head), fmt, ap); va_end(ap);
    printf("%s:%s\n", head, p_out);
}

static void p_settle(float x, float y, float z, int cmds)
{
    p_on = 0;
    h_place(f_p, x, y, z);
    for (int i = 0; i < cmds; i++) p_cmd(0, 0, 0);
}

/* join a team with the same game time at every rate (h_join counts world
 * frames): the weapon and its idle animation then have the same history
 * at every rate when the cases begin */
static edict_t *p_join(int slot, const char *name)
{
    edict_t *ent = h_connect(slot, name);
    h_frames_n(h_div);
    h_client_command(ent, slot & 1 ? "team b" : "team a");
    for (int i = 0; i < h_div; i++) h_tick(ent, 0, 1);
    h_frames_n(2 * h_div);
    return ent;
}

/* both players in a long match (p_match_time: another match time); the
 * shooter is f_p, the target p_tgt */
static const char *p_match_time;
static int p_match(void)
{
    h_start(h_ents);
    f_p = p_join(0, "p1");
    p_tgt = p_join(1, "p2");
    h_frames_n(3 * h_rate);
    imp_cvar_set("g_match_time", p_match_time ? p_match_time : "36000");
    if (!h_tdm_start(f_p, p_tgt)) return 0;
    h_link_hook = p_link;
    h_unlink_hook = p_unlink;
    f_align();
    /* finish the first window at every rate (f_align leaves 10 Hz on usercmd 0
     * and 60 Hz on usercmd 5 of it): the same usercmd index from here on */
    do p_cmd(0, 0, 0); while (f_c % 6);
    return 1;
}

static void p_place_target(float x, float y, short yaw)
{
    h_place(p_tgt, x, y, 24);
    p_think(p_tgt, 0, 0, 0, yaw);
    p_tgt->health = 100000;
}

/* the world of the parity scenarios: the floor, a wall behind the weapon
 * lane, rocket jump walls at 40/100/200 units from the muzzle, a 30 degree
 * slope */
#define P_WX    (-1500.0f)              /* weapon lane: shooter x */
#define P_TX    (P_WX + 256)            /* target x */
#define P_RX    1000.0f                 /* rocket jump lanes: shooter x */
static void p_scene(void)
{
    h_scene_base();
    h_add_world(-1000, -400, 0, -968, 400, 400);                  /* far wall: 492 from the muzzle */
    h_add_world(P_RX + 8 + 40, 850, 0, P_RX + 80, 1150, 400);     /* wall 40, lane y 1000 */
    h_add_world(P_RX + 8 + 100, 1250, 0, P_RX + 140, 1550, 400);  /* wall 100, lane y 1400 */
    h_add_world(P_RX + 8 + 200, 1650, 0, P_RX + 240, 1950, 400);  /* wall 200, lane y 1800 */
    /* slope rising to +x: 0.5 x - 0.866 z >= 400 is air; surface z = (0.5 x - 400) / 0.866 */
    h_add_slope(800, -1250, 0, 1400, -750, 400, -0.5f, 0, 0.8660254f, -400);
    h_ent("{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"300 0 24\"\n\"angle\" \"0\"\n}\n");
}

/* ------------------------------------------------------------ weapons */

static const struct { const char *give, *ammo, *tag; int count; } p_weapons[] = {
    { "blaster", NULL, "blaster", 0 },
    { "shotgun", "shells", "sg", 50 },
    { "super shotgun", "shells", "ssg", 50 },
    { "machinegun", "bullets", "mg", 200 },
    { "chaingun", "bullets", "cg", 200 },
    { "grenade launcher", "grenades", "gl", 50 },
    { "grenades", "grenades", "hg", 50 },
    { "rocket launcher", "rockets", "rl", 50 },
    { "hyperblaster", "cells", "hb", 200 },
    { "railgun", "slugs", "rg", 50 },
    { "bfg10k", "cells", "bfg", 200 },
};

/* attack of pattern `pat` at offset r from the press */
static int p_pattern(const char *pat, int r)
{
    if (r < 0) return 0;
    if (!strcmp(pat, "tap") || !strcmp(pat, "btap") || !strcmp(pat, "far") || !strcmp(pat, "run"))
        return r == 0;
    if (!strcmp(pat, "hold") || !strcmp(pat, "bhold")) return r < 90;
    if (!strcmp(pat, "spam")) return r < 90 && !(r & 1);
    if (!strncmp(pat, "tap2_", 5)) return r == 0 || r == atoi(pat + 5);
    return 0;
}

static void p_arm(int w)
{
    h_give(f_p, p_weapons[w].give, 0);
    if (p_weapons[w].ammo) h_give(f_p, p_weapons[w].ammo, p_weapons[w].count);
    h_client_command(f_p, va("use %s", p_weapons[w].give));
}

/* every weapon, every pattern, every phase */
static void sc_w_timing(void)
{
    static const char *pats[] = { "tap", "hold", "tap2_3", "tap2_6", "tap2_9", "spam", "btap", "bhold", "far", "run" };
    if (60 % h_rate) { printf("OBS w_timing: 60 usercmds per second do not divide into %d Hz ticks\n", h_rate); return; }
    p_scene();
    if (!p_match()) return;
    for (int w = 0; w < (int)(sizeof(p_weapons) / sizeof(p_weapons[0])); w++) {
        p_arm(w);
        for (int i = 0; i < 120; i++) p_cmd(0, 0, 0);          /* switch done */
        for (int pi = 0; pi < (int)(sizeof(pats) / sizeof(pats[0])); pi++) {
            const char *pat = pats[pi];
            for (int k = 0; k < 6; k++) {
                int far = !strcmp(pat, "far"), run = !strcmp(pat, "run");
                if (run) p_place_target(P_WX + 400, -300, ANGLE2SHORT(90));
                else p_place_target(far ? P_TX : P_TX, far ? 700 : 0, 0);
                p_tgt_fwd = 0;
                p_yaw = 0;
                f_p->health = 1000;
                p_arm(w);
                p_settle(P_WX, 0, 24, 240);
                p_tgt->health = 100000;
                p_begin(1000 + 10 * w + k);
                p_batch = pat[0] == 'b';
                p_tgt_fwd = run ? 400 : 0;
                p_tgt_yaw = ANGLE2SHORT(90);
                for (int c = 0; c < 360; c++) p_cmd(0, 0, p_pattern(pat, c - k) ? BUTTON_ATTACK : 0);
                p_batch = 0;
                p_tgt_fwd = 0;
                p_end("W %s %s k=%d", p_weapons[w].tag, pat, k);
            }
        }
    }
    CHECK(1, "w_timing ran (lines compared by compare_parity.py)");
}

/* weapon switch, no ammo, death and respawn, pause and resume */
static void sc_w_special(void)
{
    if (60 % h_rate) { printf("OBS w_special: 60 usercmds per second do not divide into %d Hz ticks\n", h_rate); return; }
    p_scene();
    if (!p_match()) return;
    for (int k = 0; k < 6; k++) {
        /* switch shotgun -> rocket launcher at k, attack tapped at k + 3, then held from k + 30 */
        p_place_target(P_TX, 0, 0);
        f_p->health = 1000;
        p_arm(1);
        p_settle(P_WX, 0, 24, 240);
        h_give(f_p, "rocket launcher", 0);
        h_give(f_p, "rockets", 20);
        p_begin(2000 + k);
        for (int c = 0; c < 240; c++) {
            if (c == k) h_client_command(f_p, "use rocket launcher");
            p_cmd(0, 0, c == k + 3 || (c >= k + 30 && c < k + 90) ? BUTTON_ATTACK : 0);
        }
        p_end("WS switch k=%d", k);

        /* one rocket left: held from k, the weapon runs out and changes */
        f_p->health = 1000;
        p_arm(7);
        p_settle(P_WX, 0, 24, 240);
        f_p->client->inventory[ITEM_INDEX(FindItem("rockets"))] = 1;
        p_begin(2100 + k);
        for (int c = 0; c < 240; c++) p_cmd(0, 0, c >= k && c < k + 150 ? BUTTON_ATTACK : 0);
        p_end("WS noammo k=%d", k);

        /* killed at k while firing the machinegun; taps at +60 and +90, held from +150 */
        f_p->health = 1000;
        p_arm(3);
        p_settle(P_WX, 0, 24, 240);
        p_begin(2200 + k);
        for (int c = 0; c < 420; c++) {
            if (c == k + 6) h_client_command(f_p, "kill");
            int b = c < k + 6 || c == k + 66 || c == k + 96 || (c >= k + 156 && c < k + 216);
            p_cmd(0, 0, b ? BUTTON_ATTACK : 0);
        }
        p_end("WS death k=%d", k);
        for (int i = 0; i < 1200 && f_p->deadflag; i++) p_cmd(0, 0, (i & 7) == 0 ? BUTTON_ATTACK : 0);
        CHECK(!f_p->deadflag, "death k=%d: respawned", k);
        p_settle(P_WX, 0, 24, 120);

        /* pause called at k + 30 while holding the machinegun (by either
         * player), resume asked 1 s later at the same phase or 3 usercmds
         * later: held through, then taps after the restart */
        for (int j = 0; j < 2; j++) {
            edict_t *caller = j ? p_tgt : f_p;
            f_p->health = 1000;
            p_arm(3);
            p_settle(P_WX, 0, 24, 240);
            p_begin(2300 + 10 * j + k);
            int pause = k + 30, resume = pause + 60 + 3 * j;
            for (int c = 0; c < 1260; c++) {
                if (c == pause || c == resume) h_client_command(caller, "time");
                int b = (c >= k && c < resume + 700) || c == resume + 760 || c == resume + 763;
                p_cmd(0, 0, b ? BUTTON_ATTACK : 0);
            }
            p_end("WS pause%d k=%d", j, k);
            CHECK(tdm_match_status == MM_PLAYING, "pause%d k=%d: the match resumed (status %d)", j, k, tdm_match_status);
        }
    }
}

/* the match countdown: both ready at k, attack held through "Fight!" */
void TDM_ResetGameState (void);
static void sc_w_countdown(void)
{
    if (60 % h_rate) { printf("OBS w_countdown: 60 usercmds per second do not divide into %d Hz ticks\n", h_rate); return; }
    p_scene();
    h_start(h_ents);
    f_p = p_join(0, "p1");
    p_tgt = p_join(1, "p2");
    h_frames_n(3 * h_rate);
    imp_cvar_set("g_match_time", "36000");
    imp_cvar_set("g_idle_time", "0");       /* the target never presses a button in the warmups */
    h_link_hook = p_link;
    h_unlink_hook = p_unlink;
    f_align();
    /* finish the first window at every rate (f_align leaves 10 Hz on usercmd 0
     * and 60 Hz on usercmd 5 of it): the same usercmd index from here on */
    do p_cmd(0, 0, 0); while (f_c % 6);
    for (int k = 0; k < 6; k++) {
        p_place_target(P_TX, 0, 0);
        h_give(f_p, "machinegun", 0);
        h_give(f_p, "bullets", 200);
        h_client_command(f_p, "use machinegun");
        p_settle(P_WX, 0, 24, 240);
        p_begin(3000 + k);
        for (int c = 0; c < 1200; c++) {
            if (c == k) { h_client_command(f_p, "ready"); h_client_command(p_tgt, "ready"); }
            p_cmd(0, 0, c >= k && c < k + 1020 ? BUTTON_ATTACK : 0);
        }
        p_end("WC countdown k=%d", k);
        CHECK(tdm_match_status == MM_PLAYING, "countdown k=%d: the match started (status %d)", k, tdm_match_status);
        TDM_ResetGameState();
        p_settle(P_WX, 0, 24, 240);
        if (f_p->client->pers.team != TEAM_A) h_client_command(f_p, "team a");
        if (p_tgt->client->pers.team != TEAM_B) h_client_command(p_tgt, "team b");
        p_settle(P_WX, 0, 24, 240);
        CHECK(f_p->client->pers.team == TEAM_A && p_tgt->client->pers.team == TEAM_B, "countdown k=%d: teams again", k);
    }
}

/* ------------------------------------------------------- rocket jumps */

void fire_rocket (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int radius_damage);

/* rocket jumps besides f_rocket_jump: off walls at 40/100/200 units from
 * the muzzle, off a 30 degree slope, while running, while falling, a
 * rocket created inside solid; the grenade launcher at the feet */
static void sc_r_ext(void)
{
    if (60 % h_rate) { printf("OBS r_ext: 60 usercmds per second do not divide into %d Hz ticks\n", h_rate); return; }
    p_scene();
    if (!p_match()) return;
    p_place_target(-3000, 3000, 0);
    static const struct { int d; float y; } walls[] = { { 40, 1000 }, { 100, 1400 }, { 200, 1800 } };
    for (int k = 0; k < 6; k++) {
        for (int wi = 0; wi < 3; wi++)
            for (int jump = 0; jump < 2; jump++) {
                /* facing the wall, 8 degrees down (the floor is 270 units away); jump and fire at k */
                f_p->health = 1000;
                p_arm(7);
                p_yaw = 0;
                f_pitch = ANGLE2SHORT(8);
                p_settle(P_RX, walls[wi].y, 24, 240);
                p_begin(4000 + 100 * wi + 10 * jump + k);
                for (int c = 0; c < 180; c++)
                    p_cmd(0, jump && c >= k && c < k + 6 ? 400 : 0, c == k ? BUTTON_ATTACK : 0);
                p_end("RX wall%d%s k=%d apex=%.1f", walls[wi].d, jump ? "j" : "", k, p_apex);
            }

        /* on the slope (surface z 173 at x 1100), jump + fire down */
        f_p->health = 1000;
        p_arm(7);
        f_pitch = ANGLE2SHORT(89);
        p_settle(1100, -1000, 220, 240);
        p_begin(4500 + k);
        for (int c = 0; c < 180; c++) p_cmd(0, c >= k && c < k + 6 ? 400 : 0, c == k ? BUTTON_ATTACK : 0);
        p_end("RX slope k=%d apex=%.1f z0=%.1f", k, p_apex, p_z0);

        /* running +x (1 s), jump + fire down at 60 + k */
        f_p->health = 1000;
        p_arm(7);
        f_pitch = ANGLE2SHORT(89);
        p_settle(-600, -2000, 24, 240);
        p_begin(4600 + k);
        for (int c = 0; c < 240; c++)
            p_cmd(c < 120 ? 400 : 0, c >= 60 + k && c < 66 + k ? 400 : 0, c == 60 + k ? BUTTON_ATTACK : 0);
        p_end("RX run k=%d apex=%.1f", k, p_apex);

        /* falling from 200 units, fire down at 12 + k */
        f_p->health = 1000;
        p_arm(7);
        f_pitch = ANGLE2SHORT(89);
        p_settle(0, 2500, 24, 240);
        p_begin(4700 + k);
        h_place(f_p, 0, 2500, 224);
        for (int c = 0; c < 180; c++) p_cmd(0, 0, c == 12 + k ? BUTTON_ATTACK : 0);
        p_end("RX fall k=%d apex=%.1f", k, p_apex);

        /* a rocket created inside the floor at k: explodes at once */
        f_p->health = 1000;
        p_settle(0, -2500, 24, 240);
        p_begin(4800 + k);
        for (int c = 0; c < 60; c++) {
            if (c == k) {
                vec3_t start = { f_p->s.origin[0] + 32, f_p->s.origin[1], -8 }, dir = { 0, 0, -1 };
                p_slot = 2 * (f_c - p_c0);
                p_seg_begin();
                fire_rocket(f_p, start, dir, 100, 650, 120, 120);
                p_seg_end();
            }
            p_cmd(0, 0, 0);
        }
        p_end("RX solid k=%d", k);

        /* grenade launcher at the feet; jump at 150 + k (near the explosion) */
        f_p->health = 1000;
        p_arm(5);
        f_pitch = ANGLE2SHORT(89);
        p_settle(0, -3000, 24, 240);
        p_begin(4900 + k);
        for (int c = 0; c < 300; c++)
            p_cmd(0, c >= 150 + k && c < 156 + k ? 400 : 0, c == k ? BUTTON_ATTACK : 0);
        p_end("RX gjump k=%d apex=%.1f", k, p_apex);
        f_pitch = 0;
    }
    CHECK(1, "r_ext ran (lines compared by compare_parity.py)");
}

/* Z2 (Fable 18.11): a moving player hit at long range. "toward": the target
 * runs at the shooter from 470 units (it meets the projectile on the line);
 * "cross": it runs across the line 400 units away, started so that it
 * reaches the line when the projectile does (per weapon: projectile speed and
 * the weapon's own delay before the shot). A tap at k; the target's hits are
 * in the line (T events) with their moments. */
static float p_cross_lead(int w)
{
    switch (w) {
    case 0: case 8: return 392.0f / 1000;           /* blaster, hyperblaster bolts */
    case 5: return 392.0f / 600;                    /* grenade launcher */
    case 7: return 392.0f / 650;                    /* rocket */
    case 10: return 0.9f + 392.0f / 400;            /* BFG: about 0.9 s to the shot */
    default: return 0;                              /* hitscan, hand grenade: toward only */
    }
}

static void sc_w_moving(void)
{
    if (60 % h_rate) { printf("OBS w_moving: 60 usercmds per second do not divide into %d Hz ticks\n", h_rate); return; }
    p_scene();
    if (!p_match()) return;
    int hits = 0, cases = 0;
    for (int w = 0; w < (int)(sizeof(p_weapons) / sizeof(p_weapons[0])); w++) {
        p_arm(w);
        for (int i = 0; i < 120; i++) p_cmd(0, 0, 0);
        for (int cross = 0; cross < 2; cross++) {
            float lead = p_cross_lead(w);
            if (cross && !lead) continue;
            for (int k = 0; k < 6; k++) {
                if (cross) p_place_target(P_WX + 400, -300.0f * (lead + k / 60.0f) + 20, ANGLE2SHORT(90));
                else p_place_target(P_WX + 470, 0, ANGLE2SHORT(180));
                p_tgt_fwd = 0;
                p_yaw = 0;
                f_p->health = 1000;
                p_arm(w);
                p_settle(P_WX, 0, 24, 240);
                p_tgt->health = 100000;
                p_begin(6000 + 10 * w + k);
                p_tgt_fwd = 400;
                p_tgt_yaw = cross ? ANGLE2SHORT(90) : ANGLE2SHORT(180);
                for (int c = 0; c < 240; c++) p_cmd(0, 0, c == k ? BUTTON_ATTACK : 0);
                p_tgt_fwd = 0;
                hits += strstr(p_out, " T-") != NULL;
                cases++;
                p_end("W %s %s k=%d", p_weapons[w].tag, cross ? "cross" : "toward", k);
            }
        }
    }
    printf("OBS w_moving rate=%d: the moving target hit in %d of %d cases\n", h_rate, hits, cases);
    CHECK(hits * 4 >= cases * 3, "the moving target is hit in most cases (%d of %d)", hits, cases);
}

/* Z6 (Fable 18.11): timers set by a pickup or a power-up use between two
 * world frames. The shooter is put onto the item with usercmd k (the touch
 * runs in its ClientThink) and off it right after; the quad is used by a
 * command before usercmd k + 1. Events: GONE/BACK (the item taken / shown
 * again), QON/QOFF (the quad effect), the health of the shooter (mega
 * health running down). */
static edict_t *p_item;
static int p_item_shown, p_quad_on;

static void p_item_watch(void)
{
    if (!p_on) return;
    if (p_item) {
        int shown = p_item->inuse && p_item->solid == SOLID_TRIGGER && !(p_item->svflags & SVF_NOCLIENT);
        if (shown != p_item_shown) p_put(shown ? " BACK@%d" : " GONE@%d", p_slot);
        p_item_shown = shown;
    }
    int q = (f_p->s.effects & EF_QUAD) != 0;
    if (q != p_quad_on) p_put(q ? " QON@%d" : " QOFF@%d", p_slot);
    p_quad_on = q;
}

static edict_t *p_find_class(const char *classname)
{
    for (int n = game.maxclients + 1; n < ge->num_edicts; n++) {
        edict_t *e = &g_edicts[n];
        if (e->inuse && e->classname && !strcmp(e->classname, classname)) return e;
    }
    return NULL;
}

static void sc_i_items(void)
{
    static const struct { const char *cls, *tag; float x; int cmds; } items[] = {
        { "item_armor_body", "ra", 2000, 1800 },
        { "weapon_railgun", "rg", 2200, 2400 },
        { "item_quad", "quad", 2400, 4200 },
        { "item_health_mega", "mega", 2600, 1200 },
    };
    if (60 % h_rate) { printf("OBS i_items: 60 usercmds per second do not divide into %d Hz ticks\n", h_rate); return; }
    p_scene();
    for (int i = 0; i < 4; i++)
        h_ent("{\n\"classname\" \"%s\"\n\"origin\" \"%g 3500 16\"\n}\n", items[i].cls, items[i].x);
    if (!p_match()) return;
    p_place_target(-3000, 3000, 0);
    for (int i = 0; i < 4; i++) {
        edict_t *it = p_find_class(items[i].cls);
        if (!it) { printf("OBS i_items: no %s in this profile\n", items[i].cls); continue; }
        for (int k = 0; k < 6; k++) {
            /* the item is there again and the shooter is plain */
            p_settle(items[i].x, 3600, 24, 60);
            for (int n = 0; n < 200 && !(it->inuse && it->solid == SOLID_TRIGGER && !(it->svflags & SVF_NOCLIENT)); n++)
                p_settle(items[i].x, 3600, 24, 60);
            f_p->health = 100;
            f_p->client->quad_framenum = 0;
            for (int n = 0; n < game.num_items; n++)
                if (itemlist[n].flags & (IT_ARMOR | IT_POWERUP)) f_p->client->inventory[n] = 0;
            f_p->client->inventory[ITEM_INDEX(FindItem("railgun"))] = 0;
            p_settle(items[i].x, 3600, 24, 120);
            p_item = it;
            p_seg_hook = p_item_watch;
            p_item_shown = 1;
            p_quad_on = 0;
            p_begin(7000 + 10 * i + k);
            for (int c = 0; c < items[i].cmds; c++) {
                if (c == k) h_place(f_p, items[i].x, 3500, 24);
                if (c == k + 1) {
                    h_place(f_p, items[i].x, 3600, 24);
                    if (!strcmp(items[i].tag, "quad")) h_client_command(f_p, "use quad damage");
                }
                p_cmd(0, 0, 0);
            }
            p_item = NULL;
            p_seg_hook = NULL;
            p_end("WI %s k=%d", items[i].tag, k);
        }
    }
    CHECK(1, "i_items ran (lines compared by compare_parity.py)");
}

/* Rework 2 (Fable 18.11, D1 and D3; the sweep of the timers set between two
 * world frames by a death, a drop, a command or a vote):
 *   qpick k  the target uses a quad, dies ("kill") 1 s later and drops it;
 *            the shooter is put onto the dropped quad with usercmd 180 + k
 *            (a dropped player quad is used at once, with the rest of it)
 *   qthr k   the target dies ("kill") before usercmd 1734 + k of its quad: the
 *            quad is dropped only with more than 1 s left (k = 0..11 crosses it)
 *   drop k   "drop railgun" before usercmd k: when the dropped weapon turns
 *            touchable (Touch_Item), with its origin, and when it goes
 *   kill5 k  the shooter "kill"s before usercmd k, respawns, then "kill"s
 *            before every usercmd: the first one allowed 5 s after the respawn
 * Events: QD (a player quad was dropped), TCH(origin) / FREE (the tracked
 * dropped item), QON/QOFF (the shooter's quad glow), W2 (the quad fade
 * warning to the shooter), T (the target's health: +100 on its respawn),
 * KD/KR (the shooter dead / alive again; a "kill" is before the segment). */
static edict_t *p_drop;
static int p_drop_tch, p_qd, p_snd_i, p_w2, p_dead_w;

static edict_t *p_find_dropped(const char *classname, int flag, edict_t *owner)
{
    for (int n = game.maxclients + 1; n < ge->num_edicts; n++) {
        edict_t *e = &g_edicts[n];
        if (e->inuse && e->classname && !strcmp(e->classname, classname) && (e->spawnflags & flag) &&
            (!owner || e->owner == owner))
            return e;
    }
    return NULL;
}

static void p_drops_watch(void)
{
    if (!p_on) return;
    if (!p_qd && p_find_dropped("item_quad", DROPPED_PLAYER_ITEM, NULL)) { p_qd = 1; p_put(" QD@%d", p_slot); }
    if (p_drop) {
        if (!p_drop->inuse) { p_put(" FREE@%d", p_slot); p_drop = NULL; }
        else if (!p_drop_tch && p_drop->touch == Touch_Item) {
            p_drop_tch = 1;
            p_put(" TCH@%d(%.1f %.1f %.1f)", p_slot, p_drop->s.origin[0], p_drop->s.origin[1], p_drop->s.origin[2]);
        }
    }
    int q = (f_p->s.effects & EF_QUAD) != 0;
    if (q != p_quad_on) p_put(q ? " QON@%d" : " QOFF@%d", p_slot);
    p_quad_on = q;
    /* a death by a command is before the segment: registered here */
    if (f_p->deadflag != p_dead_w) p_put(f_p->deadflag ? " KD@%d" : " KR@%d", p_slot);
    p_dead_w = f_p->deadflag;
    for (; p_snd_i < h_nsounds; p_snd_i++)
        if (h_sounds_log[p_snd_i].soundindex == p_w2 && h_sounds_log[p_snd_i].entnum == NUM(f_p))
            p_put(" W2@%d", p_slot);
}

static void p_drops_case(unsigned seed)
{
    p_on = 0;
    for (int i = 0; i < 900 && (f_p->deadflag || p_tgt->deadflag); i++) p_cmd(0, 0, 0);
    p_place_target(-2000, 2500, 0);
    f_p->client->quad_framenum = 0;
    p_settle(-2600, 2500, 24, 360);
    p_drop = NULL;
    p_drop_tch = p_qd = p_snd_i = p_quad_on = 0;
    p_dead_w = f_p->deadflag;
    p_seg_hook = p_drops_watch;
    p_begin(seed);
}

static void sc_i_drops(void)
{
    if (60 % h_rate) { printf("OBS i_drops: 60 usercmds per second do not divide into %d Hz ticks\n", h_rate); return; }
    p_scene();
    if (!p_match()) return;
    imp_cvar_set("dmflags", "17408");       /* DF_QUAD_DROP | DF_FORCE_RESPAWN */
    p_w2 = imp_soundindex("items/damage2.wav");
    p_yaw = 0;
    p_arm(0);                                /* the shooter holds the blaster */
    for (int i = 0; i < 120; i++) p_cmd(0, 0, 0);

    for (int k = 0; k < 6; k++) {
        p_drops_case(8000 + k);
        h_give(p_tgt, "quad damage", 0);
        h_client_command(p_tgt, "use quad damage");
        for (int c = 0; c < 1900; c++) {
            if (c == 60) {
                h_client_command(p_tgt, "kill");
                p_drop = p_find_dropped("item_quad", DROPPED_PLAYER_ITEM, NULL);
                p_drop_tch = 1;
            }
            if (c == 180 + k && p_drop) h_place(f_p, p_drop->s.origin[0], p_drop->s.origin[1], 24);
            if (c == 181 + k) h_place(f_p, -2600, 2500, 24);
            p_cmd(0, 0, 0);
        }
        p_seg_hook = NULL;
        p_end("WD qpick k=%d", k);
    }

    for (int k = 0; k < 12; k++) {
        p_drops_case(8100 + k);
        h_give(p_tgt, "quad damage", 0);
        h_client_command(p_tgt, "use quad damage");
        for (int c = 0; c < 1800; c++) {
            if (c == 1734 + k) h_client_command(p_tgt, "kill");
            p_cmd(0, 0, 0);
        }
        p_seg_hook = NULL;
        p_end("WD qthr k=%d", k);
    }

    for (int k = 0; k < 6; k++) {
        /* a weapon given to a player with the blaster in hand is switched to:
         * let it, then back to the blaster (the weapon in hand cannot be dropped) */
        h_give(f_p, "railgun", 0);
        h_give(f_p, "slugs", 10);
        p_on = 0;
        for (int i = 0; i < 120; i++) p_cmd(0, 0, 0);
        h_client_command(f_p, "use blaster");
        p_drops_case(8200 + k);
        for (int c = 0; c < 1860; c++) {
            if (c == k) {
                h_client_command(f_p, "drop railgun");
                p_drop = p_find_dropped("weapon_railgun", DROPPED_ITEM, f_p);
            }
            p_cmd(0, 0, 0);
        }
        p_seg_hook = NULL;
        p_end("WD drop k=%d", k);
    }

    for (int k = 0; k < 6; k++) {
        p_drops_case(8300 + k);
        int back = 0, died = 0;
        for (int c = 0; c < 1200; c++) {
            if (c == k) h_client_command(f_p, "kill");
            if (back && !died) h_client_command(f_p, "kill");
            p_cmd(0, 0, 0);
            if (c > k && !f_p->deadflag) back = 1;
            if (back && f_p->deadflag) died = 1;
        }
        p_seg_hook = NULL;
        p_end("WD kill5 k=%d", k);
        for (int i = 0; i < 400 && f_p->deadflag; i++) p_cmd(0, 0, 0);
    }
    CHECK(1, "i_drops ran (lines compared by compare_parity.py)");
}

/* the time limit cut by a vote below the time played: the match ends at once
 * (on the coming grid frame, as on the coming tick at 10 Hz). One match per
 * phase k: the shooter calls the vote, the second player's "vote yes" before
 * usercmd k passes it. Events: ST<n> (the match status changes). */
static int p_status0;

static void p_status_watch(void)
{
    if (!p_on) return;
    if ((int)tdm_match_status != p_status0) p_put(" ST%d@%d", (int)tdm_match_status, p_slot);
    p_status0 = tdm_match_status;
}

static void p_vote_end(int k)
{
    if (60 % h_rate) { printf("OBS i_vote: 60 usercmds per second do not divide into %d Hz ticks\n", h_rate); return; }
    p_scene();
    if (!p_match()) return;
    for (int c = 0; c < 3700; c++) p_cmd(0, 0, 0);      /* more than a minute of the match */
    p_settle(P_WX, 0, 24, 60);
    p_status0 = tdm_match_status;
    p_seg_hook = p_status_watch;
    p_begin(8400 + k);
    h_client_command(f_p, "vote timelimit 1");
    for (int c = 0; c < 60; c++) {
        if (c == k) h_client_command(p_tgt, "vote yes");
        p_cmd(0, 0, 0);
    }
    p_seg_hook = NULL;
    p_end("WV k=%d", k);
    CHECK(1, "i_vote ran (lines compared by compare_parity.py)");
}
static void sc_i_vote0(void) { p_vote_end(0); }
static void sc_i_vote1(void) { p_vote_end(1); }
static void sc_i_vote2(void) { p_vote_end(2); }
static void sc_i_vote3(void) { p_vote_end(3); }
static void sc_i_vote4(void) { p_vote_end(4); }
static void sc_i_vote5(void) { p_vote_end(5); }

/* Rework 3 (Fable 18.11). Shared: the sounds of the shooter by name
 * (" snd:<file>@slot", the name without its path and ".wav"; not its own
 * voice, "*pain..." and the like: the pain sound debounce is a separate
 * item of the report) and the broadcast texts of match events. */
static int p_snd_j, p_bp_j;

static void p_sounds_of_shooter(void)
{
    for (; p_snd_j < h_nsounds; p_snd_j++) {
        if (h_sounds_log[p_snd_j].entnum != NUM(f_p)) continue;
        const char *n = h_sounds[h_sounds_log[p_snd_j].soundindex];
        if (n && n[0] == '*') continue;
        const char *b = n ? strrchr(n, '/') : NULL;
        char s[64];
        snprintf(s, sizeof(s), "%s", b ? b + 1 : n ? n : "?");
        char *dot = strrchr(s, '.');
        if (dot) *dot = 0;
        p_put(" snd:%s@%d", s, p_slot);
    }
}

/* E1: a time limit cut below the time played while overtime runs (the match
 * goes on into a new overtime, nothing is "extended"), then the same in
 * sudden death (it goes on). Events: ST<n> (the match status), TL (new time
 * limit), XT ("Timelimit extended, ... canceled!"), OT (overtime added), SD
 * (sudden death entered). One match per phase k of the second "vote yes". */
static void p_ot_watch(void)
{
    if (!p_on) return;
    if ((int)tdm_match_status != p_status0) p_put(" ST%d@%d", (int)tdm_match_status, p_slot);
    p_status0 = tdm_match_status;
    for (; p_bp_j < h_nbprints; p_bp_j++) {
        const char *t = h_bprints[p_bp_j].text;
        if (strstr(t, "Timelimit extended")) p_put(" XT@%d", p_slot);
        else if (strstr(t, "overtime.")) p_put(" OT@%d", p_slot);
        else if (strstr(t, "Sudden Death")) p_put(" SD@%d", p_slot);
        else if (strstr(t, "New timelimit")) p_put(" TL@%d", p_slot);
    }
}

static void p_ot_case(const char *tag, int k, int until, const char *limit)
{
    for (int c = 0; c < 12000 && (int)tdm_match_status != until; c++) p_cmd(0, 0, 0);
    CHECK((int)tdm_match_status == until, "%s k=%d: status %d reached (%d)", tag, k, until, (int)tdm_match_status);
    p_settle(P_WX, 0, 24, 60);
    p_status0 = tdm_match_status;
    p_seg_hook = p_ot_watch;
    p_begin(8500 + 10 * until + k);
    p_bp_j = 0;
    h_client_command(f_p, va("vote timelimit %s", limit));
    for (int c = 0; c < 120; c++) {
        if (c == k) h_client_command(p_tgt, "vote yes");
        p_cmd(0, 0, 0);
    }
    p_seg_hook = NULL;
    p_end("WT %s k=%d", tag, k);
}

static void p_ot_vote(int k)
{
    if (60 % h_rate) { printf("OBS i_ot: 60 usercmds per second do not divide into %d Hz ticks\n", h_rate); return; }
    p_scene();
    p_match_time = "90";
    if (!p_match()) return;
    imp_cvar_set("g_tie_mode", "1");
    imp_cvar_set("g_overtime", "60");
    p_ot_case("ot", k, MM_OVERTIME, "1");       /* 60 s < 91 s played */
    imp_cvar_set("g_tie_mode", "2");            /* the next overtime ends tied: sudden death */
    p_ot_case("sd", k, MM_SUDDEN_DEATH, "2");   /* 120 s < 150 s played */
    CHECK(1, "i_ot ran (lines compared by compare_parity.py)");
}
static void sc_i_ot0(void) { p_ot_vote(0); }
static void sc_i_ot1(void) { p_ot_vote(1); }
static void sc_i_ot2(void) { p_ot_vote(2); }
static void sc_i_ot3(void) { p_ot_vote(3); }
static void sc_i_ot4(void) { p_ot_vote(4); }
static void sc_i_ot5(void) { p_ot_vote(5); }

/* E2 and E4: hurt zones, trigger_multiple and delayed targets, drowning.
 * The shooter steps in at usercmd k (h_place acts in that usercmd's
 * ClientThink, where the touches are), stays, steps out. Events: S-n (its
 * health; no armor), snd:<name> (its sounds: world/electro is the hurt
 * zone, misc/talk the trigger messages, player/gurp*, drown1, watr_*,
 * gasp* the water). */
#define P_ZX    2064.0f                 /* the zones: x; y below */
static void p_world_scene(void)
{
    p_scene();
    int m;
    m = h_add_inline(P_ZX - 64, 1936, 0, P_ZX + 64, 2064, 128);   /* hurt, plain */
    h_ent("{\n\"classname\" \"trigger_hurt\"\n\"model\" \"*%d\"\n\"dmg\" \"1\"\n}\n", m);
    m = h_add_inline(P_ZX - 64, 2436, 0, P_ZX + 64, 2564, 128);   /* hurt, SLOW */
    h_ent("{\n\"classname\" \"trigger_hurt\"\n\"model\" \"*%d\"\n\"dmg\" \"1\"\n\"spawnflags\" \"16\"\n}\n", m);
    m = h_add_inline(P_ZX - 64, 2936, 0, P_ZX + 64, 3064, 128);   /* trigger_multiple, wait 1 */
    h_ent("{\n\"classname\" \"trigger_multiple\"\n\"model\" \"*%d\"\n\"wait\" \"1\"\n\"sounds\" \"2\"\n\"message\" \"m\"\n}\n", m);
    m = h_add_inline(P_ZX - 64, 3436, 0, P_ZX + 64, 3564, 128);   /* trigger_multiple, delay 0.5, wait 2 */
    h_ent("{\n\"classname\" \"trigger_multiple\"\n\"model\" \"*%d\"\n\"wait\" \"2\"\n\"delay\" \"0.5\"\n\"message\" \"d\"\n}\n", m);
    m = h_add_inline(P_ZX - 64, 3936, 0, P_ZX + 64, 4064, 128);   /* trigger_multiple, delay 0.25, wait 1 */
    h_ent("{\n\"classname\" \"trigger_multiple\"\n\"model\" \"*%d\"\n\"wait\" \"1\"\n\"delay\" \"0.25\"\n\"message\" \"q\"\n}\n", m);
    m = h_add_inline(P_ZX + 336, 1936, 0, P_ZX + 464, 2064, 128); /* jump pad: up, 500 units/s */
    h_ent("{\n\"classname\" \"trigger_push\"\n\"model\" \"*%d\"\n\"angle\" \"-1\"\n\"speed\" \"50\"\n}\n", m);
    m = h_add_inline(P_ZX + 336, 2400, 0, P_ZX + 464, 2800, 128); /* push pad: +y, 50 units/s, a long touch */
    h_ent("{\n\"classname\" \"trigger_push\"\n\"model\" \"*%d\"\n\"angle\" \"90\"\n\"speed\" \"5\"\n}\n", m);
    h_add_water(P_ZX - 200, -3200, 0, P_ZX + 200, -2800, 300);    /* deep water */
}

/* the push pads also: the shooter's height (jump pad, axis 2) or y (push
 * pad, axis 1) on the grid frames when it changed */
static int p_zone_axis;
static float p_zone_v0;

static void p_zone_watch(void)
{
    p_sounds_of_shooter();
    if (p_zone_axis && (p_slot & 1) && ((p_slot - 1) / 2 + 1) % 6 == 0 && f_p->s.origin[p_zone_axis] != p_zone_v0) {
        p_zone_v0 = f_p->s.origin[p_zone_axis];
        p_put(" %c%.1f@%d", p_zone_axis == 2 ? 'z' : 'y', p_zone_v0, p_slot);
    }
}

static void p_zone_case(const char *tag, int k, float y, int stay, int after, unsigned seed)
{
    for (int i = 0; i < game.num_items; i++)
        if (itemlist[i].flags & IT_ARMOR) f_p->client->inventory[i] = 0;
    f_p->health = 1000;
    p_settle(P_ZX, 1700, 24, 120);
    p_zone_axis = !strcmp(tag, "jump") ? 2 : !strcmp(tag, "slide") ? 1 : 0;
    p_zone_v0 = p_zone_axis ? f_p->s.origin[p_zone_axis] : 0;
    p_seg_hook = p_zone_watch;
    p_begin(seed);
    p_snd_j = 0;
    for (int c = 0; c < k + stay + after; c++) {
        if (c == k) h_place(f_p, p_zone_axis ? P_ZX + 400 : P_ZX, y, 24);
        if (c == k + stay) h_place(f_p, P_ZX, 1700, 24);
        p_cmd(0, 0, 0);
    }
    p_seg_hook = NULL;
    p_end("WH %s k=%d", tag, k);
}

static void sc_i_world(void)
{
    if (60 % h_rate) { printf("OBS i_world: 60 usercmds per second do not divide into %d Hz ticks\n", h_rate); return; }
    p_world_scene();
    if (!p_match()) return;
    p_yaw = 0;
    p_arm(0);
    for (int i = 0; i < 120; i++) p_cmd(0, 0, 0);
    for (int k = 0; k < 6; k++) {
        p_zone_case("hurt", k, 2000, 120, 60, 9000 + k);
        p_zone_case("slow", k, 2500, 180, 60, 9010 + k);
        p_zone_case("multi", k, 3000, 150, 60, 9020 + k);
        p_zone_case("delay", k, 3500, 30, 180, 9030 + k);
        p_zone_case("delayq", k, 4000, 30, 120, 9040 + k);
        p_zone_case("drown", k, -3000, 960, 60, 9050 + k);
        p_zone_case("jump", k, 2000, 300, 120, 9060 + k);
        p_zone_case("slide", k, 2420, 240, 60, 9070 + k);
    }
    CHECK(1, "i_world ran (lines compared by compare_parity.py)");
}

/* a timeout and a dropped weapon: "drop railgun" at usercmd 0, the timeout
 * at `pause` (tfly: the weapon in flight; tthink: the window whose grid
 * frame makes it touchable, its think due on the frozen frame), resumed 1 s
 * later. Events: I(..) its origin on the grid frames, TCH touchable, FREE. */
static edict_t *p_titem;
static int p_titem_tch;
static vec3_t p_titem_org, p_titem_last;

static void p_titem_watch(void)
{
    if (!p_on || !p_titem) return;
    if (!p_titem->inuse) { p_put(" FREE@%d", p_slot); p_titem = NULL; return; }
    if (!p_titem_tch && p_titem->touch == Touch_Item) { p_titem_tch = 1; p_put(" TCH@%d", p_slot); }
    /* its origin as packed for the clients (1/8 units), on the grid frames
     * out of the timeout (during a timeout the 60 Hz world shows a thing in
     * flight up to 5 frames further: the report's boundaries) */
    int grid = (p_slot & 1) && ((p_slot - 1) / 2 + 1) % 6 == 0;
    int moved = grid && !VectorCompare(p_titem->s.origin, p_titem_last);   /* since the last grid frame */
    if (grid) VectorCopy(p_titem->s.origin, p_titem_last);
    if (grid && moved && tdm_match_status != MM_TIMEOUT && !VectorCompare(p_titem->s.origin, p_titem_org)) {
        p_put(" I(%.3f %.3f %.3f)@%d", floorf(p_titem->s.origin[0] * 8 + 0.5f) / 8, floorf(p_titem->s.origin[1] * 8 + 0.5f) / 8,
              floorf(p_titem->s.origin[2] * 8 + 0.5f) / 8, p_slot);
        VectorCopy(p_titem->s.origin, p_titem_org);
    }
}

static void p_tpause_case(const char *tag, int k, unsigned seed, int pause)
{
    h_give(f_p, "railgun", 0);
    h_give(f_p, "slugs", 10);
    p_on = 0;
    for (int i = 0; i < 120; i++) p_cmd(0, 0, 0);
    h_client_command(f_p, "use blaster");
    f_p->health = 1000;
    p_yaw = 0;
    p_settle(2600, -1500, 24, 240);
    p_seg_hook = p_titem_watch;
    p_begin(seed);
    p_titem = NULL;
    p_titem_tch = 0;
    for (int c = 0; c < 1500; c++) {
        if (c == 0) {
            h_client_command(f_p, "drop railgun");
            p_titem = p_find_dropped("weapon_railgun", DROPPED_ITEM, f_p);
            if (p_titem) {
                VectorCopy(p_titem->s.origin, p_titem_org);
                VectorCopy(p_titem->s.origin, p_titem_last);
            }
        }
        if (c == pause || c == pause + 60) h_client_command(f_p, "time");
        p_cmd(0, 0, 0);
    }
    p_seg_hook = NULL;
    p_end("WM %s k=%d", tag, k);
    for (int i = 0; i < 1800 && tdm_match_status == MM_TIMEOUT; i++) p_cmd(0, 0, 0);
    if (p_titem && p_titem->inuse) G_FreeEdict(p_titem);
    p_titem = NULL;
}

/* rework 4 (Fable 18.11 Zh1), against the original at 10 Hz: a weapon
 * dropped at usercmd 0 and a timeout at 12 + k (tfly: it is in flight) or
 * 60 + k (tthink: its think due on the frozen frame), resumed 1 s later.
 * (Rework 7: the moving parts are the DosZone code again; their cases of
 * reworks 3-6 are gone, these timer cases stay.) */
static void sc_i_tpause(void)
{
    if (60 % h_rate) { printf("OBS i_tpause: 60 usercmds per second do not divide into %d Hz ticks\n", h_rate); return; }
    p_scene();
    if (!p_match()) return;
    p_yaw = 0;
    p_arm(0);
    for (int i = 0; i < 120; i++) p_cmd(0, 0, 0);
    for (int k = 0; k < 6; k++) {
        p_tpause_case("tfly", k, 9900 + k, 12 + k);
        p_tpause_case("tthink", k, 9910 + k, 60 + k);
    }
    CHECK(1, "i_tpause ran (lines compared by compare_parity.py)");
}

#define H_SCENARIOS_PARITY \
    { "w_moving", sc_w_moving }, \
    { "i_items", sc_i_items }, \
    { "i_drops", sc_i_drops }, \
    { "i_vote0", sc_i_vote0 }, \
    { "i_vote1", sc_i_vote1 }, \
    { "i_vote2", sc_i_vote2 }, \
    { "i_vote3", sc_i_vote3 }, \
    { "i_vote4", sc_i_vote4 }, \
    { "i_vote5", sc_i_vote5 }, \
    { "i_ot0", sc_i_ot0 }, \
    { "i_ot1", sc_i_ot1 }, \
    { "i_ot2", sc_i_ot2 }, \
    { "i_ot3", sc_i_ot3 }, \
    { "i_ot4", sc_i_ot4 }, \
    { "i_ot5", sc_i_ot5 }, \
    { "i_world", sc_i_world }, \
    { "i_tpause", sc_i_tpause }, \
    { "w_timing", sc_w_timing }, \
    { "w_special", sc_w_special }, \
    { "w_countdown", sc_w_countdown }, \
    { "r_ext", sc_r_ext },

#endif /* SCENARIOS_PARITY_H */
