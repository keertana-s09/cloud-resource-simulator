// tests.cpp — separate test executable (its own main), no external framework.
// Each test maps to an item in the team plan's Test Plan.
#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "DecisionAuditor.h"
#include "Metrics.h"
#include "Replay.h"
#include "ResourceAllocator.h"
#include "Scheduler.h"
#include "Simulator.h"
#include "Workload.h"

using namespace crs;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) {                                                                \
            ++g_failures;                                                             \
            std::cerr << "    FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        }                                                                             \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        ++g_checks;                                                                        \
        auto va = (a);                                                                     \
        auto vb = (b);                                                                     \
        if (!(va == vb)) {                                                                 \
            ++g_failures;                                                                  \
            std::cerr << "    FAIL " << __FILE__ << ":" << __LINE__ << "  " #a " == " #b \
                      << "  (" << va << " vs " << vb << ")\n";                             \
        }                                                                                  \
    } while (0)

#define CHECK_NEAR(a, b) CHECK(std::fabs((a) - (b)) < 1e-9)

bool contains(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }

Workload makeWorkload(const std::string& text) {
    std::istringstream in(text);
    return parseWorkload(in, "test");
}

const Task& taskById(const SimulationResult& r, int id) {
    for (const Task& t : r.tasks) {
        if (t.id == id) return t;
    }
    throw std::runtime_error("no task " + std::to_string(id));
}

std::vector<VM> twoVms() { return {VM(1, "VM1", 4, 4096), VM(2, "VM2", 8, 8192)}; }

// ------------------------------------------------------------------ Member 1: allocator

void testFitsExactly() {
    ResourceAllocator a({VM(1, "VM1", 4, 4096)});
    PlacementResult r = a.allocate(Task(1, 4, 4096, 0, 1, 0), PlacementPolicy::FirstFit);
    CHECK(r.success);
    CHECK_EQ(r.vmId, 1);
    CHECK_EQ(a.vms()[0].freeCpu(), 0);
    CHECK_EQ(a.vms()[0].freeRam(), 0);
    PlacementResult r2 = a.allocate(Task(2, 1, 1, 0, 1, 0), PlacementPolicy::FirstFit);
    CHECK(!r2.success);
    CHECK_EQ(a.vms()[0].usedCpu(), 4);  // never exceeds capacity
}

void testCpuBottleneck() {
    ResourceAllocator a({VM(1, "VM1", 4, 8192)});
    CHECK(a.allocate(Task(1, 3, 1024, 0, 1, 0), PlacementPolicy::FirstFit).success);
    PlacementResult r = a.allocate(Task(2, 2, 1024, 0, 1, 0), PlacementPolicy::FirstFit);
    CHECK(!r.success);
    CHECK(r.outcome == PlacementOutcome::NoCapacityNow);
    CHECK(r.bottleneck == Bottleneck::Cpu);
    CHECK_EQ(r.cpuDeficit, 1);
    CHECK_EQ(r.ramDeficit, 0);
    CHECK(contains(r.reason, "free CPU"));
    CHECK_EQ(a.vms()[0].usedCpu(), 3);  // failed attempt changed nothing
    CHECK_EQ(a.vms()[0].usedRam(), 1024);
}

void testMemoryBottleneck() {
    ResourceAllocator a({VM(1, "VM1", 16, 4096)});
    CHECK(a.allocate(Task(1, 1, 3072, 0, 1, 0), PlacementPolicy::BestFit).success);
    PlacementResult r = a.allocate(Task(2, 1, 2048, 0, 1, 0), PlacementPolicy::BestFit);
    CHECK(!r.success);
    CHECK(r.bottleneck == Bottleneck::Ram);
    CHECK_EQ(r.cpuDeficit, 0);
    CHECK_EQ(r.ramDeficit, 1024);
    CHECK(contains(r.reason, "free RAM"));
    CHECK_EQ(a.vms()[0].usedRam(), 3072);
}

void testRelease() {
    ResourceAllocator a(twoVms());
    PlacementResult r = a.allocate(Task(7, 6, 6000, 0, 1, 0), PlacementPolicy::FirstFit);
    CHECK(r.success);
    CHECK_EQ(r.vmId, 2);  // VM1 too small
    ReleaseResult rel = a.release(7);
    CHECK(rel.success);
    CHECK_EQ(rel.vmId, 2);
    CHECK_EQ(a.vms()[1].freeCpu(), 8);
    CHECK_EQ(a.vms()[1].freeRam(), 8192);
    CHECK_EQ(a.vms()[0].freeCpu(), 4);  // other VM untouched
}

void testReleaseErrors() {
    ResourceAllocator a(twoVms());
    CHECK(!a.release(99).success);  // unknown task
    CHECK(a.allocate(Task(1, 1, 100, 0, 1, 0), PlacementPolicy::FirstFit).success);
    CHECK(a.release(1).success);
    ReleaseResult again = a.release(1);  // double release
    CHECK(!again.success);
    CHECK_EQ(a.vms()[0].usedCpu(), 0);
    CHECK_EQ(a.vms()[0].usedRam(), 0);
}

void testInvalidRequests() {
    ResourceAllocator a(twoVms());
    PlacementResult r = a.allocate(Task(1, 0, 100, 0, 1, 0), PlacementPolicy::FirstFit);
    CHECK(r.outcome == PlacementOutcome::InvalidRequest);
    r = a.allocate(Task(2, 1, -5, 0, 1, 0), PlacementPolicy::FirstFit);
    CHECK(r.outcome == PlacementOutcome::InvalidRequest);
    CHECK(a.allocate(Task(3, 1, 100, 0, 1, 0), PlacementPolicy::FirstFit).success);
    r = a.allocate(Task(3, 1, 100, 0, 1, 0), PlacementPolicy::FirstFit);  // same task twice
    CHECK(r.outcome == PlacementOutcome::InvalidRequest);
    CHECK_EQ(a.usedCpu(), 1);
}

void testNeverFits() {
    ResourceAllocator a(twoVms());
    PlacementResult r = a.allocate(Task(1, 16, 1024, 0, 1, 0), PlacementPolicy::FirstFit);
    CHECK(r.outcome == PlacementOutcome::NeverFits);
    CHECK(r.bottleneck == Bottleneck::Cpu);
    CHECK_EQ(r.cpuDeficit, 8);
    // CPU fits VM2, RAM fits VM2 too -> check the "no single VM" case with a different config
    ResourceAllocator b({VM(1, "Big-CPU", 16, 2048), VM(2, "Big-RAM", 2, 16384)});
    PlacementResult m = b.allocate(Task(2, 8, 8192, 0, 1, 0), PlacementPolicy::FirstFit);
    CHECK(m.outcome == PlacementOutcome::NeverFits);
    CHECK(m.bottleneck == Bottleneck::Mixed);
    CHECK(contains(m.reason, "no single VM"));
}

void testPlacementPolicies() {
    // VM1 4/4096, VM2 8/8192, VM3 2/2048 ; task 2 CPU / 2048 MB fits all
    auto vms = [] { return std::vector<VM>{VM(1, "VM1", 4, 4096), VM(2, "VM2", 8, 8192), VM(3, "VM3", 2, 2048)}; };
    Task t(1, 2, 2048, 0, 1, 0);

    ResourceAllocator ff(vms());
    CHECK_EQ(ff.allocate(t, PlacementPolicy::FirstFit).vmId, 1);

    ResourceAllocator bf(vms());  // VM3 is an exact fit: residual 0
    PlacementResult b = bf.allocate(t, PlacementPolicy::BestFit);
    CHECK_EQ(b.vmId, 3);
    CHECK_NEAR(b.score, 0.0);

    ResourceAllocator la(vms());  // VM2 ends at max(25%,25%) = 25%, lowest
    PlacementResult l = la.allocate(t, PlacementPolicy::LoadAware);
    CHECK_EQ(l.vmId, 2);
    CHECK_NEAR(l.score, 0.25);
}

void testBestFitConsidersBothResources() {
    // VM1 leaves lots of RAM but little CPU; VM2 leaves moderate amounts of both.
    // CPU-only Best-Fit would choose VM1; the combined score must choose VM2.
    ResourceAllocator a({VM(1, "VM1", 4, 16384), VM(2, "VM2", 8, 4096)});
    PlacementResult r = a.allocate(Task(1, 3, 2048, 0, 1, 0), PlacementPolicy::BestFit);
    // VM1: 1/4 + 14336/16384 = 0.25 + 0.875 = 1.125 ; VM2: 5/8 + 2048/4096 = 1.125 -> tie -> VM1
    CHECK_EQ(r.vmId, 1);
    ResourceAllocator b({VM(1, "VM1", 4, 16384), VM(2, "VM2", 8, 4096)});
    PlacementResult r2 = b.allocate(Task(2, 3, 3072, 0, 1, 0), PlacementPolicy::BestFit);
    // VM1: 0.25 + 13312/16384 = 1.0625 ; VM2: 0.625 + 1024/4096 = 0.875 -> VM2
    CHECK_EQ(r2.vmId, 2);
}

void testLoadAwareTieBreak() {
    ResourceAllocator a({VM(1, "VM1", 4, 4096), VM(2, "VM2", 4, 4096)});
    CHECK_EQ(a.allocate(Task(1, 1, 1024, 0, 1, 0), PlacementPolicy::LoadAware).vmId, 1);  // tie -> first
    CHECK_EQ(a.allocate(Task(2, 1, 1024, 0, 1, 0), PlacementPolicy::LoadAware).vmId, 2);  // VM1 now busier
}

// ------------------------------------------------------------------ Member 2: scheduler

void testFcfsOrder() {
    Scheduler s(SchedulingPolicy::FCFS);
    Task a(3, 1, 1, 5, 1, 9), b(1, 1, 1, 5, 1, 0), c(2, 1, 1, 2, 1, 0);
    auto o = s.order({&a, &b, &c}, 10);
    CHECK_EQ(o[0]->id, 2);
    CHECK_EQ(o[1]->id, 1);  // same arrival as T3, lower id first
    CHECK_EQ(o[2]->id, 3);
}

void testPriorityTie() {
    Scheduler s(SchedulingPolicy::Priority);
    Task a(5, 1, 1, 3, 1, 4), b(2, 1, 1, 3, 1, 4), c(1, 1, 1, 4, 1, 4), d(9, 1, 1, 0, 1, 7);
    auto o = s.order({&a, &b, &c, &d}, 10);
    CHECK_EQ(o[0]->id, 9);  // highest priority
    CHECK_EQ(o[1]->id, 2);  // tie: arrival 3, id 2
    CHECK_EQ(o[2]->id, 5);  // tie: arrival 3, id 5
    CHECK_EQ(o[3]->id, 1);  // tie: arrival 4
}

void testAging() {
    Scheduler s(SchedulingPolicy::AgingPriority, 3);
    Task t(1, 1, 1, 10, 1, 2);
    t.status = TaskStatus::Waiting;
    CHECK_EQ(s.effectivePriority(t, 10), 2);
    CHECK_EQ(s.effectivePriority(t, 12), 2);
    CHECK_EQ(s.effectivePriority(t, 13), 3);
    CHECK_EQ(s.effectivePriority(t, 19), 5);
    CHECK_EQ(t.basePriority, 2);  // never mutated

    Task low(1, 1, 1, 0, 1, 1), high(2, 1, 1, 9, 1, 3);
    auto o = s.order({&high, &low}, 9);  // low: 1 + 9/3 = 4 > 3
    CHECK_EQ(o[0]->id, 1);
    Scheduler plain(SchedulingPolicy::Priority);
    CHECK_EQ(plain.order({&high, &low}, 9)[0]->id, 2);
}

// ------------------------------------------------------------------ Member 3: simulator

void testTemporaryShortageThenAllocated() {
    Workload w = makeWorkload(
        "VM A 4 4096\n"
        "TASK 1 4 1024 0 3 0\n"
        "TASK 2 2 1024 1 2 0\n");
    SimulationResult r = Simulator(w, SimulationConfig{}).run();
    const Task& t2 = taskById(r, 2);
    CHECK(t2.status == TaskStatus::Completed);
    CHECK_EQ(t2.startTime, 3);  // released at t=3 by T1, then placed in the same tick
    bool sawWaiting = false;
    for (const auto& e : DecisionAuditor::historyOf(r.events, 2)) {
        if (e.type == EventType::Waiting) {
            sawWaiting = true;
            CHECK(contains(e.reason, "free CPU"));
        }
    }
    CHECK(sawWaiting);
}

void testUnschedulableRejected() {
    Workload w = makeWorkload(
        "VM A 4 4096\n"
        "TASK 1 2 8192 0 3 0\n"
        "TASK 2 0 512 0 3 0\n"
        "TASK 3 1 512 0 2 0\n");
    SimulationResult r = Simulator(w, SimulationConfig{}).run();
    CHECK(taskById(r, 1).status == TaskStatus::Rejected);
    CHECK(contains(taskById(r, 1).reason, "never be placed"));
    CHECK(taskById(r, 2).status == TaskStatus::Rejected);
    CHECK(contains(taskById(r, 2).reason, "invalid"));
    CHECK(taskById(r, 3).status == TaskStatus::Completed);
    CHECK(!r.hitTickLimit);
    CHECK_EQ(r.metrics.rejected, 2);
}

void testHandCalculatedMetrics() {
    // T1 runs [0,4); T2 arrives at 1, needs all CPU, waits until 4, runs [4,6).
    Workload w = makeWorkload(
        "VM A 4 4096\n"
        "TASK 1 2 1024 0 4 0\n"
        "TASK 2 4 1024 1 2 0\n");
    SimulationResult r = Simulator(w, SimulationConfig{}).run();
    const Metrics& m = r.metrics;
    CHECK_EQ(m.completed, 2);
    CHECK_EQ(m.endTick, 6);
    CHECK_NEAR(m.avgWaiting, 1.5);      // (0 + 3) / 2
    CHECK_NEAR(m.avgTurnaround, 4.5);   // (4 + 5) / 2
    CHECK_NEAR(m.throughput, 2.0 / 6.0);
    CHECK_NEAR(m.cpuUtilization, 16.0 / 24.0);   // 4 ticks x 2 + 2 ticks x 4
    CHECK_NEAR(m.ramUtilization, 6144.0 / 24576.0);
    CHECK_EQ(m.maxWaiting, 3);
}

void testStarvationAndAging() {
    Workload w = loadWorkload("workloads/starvation.txt");
    SimulationConfig cfg;
    applyWorkloadConfig(w, cfg);
    CHECK(cfg.queueMode == QueueMode::Strict);

    cfg.scheduling = SchedulingPolicy::Priority;
    SimulationResult prio = Simulator(w, cfg).run();
    cfg.scheduling = SchedulingPolicy::AgingPriority;
    SimulationResult aging = Simulator(w, cfg).run();

    int bigWaitPrio = taskById(prio, 1).startTime - taskById(prio, 1).arrival;
    int bigWaitAging = taskById(aging, 1).startTime - taskById(aging, 1).arrival;
    CHECK(bigWaitAging < bigWaitPrio);
    CHECK(!prio.metrics.starvedTasks.empty());
    CHECK(aging.metrics.starvedTasks.empty());
}

void testReplayFairnessAndIsolation() {
    Workload w = loadWorkload("workloads/normal.txt");
    Workload original = w;
    SimulationConfig base;
    auto results = replay(w, base, allPolicyCombos());
    CHECK_EQ(results.size(), static_cast<std::size_t>(9));

    // Caller's workload untouched.
    CHECK_EQ(w.tasks.size(), original.tasks.size());
    for (std::size_t i = 0; i < w.tasks.size(); ++i) CHECK(w.tasks[i].status == TaskStatus::Pending);

    // Every run sees identical VMs and tasks.
    for (const auto& r : results) {
        CHECK_EQ(r.vms.size(), original.vms.size());
        CHECK_EQ(r.metrics.totalTasks, static_cast<int>(original.tasks.size()));
        CHECK_EQ(r.events.front().tick, 0);
    }

    // Running a policy alone gives the same result as running it inside the replay.
    SimulationConfig c = base;
    c.scheduling = SchedulingPolicy::AgingPriority;
    c.placement = PlacementPolicy::LoadAware;
    SimulationResult alone = Simulator(w, c).run();
    const SimulationResult& inReplay = results.back();
    CHECK_EQ(alone.events.size(), inReplay.events.size());
    CHECK_NEAR(alone.metrics.avgWaiting, inReplay.metrics.avgWaiting);
}

void testReproducibility() {
    Workload w = generateWorkload(7, 3, 30);
    Workload w2 = generateWorkload(7, 3, 30);
    CHECK_EQ(w.tasks.size(), w2.tasks.size());
    for (std::size_t i = 0; i < w.tasks.size(); ++i) {
        CHECK_EQ(w.tasks[i].cpu, w2.tasks[i].cpu);
        CHECK_EQ(w.tasks[i].arrival, w2.tasks[i].arrival);
    }
    SimulationConfig cfg;
    cfg.scheduling = SchedulingPolicy::Priority;
    cfg.placement = PlacementPolicy::BestFit;
    SimulationResult a = Simulator(w, cfg).run();
    SimulationResult b = Simulator(w, cfg).run();
    CHECK_EQ(a.events.size(), b.events.size());
    bool same = true;
    for (std::size_t i = 0; i < a.events.size() && i < b.events.size(); ++i) {
        same = same && a.events[i].tick == b.events[i].tick && a.events[i].taskId == b.events[i].taskId &&
               a.events[i].type == b.events[i].type && a.events[i].vmId == b.events[i].vmId &&
               a.events[i].reason == b.events[i].reason;
    }
    CHECK(same);
}

void testCapacityNeverExceededAcrossWorkloads() {
    for (const char* name : {"normal", "cpu_bottleneck", "memory_bottleneck", "starvation"}) {
        Workload w = loadWorkload(std::string("workloads/") + name + ".txt");
        int totalCpu = 0, totalRam = 0;
        for (const auto& v : w.vms) {
            totalCpu += v.cpu;
            totalRam += v.ram;
        }
        for (const auto& combo : allPolicyCombos()) {
            for (QueueMode q : {QueueMode::Backfill, QueueMode::Strict}) {
                SimulationConfig cfg;
                cfg.scheduling = combo.scheduling;
                cfg.placement = combo.placement;
                cfg.queueMode = q;
                SimulationResult r = Simulator(w, cfg).run();
                CHECK(!r.hitTickLimit);
                CHECK_EQ(r.metrics.unfinished, 0);
                bool ok = true;
                for (const auto& s : r.utilization) ok = ok && s.usedCpu <= totalCpu && s.usedRam <= totalRam;
                CHECK(ok);
                for (const auto& e : r.events) CHECK(e.type != EventType::Error);
            }
        }
    }
}

void testStrictModeBlocks() {
    // T1 (big) cannot fit while T0 runs; T2 (small) could, but strict FCFS keeps it behind T1.
    Workload w = makeWorkload(
        "VM A 4 4096\n"
        "TASK 10 2 1024 0 5 0\n"
        "TASK 11 4 1024 1 2 0\n"
        "TASK 12 1 512 2 1 0\n");
    SimulationConfig cfg;
    cfg.queueMode = QueueMode::Backfill;
    SimulationResult bf = Simulator(w, cfg).run();
    CHECK_EQ(taskById(bf, 12).startTime, 2);
    cfg.queueMode = QueueMode::Strict;
    SimulationResult st = Simulator(w, cfg).run();
    CHECK_EQ(taskById(st, 11).startTime, 5);
    CHECK_EQ(taskById(st, 12).startTime, 7);  // waits for T11 even though 2 CPU were free at t=2
    bool blockedMsg = false;
    for (const auto& e : DecisionAuditor::historyOf(st.events, 12)) {
        blockedMsg = blockedMsg || contains(e.reason, "blocked behind higher-ranked T11");
    }
    CHECK(blockedMsg);
}

void testParserErrors() {
    bool threw = false;
    try {
        makeWorkload("VM A 4\n");
    } catch (const std::runtime_error& e) {
        threw = contains(e.what(), "test:1");
    }
    CHECK(threw);
    threw = false;
    try {
        makeWorkload("VM A 4 4096\nTASK 1 1 1 0 1 0\nTASK 1 1 1 0 1 0\n");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"fits exactly", testFitsExactly},
        {"CPU bottleneck", testCpuBottleneck},
        {"memory bottleneck", testMemoryBottleneck},
        {"release returns resources to the correct VM", testRelease},
        {"release errors leave state intact", testReleaseErrors},
        {"invalid requests", testInvalidRequests},
        {"never-fits detection", testNeverFits},
        {"First-Fit / Best-Fit / Load-Aware choices", testPlacementPolicies},
        {"Best-Fit uses CPU and RAM", testBestFitConsidersBothResources},
        {"Load-Aware tie-break", testLoadAwareTieBreak},
        {"FCFS order and tie-break", testFcfsOrder},
        {"priority tie-break", testPriorityTie},
        {"aging formula", testAging},
        {"temporary shortage then allocation", testTemporaryShortageThenAllocated},
        {"unschedulable request rejected", testUnschedulableRejected},
        {"hand-calculated metrics", testHandCalculatedMetrics},
        {"starvation vs aging", testStarvationAndAging},
        {"replay fairness and isolation", testReplayFairnessAndIsolation},
        {"reproducibility", testReproducibility},
        {"capacity never exceeded (all workloads x policies)", testCapacityNeverExceededAcrossWorkloads},
        {"strict queue head-of-line blocking", testStrictModeBlocks},
        {"workload parser errors", testParserErrors},
    };
    for (const auto& t : tests) {
        int before = g_failures;
        try {
            t.second();
        } catch (const std::exception& e) {
            ++g_failures;
            std::cerr << "    EXCEPTION " << e.what() << "\n";
        }
        std::cout << (g_failures == before ? "[PASS] " : "[FAIL] ") << t.first << "\n";
    }
    std::cout << "\n" << g_checks << " checks, " << g_failures << " failure(s)\n";
    return g_failures == 0 ? 0 : 1;
}
