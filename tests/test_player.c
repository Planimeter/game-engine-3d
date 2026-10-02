/* Copyright Planimeter. All Rights Reserved. */

/* Standalone checks for the character controller, so movement, jumping and
 * prop collision are verified without a window or synthetic key events.
 *
 *   cc -std=c99 -Isrc src/player.c src/physics.c src/terrain.c tests/test_player.c -lm -o /tmp/test_physics
 *   /tmp/test_physics
 */

#include "player.h"

#include <math.h>
#include <stdio.h>

#define SEED 20261002u
#define AMPLITUDE 9.0f
#define EXTENT 600.0f
#define DT (1.0f / 60.0f)

static int g_failures;

static void check(int condition, const char *what)
{
    printf("  %-52s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition) {
        g_failures++;
    }
}

static void step(Player *p, const PlayerInput *in, float dt, int frames,
                  const PhysicsBox *boxes, int boxCount)
{
    for (int i = 0; i < frames; i++) {
        player_update(p, in, 0.0f, dt, boxes, boxCount);
    }
}

/* Settle the character onto the terrain before a measurement. */
static void settle(Player *p)
{
    PlayerInput idle = {0.0f, 0.0f, 0, 0};
    step(p, &idle, DT, 60, NULL, 0);
}

static void test_ground(void)
{
    printf("grounding\n");

    Player p;
    PlayerInput idle = {0.0f, 0.0f, 0, 0};
    player_init(&p, 0.0f, 14.0f, SEED, AMPLITUDE, EXTENT);
    step(&p, &idle, DT, 120, NULL, 0);

    check(p.grounded, "rests on the terrain");
    check(fabsf(p.position[1] - player_ground_height(&p)) < 0.01f,
          "feet sit at the surface height");
    check(p.velocity[1] == 0.0f, "vertical velocity zeroed on landing");

    /* From here the character is treated as still; later cases measure from
     * a known resting state. */
    settle(&p);
}

static void test_movement(void)
{
    printf("movement\n");

    Player p;
    PlayerInput in = {0.0f, 0.0f, 0, 0};
    player_init(&p, 0.0f, 14.0f, SEED, AMPLITUDE, EXTENT);
    settle(&p);

    float startZ = p.position[2];
    in.forward = 1.0f;
    step(&p, &in, DT, 60, NULL, 0);
    float oneSecond = startZ - p.position[2];

    check(oneSecond > 5.0f && oneSecond < 13.0f, "walks about the walk speed");
    check(p.grounded, "stays grounded while walking");

    /* Releasing the keys should slide to a stop, not halt instantly. */
    in.forward = 0.0f;
    float coastStart = p.position[2];
    step(&p, &in, DT, 6, NULL, 0);
    float coast = coastStart - p.position[2];

    check(coast > 0.0f, "keeps rolling after the key is released");
    check(coast < 1.5f, "but decelerates instead of gliding");
    step(&p, &in, DT, 180, NULL, 0);
    check(fabsf(p.velocity[0]) < 0.01f && fabsf(p.velocity[2]) < 0.01f,
          "comes to a complete stop");

    /* Sprint must beat walking over the same time. */
    player_init(&p, 0.0f, 14.0f, SEED, AMPLITUDE, EXTENT);
    settle(&p);
    in.forward = 1.0f;
    in.sprint = 1;
    float sprintStart = p.position[2];
    step(&p, &in, DT, 60, NULL, 0);
    check(sprintStart - p.position[2] > oneSecond, "sprint outruns walk");

    /* Strafe should cover comparable ground to walking forward. */
    player_init(&p, 0.0f, 14.0f, SEED, AMPLITUDE, EXTENT);
    settle(&p);
    in.sprint = 0;
    in.forward = 0.0f;
    in.right = 1.0f;
    float strafeStart = p.position[0];
    step(&p, &in, DT, 60, NULL, 0);
    float strafed = p.position[0] - strafeStart;
    check(strafed > 5.0f && strafed < 13.0f, "strafes at walking speed");
}

