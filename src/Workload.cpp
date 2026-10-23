#include "Workload.h"

#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace crs {

namespace {

std::string stem(const std::string& path) {
    std::size_t slash = path.find_last_of("/\\");
    std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    std::size_t dot = base.find_last_of('.');
    return dot == std::string::npos ? base : base.substr(0, dot);
}

[[noreturn]] void fail(const std::string& name, int line, const std::string& msg) {
    throw std::runtime_error(name + ":" + std::to_string(line) + ": " + msg);
}

// Small LCG so generated workloads are identical on every compiler/platform.
class Lcg {
public:
    explicit Lcg(unsigned seed) : state_(seed * 2654435761u + 1u) {}
    std::uint32_t next() {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state_ >> 33);
    }
    int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1)); }

private:
    std::uint64_t state_;
};

}  // namespace

Workload parseWorkload(std::istream& in, const std::string& name) {
    Workload w;
    w.name = name;
    std::set<int> ids;
    std::set<std::string> vmNames;
    std::string raw;
    int lineNo = 0;

    while (std::getline(in, raw)) {
        ++lineNo;
        std::size_t hash = raw.find('#');
        if (hash != std::string::npos) raw.erase(hash);
        std::istringstream ls(raw);
        std::string kind;
        if (!(ls >> kind)) continue;

        if (kind == "VM") {
            VMSpec v;
            if (!(ls >> v.name >> v.cpu >> v.ram)) fail(name, lineNo, "expected: VM <name> <cpu> <ram_mb>");
            if (v.cpu <= 0 || v.ram <= 0) fail(name, lineNo, "VM capacity must be positive");
            if (!vmNames.insert(v.name).second) fail(name, lineNo, "duplicate VM name '" + v.name + "'");
            w.vms.push_back(v);
        } else if (kind == "TASK") {
            Task t;
            if (!(ls >> t.id >> t.cpu >> t.ram >> t.arrival >> t.duration >> t.basePriority)) {
                fail(name, lineNo, "expected: TASK <id> <cpu> <ram_mb> <arrival> <duration> <priority>");
            }
            if (!ids.insert(t.id).second) fail(name, lineNo, "duplicate task id " + std::to_string(t.id));
            // Semantically invalid tasks (e.g. zero CPU) are kept on purpose: the
            // simulator rejects them with a reason instead of the parser hiding them.
            w.tasks.push_back(t);
        } else if (kind == "CONFIG") {
            std::string key, value;
            if (!(ls >> key >> value)) fail(name, lineNo, "expected: CONFIG <key> <value>");
            w.config[key] = value;
        } else {
            fail(name, lineNo, "unknown directive '" + kind + "'");
        }
        std::string extra;
        if (ls >> extra) fail(name, lineNo, "unexpected extra token '" + extra + "'");
    }
    if (w.vms.empty()) fail(name, lineNo, "workload defines no VMs");
    return w;
}

Workload loadWorkload(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open workload file '" + path + "'");
    return parseWorkload(in, stem(path));
}

void saveWorkload(const Workload& w, const std::string& path) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    out << "# Workload: " << w.name << "\n";
    for (const auto& kv : w.config) out << "CONFIG " << kv.first << " " << kv.second << "\n";
    out << "# VM <name> <cpu> <ram_mb>\n";
    for (const VMSpec& v : w.vms) out << "VM " << v.name << " " << v.cpu << " " << v.ram << "\n";
    out << "# TASK <id> <cpu> <ram_mb> <arrival> <duration> <priority>\n";
    for (const Task& t : w.tasks) {
        out << "TASK " << t.id << " " << t.cpu << " " << t.ram << " " << t.arrival << " " << t.duration
            << " " << t.basePriority << "\n";
    }
}

Workload generateWorkload(unsigned seed, int numVms, int numTasks) {
    if (numVms < 1 || numTasks < 1) throw std::invalid_argument("need at least one VM and one task");
    Lcg rng(seed);
    Workload w;
    w.name = "random_seed" + std::to_string(seed);

    const int cpuChoices[] = {4, 8, 16};
    const int ramChoices[] = {4096, 8192, 16384};
    for (int i = 0; i < numVms; ++i) {
        VMSpec v;
        v.name = "VM" + std::to_string(i + 1);
        v.cpu = cpuChoices[rng.range(0, 2)];
        v.ram = ramChoices[rng.range(0, 2)];
        w.vms.push_back(v);
    }
    int arrival = 0;
    for (int i = 0; i < numTasks; ++i) {
        arrival += rng.range(0, 2);
        Task t(i + 1, rng.range(1, 6), 512 * rng.range(1, 12), arrival, rng.range(1, 10), rng.range(1, 5));
        w.tasks.push_back(t);
    }
    return w;
}

}  // namespace crs
