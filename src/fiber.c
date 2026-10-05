/*
 * fiber.c  —  FiberLib complete implementation
 *
 * M1  (W1–W4):  fiber_create, swapcontext, yield, exit, join, fork, waitpid
 * M2  (W5–W8):  mmap stacks, guard pages, signals/pre-emption, pipes/channels
 * M3  (W9–W12): mutex, condvar, non-blocking I/O, benchmark
 */

#define _GNU_SOURCE
#include "fiber.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>

/* ═══════════════════════════════════════════════════════════════
 *  INTERNAL TYPES
 * ═══════════════════════════════════════════════════════════════ */

typedef struct {
    void (*fn)(void *);
    void  *arg;
} call_t;

/* ═══════════════════════════════════════════════════════════════
 *  GLOBAL SCHEDULER STATE
 * ═══════════════════════════════════════════════════════════════ */

static fiber_t        *current   = NULL;
static fiber_t        *run_tail  = NULL;   /* circular ready queue  */
static ucontext_t      main_ctx;           /* main() context        */
static volatile int    preempt_enabled = 0;

/* ═══════════════════════════════════════════════════════════════
 *  RUN QUEUE  — O(1) circular singly-linked list
 * ═══════════════════════════════════════════════════════════════ */

static void enqueue(fiber_t *f)
{
    f->state = FIBER_READY;
    if (!run_tail) {
        f->next  = f;
        run_tail = f;
    } else {
        f->next        = run_tail->next;
        run_tail->next = f;
        run_tail       = f;
    }
}

static fiber_t *dequeue(void)
{
    if (!run_tail) return NULL;
    fiber_t *head = run_tail->next;
    if (run_tail == head) run_tail = NULL;
    else run_tail->next = head->next;
    head->next = NULL;
    return head;
}

/* ═══════════════════════════════════════════════════════════════
 *  M2 — STACK ALLOCATION  (mmap + guard page)
 * ═══════════════════════════════════════════════════════════════ */

static void *alloc_stack(void)
{
    size_t total = FIBER_GUARD_SIZE + FIBER_STACK_SIZE;
    void  *base  = mmap(NULL, total,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(base != MAP_FAILED);
    /* Guard page: any stack overflow → immediate SIGSEGV */
    mprotect(base, FIBER_GUARD_SIZE, PROT_NONE);
    return base;
}

static void free_stack(void *base)
{
    munmap(base, FIBER_GUARD_SIZE + FIBER_STACK_SIZE);
}

/* ═══════════════════════════════════════════════════════════════
 *  M1 — TRAMPOLINE  (makecontext passes only ints)
 * ═══════════════════════════════════════════════════════════════ */

static void fiber_trampoline(uint32_t hi, uint32_t lo)
{
    uintptr_t p = ((uintptr_t)hi << 32) | (uintptr_t)lo;
    call_t   *c = (call_t *)p;
    c->fn(c->arg);
    free(c);
    fiber_exit();
}

/* ═══════════════════════════════════════════════════════════════
 *  M1 — fiber_create
 * ═══════════════════════════════════════════════════════════════ */

int fiber_create(fiber_t **out, void (*fn)(void *), void *arg)
{
    fiber_t *f = calloc(1, sizeof *f);
    if (!f) return -1;

    call_t *c = malloc(sizeof *c);
    if (!c) { free(f); return -1; }
    c->fn  = fn;
    c->arg = arg;

    f->stack_base = alloc_stack();
    getcontext(&f->ctx);
    f->ctx.uc_stack.ss_sp   = (char *)f->stack_base + FIBER_GUARD_SIZE;
    f->ctx.uc_stack.ss_size = FIBER_STACK_SIZE;
    f->ctx.uc_link          = NULL;

    uintptr_t p = (uintptr_t)c;
    makecontext(&f->ctx,
                (void (*)(void))fiber_trampoline, 2,
                (uint32_t)(p >> 32),
                (uint32_t)(p & 0xFFFFFFFFu));

    enqueue(f);
    *out = f;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  M1 — SCHEDULER: yield / exit / run / join
 * ═══════════════════════════════════════════════════════════════ */

void fiber_yield(void)
{
    fiber_signal_mask();
    fiber_t *prev = current;
    enqueue(prev);
    current = dequeue();
    current->state = FIBER_RUNNING;
    fiber_signal_unmask();
    if (prev != current)
        swapcontext(&prev->ctx, &current->ctx);
}

void fiber_exit(void)
{
    fiber_signal_mask();
    current->state = FIBER_DONE;

    /* Wake any fiber waiting in fiber_join */
    if (current->join_waiter) {
        enqueue(current->join_waiter);
        current->join_waiter = NULL;
    }

    fiber_t *next = dequeue();
    if (!next) {
        printf("[fiberlib] all fibers finished.\n");
        /* save dying context pointer, swap back to main */
        ucontext_t tmp;
        fiber_t *done = current;
        current = NULL;
        swapcontext(&tmp, &main_ctx);
        (void)done;
        return;
    }
    fiber_t *dying = current;
    current        = next;
    current->state = FIBER_RUNNING;
    fiber_signal_unmask();
    swapcontext(&dying->ctx, &current->ctx);
}

void fiber_run(void)
{
    fiber_t *first = dequeue();
    if (!first) { fprintf(stderr, "[fiberlib] no fibers\n"); return; }
    current        = first;
    current->state = FIBER_RUNNING;
    swapcontext(&main_ctx, &current->ctx);
}

/* fiber_join: block current fiber until f finishes */
int fiber_join(fiber_t *f)
{
    if (f->state == FIBER_DONE) return f->exit_code;

    fiber_signal_mask();
    f->join_waiter    = current;
    current->state    = FIBER_BLOCKED;
    fiber_t *next     = dequeue();
    if (!next) { fprintf(stderr,"[fiberlib] deadlock in join\n"); exit(1); }
    fiber_t *blocker  = current;
    current           = next;
    current->state    = FIBER_RUNNING;
    fiber_signal_unmask();
    swapcontext(&blocker->ctx, &current->ctx);
    return f->exit_code;
}

/* ═══════════════════════════════════════════════════════════════
 *  M1/W4 — PROCESS INTEGRATION: fork / waitpid / exec
 * ═══════════════════════════════════════════════════════════════ */

pid_t fiber_fork(void)
{
    /* Fork without blocking the scheduler in the parent */
    pid_t pid = fork();
    return pid;  /* child gets 0, parent gets child pid */
}

int fiber_waitpid(pid_t pid)
{
    int status;
    /* Poll in a non-blocking loop, yielding between attempts */
    while (1) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)  return WEXITSTATUS(status);
        if (r == -1)   return -1;
        fiber_yield();   /* give other fibers a turn while waiting */
    }
}

