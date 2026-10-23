# Cloud Resource Allocation Simulator

A C++17 command-line **discrete-time simulation** of how tasks request CPU and RAM and how a
cloud-like environment decides where and when they run. Tasks arrive, wait when capacity is
temporarily unavailable, run once resources are reserved on a VM, and release them on completion.

The same workload can be replayed under 3 scheduling × 3 placement policies and the measured
metrics compared. Every decision is logged with a reason derived from actual simulator state.

> **Scope.** This program *models* allocation and scheduling decisions. It does not control,
> observe or reproduce the real physical scheduler inside AWS or any other cloud. It runs fully
> offline: no web server, localhost, browser or cloud credentials. Any real-cloud experiment
> (e.g. AWS Lambda) would be a separate validation, not evidence that the simulator controls AWS.

---

## Build and run

### With CMake (recommended)

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Executable location:

| Platform / generator | Path |
|---|---|
| Linux / macOS (Makefiles, Ninja) | `build/CloudResourceSimulator` |
| Windows, single-config (Ninja, MinGW Makefiles) | `build\CloudResourceSimulator.exe` |
| Windows, Visual Studio generator | `build\Debug\CloudResourceSimulator.exe` (or `build\Release\...` with `--config Release`) |

On Windows with Visual Studio, run the tests with `ctest --test-dir build -C Debug --output-on-failure`.
**Confirm the actual path on your project machine and update this table.**

### Without CMake

```bash
make          # builds build/CloudResourceSimulator and build/crs_tests
make test
```

Or directly: `g++ -std=c++17 -Iinclude src/*.cpp -o CloudResourceSimulator`.

### Running

Run from the project root so `workloads/` is found (the program also checks `../workloads`).

```bash
./build/CloudResourceSimulator                     # interactive terminal menu
./build/CloudResourceSimulator --help              # non-interactive options

# Examples
./build/CloudResourceSimulator --workload workloads/normal.txt --sched priority --place best --log
./build/CloudResourceSimulator --workload workloads/normal.txt --compare --export results
./build/CloudResourceSimulator --workload workloads/starvation.txt --sched aging --explain 1
./build/CloudResourceSimulator --random 42 --compare
```

### Optional: localhost web view

A separate program, `CloudResourceSimulatorWeb`, serves a browser UI for the **same engine**
(it links the same `crs_core` library, so results are identical to the terminal app).

```bash
make web                                   # or: ./build/CloudResourceSimulatorWeb [--port 8080]
# then open http://127.0.0.1:8080
```

It provides policy controls, an editable workload, metric tiles, a per-VM task timeline,
CPU/RAM utilization over time, a clickable task table with each task's decision autopsy,
the 9-policy comparison and a filterable event log.

* It is **optional**. The core deliverable is still the terminal program; nothing in `src/`
  depends on the web view.
* It binds to `127.0.0.1` only (not reachable from other machines), uses no third-party
  libraries, and works offline. Only the four bundled workloads can be read from disk;
  edited workloads are sent in the request body and never written to disk.
* Windows: CMake links `ws2_32` automatically. Run it from the project root so it can find `web/index.html`.

---

## Architecture and ownership

```
              ┌────────────┐  ranks waiting tasks    ┌──────────────────┐
  Workload ──►│ Simulator  │────────────────────────►│ Scheduler        │  Member 2
  (file /     │ clock,     │                         └──────────────────┘
   generator) │ lifecycle  │  "place this task"      ┌──────────────────┐
              │            │────────────────────────►│ ResourceAllocator│  Member 1
              │  Member 3  │◄──── PlacementResult ───│  owns VM state   │
              │            │                         └──────────────────┘
              │            │  build explanation      ┌──────────────────┐
              │            │────────────────────────►│ DecisionAuditor  │  Member 2
              └─────┬──────┘                         └──────────────────┘
                    │ events + utilization samples
                    ▼
              ┌────────────┐      ┌────────────┐
              │  Metrics   │◄─────│  Replay    │  fresh Simulator per policy   Member 3
              └────────────┘      └────────────┘
```

| Member | Files | Responsibility |
|---|---|---|
| 1 | `VM.h/.cpp`, `ResourceAllocator.h/.cpp` | VM capacity, placement policies, reserve/release, capacity safety |
| 2 | `Task.h/.cpp`, `Scheduler.h/.cpp`, `DecisionAuditor.h/.cpp` | Task model, scheduling order, aging, decision explanations |
| 3 | `Simulator`, `Metrics`, `Replay`, `Workload`, `SimulationEvent`, `main.cpp`, `CMakeLists.txt`, `Makefile`, `tests/`, optional `web/` | Clock, event order, metrics, replay, terminal UI, build, integration tests |

Integration rules enforced in code:

* All code is in namespace `crs`; there is one `Task` type (`Task.h`) and one VM type (`VM.h`).
* **Only `ResourceAllocator` can change VM CPU/RAM counters.** `VM::reserve` / `VM::release` are
  private and `ResourceAllocator` is the only friend, so the scheduler or simulator *cannot*
  decrement resources even by mistake: it would not compile.
