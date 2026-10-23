// main.cpp — the single application entry point: interactive terminal menu,
// plus a non-interactive mode driven by command-line flags.
// Owner: Member 3
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "DecisionAuditor.h"
#include "Replay.h"
#include "Simulator.h"
#include "Workload.h"

using namespace crs;

namespace {

// ---------------------------------------------------------------- output helpers

void printWorkload(const Workload& w) {
    std::cout << "\nWorkload '" << w.name << "'\n  VMs:\n";
    for (std::size_t i = 0; i < w.vms.size(); ++i) {
        std::cout << "    VM" << (i + 1) << " (" << w.vms[i].name << "): " << w.vms[i].cpu << " CPU, "
                  << w.vms[i].ram << " MB\n";
    }
    std::cout << "  Tasks:\n    " << std::left << std::setw(6) << "ID" << std::right << std::setw(5) << "CPU"
              << std::setw(8) << "RAM" << std::setw(8) << "Arrive" << std::setw(6) << "Dur" << std::setw(6)
              << "Prio" << "\n";
    for (const Task& t : w.tasks) {
        std::cout << "    " << std::left << std::setw(6) << ("T" + std::to_string(t.id)) << std::right
                  << std::setw(5) << t.cpu << std::setw(8) << t.ram << std::setw(8) << t.arrival << std::setw(6)
                  << t.duration << std::setw(6) << t.basePriority << "\n";
    }
    if (!w.config.empty()) {
        std::cout << "  Suggested settings:";
        for (const auto& kv : w.config) std::cout << " " << kv.first << "=" << kv.second;
        std::cout << "\n";
    }
}

void printMetrics(const Metrics& m) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Tasks: " << m.totalTasks << " total, " << m.completed << " completed, " << m.rejected
              << " rejected, " << m.unfinished << " unfinished\n"
              << "  Measurement interval: [0, " << m.endTick << ") ticks\n"
              << "  Average waiting time:    " << m.avgWaiting << " ticks (completed tasks)\n"
              << "  Average turnaround time: " << m.avgTurnaround << " ticks (completed tasks)\n"
              << "  Maximum waiting time:    " << m.maxWaiting << " ticks\n"
              << std::setprecision(3) << "  Throughput:              " << m.throughput << " tasks/tick\n"
              << std::setprecision(1) << "  CPU utilization:         " << m.cpuUtilization * 100 << "%\n"
              << "  RAM utilization:         " << m.ramUtilization * 100 << "%\n"
              << "  Starved (wait > " << m.starvationThreshold << "):     " << m.starvedTasks.size();
    if (!m.starvedTasks.empty()) {
        std::cout << "  [";
        for (std::size_t i = 0; i < m.starvedTasks.size(); ++i) std::cout << (i ? " " : "") << "T" << m.starvedTasks[i];
        std::cout << "]";
    }
    std::cout << "\n";
    std::cout.unsetf(std::ios::floatfield);
}

void printResult(const SimulationResult& r) {
    std::cout << "\n=== " << r.workloadName << ": " << r.config.label() << " ===\n";
    std::cout << "  " << std::left << std::setw(6) << "ID" << std::setw(11) << "Status" << std::setw(6) << "VM"
              << std::right << std::setw(7) << "Arrive" << std::setw(7) << "Start" << std::setw(7) << "End"
              << std::setw(6) << "Wait" << std::setw(6) << "TAT" << "\n";
    for (const Task& t : r.tasks) {
        auto opt = [](int v) { return v >= 0 ? std::to_string(v) : std::string("-"); };
        std::cout << "  " << std::left << std::setw(6) << ("T" + std::to_string(t.id)) << std::setw(11)
                  << toString(t.status) << std::setw(6) << (t.vmId >= 0 ? "VM" + std::to_string(t.vmId) : "-")
                  << std::right << std::setw(7) << t.arrival << std::setw(7) << opt(t.startTime) << std::setw(7)
                  << opt(t.completionTime >= 0 ? t.completionTime : t.rejectionTime) << std::setw(6)
                  << (t.startTime >= 0 ? std::to_string(t.startTime - t.arrival) : "-") << std::setw(6)
                  << (t.completionTime >= 0 ? std::to_string(t.completionTime - t.arrival) : "-") << "\n";
    }
    if (r.hitTickLimit) std::cout << "  WARNING: tick limit " << r.config.maxTicks << " reached\n";
    std::cout << "\n";
    printMetrics(r.metrics);
}