int fiber_exec(const char *path, char *const argv[])
{
    /* Should be called after fiber_fork() in the child */
    execv(path, argv);
    perror("execv");
    return -1;
}

/* ═══════════════════════════════════════════════════════════════
 *  M2/W6 — SIGNALS & PRE-EMPTION  (SIGALRM timer)
 * ═══════════════════════════════════════════════════════════════ */

static sigset_t sig_mask;

void fiber_signal_mask(void)
{
    sigprocmask(SIG_BLOCK, &sig_mask, NULL);
}

void fiber_signal_unmask(void)
{
    sigprocmask(SIG_UNBLOCK, &sig_mask, NULL);
}

static void sigalrm_handler(int sig)
{
    (void)sig;
    if (!preempt_enabled || !current) return;
    /* Force a reschedule from signal context */
    fiber_t *prev = current;
    if (!run_tail) return;      /* nothing else to run */
    enqueue(prev);
    current = dequeue();
    current->state = FIBER_RUNNING;
    if (prev != current)
        swapcontext(&prev->ctx, &current->ctx);
}

void fiber_signal_init(void)
{
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGALRM);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = sigalrm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    /* Fire SIGALRM every 10 ms for pre-emptive scheduling */
    struct itimerval itv;
    itv.it_value.tv_sec     = 0;
    itv.it_value.tv_usec    = 10000;   /* 10 ms first tick  */
    itv.it_interval.tv_sec  = 0;
    itv.it_interval.tv_usec = 10000;   /* 10 ms repeat      */
    setitimer(ITIMER_REAL, &itv, NULL);

    preempt_enabled = 1;
}

/* ═══════════════════════════════════════════════════════════════
 *  M2/W7 — FIBER CHANNEL  (blocking producer/consumer)
 * ═══════════════════════════════════════════════════════════════ */

struct fiber_chan {
    void    **buf;
    int       cap;
    int       head, tail, count;
    fiber_t  *send_wait;   /* fiber blocked waiting to send */
    fiber_t  *recv_wait;   /* fiber blocked waiting to recv */
};

fiber_chan_t *fiber_chan_create(int capacity)
{
    fiber_chan_t *c = calloc(1, sizeof *c);
    c->buf = calloc(capacity, sizeof(void *));
    c->cap = capacity;
    return c;
}

void fiber_chan_send(fiber_chan_t *c, void *item)
{
    /* Block if full */
    while (c->count == c->cap) {
        fiber_signal_mask();
        c->send_wait   = current;
        current->state = FIBER_BLOCKED;
        fiber_t *next  = dequeue();
        assert(next);
        fiber_t *me    = current;
        current        = next;
        current->state = FIBER_RUNNING;
        fiber_signal_unmask();
        swapcontext(&me->ctx, &current->ctx);
    }
    c->buf[c->tail] = item;
    c->tail = (c->tail + 1) % c->cap;
    c->count++;
    /* Wake a waiting receiver */
    if (c->recv_wait) {
        enqueue(c->recv_wait);
        c->recv_wait = NULL;
    }
}

