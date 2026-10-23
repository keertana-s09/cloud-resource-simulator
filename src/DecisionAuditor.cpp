#include "DecisionAuditor.h"

#include <iomanip>
#include <sstream>

namespace crs {

std::string DecisionAuditor::schedulingContext(const Task& task, const Scheduler& scheduler, int now,
                                               int rank, int queueSize) {
    std::ostringstream os;
    os << "considered #" << rank << " of " << queueSize << " under " << toString(scheduler.policy());
    switch (scheduler.policy()) {
        case SchedulingPolicy::FCFS:
            os << " (arrived t=" << task.arrival << ")";
            break;
        case SchedulingPolicy::Priority:
            os << " (priority " << task.basePriority << ", arrived t=" << task.arrival << ")";
            break;
        case SchedulingPolicy::AgingPriority: {
            int waited = task.waitingTicks(now);
            os << " (effective " << scheduler.effectivePriority(task, now) << " = base "
               << task.basePriority << " + floor(" << waited << "/" << scheduler.agingInterval() << "))";
            break;
        }
    }
    return os.str();
}

std::string DecisionAuditor::explainAllocation(const Task& task, const PlacementResult& result,
                                               PlacementPolicy placement, const Scheduler& scheduler,
                                               int now, int rank, int queueSize) {
    std::ostringstream os;
    os << "Allocated to VM" << result.vmId << " by " << toString(placement) << ": " << result.reason
       << ". Task " << schedulingContext(task, scheduler, now, rank, queueSize);
    if (task.waitingTicks(now) > 0) os << ", after waiting " << task.waitingTicks(now) << " tick(s)";
    return os.str();
}

std::string DecisionAuditor::explainWaiting(const Task& task, const PlacementResult& result,
                                            const Scheduler& scheduler, int now, int rank, int queueSize) {
    std::ostringstream os;
    os << "Waiting: " << result.reason;
    if (result.closestVmId >= 0) {
        os << ". Closest is VM" << result.closestVmId << ", short by " << result.cpuDeficit << " CPU and "
           << result.ramDeficit << " MB";
    }
    os << ". Task " << schedulingContext(task, scheduler, now, rank, queueSize);
    return os.str();
}

std::string DecisionAuditor::explainBlocked(const Task& task, const Task& blocker, bool wouldFitNow,
                                            const Scheduler& scheduler, int now, int rank, int queueSize) {
    std::ostringstream os;
    os << "Waiting: strict queue, blocked behind higher-ranked T" << blocker.id
       << " which cannot be placed yet";
    if (wouldFitNow) os << " (free capacity exists for this task, so this wait is caused by ordering only)";
    os << ". Task " << schedulingContext(task, scheduler, now, rank, queueSize);
    return os.str();
}

std::string DecisionAuditor::explainRejection(const Task& task, const std::string& cause) {
    return "Rejected: task T" + std::to_string(task.id) + " " + cause;
}

std::string DecisionAuditor::formatEvent(const SimulationEvent& e) {
    std::ostringstream os;
    os << "[t=" << std::setw(4) << e.tick << "] T" << std::left << std::setw(4) << e.taskId << " "
       << std::setw(10) << toString(e.type) << " " << std::setw(5)
       << (e.vmId >= 0 ? "VM" + std::to_string(e.vmId) : "-") << std::right << " " << e.reason;
    return os.str();
}

void DecisionAuditor::printLog(std::ostream& os, const std::vector<SimulationEvent>& events) {
    for (const SimulationEvent& e : events) os << formatEvent(e) << "\n";
}

std::vector<SimulationEvent> DecisionAuditor::historyOf(const std::vector<SimulationEvent>& events,
                                                        int taskId) {
    std::vector<SimulationEvent> out;
    for (const SimulationEvent& e : events) {
        if (e.taskId == taskId) out.push_back(e);
    }
    return out;
}

}  // namespace crs
