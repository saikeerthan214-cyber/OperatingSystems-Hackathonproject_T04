/*
 * test_m1.c  —  Module 1 (W1–W4)
 * Tests: fiber_create, fiber_yield, fiber_exit, fiber_join, fiber_fork, fiber_waitpid
 */
#include "fiber.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* ── W1/W2/W3: basic round-robin ── */
static void task(void *arg)
{
    int id = *(int *)arg;
    for (int i = 0; i < 3; i++) {
        printf("[Fiber %d] step %d\n", id, i);
        fiber_yield();
    }
    printf("[Fiber %d] done\n", id);
}

/* ── W4: fork + waitpid inside a fiber ── */
static void fork_task(void *arg)
{
    (void)arg;
    printf("[Fork fiber] about to fork a child process\n");

    pid_t pid = fiber_fork();
    if (pid == 0) {
        /* Child process — use _exit to skip ASan cleanup */
        printf("[Child process %d] Hello from child!\n", getpid());
        _exit(42);
    } else {
        /* Parent fiber waits non-blocking — other fibers keep running */
        int code = fiber_waitpid(pid);
        printf("[Fork fiber] child exited with code %d\n", code);
    }
}

int main(void)
{
    printf("=== Module 1 Test (W1–W4) ===\n\n");

    fiber_t *fa, *fb, *fc;
    int ia=1, ib=2;

    /* W1–W3: round-robin */
    fiber_create(&fa, task, &ia);
    fiber_create(&fb, task, &ib);

    /* W4: fork/waitpid */
    fiber_create(&fc, fork_task, NULL);

    printf("[M1] Starting scheduler...\n\n");
    fiber_run();
    return 0;
}
