// DecisionAuditor.h — turns allocator/scheduler state into "Decision Autopsy" text.
// Owner: Member 2
//
// Every explanation is assembled from the PlacementResult and Scheduler state
// passed in; nothing here is hard-coded per scenario.
#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "ResourceAllocator.h"
#include "Scheduler.h"
#include "SimulationEvent.h"
#include "Task.h"

namespace crs {

class DecisionAuditor {
public:
    // "considered #2 of 5 under Aging Priority (effective 4 = base 2 + floor(7/3))"
    static std::string schedulingContext(const Task& task, const Scheduler& scheduler, int now,
                                         int rank, int queueSize);

    static std::string explainAllocation(const Task& task, const PlacementResult& result,
                                         PlacementPolicy placement, const Scheduler& scheduler,
                                         int now, int rank, int queueSize);

    static std::string explainWaiting(const Task& task, const PlacementResult& result,
                                      const Scheduler& scheduler, int now, int rank, int queueSize);

    // Strict queue: task was not tried because a higher-ranked task could not be placed.
    static std::string explainBlocked(const Task& task, const Task& blocker, bool wouldFitNow,
                                      const Scheduler& scheduler, int now, int rank, int queueSize);

    static std::string explainRejection(const Task& task, const std::string& cause);

    // One formatted line per event: "[t=  4] T3   ALLOCATED  VM2   reason".
    static std::string formatEvent(const SimulationEvent& e);
    static void printLog(std::ostream& os, const std::vector<SimulationEvent>& events);

    static std::vector<SimulationEvent> historyOf(const std::vector<SimulationEvent>& events, int taskId);
};

}  // namespace crs
