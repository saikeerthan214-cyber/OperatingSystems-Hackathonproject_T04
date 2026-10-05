/*
 * test_m3.c  —  Module 3 (W9–W12)
 * Tests: non-blocking I/O, mutex, condvar, benchmark
 */
#include "fiber.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── W9: non-blocking pipe I/O ── */
static int pipe_fds[2];

static void io_writer(void *arg)
{
    (void)arg;
    const char *msgs[] = {"Hello", "from", "fiber", "IO", NULL};
    for (int i = 0; msgs[i]; i++) {
        fiber_write(pipe_fds[1], msgs[i], strlen(msgs[i])+1);
        printf("[Writer] sent: %s\n", msgs[i]);
        fiber_yield();
    }
    close(pipe_fds[1]);
}

static void io_reader(void *arg)
{
    (void)arg;
    char buf[64];
    ssize_t n;
    while ((n = fiber_read(pipe_fds[0], buf, sizeof buf)) > 0)
        printf("[Reader] got: %s\n", buf);
    close(pipe_fds[0]);
}

/* ── W10/W11: mutex + condvar — shared counter ── */
static fiber_mutex_t mtx;
static fiber_cond_t  cond;
static int           shared = 0;
static int           ready  = 0;

static void mutex_producer(void *arg)
{
    (void)arg;
    for (int i = 1; i <= 5; i++) {
        fiber_mutex_lock(&mtx);
        shared = i;
        ready  = 1;
        printf("[Mutex Producer] set shared = %d\n", shared);
        fiber_cond_signal(&cond);
        fiber_mutex_unlock(&mtx);
        fiber_yield();
    }
}

static void mutex_consumer(void *arg)
{
    (void)arg;
    int received = 0;
    while (received < 5) {
        fiber_mutex_lock(&mtx);
        while (!ready)
            fiber_cond_wait(&cond, &mtx);
        printf("[Mutex Consumer] got shared = %d\n", shared);
        ready = 0;
        received++;
        fiber_mutex_unlock(&mtx);
    }
    printf("[Mutex Consumer] all values received — no deadlock\n");
}

/* ── W12: benchmark — context switch cost ── */
#define BENCH_ITERS 100000

static volatile int bench_done = 0;

static void bench_ping(void *arg)
{
    (void)arg;
    for (int i = 0; i < BENCH_ITERS; i++)
        fiber_yield();
    bench_done = 1;
}

static void bench_pong(void *arg)
{
    (void)arg;
    while (!bench_done)
        fiber_yield();
}

int main(void)
{
    printf("=== Module 3 Test (W9–W12) ===\n\n");

    /* W9: pipe I/O */
    printf("── W9: Non-blocking I/O ──\n");
    pipe(pipe_fds);
    fiber_t *fw, *fr;
    fiber_create(&fw, io_writer, NULL);
    fiber_create(&fr, io_reader, NULL);
    fiber_run();

    printf("\n── W10/W11: Mutex + Condvar ──\n");
    fiber_mutex_init(&mtx);
    fiber_cond_init(&cond);
    fiber_t *mp, *mc;
    fiber_create(&mp, mutex_producer, NULL);
    fiber_create(&mc, mutex_consumer, NULL);
    fiber_run();

    /* W12: benchmark */
    printf("\n── W12: Context-switch Benchmark ──\n");
    printf("Running %d ping-pong yields...\n", BENCH_ITERS * 2);
    uint64_t t0 = fiber_now_ns();
    fiber_t *bp, *bq;
    fiber_create(&bp, bench_ping, NULL);
    fiber_create(&bq, bench_pong, NULL);
    fiber_run();
    uint64_t t1 = fiber_now_ns();
    uint64_t total_ns = t1 - t0;
    uint64_t per_switch = total_ns / (BENCH_ITERS * 2);
    printf("[Benchmark] %d switches in %lu ms\n",
           BENCH_ITERS * 2, total_ns / 1000000);
    printf("[Benchmark] ~%lu ns per context switch\n", per_switch);
    printf("[Benchmark] (pthreads typically ~1000–3000 ns)\n");

    return 0;
}
