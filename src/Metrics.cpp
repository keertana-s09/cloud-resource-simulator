#include "Metrics.h"

#include <algorithm>
#include <map>

namespace crs {

Metrics computeMetrics(const std::vector<SimulationEvent>& events,
                       const std::vector<UtilizationSample>& samples, int totalCpu, int totalRam,
                       int totalTasks, int endTick, int starvationThreshold) {
    Metrics m;
    m.totalTasks = totalTasks;
    m.endTick = endTick;
    m.starvationThreshold = starvationThreshold;

    std::map<int, int> arrival, start, completion;
    std::map<int, bool> rejected;
    for (const SimulationEvent& e : events) {
        switch (e.type) {
            case EventType::Arrived:   arrival[e.taskId] = e.tick; break;
            case EventType::Allocated: start[e.taskId] = e.tick; break;
            case EventType::Completed: completion[e.taskId] = e.tick; break;
            case EventType::Rejected:  rejected[e.taskId] = true; break;
            default: break;
        }
    }

    long long sumWait = 0, sumTurn = 0;
    for (const auto& kv : completion) {
        int id = kv.first;
        sumWait += start.at(id) - arrival.at(id);
        sumTurn += kv.second - arrival.at(id);
    }
    m.completed = static_cast<int>(completion.size());
    m.rejected = static_cast<int>(rejected.size());
    m.unfinished = totalTasks - m.completed - m.rejected;
    if (m.completed > 0) {
        m.avgWaiting = static_cast<double>(sumWait) / m.completed;
        m.avgTurnaround = static_cast<double>(sumTurn) / m.completed;
    }

    for (const auto& kv : arrival) {
        int id = kv.first;
        if (rejected.count(id)) continue;
        auto s = start.find(id);
        int wait = (s != start.end() ? s->second : endTick) - kv.second;
        m.maxWaiting = std::max(m.maxWaiting, wait);
        if (wait > starvationThreshold) m.starvedTasks.push_back(id);
    }

    if (endTick > 0) {
        m.throughput = static_cast<double>(m.completed) / endTick;
        long long cpuTicks = 0, ramTicks = 0;
        for (const UtilizationSample& s : samples) {
            if (s.tick < 0 || s.tick >= endTick) continue;
            cpuTicks += s.usedCpu;
            ramTicks += s.usedRam;
        }
        if (totalCpu > 0) m.cpuUtilization = static_cast<double>(cpuTicks) / (static_cast<double>(totalCpu) * endTick);
        if (totalRam > 0) m.ramUtilization = static_cast<double>(ramTicks) / (static_cast<double>(totalRam) * endTick);
    }
    return m;
}

}  // namespace crs
