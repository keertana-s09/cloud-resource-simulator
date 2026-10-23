// Replay.h — counterfactual replay: the same workload under several policies,
// each run starting from a fresh copy of the identical initial state.
// Owner: Member 3
#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "Simulator.h"
#include "Workload.h"

namespace crs {

struct PolicyCombo {
    SchedulingPolicy scheduling;
    PlacementPolicy placement;
};

// All 3 x 3 scheduling/placement combinations.
std::vector<PolicyCombo> allPolicyCombos();

// Runs one fresh Simulator per combo. `base` supplies queue mode, aging K, thresholds.
std::vector<SimulationResult> replay(const Workload& workload, const SimulationConfig& base,
                                     const std::vector<PolicyCombo>& combos);

void printComparison(std::ostream& os, const std::vector<SimulationResult>& results);

// CSV exports (directories must exist).
void writeComparisonCsv(const std::string& path, const std::vector<SimulationResult>& results);
void writeEventsCsv(const std::string& path, const SimulationResult& result);
void writeTasksCsv(const std::string& path, const SimulationResult& result);

}  // namespace crs
