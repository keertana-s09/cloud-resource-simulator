#include "Task.h"

namespace crs {

std::string toString(TaskStatus status) {
    switch (status) {
        case TaskStatus::Pending:   return "PENDING";
        case TaskStatus::Waiting:   return "WAITING";
        case TaskStatus::Running:   return "RUNNING";
        case TaskStatus::Completed: return "COMPLETED";
        case TaskStatus::Rejected:  return "REJECTED";
    }
    return "UNKNOWN";
}

Task::Task(int id_, int cpu_, int ram_, int arrival_, int duration_, int basePriority_)
    : id(id_), cpu(cpu_), ram(ram_), arrival(arrival_), duration(duration_),
      basePriority(basePriority_) {}

bool Task::isTerminal() const {
    return status == TaskStatus::Completed || status == TaskStatus::Rejected;
}

int Task::waitingTicks(int now) const {
    if (startTime >= 0) return startTime - arrival;
    if (now < arrival) return 0;
    return now - arrival;
}

std::string Task::validate() const {
    if (cpu <= 0) return "CPU demand must be positive (got " + std::to_string(cpu) + ")";
    if (ram <= 0) return "RAM demand must be positive (got " + std::to_string(ram) + " MB)";
    if (duration <= 0) return "duration must be positive (got " + std::to_string(duration) + ")";
    if (arrival < 0) return "arrival time cannot be negative (got " + std::to_string(arrival) + ")";
    return "";
}

void Task::resetState() {
    status = TaskStatus::Pending;
    startTime = completionTime = rejectionTime = vmId = -1;
    reason.clear();
}

}  // namespace crs
