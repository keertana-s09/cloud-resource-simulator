// ResourceAllocator.h — chooses a VM for a task and reserves/releases CPU and RAM.
// Owner: Member 1
//
// This is the ONLY component that mutates VM resource counters.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "Task.h"
#include "VM.h"

namespace crs {

enum class PlacementPolicy { FirstFit, BestFit, LoadAware };

std::string toString(PlacementPolicy policy);
bool parsePlacementPolicy(const std::string& text, PlacementPolicy& out);

enum class PlacementOutcome {
    Allocated,       // reserved on vmId
    NoCapacityNow,   // could fit some VM when it is emptier: wait
    NeverFits,       // exceeds every configured VM: reject
    InvalidRequest   // malformed demand or duplicate allocation: reject
};

std::string toString(PlacementOutcome outcome);

// Which resource prevented placement (None when allocated).
enum class Bottleneck { None, Cpu, Ram, Both, Mixed };

std::string toString(Bottleneck b);

struct PlacementResult {
    bool success = false;
    PlacementOutcome outcome = PlacementOutcome::InvalidRequest;
    Bottleneck bottleneck = Bottleneck::None;
    int vmId = -1;          // chosen VM on success
    int closestVmId = -1;   // on failure: VM with the smallest normalized shortfall
    int cpuDeficit = 0;     // on failure: CPU missing on closestVmId
    int ramDeficit = 0;     // on failure: RAM (MB) missing on closestVmId
    double score = 0.0;     // policy score of the chosen VM (Best-Fit / Load-Aware)
    std::string reason;
};

struct ReleaseResult {
    bool success = false;
    int vmId = -1;
    int cpu = 0;
    int ram = 0;
    std::string reason;
};

class ResourceAllocator {
public:
    explicit ResourceAllocator(std::vector<VM> vms);

    // Checks every VM, picks one according to the policy, and reserves resources.
    // On failure no VM state is changed.
    PlacementResult allocate(const Task& task, PlacementPolicy policy);

    // Returns resources held by taskId to the VM it was placed on.
    // Unknown/duplicate releases fail without touching any VM.
    ReleaseResult release(int taskId);

    // Read-only queries.
    bool canEverFit(int cpu, int ram) const;
    bool anyFitsNow(int cpu, int ram) const;
    const std::vector<VM>& vms() const { return vms_; }
    const VM* findVm(int vmId) const;
    int totalCpu() const;
    int totalRam() const;
    int usedCpu() const;
    int usedRam() const;
    std::size_t activeAllocations() const { return active_.size(); }

private:
    struct Allocation {
        std::size_t vmIndex;
        int cpu;
        int ram;
    };

    PlacementResult failNeverFits(const Task& task) const;
    PlacementResult failNoCapacity(const Task& task) const;

    std::vector<VM> vms_;
    std::map<int, Allocation> active_;  // taskId -> where its resources are held
};

}  // namespace crs