* The allocator records `taskId → (VM, cpu, ram)` for each reservation. Release looks up that
  record, so resources always return to the VM that granted them. Unknown or double releases
  are refused without touching any VM.
* `src/main.cpp` is the only application entry point; tests are a separate executable.

---

## Simulation model

Time is measured in integer **ticks**. CPU is in integer units (vCPUs), RAM in MB.

### Event order at every tick `t` (identical for every policy)

1. **Completions:** running tasks with `start + duration == t` finish, and the allocator releases their resources.
2. **Arrivals:** tasks with `arrival <= t` become `WAITING`, or `REJECTED` if the request is
   invalid (non-positive CPU/RAM/duration, negative arrival) or larger than every configured VM.
3. **Scheduling:** the scheduler ranks all `WAITING` tasks.
4. **Placement:** in rank order the allocator tries to place each task (see queue mode below).
5. **Recording:** events are logged and a utilization sample for `[t, t+1)` is taken.

Because completions come first, resources freed at `t` can be used by a task in the same tick.
A task started at `t` with duration `d` occupies ticks `t … t+d-1` and completes at `t+d`.

### Task states

`PENDING` (not yet arrived) → `WAITING` → `RUNNING` → `COMPLETED`, or `PENDING` → `REJECTED`.

**Temporarily unavailable vs unschedulable.** A task that fits at least one VM's *total*
capacity is kept waiting: once enough tasks finish, it will eventually fit. A task that exceeds
every VM's total capacity (or needs CPU from one VM and RAM from another, since tasks cannot be
split) is rejected at arrival with the reason, so it never waits forever.

### Queue mode

| Mode | Behaviour |
|---|---|
| **Backfill** (default) | Every waiting task is tried in rank order; a task that doesn't fit is skipped and lower-ranked tasks may start. Better utilization, but a large task can be overtaken indefinitely. |
| **Strict** | Placement stops at the first task that doesn't fit; everything ranked below it waits (head-of-line blocking). The ranking is honoured exactly, at the cost of idle capacity. |

---

## Policy definitions

### Scheduling (Member 2): which waiting task is considered next

| Policy | Order | Tie-breaks |
|---|---|---|
| FCFS | arrival time ascending | task ID ascending |
| Priority | priority **descending** (larger number = more important) | earlier arrival, then lower task ID |
| Aging Priority | effective priority descending, with `effective = basePriority + floor(waitingTicks / K)` | earlier arrival, then lower task ID |

`K` (aging interval) defaults to 5 and can be changed in the menu, with `--aging K`, or in a
workload file. `basePriority` is never modified; effective priority is computed on demand.

### Placement (Member 1): which VM hosts the task

A VM is **feasible** only if `cpu <= freeCpu` **and** `ram <= freeRam`; both resources must fit.

| Policy | Rule | Ties |
|---|---|---|
| First-Fit | first feasible VM in configured order | n/a |
| Best-Fit | minimise residual score `(freeCpu − cpu)/totalCpu + (freeRam − ram)/totalRam` (tightest fit, CPU and RAM weighted equally after normalisation) | earlier VM in configured order |
| Load-Aware | minimise post-placement bottleneck load `max((usedCpu+cpu)/totalCpu, (usedRam+ram)/totalRam)` | lower mean of the two utilizations, then earlier VM |

Floating-point comparisons use an epsilon of 1e-9 so ties are detected reliably.

---

## Metrics (Member 3)

All values are computed from the recorded event log and per-tick utilization samples.

| Metric | Definition |
|---|---|
| Measurement interval | `[0, End)` where `End` is the tick of the last completion or rejection |
| Waiting time | `start − arrival`, averaged over **completed** tasks |
| Turnaround time | `completion − arrival`, averaged over **completed** tasks |
| Max waiting | largest wait of any non-rejected task (unstarted tasks count up to `End`) |
| Throughput | completed tasks ÷ `End` (tasks per tick) |
| CPU / RAM utilization | Σ over ticks in the interval of allocated capacity ÷ (total capacity × `End`) |
| Starved | non-rejected task whose wait exceeds the **starvation threshold** (default 20 ticks; configurable) |

Rejected tasks are excluded from waiting/turnaround averages and counted separately.

---

## Decision Autopsy

Every important decision is a `SimulationEvent {tick, taskId, type, vmId, reason}`. Reasons are
assembled from the allocator's `PlacementResult` (outcome, bottleneck, closest VM, CPU/RAM
deficit, policy score) and the scheduler's state (rank, effective priority). Examples from real runs:

