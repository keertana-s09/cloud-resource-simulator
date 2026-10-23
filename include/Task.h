// Task.h — the single authoritative task definition shared by every module.
// Owner: Member 2
#pragma once

#include <string>

namespace crs {

enum class TaskStatus { Pending, Waiting, Running, Completed, Rejected };

std::string toString(TaskStatus status);

struct Task {
    // ---- static description (from the workload) ----
    int id = 0;
    int cpu = 0;           // CPU units (vCPUs)
    int ram = 0;           // RAM in MB
    int arrival = 0;       // tick at which the task arrives
    int duration = 0;      // ticks of execution once started
    int basePriority = 0;  // larger number = higher priority; never mutated by aging

    // ---- dynamic state (owned by the simulator) ----
    TaskStatus status = TaskStatus::Pending;
    int startTime = -1;
    int completionTime = -1;
    int rejectionTime = -1;
    int vmId = -1;         // VM the task ran on, -1 if none
    std::string reason;    // latest waiting / rejection reason

    Task() = default;
    Task(int id, int cpu, int ram, int arrival, int duration, int basePriority);

    bool isTerminal() const;

    // Ticks spent waiting: arrival -> start if started, otherwise arrival -> now.
    int waitingTicks(int now) const;

    // Returns an empty string if the request is well formed, otherwise the problem.
    std::string validate() const;

    // Restores the dynamic state to "not yet arrived".
    void resetState();
};

}  // namespace crs
