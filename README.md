# FiberLib — User-Level Thread Library
### OS Capstone Project | Use Case #09

---

## Team Members

| # | Name | Roll No | Module |
|---|------|---------|--------|
| 1 | Nikhil | 2500030932 | M1 (W1–W4) — Context Switch, fiber_create, fork/waitpid |
| 2 | Sai Keerthan | 2500031479 | M2 (W5–W8) — Stack Alloc, Signals, Channels |
| 3 | Simhadhar | 2500031371 | M3 (W9–W12) — Scheduler, Mutex, Condvar, Benchmark |

---

## Module Progress

| Module | Weeks | Topics | Status |
|--------|-------|--------|--------|
| M1 | W1–W4 | fiber_create, swapcontext, yield, exit, join, fork, waitpid | ✅ Done |
| M2 | W5–W8 | mmap stack, guard page, SIGALRM pre-emption, fiber channel | ✅ Done |
| M3 | W9–W12 | Non-blocking I/O, mutex, condvar, benchmark | ✅ Done |

---

## Project Structure

```
fiberlib/
├── include/
│   └── fiber.h           # Full API + FCB + mutex + condvar + channel
├── src/
│   └── fiber.c           # Complete M1 + M2 + M3 implementation
├── tests/
│   ├── test_basic.c      # Quick IR-1 demo
│   ├── test_m1.c         # M1: round-robin + fork/waitpid
│   ├── test_m2.c         # M2: pre-emption + channels + stack info
│   └── test_m3.c         # M3: I/O + mutex + condvar + benchmark
├── Makefile
└── README.md
```

---

## Build & Run (Ubuntu / WSL2)

```bash
# Install tools (once)
sudo apt update && sudo apt install gcc make -y

# Copy from Windows (if using WSL)
cp -r "/mnt/c/Operating Systems_T4-Hackathon Project" ~/fiberlib
cd ~/fiberlib

# Build everything
make

# Run each module
make run       # quick demo
make run_m1    # Module 1 test
make run_m2    # Module 2 test
make run_m3    # Module 3 test
```

---

## Individual Contributions

| Module | Owner | Roll No | What they implemented |
|--------|-------|---------|----------------------|
| M1 | Nikhil | 2500030932 | fiber_create, trampoline, yield, exit, join, fiber_fork, fiber_waitpid |
| M2 | Sai Keerthan | 2500031479 | alloc_stack (mmap+guard), fiber_signal_init (SIGALRM), fiber_chan |
| M3 | Simhadhar | 2500031371 | fiber_mutex, fiber_cond, fiber_read/write (non-blocking), benchmark |