void *fiber_chan_recv(fiber_chan_t *c)
{
    /* Block if empty */
    while (c->count == 0) {
        fiber_signal_mask();
        c->recv_wait   = current;
        current->state = FIBER_BLOCKED;
        fiber_t *next  = dequeue();
        assert(next);
        fiber_t *me    = current;
        current        = next;
        current->state = FIBER_RUNNING;
        fiber_signal_unmask();
        swapcontext(&me->ctx, &current->ctx);
    }
    void *item = c->buf[c->head];
    c->head = (c->head + 1) % c->cap;
    c->count--;
    /* Wake a waiting sender */
    if (c->send_wait) {
        enqueue(c->send_wait);
        c->send_wait = NULL;
    }
    return item;
}

void fiber_chan_destroy(fiber_chan_t *c)
{
    free(c->buf);
    free(c);
}

/* ═══════════════════════════════════════════════════════════════
 *  M3/W11 — MUTEX
 * ═══════════════════════════════════════════════════════════════ */

void fiber_mutex_init(fiber_mutex_t *m)
{
    memset(m, 0, sizeof *m);
}

void fiber_mutex_lock(fiber_mutex_t *m)
{
    fiber_signal_mask();
    while (m->locked) {
        /* Park: move self to mutex wait queue */
        current->state = FIBER_BLOCKED;
        current->next  = NULL;
        if (!m->wait_tail) {
            m->wait_head = m->wait_tail = current;
        } else {
            m->wait_tail->next = current;
            m->wait_tail       = current;
        }
        fiber_t *next = dequeue();
        assert(next);
        fiber_t *me   = current;
        current       = next;
        current->state = FIBER_RUNNING;
        fiber_signal_unmask();
        swapcontext(&me->ctx, &current->ctx);
        fiber_signal_mask();
    }
    m->locked = 1;
    m->owner  = current;
    fiber_signal_unmask();
}

void fiber_mutex_unlock(fiber_mutex_t *m)
{
    fiber_signal_mask();
    assert(m->locked && m->owner == current);
    m->locked = 0;
    m->owner  = NULL;
    /* Unpark one waiter */
    if (m->wait_head) {
        fiber_t *w   = m->wait_head;
        m->wait_head = w->next;
        if (!m->wait_head) m->wait_tail = NULL;
        w->next = NULL;
        enqueue(w);
    }
    fiber_signal_unmask();
}

/* ═══════════════════════════════════════════════════════════════
 *  M3/W11 — CONDITION VARIABLE
 * ═══════════════════════════════════════════════════════════════ */

void fiber_cond_init(fiber_cond_t *cv)
{
    memset(cv, 0, sizeof *cv);
}

void fiber_cond_wait(fiber_cond_t *cv, fiber_mutex_t *m)
{
    fiber_signal_mask();
    /* Add self to condvar wait queue */
    current->state = FIBER_BLOCKED;
    current->next  = NULL;
    if (!cv->wait_tail) {
        cv->wait_head = cv->wait_tail = current;
    } else {
        cv->wait_tail->next = current;
        cv->wait_tail       = current;
    }
    /* Release mutex while waiting */
    m->locked = 0;
    m->owner  = NULL;
    if (m->wait_head) {
        fiber_t *w   = m->wait_head;
        m->wait_head = w->next;
        if (!m->wait_head) m->wait_tail = NULL;
        w->next = NULL;
        enqueue(w);
    }
    fiber_t *next = dequeue();
    assert(next);
    fiber_t *me   = current;
    current       = next;
    current->state = FIBER_RUNNING;
    fiber_signal_unmask();
    swapcontext(&me->ctx, &current->ctx);
    /* Re-acquire mutex on wake */
    fiber_mutex_lock(m);
}

void fiber_cond_signal(fiber_cond_t *cv)
{
    fiber_signal_mask();
    if (cv->wait_head) {
        fiber_t *w    = cv->wait_head;
        cv->wait_head = w->next;
        if (!cv->wait_head) cv->wait_tail = NULL;
        w->next = NULL;
        enqueue(w);
    }
    fiber_signal_unmask();
}

void fiber_cond_broadcast(fiber_cond_t *cv)
{
    fiber_signal_mask();
    while (cv->wait_head) {
        fiber_t *w    = cv->wait_head;
        cv->wait_head = w->next;
        w->next       = NULL;
        enqueue(w);
    }
    cv->wait_tail = NULL;
    fiber_signal_unmask();
}

/* ═══════════════════════════════════════════════════════════════
 *  M3/W9 — NON-BLOCKING I/O  (fiber yields while waiting for fd)
 * ═══════════════════════════════════════════════════════════════ */

ssize_t fiber_read(int fd, void *buf, size_t n)
{
    /* Set fd non-blocking */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    ssize_t r;
    while ((r = read(fd, buf, n)) == -1 && errno == EAGAIN)
        fiber_yield();   /* yield instead of blocking */

    fcntl(fd, F_SETFL, flags);   /* restore */
    return r;
}

ssize_t fiber_write(int fd, const void *buf, size_t n)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    ssize_t w;
    while ((w = write(fd, buf, n)) == -1 && errno == EAGAIN)
        fiber_yield();

    fcntl(fd, F_SETFL, flags);
    return w;
}

/* ═══════════════════════════════════════════════════════════════
 *  M3/W12 — BENCHMARK HELPER
 * ═══════════════════════════════════════════════════════════════ */

uint64_t fiber_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
