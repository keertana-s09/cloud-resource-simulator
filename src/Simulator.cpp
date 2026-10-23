#include "Simulator.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "DecisionAuditor.h"

namespace crs {

std::string SimulationConfig::label() const {
    return toString(scheduling) + " + " + toString(placement) + " [" + toString(queueMode) + "]";
}

void applyWorkloadConfig(const Workload& w, SimulationConfig& config) {
    for (const auto& kv : w.config) {
        const std::string& key = kv.first;
        const std::string& value = kv.second;
        try {
            if (key == "queue_mode") {
                if (!parseQueueMode(value, config.queueMode)) throw std::invalid_argument(value);
            } else if (key == "aging_interval") {
                config.agingInterval = std::max(1, std::stoi(value));
            } else if (key == "starvation_threshold") {
                config.starvationThreshold = std::max(0, std::stoi(value));
            } else if (key == "scheduling") {
                if (!parseSchedulingPolicy(value, config.scheduling)) throw std::invalid_argument(value);
            } else if (key == "placement") {
                if (!parsePlacementPolicy(value, config.placement)) throw std::invalid_argument(value);
            } else {
                throw std::invalid_argument("unknown key");
            }
        } catch (const std::exception&) {
            throw std::runtime_error("invalid CONFIG " + key + " " + value);
        }
    }
}

Simulator::Simulator(Workload workload, SimulationConfig config)
    : workload_(std::move(workload)), config_(config) {}

SimulationResult Simulator::run() {
    // Fresh state for every run: new VMs, new allocator, reset task copies.
    std::vector<VM> vms;
    for (std::size_t i = 0; i < workload_.vms.size(); ++i) {
        const VMSpec& s = workload_.vms[i];
        vms.emplace_back(static_cast<int>(i + 1), s.name, s.cpu, s.ram);
    }
    ResourceAllocator allocator(std::move(vms));
    Scheduler scheduler(config_.scheduling, config_.agingInterval);

    SimulationResult result;
    result.config = config_;
    result.workloadName = workload_.name;
    result.vms = workload_.vms;
    result.tasks = workload_.tasks;
    std::sort(result.tasks.begin(), result.tasks.end(),
              [](const Task& a, const Task& b) { return a.id < b.id; });
    for (Task& t : result.tasks) t.resetState();

    std::vector<Task>& tasks = result.tasks;
    auto& events = result.events;
    auto log = [&](int tick, const Task& t, EventType type, int vmId, std::string reason) {
        events.push_back(SimulationEvent{tick, t.id, type, vmId, std::move(reason)});
    };

    // Waiting events are logged when the *kind* of wait changes (cause, blocker, or
    // reaching the head of the queue), not every tick.
    using WaitKey = std::tuple<int, int, int, bool>;  // outcome, bottleneck, blocker id, at head
    std::map<int, WaitKey> lastWait;

    int remaining = static_cast<int>(tasks.size());
    int lastTerminalTick = 0;
    int tick = 0;

    for (; remaining > 0; ++tick) {
        if (tick > config_.maxTicks) {
            result.hitTickLimit = true;
            break;
        }

        // 1. Completions (task-id order) and resource release.
        for (Task& t : tasks) {
            if (t.status != TaskStatus::Running || t.startTime + t.duration != tick) continue;
            ReleaseResult rel = allocator.release(t.id);
            if (!rel.success) {
                log(tick, t, EventType::Error, rel.vmId, rel.reason);
                continue;
            }
            t.status = TaskStatus::Completed;
            t.completionTime = tick;
            --remaining;
            lastTerminalTick = tick;
            log(tick, t, EventType::Completed, rel.vmId,
                "Finished after " + std::to_string(t.duration) + " tick(s); " + rel.reason);
        }

        // 2. Arrivals.
        for (Task& t : tasks) {
            if (t.status != TaskStatus::Pending || t.arrival > tick) continue;
            log(tick, t, EventType::Arrived, -1,
                "Arrived needing " + std::to_string(t.cpu) + " CPU / " + std::to_string(t.ram) +
                    " MB for " + std::to_string(t.duration) + " tick(s), base priority " +
                    std::to_string(t.basePriority));

            std::string problem = t.validate();
            if (problem.empty() && !allocator.canEverFit(t.cpu, t.ram)) {
                // Ask the allocator for the exact reason; nothing is reserved on failure.
                problem = allocator.allocate(t, config_.placement).reason;
            } else if (!problem.empty()) {
                problem = "is an invalid request: " + problem;
            }
            if (!problem.empty()) {
                t.status = TaskStatus::Rejected;
                t.rejectionTime = tick;
                t.reason = DecisionAuditor::explainRejection(t, problem);
                --remaining;
                lastTerminalTick = tick;
                log(tick, t, EventType::Rejected, -1, t.reason);
            } else {
                t.status = TaskStatus::Waiting;
            }
        }

        // 3. Scheduling: rank waiting tasks.
        std::vector<const Task*> waiting;
        for (const Task& t : tasks) {
            if (t.status == TaskStatus::Waiting) waiting.push_back(&t);
        }
        std::vector<const Task*> ranked = scheduler.order(waiting, tick);

        // 4. Placement in rank order.
        const Task* blocker = nullptr;
        int queueSize = static_cast<int>(ranked.size());
        for (int i = 0; i < queueSize; ++i) {
            Task& t = tasks[static_cast<std::size_t>(ranked[i] - tasks.data())];
            int rank = i + 1;

            if (blocker != nullptr) {
                bool fitsNow = allocator.anyFitsNow(t.cpu, t.ram);
                WaitKey key{-1, -1, blocker->id, false};
                std::string why = DecisionAuditor::explainBlocked(t, *blocker, fitsNow, scheduler, tick,
                                                                  rank, queueSize);
                t.reason = why;
                if (lastWait.count(t.id) == 0 || lastWait[t.id] != key) {
                    lastWait[t.id] = key;
                    log(tick, t, EventType::Waiting, -1, why);
                }
                continue;
            }

            PlacementResult pr = allocator.allocate(t, config_.placement);
            if (pr.success) {
                t.status = TaskStatus::Running;
                t.startTime = tick;
                t.vmId = pr.vmId;
                t.reason.clear();
                log(tick, t, EventType::Allocated, pr.vmId,
                    DecisionAuditor::explainAllocation(t, pr, config_.placement, scheduler, tick, rank,
                                                       queueSize));
            } else {
                WaitKey key{static_cast<int>(pr.outcome), static_cast<int>(pr.bottleneck), -1, rank == 1};
                std::string why = DecisionAuditor::explainWaiting(t, pr, scheduler, tick, rank, queueSize);
                t.reason = why;
                if (lastWait.count(t.id) == 0 || lastWait[t.id] != key) {
                    lastWait[t.id] = key;
                    log(tick, t, EventType::Waiting, -1, why);
                }
                if (config_.queueMode == QueueMode::Strict) blocker = &t;
            }
        }

        // 5. Utilization sample for [tick, tick + 1).
        result.utilization.push_back(UtilizationSample{tick, allocator.usedCpu(), allocator.usedRam()});
    }

    int endTick = result.hitTickLimit ? tick : lastTerminalTick;
    result.metrics = computeMetrics(result.events, result.utilization, allocator.totalCpu(),
                                    allocator.totalRam(), static_cast<int>(tasks.size()), endTick,
                                    config_.starvationThreshold);
    return result;
}

}  // namespace crs
