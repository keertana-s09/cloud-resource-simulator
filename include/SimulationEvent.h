// SimulationEvent.h — one entry in the structured event log.
// Shared contract between Simulator (writer), DecisionAuditor and Metrics (readers).
#pragma once

#include <string>

namespace crs {

enum class EventType { Arrived, Waiting, Allocated, Completed, Rejected, Error };

std::string toString(EventType type);

struct SimulationEvent {
    int tick = 0;
    int taskId = -1;
    EventType type = EventType::Arrived;
    int vmId = -1;          // VM involved, -1 if none
    std::string reason;     // human-readable explanation derived from actual state
};

}  // namespace crs