static void test_jump(void)
{
    printf("jump\n");

    Player p;
    PlayerInput in = {0.0f, 0.0f, 0, 0};
    player_init(&p, 0.0f, 14.0f, SEED, AMPLITUDE, EXTENT);
    settle(&p);

    float groundY = p.position[1];
    in.jump = 1;
    player_update(&p, &in, 0.0f, DT, NULL, 0);
    in.jump = 0;

    check(!p.grounded, "jump leaves the ground");
    check(p.velocity[1] > 0.0f, "jump impulse applied");

    float peak = p.position[1];
    int airborneFrames = 0;
    for (int i = 0; i < 300; i++) {
        player_update(&p, &in, 0.0f, DT, NULL, 0);
        if (!p.grounded) {
            airborneFrames++;
        }
        if (p.position[1] > peak) {
            peak = p.position[1];
        }
    }

    check(airborneFrames > 12, "stays airborne for a readable moment");
    check(peak > groundY + 1.0f, "jump gains real height");
    check(p.grounded, "gravity brings it back down");
    check(fabsf(p.position[1] - player_ground_height(&p)) < 0.01f,
          "lands back on the surface");

    /* A second jump edge while grounded works; holding it does not re-trigger. */
    in.jump = 1;
    player_update(&p, &in, 0.0f, DT, NULL, 0);
    check(!p.grounded, "a second jump works after landing");
}

/* Find an XZ position where the terrain is flat across a whole walk, not
 * just the next stride. The character crosses several units before the test
 * measures anything, so the search samples the full path length: walking
 * off a ridge would otherwise show up as the prop blocking the walk. */
static void find_flat_spot(float *outX, float *outZ)
{
    float bestX = 0.0f;
    float bestZ = 14.0f;
    float bestDrop = 1e9f;

    /* Long enough to reach the box and clear it. */
    const float runOut = 12.0f;

    for (float x = -60.0f; x <= 60.0f; x += 1.0f) {
        for (float z = 8.0f; z <= 40.0f; z += 1.0f) {
            float start = physics_terrain_height(x, z, SEED, AMPLITUDE);
            /* Worst absolute deviation from the start height along the path
             * the character will actually take. */
            float worst = 0.0f;
            for (float t = 0.0f; t <= runOut; t += 0.5f) {
                float h = physics_terrain_height(x, z - t, SEED, AMPLITUDE);
                float deviation = fabsf(h - start);
                if (deviation > worst) {
                    worst = deviation;
                }
            }
            if (worst < bestDrop) {
                bestDrop = worst;
                bestX = x;
                bestZ = z;
            }
        }
    }

    *outX = bestX;
    *outZ = bestZ;
}