void explainTask(const SimulationResult& r, int id) {
    auto hist = DecisionAuditor::historyOf(r.events, id);
    if (hist.empty()) {
        std::cout << "No events for task T" << id << ".\n";
        return;
    }
    std::cout << "\nDecision autopsy for T" << id << " under " << r.config.label() << ":\n";
    for (const SimulationEvent& e : hist) std::cout << "  " << DecisionAuditor::formatEvent(e) << "\n";
}

std::string findWorkloadDir() {
    for (const char* dir : {"workloads", "../workloads", "../../workloads"}) {
        std::ifstream probe(std::string(dir) + "/normal.txt");
        if (probe) return dir;
    }
    return "workloads";
}

void exportResults(const std::string& dir, const SimulationResult* single,
                   const std::vector<SimulationResult>& compared) {
    if (single) {
        std::string base = dir + "/" + single->workloadName;
        writeEventsCsv(base + "_events.csv", *single);
        writeTasksCsv(base + "_tasks.csv", *single);
        std::cout << "Wrote " << base << "_events.csv and " << base << "_tasks.csv\n";
    }
    if (!compared.empty()) {
        std::string path = dir + "/" + compared.front().workloadName + "_comparison.csv";
        writeComparisonCsv(path, compared);
        std::cout << "Wrote " << path << "\n";
    }
}

// ---------------------------------------------------------------- interactive input

bool readLine(const std::string& prompt, std::string& out) {
    std::cout << prompt << std::flush;
    if (!std::getline(std::cin, out)) return false;
    std::size_t a = out.find_first_not_of(" \t\r");
    std::size_t b = out.find_last_not_of(" \t\r");
    out = a == std::string::npos ? "" : out.substr(a, b - a + 1);
    return true;
}

