#pragma once
#include <ucontext.h>
#include <stdint.h>
#include <sys/types.h>

/* ── Stack sizes ── */
#define FIBER_STACK_SIZE  (64 * 1024)
#define FIBER_GUARD_SIZE  4096

/* ── Fiber states ── */
typedef enum {
    FIBER_READY,
    FIBER_RUNNING,
    FIBER_BLOCKED,
    FIBER_DONE
} fiber_state_t;

/* ── Forward declarations ── */
typedef struct fiber fiber_t;
typedef struct fiber_mutex fiber_mutex_t;
typedef struct fiber_cond  fiber_cond_t;

/* ── Fiber Control Block (FCB) ── */
struct fiber {
    ucontext_t      ctx;
    void           *stack_base;
    fiber_state_t   state;
    int             exit_code;
    struct fiber   *next;       /* run-queue / wait-queue link */
    struct fiber   *join_waiter;/* fiber waiting on this one  */
};

/* ── Mutex ── */
struct fiber_mutex {
    int       locked;
    fiber_t  *owner;
    fiber_t  *wait_head;
    fiber_t  *wait_tail;
};

/* ── Condition variable ── */
struct fiber_cond {
    fiber_t  *wait_head;
    fiber_t  *wait_tail;
};

/* ══ PUBLIC API ══════════════════════════════════════════════ */

/* M1 – core */
int  fiber_create(fiber_t **out, void (*fn)(void *), void *arg);
void fiber_yield(void);
void fiber_exit(void);
void fiber_run(void);
int  fiber_join(fiber_t *f);   /* wait for fiber f to finish  */

/* M2 – process integration */
pid_t fiber_fork(void);        /* fork without blocking scheduler */
int   fiber_waitpid(pid_t pid);/* wait for child process          */
int   fiber_exec(const char *path, char *const argv[]); /* exec wrapper */

/* M2 – signals */
void fiber_signal_init(void);  /* set up SIGALRM pre-emption      */
void fiber_signal_mask(void);  /* mask signals in critical section */
void fiber_signal_unmask(void);

/* M2 – pipes / channels */
typedef struct fiber_chan fiber_chan_t;
fiber_chan_t *fiber_chan_create(int capacity);
void  fiber_chan_send(fiber_chan_t *c, void *item);
void *fiber_chan_recv(fiber_chan_t *c);
void  fiber_chan_destroy(fiber_chan_t *c);

/* M3 – scheduler (mutex + condvar + semaphore) */
void fiber_mutex_init(fiber_mutex_t *m);
void fiber_mutex_lock(fiber_mutex_t *m);
void fiber_mutex_unlock(fiber_mutex_t *m);

void fiber_cond_init(fiber_cond_t *cv);
void fiber_cond_wait(fiber_cond_t *cv, fiber_mutex_t *m);
void fiber_cond_signal(fiber_cond_t *cv);
void fiber_cond_broadcast(fiber_cond_t *cv);

/* M3 – non-blocking I/O */
ssize_t fiber_read(int fd, void *buf, size_t n);
ssize_t fiber_write(int fd, const void *buf, size_t n);

/* M3 – benchmark helper */
uint64_t fiber_now_ns(void);
