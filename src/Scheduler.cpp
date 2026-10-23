#include "Scheduler.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace crs {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

std::string toString(SchedulingPolicy policy) {
    switch (policy) {
        case SchedulingPolicy::FCFS:          return "FCFS";
        case SchedulingPolicy::Priority:      return "Priority";
        case SchedulingPolicy::AgingPriority: return "Aging Priority";
    }
    return "Unknown";
}

bool parseSchedulingPolicy(const std::string& text, SchedulingPolicy& out) {
    std::string t = lower(text);
    if (t == "fcfs" || t == "fifo") {
        out = SchedulingPolicy::FCFS;
    } else if (t == "priority" || t == "prio") {
        out = SchedulingPolicy::Priority;
    } else if (t == "aging" || t == "agingpriority" || t == "aging-priority") {
        out = SchedulingPolicy::AgingPriority;
    } else {
        return false;
    }
    return true;
}

std::string toString(QueueMode mode) {
    return mode == QueueMode::Strict ? "Strict" : "Backfill";
}

bool parseQueueMode(const std::string& text, QueueMode& out) {
    std::string t = lower(text);
    if (t == "strict" || t == "hol") {
        out = QueueMode::Strict;
    } else if (t == "backfill") {
        out = QueueMode::Backfill;
    } else {
        return false;
    }
    return true;
}

Scheduler::Scheduler(SchedulingPolicy policy, int agingInterval)
    : policy_(policy), agingInterval_(agingInterval) {
    if (agingInterval < 1) throw std::invalid_argument("aging interval K must be >= 1");
}

int Scheduler::effectivePriority(const Task& task, int now) const {
    if (policy_ != SchedulingPolicy::AgingPriority) return task.basePriority;
    return task.basePriority + task.waitingTicks(now) / agingInterval_;
}

std::vector<const Task*> Scheduler::order(std::vector<const Task*> waiting, int now) const {
    auto byArrival = [](const Task* a, const Task* b) {
        if (a->arrival != b->arrival) return a->arrival < b->arrival;
        return a->id < b->id;
    };
    if (policy_ == SchedulingPolicy::FCFS) {
        std::sort(waiting.begin(), waiting.end(), byArrival);
    } else {
        std::sort(waiting.begin(), waiting.end(), [&](const Task* a, const Task* b) {
            int pa = effectivePriority(*a, now);
            int pb = effectivePriority(*b, now);
            if (pa != pb) return pa > pb;
            return byArrival(a, b);
        });
    }
    return waiting;
}

}  // namespace crs