```
[t=  11] T16   REJECTED   -     Rejected: task T16 needs 20 CPU but the largest VM has only 16; it can never be placed in this configuration
[t=   2] T5    WAITING    -     Waiting: no VM currently has enough free CPU (needs 3, most free on a suitable VM is 2): VM1(node1) lacks CPU (free 1); VM2(node2) lacks CPU (free 0); VM3(node3) lacks CPU (free 2). Closest is VM3, short by 1 CPU and 0 MB. ...
[t=  14] T1    ALLOCATED  VM1   Allocated to VM1 by First-Fit: ... Task considered #1 of 3 under Aging Priority (effective 5 = base 1 + floor(13/3)), after waiting 13 tick(s)
```

`WAITING` events are logged when the *kind* of wait changes (different bottleneck, different
blocker, or the task reaching the head of the queue), not on every tick.

---

## Workloads

`workloads/*.txt` format (`#` starts a comment):

```
CONFIG <key> <value>          # optional: queue_mode, aging_interval, starvation_threshold,
                              #           scheduling, placement
VM   <name> <cpu> <ram_mb>
TASK <id> <cpu> <ram_mb> <arrival> <duration> <priority>
```

| File | Purpose |
|---|---|
| `normal.txt` | 3 heterogeneous VMs, 18 mixed tasks; T16 (20 CPU) is unschedulable and rejected |
| `cpu_bottleneck.txt` | RAM plentiful, CPU scarce: waits are explained by CPU |
| `memory_bottleneck.txt` | CPU plentiful, RAM scarce: waits are explained by RAM |
| `starvation.txt` | one large low-priority task vs a stream of small high-priority tasks (Strict, K=3) |

Option 2 in the menu (or `--random SEED`) generates a workload with a self-contained LCG, so the
same seed gives the same workload on every compiler and platform.

---

## Suggested demonstration

1. **Normal**: run FCFS + First-Fit, show the task table and metrics, then explain T16's
   rejection (menu 9 → `16`).
2. **Bottlenecks**: load `cpu_bottleneck`, then `memory_bottleneck`; show the event log (menu 8)
   and point out that waits cite CPU in one and RAM in the other.
3. **Starvation**: load `starvation` (applies Strict, K=3, threshold 15), compare policies (menu 10):

   | Scheduling | AvgWait | MaxWait | Starved |
   |---|---|---|---|
   | FCFS | 3.32 | 4 | 0 |
   | Priority | **0.97** | 30 | **1** (T1) |
   | Aging Priority | 2.35 | 13 | 0 |

   Priority gives the best *average* by permanently favouring small tasks, and T1 waits 30
   ticks. Aging lifts T1 to the head after 12 ticks and it starts at 14. The average wait goes up
   because the small tasks queue behind it: that's the trade-off.
4. **Aging limitation**: set queue mode to Backfill (menu 6) and re-run Aging Priority: T1
   starves again, because small tasks keep slipping into each freed 4 CPU while T1 needs 8.
   Aging changes *order*; it cannot help without strict ordering or a reservation mechanism.
5. **Replay fairness**: export results (menu 11) and show that every combination ran on the same
   initial VMs and tasks.

---

## Tests

`tests/tests.cpp` is a standalone executable (no external framework):

| Test plan item | Test |
|---|---|
| Fits exactly | `fits exactly` |
| CPU bottleneck / Memory bottleneck | `CPU bottleneck`, `memory bottleneck` (state unchanged after failure) |
| Release | `release returns resources to the correct VM`, `release errors leave state intact` |
| Temporary shortage | `temporary shortage then allocation` |
| Unschedulable request | `never-fits detection`, `unschedulable request rejected` |
| Priority tie | `FCFS order and tie-break`, `priority tie-break` |
| Aging | `aging formula` (base priority not mutated), `starvation vs aging` |
| Replay fairness | `replay fairness and isolation` |
| Reproducibility | `reproducibility` (identical event logs across runs) |
| Metrics | `hand-calculated metrics` (worked example below) |
| Capacity safety | `capacity never exceeded`: all 4 workloads × 9 policies × 2 queue modes |
| Placement policies | `First-Fit / Best-Fit / Load-Aware choices`, `Best-Fit uses CPU and RAM`, `Load-Aware tie-break` |

Hand-calculated metrics example: one VM (4 CPU, 4096 MB); T1 = 2 CPU/1024 MB, arrives 0, runs 4;
T2 = 4 CPU/1024 MB, arrives 1, runs 2. T1 runs [0,4). T2 waits until 4, runs [4,6). Waiting
(0+3)/2 = 1.5, turnaround (4+5)/2 = 4.5, throughput 2/6, CPU utilization (4·2 + 2·4)/(4·6) = 66.7%,
RAM (6·1024)/(4096·6) = 25%.

---

## Limitations

* Discrete integer ticks; no network, disk, I/O contention, migration or preemption.
* Each task runs on exactly one VM; demands are fixed and known in advance.
* VMs are static (no autoscaling or failures).
* Aging does not fix starvation under backfilling (see demonstration step 4).
* Placement policy differences appear only when there are several feasible VMs. In
  `starvation.txt` (one VM) all placement rows are identical by construction.
* Results are from synthetic workloads; they show relative policy behaviour, not real cloud performance.
