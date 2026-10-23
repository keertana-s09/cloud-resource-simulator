#include "Replay.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace crs {

namespace {

std::string csvEscape(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

std::ofstream openOut(const std::string& path) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    return out;
}

}  // namespace

std::vector<PolicyCombo> allPolicyCombos() {
    std::vector<PolicyCombo> combos;
    for (SchedulingPolicy s : {SchedulingPolicy::FCFS, SchedulingPolicy::Priority, SchedulingPolicy::AgingPriority}) {
        for (PlacementPolicy p : {PlacementPolicy::FirstFit, PlacementPolicy::BestFit, PlacementPolicy::LoadAware}) {
            combos.push_back(PolicyCombo{s, p});
        }
    }
    return combos;
}

std::vector<SimulationResult> replay(const Workload& workload, const SimulationConfig& base,
                                     const std::vector<PolicyCombo>& combos) {
    std::vector<SimulationResult> results;
    for (const PolicyCombo& c : combos) {
        SimulationConfig cfg = base;
        cfg.scheduling = c.scheduling;
        cfg.placement = c.placement;
        Simulator sim(workload, cfg);  // copies the workload: runs cannot contaminate each other
        results.push_back(sim.run());
    }
    return results;
}

void printComparison(std::ostream& os, const std::vector<SimulationResult>& results) {
    if (results.empty()) return;
    const SimulationConfig& c0 = results.front().config;
    os << "Workload '" << results.front().workloadName << "'  queue=" << toString(c0.queueMode)
       << "  aging K=" << c0.agingInterval << "  starvation threshold=" << c0.starvationThreshold
       << " ticks\n";
    os << std::left << std::setw(16) << "Scheduling" << std::setw(12) << "Placement" << std::right
       << std::setw(6) << "Done" << std::setw(6) << "Rej" << std::setw(8) << "AvgWait" << std::setw(8)
       << "MaxWait" << std::setw(8) << "AvgTAT" << std::setw(8) << "Thrpt" << std::setw(7) << "CPU%"
       << std::setw(7) << "RAM%" << std::setw(6) << "End" << std::setw(8) << "Starved" << "\n";
    os << std::string(102, '-') << "\n";

    double bestWait = 1e18, bestTat = 1e18;
    int bestEnd = 1 << 30;
    for (const SimulationResult& r : results) {
        bestWait = std::min(bestWait, r.metrics.avgWaiting);
        bestTat = std::min(bestTat, r.metrics.avgTurnaround);
        bestEnd = std::min(bestEnd, r.metrics.endTick);
    }
    for (const SimulationResult& r : results) {
        const Metrics& m = r.metrics;
        auto mark = [](bool best) { return best ? "*" : " "; };
        os << std::left << std::setw(16) << toString(r.config.scheduling) << std::setw(12)
           << toString(r.config.placement) << std::right << std::fixed << std::setprecision(2)
           << std::setw(6) << m.completed << std::setw(6) << m.rejected << std::setw(7) << m.avgWaiting
           << mark(m.avgWaiting <= bestWait + 1e-9) << std::setw(8) << m.maxWaiting << std::setw(7)
           << m.avgTurnaround << mark(m.avgTurnaround <= bestTat + 1e-9) << std::setw(8)
           << std::setprecision(3) << m.throughput << std::setprecision(1) << std::setw(7)
           << m.cpuUtilization * 100 << std::setw(7) << m.ramUtilization * 100 << std::setw(5)
           << m.endTick << mark(m.endTick == bestEnd) << std::setw(8) << m.starvedTasks.size();
        if (m.unfinished > 0) os << "  (" << m.unfinished << " unfinished)";
        os << "\n";
    }
    os.unsetf(std::ios::floatfield);
    os << "* = best in column. Waiting/turnaround averaged over completed tasks; utilization over [0, End).\n";
}

void writeComparisonCsv(const std::string& path, const std::vector<SimulationResult>& results) {
    std::ofstream out = openOut(path);
    out << "workload,scheduling,placement,queue_mode,aging_interval,completed,rejected,unfinished,"
           "avg_waiting,max_waiting,avg_turnaround,throughput,cpu_utilization,ram_utilization,end_tick,"
           "starved\n";
    for (const SimulationResult& r : results) {
        const Metrics& m = r.metrics;
        out << r.workloadName << "," << toString(r.config.scheduling) << "," << toString(r.config.placement)
            << "," << toString(r.config.queueMode) << "," << r.config.agingInterval << "," << m.completed
            << "," << m.rejected << "," << m.unfinished << "," << m.avgWaiting << "," << m.maxWaiting << ","
            << m.avgTurnaround << "," << m.throughput << "," << m.cpuUtilization << "," << m.ramUtilization
            << "," << m.endTick << "," << m.starvedTasks.size() << "\n";
    }
}

void writeEventsCsv(const std::string& path, const SimulationResult& result) {
    std::ofstream out = openOut(path);
    out << "tick,task_id,event,vm_id,reason\n";
    for (const SimulationEvent& e : result.events) {
        out << e.tick << "," << e.taskId << "," << toString(e.type) << "," << e.vmId << ","
            << csvEscape(e.reason) << "\n";
    }
}

void writeTasksCsv(const std::string& path, const SimulationResult& result) {
    std::ofstream out = openOut(path);
    out << "task_id,cpu,ram_mb,arrival,duration,base_priority,status,vm_id,start,completion,waiting,"
           "turnaround\n";
    for (const Task& t : result.tasks) {
        int wait = t.startTime >= 0 ? t.startTime - t.arrival : -1;
        int tat = t.completionTime >= 0 ? t.completionTime - t.arrival : -1;
        out << t.id << "," << t.cpu << "," << t.ram << "," << t.arrival << "," << t.duration << ","
            << t.basePriority << "," << toString(t.status) << "," << t.vmId << "," << t.startTime << ","
            << t.completionTime << "," << wait << "," << tat << "\n";
    }
}

}  // namespace crs
