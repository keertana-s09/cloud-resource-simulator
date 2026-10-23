// Metrics.h — measured results computed only from the recorded event log and
// per-tick utilization samples.
// Owner: Member 3
//
// Definitions (all times in ticks):
//   Measurement interval  [0, endTick) where endTick is the tick of the last
//                         completion/rejection (or the tick limit if hit).
//   Waiting time          start - arrival, for COMPLETED tasks only.
//   Turnaround time       completion - arrival, for COMPLETED tasks only.
//   Throughput            completed / endTick   (tasks per tick).
//   CPU / RAM utilization sum over ticks in the interval of allocated capacity,
//                         divided by (total capacity * endTick).
//   Starved               a non-rejected task whose wait (start - arrival, or
//                         endTick - arrival if it never started) exceeds
//                         starvationThreshold.
#pragma once

#include <string>
#include <vector>

#include "SimulationEvent.h"

namespace crs {

struct UtilizationSample {
    int tick = 0;     // resources held during [tick, tick + 1)
    int usedCpu = 0;
    int usedRam = 0;
};

struct Metrics {
    int totalTasks = 0;
    int completed = 0;
    int rejected = 0;
    int unfinished = 0;
    int endTick = 0;
    double avgWaiting = 0.0;
    double avgTurnaround = 0.0;
    int maxWaiting = 0;
    double throughput = 0.0;
    double cpuUtilization = 0.0;
    double ramUtilization = 0.0;
    int starvationThreshold = 0;
    std::vector<int> starvedTasks;
};

Metrics computeMetrics(const std::vector<SimulationEvent>& events,
                       const std::vector<UtilizationSample>& samples, int totalCpu, int totalRam,
                       int totalTasks, int endTick, int starvationThreshold);

}  // namespace crs
