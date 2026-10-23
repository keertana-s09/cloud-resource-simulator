#include "SimulationEvent.h"

namespace crs {

std::string toString(EventType type) {
    switch (type) {
        case EventType::Arrived:   return "ARRIVED";
        case EventType::Waiting:   return "WAITING";
        case EventType::Allocated: return "ALLOCATED";
        case EventType::Completed: return "COMPLETED";
        case EventType::Rejected:  return "REJECTED";
        case EventType::Error:     return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace crs
