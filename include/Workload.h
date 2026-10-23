// Workload.h — VM configuration + task list, loaded from a text file or generated.
// Owner: Member 3
//
// File format (one directive per line, '#' starts a comment):
//   VM     <name> <cpu> <ram_mb>
//   TASK   <id> <cpu> <ram_mb> <arrival> <duration> <priority>
//   CONFIG <key> <value>        keys: queue_mode, aging_interval, starvation_threshold
#pragma once

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "Task.h"

namespace crs {

struct VMSpec {
    std::string name;
    int cpu = 0;
    int ram = 0;
};

struct Workload {
    std::string name;
    std::vector<VMSpec> vms;
    std::vector<Task> tasks;
    std::map<std::string, std::string> config;  // suggested defaults from CONFIG lines
};

// Throw std::runtime_error with a line number on malformed input.
Workload parseWorkload(std::istream& in, const std::string& name);
Workload loadWorkload(const std::string& path);
void saveWorkload(const Workload& w, const std::string& path);

// Deterministic, platform-independent pseudo-random workload (own LCG, not std::distributions).
Workload generateWorkload(unsigned seed, int numVms, int numTasks);

}  // namespace crs