bool readInt(const std::string& prompt, int& out) {
    std::string s;
    if (!readLine(prompt, s)) return false;
    try {
        std::size_t used = 0;
        int v = std::stoi(s, &used);
        if (used != s.size()) return false;
        out = v;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

int runInteractive() {
    std::string workloadDir = findWorkloadDir();
    SimulationConfig cfg;
    std::unique_ptr<Workload> workload;
    std::unique_ptr<SimulationResult> last;
    std::vector<SimulationResult> compared;

    try {
        workload = std::make_unique<Workload>(loadWorkload(workloadDir + "/normal.txt"));
        applyWorkloadConfig(*workload, cfg);
    } catch (const std::exception& e) {
        std::cout << "(No default workload loaded: " << e.what() << ")\n";
    }

    while (true) {
        std::cout << "\n=========== Cloud Resource Allocation Simulator ===========\n";
        if (workload) {
            std::cout << "Workload: " << workload->name << " (" << workload->vms.size() << " VMs, "
                      << workload->tasks.size() << " tasks)\n";
        } else {
            std::cout << "Workload: (none loaded)\n";
        }
        std::cout << "Policy:   " << cfg.label() << "  aging K=" << cfg.agingInterval
                  << "  starvation threshold=" << cfg.starvationThreshold << "\n"
                  << "-----------------------------------------------------------\n"
                  << "  1) Load workload file           2) Generate random workload\n"
                  << "  3) Show workload                4) Choose scheduling policy\n"
                  << "  5) Choose placement policy      6) Settings (queue / aging / starvation)\n"
                  << "  7) Run simulation               8) Decision autopsy: full event log\n"
                  << "  9) Decision autopsy: one task  10) Compare all policies (replay)\n"
                  << " 11) Export results to CSV        0) Exit\n";
        int choice = -1;
        std::string line;
        if (!readLine("Choice: ", line)) return 0;
        try {
            choice = std::stoi(line);
        } catch (const std::exception&) {
            std::cout << "Please enter a number.\n";
            continue;
        }

        try {
            switch (choice) {
                case 0:
                    return 0;
                case 1: {
                    std::cout << "Bundled workloads in " << workloadDir
                              << ": normal, cpu_bottleneck, memory_bottleneck, starvation\n";
                    std::string name;
                    if (!readLine("Name or path: ", name) || name.empty()) break;
                    std::string path = name;
                    if (name.find('/') == std::string::npos && name.find('\\') == std::string::npos) {
                        path = workloadDir + "/" + name + (name.size() > 4 && name.substr(name.size() - 4) == ".txt" ? "" : ".txt");
                    }
                    auto w = std::make_unique<Workload>(loadWorkload(path));
                    SimulationConfig next = cfg;
                    applyWorkloadConfig(*w, next);
                    cfg = next;
                    workload = std::move(w);
                    last.reset();
                    compared.clear();
                    std::cout << "Loaded '" << workload->name << "'.";
                    if (!workload->config.empty()) std::cout << " Applied its suggested settings.";
                    std::cout << "\n";
                    break;
                }
                case 2: {
                    int seed = 42, vms = 3, n = 20;
                    readInt("Seed [42]: ", seed);
                    readInt("Number of VMs [3]: ", vms);
                    readInt("Number of tasks [20]: ", n);
                    workload = std::make_unique<Workload>(generateWorkload(static_cast<unsigned>(seed), vms, n));
                    last.reset();
                    compared.clear();
                    std::cout << "Generated '" << workload->name << "' (same seed => same workload).\n";
                    std::string save;
                    if (readLine("Save to file? (path or blank): ", save) && !save.empty()) {
                        saveWorkload(*workload, save);
                        std::cout << "Saved to " << save << "\n";
                    }
                    break;
                }
                case 3:
                    if (workload) printWorkload(*workload);
                    else std::cout << "Load a workload first.\n";
                    break;
                case 4: {
                    std::string s;
                    readLine("Scheduling policy (fcfs / priority / aging): ", s);
                    if (!parseSchedulingPolicy(s, cfg.scheduling)) std::cout << "Unknown policy.\n";
                    break;
                }
                case 5: {
                    std::string s;
                    readLine("Placement policy (first / best / load): ", s);
                    if (!parsePlacementPolicy(s, cfg.placement)) std::cout << "Unknown policy.\n";
                    break;
                }
                case 6: {
                    std::string s;
                    readLine("Queue mode (backfill / strict) [" + toString(cfg.queueMode) + "]: ", s);
                    if (!s.empty() && !parseQueueMode(s, cfg.queueMode)) std::cout << "Unknown mode; unchanged.\n";
                    int k = cfg.agingInterval;
                    if (readInt("Aging interval K [" + std::to_string(cfg.agingInterval) + "]: ", k)) {
                        cfg.agingInterval = std::max(1, k);
                    }
                    int th = cfg.starvationThreshold;
                    if (readInt("Starvation threshold in ticks [" + std::to_string(cfg.starvationThreshold) + "]: ", th)) {
                        cfg.starvationThreshold = std::max(0, th);
                    }
                    break;
                }
                case 7:
                    if (!workload) {
                        std::cout << "Load a workload first.\n";
                        break;
                    }
                    last = std::make_unique<SimulationResult>(Simulator(*workload, cfg).run());
                    printResult(*last);
                    break;
                case 8:
                    if (!last) {
                        std::cout << "Run a simulation first (option 7).\n";
                        break;
                    }
                    std::cout << "\nEvent log for " << last->config.label() << ":\n";
                    DecisionAuditor::printLog(std::cout, last->events);
                    break;
                case 9: {
                    if (!last) {
                        std::cout << "Run a simulation first (option 7).\n";
                        break;
                    }
                    int id = 0;
                    if (readInt("Task ID: ", id)) explainTask(*last, id);
                    break;
                }
                case 10:
                    if (!workload) {
                        std::cout << "Load a workload first.\n";
                        break;
                    }
                    compared = replay(*workload, cfg, allPolicyCombos());
                    std::cout << "\n";
                    printComparison(std::cout, compared);
                    break;
                case 11: {
                    if (!last && compared.empty()) {
                        std::cout << "Nothing to export yet.\n";
                        break;
                    }
                    std::string dir;
                    readLine("Directory [results]: ", dir);
                    exportResults(dir.empty() ? "results" : dir, last.get(), compared);
                    break;
                }
                default:
                    std::cout << "Unknown option.\n";
            }
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }
}

// ---------------------------------------------------------------- batch mode

void usage() {
    std::cout <<
        "Usage:\n"
        "  CloudResourceSimulator                      interactive menu\n"
        "  CloudResourceSimulator --workload FILE [options]\n"
        "  CloudResourceSimulator --random SEED [options]\n\n"
        "Options:\n"
        "  --sched fcfs|priority|aging     scheduling policy (default fcfs)\n"
        "  --place first|best|load         placement policy  (default first)\n"
        "  --queue backfill|strict         queue mode        (default backfill)\n"
        "  --aging K                       aging interval    (default 5)\n"
        "  --starve N                      starvation threshold in ticks (default 20)\n"
        "  --log                           print the full decision event log\n"
        "  --explain ID                    print the decision history of one task\n"
        "  --compare                       replay all 9 policy combinations\n"
        "  --export DIR                    write CSV results into DIR\n"
        "Settings given on the command line override CONFIG lines in the workload file.\n";
}

int runBatch(const std::vector<std::string>& args) {
    std::string workloadPath, exportDir;
    int randomSeed = -1, explainId = -1;
    bool showLog = false, compare = false;
    SimulationConfig overrides;
    std::vector<std::string> overridden;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= args.size()) throw std::runtime_error("missing value for " + a);
            return args[++i];
        };
        if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else if (a == "--workload") {
            workloadPath = value();
        } else if (a == "--random") {
            randomSeed = std::stoi(value());
        } else if (a == "--sched") {
            if (!parseSchedulingPolicy(value(), overrides.scheduling)) throw std::runtime_error("unknown --sched");
            overridden.push_back("sched");
        } else if (a == "--place") {
            if (!parsePlacementPolicy(value(), overrides.placement)) throw std::runtime_error("unknown --place");
            overridden.push_back("place");
        } else if (a == "--queue") {
            if (!parseQueueMode(value(), overrides.queueMode)) throw std::runtime_error("unknown --queue");
            overridden.push_back("queue");
        } else if (a == "--aging") {
            overrides.agingInterval = std::max(1, std::stoi(value()));
            overridden.push_back("aging");
        } else if (a == "--starve") {
            overrides.starvationThreshold = std::max(0, std::stoi(value()));
            overridden.push_back("starve");
        } else if (a == "--log") {
            showLog = true;
        } else if (a == "--explain") {
            explainId = std::stoi(value());
        } else if (a == "--compare") {
            compare = true;
        } else if (a == "--export") {
            exportDir = value();
        } else {
            throw std::runtime_error("unknown argument '" + a + "' (try --help)");
        }
    }

    Workload w;
    if (!workloadPath.empty()) w = loadWorkload(workloadPath);
    else if (randomSeed >= 0) w = generateWorkload(static_cast<unsigned>(randomSeed), 3, 20);
    else throw std::runtime_error("need --workload FILE or --random SEED (try --help)");

    SimulationConfig cfg;
    applyWorkloadConfig(w, cfg);
    auto has = [&](const char* k) { return std::find(overridden.begin(), overridden.end(), k) != overridden.end(); };
    if (has("sched")) cfg.scheduling = overrides.scheduling;
    if (has("place")) cfg.placement = overrides.placement;
    if (has("queue")) cfg.queueMode = overrides.queueMode;
    if (has("aging")) cfg.agingInterval = overrides.agingInterval;
    if (has("starve")) cfg.starvationThreshold = overrides.starvationThreshold;

    SimulationResult result = Simulator(w, cfg).run();
    printResult(result);
    if (showLog) {
        std::cout << "\nEvent log:\n";
        DecisionAuditor::printLog(std::cout, result.events);
    }
    if (explainId >= 0) explainTask(result, explainId);

    std::vector<SimulationResult> compared;
    if (compare) {
        compared = replay(w, cfg, allPolicyCombos());
        std::cout << "\n";
        printComparison(std::cout, compared);
    }
    if (!exportDir.empty()) exportResults(exportDir, &result, compared);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    try {
        return args.empty() ? runInteractive() : runBatch(args);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
