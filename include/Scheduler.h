// Scheduler.h — decides the order in which waiting tasks are considered.
// Owner: Member 2
//
// The scheduler never touches VM resources; it only ranks tasks.
#pragma once

#include <string>
#include <vector>

#include "Task.h"

namespace crs {

enum class SchedulingPolicy { FCFS, Priority, AgingPriority };

std::string toString(SchedulingPolicy policy);
bool parseSchedulingPolicy(const std::string& text, SchedulingPolicy& out);

// How the simulator walks the ranked queue each tick.
//   Backfill: try every waiting task in rank order; a task that does not fit is
//             skipped and lower-ranked tasks may still start.
//   Strict:   stop at the first task that does not fit; lower-ranked tasks wait
//             behind it (head-of-line blocking).
enum class QueueMode { Backfill, Strict };

std::string toString(QueueMode mode);
bool parseQueueMode(const std::string& text, QueueMode& out);

class Scheduler {
public:
    // agingInterval K: Aging Priority adds +1 effective priority per K ticks waited.
    explicit Scheduler(SchedulingPolicy policy, int agingInterval = 5);

    SchedulingPolicy policy() const { return policy_; }
    int agingInterval() const { return agingInterval_; }

    // FCFS / Priority: basePriority.
    // Aging Priority:  basePriority + floor(waitingTicks / K). Base priority is not modified.
    int effectivePriority(const Task& task, int now) const;

    // Returns the waiting tasks in the order they should be considered.
    //   FCFS:            arrival asc, then id asc
    //   Priority/Aging:  effective priority desc, then arrival asc, then id asc
    std::vector<const Task*> order(std::vector<const Task*> waiting, int now) const;

private:
    SchedulingPolicy policy_;
    int agingInterval_;
};

}  // namespace crs