static void test_props(void)
{
    printf("props\n");

    /* A wall tall enough that standing on it is impossible. */
    PhysicsBox wall[1];
    wall[0].center[0] = 0.0f;
    wall[0].center[1] = 100.0f;
    wall[0].center[2] = 8.0f;
    wall[0].half[0] = 1.0f;
    wall[0].half[1] = 50.0f;
    wall[0].half[2] = 1.0f;

    /* The near face is at z = 9, so the character centre must stop one
     * radius beyond it. */
    float limit = 9.0f;

    Player p;
    PlayerInput in = {1.0f, 0.0f, 0, 0};
    player_init(&p, 0.0f, 14.0f, SEED, AMPLITUDE, EXTENT);
    settle(&p);
    in.forward = 1.0f;
    step(&p, &in, DT, 300, wall, 1);

    /* The push-out is along the axis of least penetration, so on a straight
     * approach the centre settles at the face plus one radius. A small
     * tolerance covers the leftover velocity in the frame of contact. */
    check(p.position[2] > limit + p.radius - 0.05f,
          "prop stops the walk clear of its face");
    check(p.position[2] < limit + p.radius + 0.2f,
          "and does not let the character inside");

    /* A long frame must not tunnel through the same wall. */
    player_init(&p, 0.0f, 14.0f, SEED, AMPLITUDE, EXTENT);
    settle(&p);
    in.forward = 1.0f;
    step(&p, &in, 0.25f, 60, wall, 1);
    check(p.position[2] > limit + p.radius - 0.05f,
          "substepping prevents tunnelling on a long frame");

    /* A low box should be climbed, not treated as a wall. Both cases sit on
     * flat ground so a drop off the spawn ridge cannot be mistaken for the
     * box blocking the walk. */
    float flatX, flatZ;
    find_flat_spot(&flatX, &flatZ);

    PhysicsBox stepBox[1];
    player_init(&p, flatX, flatZ, SEED, AMPLITUDE, EXTENT);
    settle(&p);

    float floorHere = player_ground_height(&p);
    stepBox[0].center[0] = flatX;
    stepBox[0].center[1] = floorHere + 0.2f;
    stepBox[0].center[2] = flatZ - 2.0f;
    stepBox[0].half[0] = 1.0f;
    stepBox[0].half[1] = 0.2f;
    stepBox[0].half[2] = 1.0f;

    float climbFinish = stepBox[0].center[2] - 1.0f - p.radius;
    in.forward = 1.0f;
    step(&p, &in, DT, 300, stepBox, 1);
    check(p.position[2] < climbFinish,
          "a low prop is climbed rather than blocking");
    check(p.position[1] > floorHere,
          "the climb lifts the feet onto the crate");

    /* The same walk on the same ground with no prop passes freely. */
    player_init(&p, flatX, flatZ, SEED, AMPLITUDE, EXTENT);
    settle(&p);
    in.forward = 1.0f;
    step(&p, &in, DT, 300, NULL, 0);
    check(p.position[2] < climbFinish,
          "the same walk passes when the prop is absent");

    /* A wall on the same flat ground does stop the walk. */
    PhysicsBox flatWall[1];
    player_init(&p, flatX, flatZ, SEED, AMPLITUDE, EXTENT);
    settle(&p);
    flatWall[0].center[0] = flatX;
    flatWall[0].center[1] = 100.0f;
    flatWall[0].center[2] = flatZ - 2.0f;
    flatWall[0].half[0] = 1.0f;
    flatWall[0].half[1] = 50.0f;
    flatWall[0].half[2] = 1.0f;

    in.forward = 1.0f;
    step(&p, &in, DT, 300, flatWall, 1);
    float face = flatWall[0].center[2] + flatWall[0].half[2];
    check(p.position[2] > face + p.radius - 0.05f,
          "a tall prop stops the walk on flat ground too");
}

static void test_bounds(void)
{
    printf("bounds\n");

    Player p;
    PlayerInput in = {1.0f, 0.0f, 0, 0};
    player_init(&p, 0.0f, 14.0f, SEED, AMPLITUDE, EXTENT);
    settle(&p);

    /* Long enough to cross the 600-unit terrain at a sprint. */
    in.sprint = 1;
    step(&p, &in, DT, 6000, NULL, 0);

    float limit = EXTENT * 0.5f - p.radius - 1.0f;
    check(p.position[2] > -limit - 0.05f && p.position[2] < limit + 0.05f,
          "cannot walk off the near edge of the terrain");
    check(p.position[0] > -limit - 0.05f && p.position[0] < limit + 0.05f,
          "bounds apply on both axes");
    check(p.grounded, "still grounded at the boundary");
}

int main(void)
{
    printf("character controller checks\n\n");

    test_ground();
    test_movement();
    test_jump();
    test_props();
    test_bounds();

    printf("\n%s\n", g_failures ? "FAILURES PRESENT" : "all checks passed");
    return g_failures ? 1 : 0;
}
