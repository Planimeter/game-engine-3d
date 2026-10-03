/* Copyright Planimeter. All Rights Reserved. */

#include "selftest.h"

#include "physics.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SELFTEST_FRAMES 90

typedef struct {
    char label[40];
    int passed;
    char detail[56];
} SelftestResult;

#define SELFTEST_MAX_RESULTS 16

/* Each phase starts from a known spawn so one does not inherit velocity or a
 * fall from the previous. */
static const struct { float x, z; } SELFTEST_SPAWNS[] = {
    { 0.0f, 14.0f },   /* walk forward */
    { 0.0f, 14.0f },   /* walk backward */
    { 0.0f, 14.0f },   /* sprint */
    { 0.0f, 14.0f },   /* jump */
    { 0.0f, 14.0f },   /* walk off a tile edge */
    { 0.0f, 14.0f },   /* strafe */
};
#define SELFTEST_PHASES ((int)(sizeof(SELFTEST_SPAWNS) / sizeof(SELFTEST_SPAWNS[0])))

static SelftestResult g_results[SELFTEST_MAX_RESULTS];
static int g_resultCount;

static float g_startX, g_startZ, g_startY;
static float g_worstGap;     /* largest height the feet floated above the surface */
static int g_airFrames;
static float g_peakY;
static uint32_t g_seed;
static float g_amplitude;

static void selftest_record(SelfTest *t, const char *label, int passed,
                            const char *detail)
{
    if (g_resultCount >= SELFTEST_MAX_RESULTS) {
        t->failures++;
        return;
    }
    SelftestResult *r = &g_results[g_resultCount++];
    snprintf(r->label, sizeof(r->label), "%s", label);
    r->passed = passed;
    snprintf(r->detail, sizeof(r->detail), "%s", detail);
    if (passed) {
        t->checks++;
    } else {
        t->failures++;
    }
}

void selftest_begin(SelfTest *t)
{
    memset(t, 0, sizeof(*t));
    memset(g_results, 0, sizeof(g_results));
    g_resultCount = 0;
    t->started = 1;
}

const char *selftest_line(const SelfTest *t, int index)
{
    (void)t;
    static char line[112];

    if (index < 0 || index >= g_resultCount) {
        return "";
    }
    const SelftestResult *r = &g_results[index];
    snprintf(line, sizeof(line), "%s %-36s %s", r->passed ? "ok  " : "FAIL",
             r->label, r->detail);
    return line;
}

const char *selftest_status(const SelfTest *t)
{
    static char status[96];

    if (t->done) {
        snprintf(status, sizeof(status),
                 "self test done: %d checks, %d failed",
                 t->checks, t->failures);
    } else {
        /* The phase is derived from the frame count, so the number on screen
         * always matches what the character is doing. */
        int phase = t->frame / SELFTEST_FRAMES + 1;
        snprintf(status, sizeof(status),
                 "self test running: phase %d of %d", phase, SELFTEST_PHASES);
    }
    return status;
}

static void selftest_input(int phase, int frame, PlayerInput *in)
{
    memset(in, 0, sizeof(*in));
    switch (phase) {
        case 0: in->forward = 1.0f; break;
        case 1: in->forward = -1.0f; break;
        case 2: in->forward = 1.0f; in->sprint = 1; break;
        case 3: break;
        case 4: in->forward = 1.0f; break;
        case 5: in->right = 1.0f; break;
        default: break;
    }
    (void)frame;
}

/* Put the character back on the ground at a known spot, so each phase starts
 * from rest rather than inheriting motion from the last one. */
static void selftest_settle(Player *p, const PhysicsBox *boxes, int boxCount,
                            float x, float z, float yaw)
{
    PlayerInput idle;
    memset(&idle, 0, sizeof(idle));

    p->velocity[0] = p->velocity[1] = p->velocity[2] = 0.0f;
    p->position[0] = x;
    p->position[2] = z;
    p->position[1] = physics_surface_height(x, z, g_seed, g_amplitude,
                                            boxes, boxCount);
    for (int i = 0; i < 30; i++) {
        player_update(p, &idle, yaw, 1.0f / 60.0f, boxes, boxCount);
    }
}

