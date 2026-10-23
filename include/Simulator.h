// Simulator.h — owns the clock and coordinates the task lifecycle.
// Owner: Member 3
//
// Event order at every tick t (identical for every policy):
//   1. Completions: tasks with start + duration == t finish; allocator releases resources.
//   2. Arrivals:    tasks with arrival <= t become WAITING, or REJECTED if the request is
//                   invalid or can never fit any configured VM.
//   3. Scheduling:  the scheduler ranks all WAITING tasks.
//   4. Placement:   in rank order, the allocator tries to place each task
//                   (Strict mode stops at the first task that does not fit).
//   5. Recording:   events are logged; a utilization sample for [t, t+1) is taken.
// A task started at t with duration d therefore occupies ticks t .. t+d-1 and
// completes (releasing resources) at the start of tick t+d.
#pragma once

#include <string>
#include <vector>

#include "Metrics.h"
#include "ResourceAllocator.h"
#include "Scheduler.h"
#include "SimulationEvent.h"
#include "Task.h"
#include "Workload.h"

namespace crs {

struct SimulationConfig {
    SchedulingPolicy scheduling = SchedulingPolicy::FCFS;
    PlacementPolicy placement = PlacementPolicy::FirstFit;
    QueueMode queueMode = QueueMode::Backfill;
    int agingInterval = 5;          // K in basePriority + floor(wait / K)
    int starvationThreshold = 20;   // ticks of waiting before a task counts as starved
    int maxTicks = 100000;          // safety limit; tasks still active are reported unfinished

    std::string label() const;      // e.g. "Aging Priority + Best-Fit [Strict]"
};

struct SimulationResult {
    SimulationConfig config;
    std::string workloadName;
    std::vector<VMSpec> vms;
    std::vector<Task> tasks;                    // final task states
    std::vector<SimulationEvent> events;        // structured event log
    std::vector<UtilizationSample> utilization; // one sample per simulated tick
    Metrics metrics;
    bool hitTickLimit = false;
};

class Simulator {
public:
    // Takes copies: the caller's workload is never modified.
    Simulator(Workload workload, SimulationConfig config);

    SimulationResult run();

private:
    Workload workload_;
    SimulationConfig config_;
};

// Apply CONFIG lines from a workload file on top of an existing config.
void applyWorkloadConfig(const Workload& w, SimulationConfig& config);

}  // namespace crs
