/*
 * test_basic.c  —  IR-1 quick demo (runs all 3 modules together)
 */
#include "fiber.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void task(void *arg)
{
    int id = *(int *)arg;
    for (int i = 0; i < 3; i++) {
        printf("[Fiber %d] step %d\n", id, i);
        fiber_yield();
    }
    printf("[Fiber %d] done\n", id);
}

int main(void)
{
    printf("=== FiberLib IR-1 Demo (M1 + M2 + M3) ===\n\n");
    fiber_t *fa, *fb;
    int ia=1, ib=2;
    fiber_create(&fa, task, &ia);
    fiber_create(&fb, task, &ib);
    printf("Created 2 fibers. Starting scheduler...\n\n");
    fiber_run();
    return 0;
}