int selftest_update(SelfTest *t, Player *player, const PlayerInput *unused,
                    float yaw, float dt, const PhysicsBox *boxes,
                    int boxCount)
{
    (void)unused;

    if (!t->started || t->done) {
        return 0;
    }

    if (t->frame == 0) {
        /* Take the world parameters from the character, so the run reports
         * against the same map the game is drawing rather than a guess. */
        g_seed = player->terrainSeed;
        g_amplitude = player->terrainAmplitude;
    }

    int phase = t->frame / SELFTEST_FRAMES;
    int frame = t->frame % SELFTEST_FRAMES;
    PlayerInput in;
    selftest_input(phase, frame, &in);

    if (phase == 3 && frame == 0) {
        in.jump = 1;
    }

    if (frame == 0) {
        selftest_settle(player, boxes, boxCount,
                        SELFTEST_SPAWNS[phase].x, SELFTEST_SPAWNS[phase].z, yaw);
        g_startX = player->position[0];
        g_startZ = player->position[2];
        g_startY = player->position[1];
        g_worstGap = 0.0f;
        g_airFrames = 0;
        g_peakY = player->position[1];
    }

    player_update(player, &in, yaw, dt, boxes, boxCount);

    float ground = physics_surface_height(player->position[0], player->position[2],
                                          g_seed, g_amplitude, boxes, boxCount);
    float gap = player->position[1] - ground;
    if (gap > g_worstGap) {
        g_worstGap = gap;
    }
    if (!player->grounded) {
        g_airFrames++;
        if (player->position[1] > g_peakY) {
            g_peakY = player->position[1];
        }
    }

    char detail[56];

    if (frame == SELFTEST_FRAMES - 1) {
        float dx = player->position[0] - g_startX;
        float dz = player->position[2] - g_startZ;
        float dist = sqrtf(dx * dx + dz * dz);
        float speed = dist / (SELFTEST_FRAMES * dt);

        switch (phase) {
            case 0:
                snprintf(detail, sizeof(detail), "%.2f u/s", speed);
                selftest_record(t, "walks at walk speed",
                                speed > 7.0f && speed < 11.0f, detail);
                snprintf(detail, sizeof(detail),
                         "gap %.3f, %d air", g_worstGap, g_airFrames);
                selftest_record(t, "walking never floats above ground",
                                g_worstGap < 0.05f, detail);
                break;

            case 1:
                snprintf(detail, sizeof(detail), "%.2f u/s", speed);
                selftest_record(t, "walks backward", speed > 6.0f, detail);
                break;

            case 2:
                snprintf(detail, sizeof(detail), "%.2f u/s", speed);
                selftest_record(t, "sprint outruns walk", speed > 12.0f, detail);
                break;

            case 3:
                snprintf(detail, sizeof(detail),
                         "+%.2f high, %d air", g_peakY - g_startY, g_airFrames);
                selftest_record(t, "jump gains height and lands",
                                g_peakY - g_startY > 1.0f && g_airFrames > 5,
                                detail);
                break;

            case 4:
                snprintf(detail, sizeof(detail),
                         "gap %.3f, %d air", g_worstGap, g_airFrames);
                selftest_record(t, "steps down a tile without floating",
                                g_worstGap < 0.05f, detail);
                break;

            case 5:
                snprintf(detail, sizeof(detail), "%.2f u/s", speed);
                selftest_record(t, "strafes at walk speed", speed > 7.0f, detail);
                break;

            default:
                break;
        }
    }

    t->frame++;
    if (phase + 1 >= SELFTEST_PHASES) {
        t->done = 1;
        return 0;
    }
    return 1;
}
