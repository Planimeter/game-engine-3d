/* Copyright Planimeter. All Rights Reserved. */

/* Concurrency checks for the job system: the engine submits from one thread,
 * but the module makes no such promise. This drives the paths main_sdl.c does
 * not touch — several submitters at once, and parent/child fan-out — and is
 * most useful under ThreadSanitizer:
 *
 *   cc -std=c99 -fsanitize=thread -Isrc src/job_pthread.c tests/test_job.c -lpthread -o /tmp/test_job
 *   /tmp/test_job
 */

#include "job.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static int g_failures;

static void check(int condition, const char *what)
{
    printf("  %-52s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition) {
        g_failures++;
    }
}

/* Several threads submitting and waiting at once. job_alloc_slot() claims
 * slots under jobPoolMutex, but job_submit() then writes the job fields
 * outside any lock, so this is where a two-submitter race would show. */
static JobSystem *g_sys;
static int g_total;
static void work(void *c, uint32_t i)
{
    (void)c;
    (void)i;
    __atomic_fetch_add(&g_total, 1, __ATOMIC_RELAXED);
}
static void *submitter(void *arg)
{
    (void)arg;
    for (int i = 0; i < 500; i++) {
        JobDescriptor d = { work, NULL, 2, "mt", {0} };
        JobHandle h = job_submit(g_sys, &d);
        if (h.id) job_wait(g_sys, h);
    }
    return NULL;
}

/* The engine's own pattern: submit a frame's work and block on it, so the
 * workers are genuinely awake and contending when shutdown happens. */
static void frame_work(void *c, uint32_t i) { (void)c; (void)i; __atomic_fetch_add(&g_total, 1, __ATOMIC_RELAXED); }

static void parent_work(void *c, uint32_t i) { (void)c; (void)i; }

static void test_engine_loop(void)
{
    printf("engine submit/wait loop\n");

    JobSystem *sys = job_create(0);
    if (!sys) {
        check(0, "job system created");
        return;
    }

    int before = g_total;
    for (int f = 0; f < 2000; f++) {
        JobDescriptor u = { frame_work, NULL, 1, "Update", {0} };
        job_wait(sys, job_submit(sys, &u));
        JobDescriptor d = { frame_work, NULL, 1, "Draw", {0} };
        job_wait(sys, job_submit(sys, &d));
    }
    check(g_total - before == 4000, "every submitted item ran exactly once");

    job_waitall(sys);
    job_destroy(sys);
    check(1, "destroyed without hanging");
}

static void test_parent_child(void)
{
    printf("parent/child\n");

    JobSystem *sys = job_create(8);
    if (!sys) {
        check(0, "job system created");
        return;
    }

    int before = g_total;
    int allFinished = 1;
    for (int round = 0; round < 300; round++) {
        JobDescriptor p = { parent_work, NULL, 1, "parent", {0} };
        JobHandle ph = job_submit(sys, &p);
        for (int k = 0; k < 5; k++) {
            JobDescriptor c = { frame_work, NULL, 3, "child", ph };
            job_wait(sys, job_submit(sys, &c));
        }
        job_wait(sys, ph);
        if (!job_isfinished(sys, ph)) {
            allFinished = 0;
            break;
        }
    }

    check(allFinished, "a parent reports finished only after its children");
    check(g_total - before == 300 * 5 * 3, "all child items ran exactly once");

    job_waitall(sys);
    job_destroy(sys);
}

static void test_concurrent_submitters(void)
{
    printf("concurrent submitters\n");

    JobSystem *sys = job_create(4);
    if (!sys) {
        check(0, "job system created");
        return;
    }

    g_sys = sys;
    int before = g_total;

    pthread_t t[6];
    for (int i = 0; i < 6; i++) {
        pthread_create(&t[i], NULL, submitter, NULL);
    }
    for (int i = 0; i < 6; i++) {
        pthread_join(t[i], NULL);
    }

    check(g_total - before == 6 * 500 * 2,
          "six submitters ran every item exactly once");

    job_waitall(sys);
    job_destroy(sys);
}

int main(void)
{
    printf("job system checks\n\n");

    test_engine_loop();
    test_parent_child();
    test_concurrent_submitters();

    printf("\n%s\n", g_failures ? "FAILURES PRESENT" : "all checks passed");
    return g_failures ? 1 : 0;
}
