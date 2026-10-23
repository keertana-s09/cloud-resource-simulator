#include "ResourceAllocator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace crs {

namespace {

constexpr double kEps = 1e-9;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string fmt(double v, int precision = 3) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(precision) << v;
    return os.str();
}

std::string pct(double v) { return fmt(v * 100.0, 0) + "%"; }

std::string demand(const Task& t) {
    return std::to_string(t.cpu) + " CPU / " + std::to_string(t.ram) + " MB";
}

// Why a VM cannot host the task right now (assumes it cannot).
std::string shortage(const VM& vm, int cpu, int ram) {
    if (!vm.canEverFit(cpu, ram)) return vm.label() + " too small overall";
    bool c = cpu > vm.freeCpu();
    bool r = ram > vm.freeRam();
    std::string s = vm.label() + " lacks ";
    if (c && r) {
        s += "CPU and RAM (free " + std::to_string(vm.freeCpu()) + " CPU, " +
             std::to_string(vm.freeRam()) + " MB)";
    } else if (c) {
        s += "CPU (free " + std::to_string(vm.freeCpu()) + ")";
    } else {
        s += "RAM (free " + std::to_string(vm.freeRam()) + " MB)";
    }
    return s;
}

// Best-Fit score: normalized residual capacity left on the VM after placement.
//   residual = (freeCpu - cpu) / totalCpu + (freeRam - ram) / totalRam
// Lower is a tighter fit. Both resources contribute equally.
double bestFitScore(const VM& vm, int cpu, int ram) {
    return static_cast<double>(vm.freeCpu() - cpu) / vm.totalCpu() +
           static_cast<double>(vm.freeRam() - ram) / vm.totalRam();
}

// Load-Aware score: the VM's bottleneck utilization after placement.
//   load = max((usedCpu + cpu) / totalCpu, (usedRam + ram) / totalRam)
// Lower is a less loaded VM. Ties break on the mean of the two, then VM order.
double loadAfter(const VM& vm, int cpu, int ram) {
    double c = static_cast<double>(vm.usedCpu() + cpu) / vm.totalCpu();
    double r = static_cast<double>(vm.usedRam() + ram) / vm.totalRam();
    return std::max(c, r);
}

double meanLoadAfter(const VM& vm, int cpu, int ram) {
    double c = static_cast<double>(vm.usedCpu() + cpu) / vm.totalCpu();
    double r = static_cast<double>(vm.usedRam() + ram) / vm.totalRam();
    return (c + r) / 2.0;
}

// Normalized shortfall used to pick the "closest" VM in failure reports.
double shortfall(int needCpu, int haveCpu, int totalCpu, int needRam, int haveRam, int totalRam) {
    return static_cast<double>(std::max(0, needCpu - haveCpu)) / totalCpu +
           static_cast<double>(std::max(0, needRam - haveRam)) / totalRam;
}

}  // namespace

std::string toString(PlacementPolicy policy) {
    switch (policy) {
        case PlacementPolicy::FirstFit:  return "First-Fit";
        case PlacementPolicy::BestFit:   return "Best-Fit";
        case PlacementPolicy::LoadAware: return "Load-Aware";
    }
    return "Unknown";
}

bool parsePlacementPolicy(const std::string& text, PlacementPolicy& out) {
    std::string t = lower(text);
    if (t == "first" || t == "firstfit" || t == "first-fit" || t == "ff") {
        out = PlacementPolicy::FirstFit;
    } else if (t == "best" || t == "bestfit" || t == "best-fit" || t == "bf") {
        out = PlacementPolicy::BestFit;
    } else if (t == "load" || t == "loadaware" || t == "load-aware" || t == "la") {
        out = PlacementPolicy::LoadAware;
    } else {
        return false;
    }
    return true;
}

std::string toString(PlacementOutcome outcome) {
    switch (outcome) {
        case PlacementOutcome::Allocated:      return "Allocated";
        case PlacementOutcome::NoCapacityNow:  return "NoCapacityNow";
        case PlacementOutcome::NeverFits:      return "NeverFits";
        case PlacementOutcome::InvalidRequest: return "InvalidRequest";
    }
    return "Unknown";
}

std::string toString(Bottleneck b) {
    switch (b) {
        case Bottleneck::None:  return "none";
        case Bottleneck::Cpu:   return "CPU";
        case Bottleneck::Ram:   return "RAM";
        case Bottleneck::Both:  return "CPU+RAM";
        case Bottleneck::Mixed: return "mixed";
    }
    return "unknown";
}

ResourceAllocator::ResourceAllocator(std::vector<VM> vms) : vms_(std::move(vms)) {}

bool ResourceAllocator::canEverFit(int cpu, int ram) const {
    return std::any_of(vms_.begin(), vms_.end(),
                       [&](const VM& vm) { return vm.canEverFit(cpu, ram); });
}

bool ResourceAllocator::anyFitsNow(int cpu, int ram) const {
    return std::any_of(vms_.begin(), vms_.end(),
                       [&](const VM& vm) { return vm.canFitNow(cpu, ram); });
}

const VM* ResourceAllocator::findVm(int vmId) const {
    for (const VM& vm : vms_) {
        if (vm.id() == vmId) return &vm;
    }
    return nullptr;
}

int ResourceAllocator::totalCpu() const {
    int s = 0;
    for (const VM& vm : vms_) s += vm.totalCpu();
    return s;
}
int ResourceAllocator::totalRam() const {
    int s = 0;
    for (const VM& vm : vms_) s += vm.totalRam();
    return s;
}
int ResourceAllocator::usedCpu() const {
    int s = 0;
    for (const VM& vm : vms_) s += vm.usedCpu();
    return s;
}
int ResourceAllocator::usedRam() const {
    int s = 0;
    for (const VM& vm : vms_) s += vm.usedRam();
    return s;
}

PlacementResult ResourceAllocator::allocate(const Task& task, PlacementPolicy policy) {
    PlacementResult r;

    if (task.cpu <= 0 || task.ram <= 0) {
        r.outcome = PlacementOutcome::InvalidRequest;
        r.reason = "invalid request: CPU and RAM demand must be positive (got " + demand(task) + ")";
        return r;
    }
    auto held = active_.find(task.id);
    if (held != active_.end()) {
        r.outcome = PlacementOutcome::InvalidRequest;
        r.reason = "invalid request: task already holds resources on " +
                   vms_[held->second.vmIndex].label();
        return r;
    }
    if (!canEverFit(task.cpu, task.ram)) return failNeverFits(task);

    std::vector<std::size_t> feasible;
    for (std::size_t i = 0; i < vms_.size(); ++i) {
        if (vms_[i].canFitNow(task.cpu, task.ram)) feasible.push_back(i);
    }
    if (feasible.empty()) return failNoCapacity(task);

    std::size_t chosen = feasible.front();
    std::ostringstream why;

    switch (policy) {
        case PlacementPolicy::FirstFit: {
            why << vms_[chosen].label() << " is the first VM in configured order with "
                << "both " << task.cpu << " CPU and " << task.ram << " MB free";
            std::vector<std::string> skipped;
            for (std::size_t i = 0; i < chosen; ++i) {
                skipped.push_back(shortage(vms_[i], task.cpu, task.ram));
            }
            if (!skipped.empty()) {
                why << " (skipped: ";
                for (std::size_t i = 0; i < skipped.size(); ++i) why << (i ? "; " : "") << skipped[i];
                why << ")";
            }
            break;
        }
        case PlacementPolicy::BestFit: {
            double best = bestFitScore(vms_[chosen], task.cpu, task.ram);
            for (std::size_t i : feasible) {
                double s = bestFitScore(vms_[i], task.cpu, task.ram);
                if (s < best - kEps) {  // strict: earlier VM wins ties
                    best = s;
                    chosen = i;
                }
            }
            r.score = best;
            const VM& vm = vms_[chosen];
            why << vm.label() << " leaves the least residual capacity: score " << fmt(best)
                << " = " << (vm.freeCpu() - task.cpu) << "/" << vm.totalCpu() << " CPU + "
                << (vm.freeRam() - task.ram) << "/" << vm.totalRam() << " MB RAM";
            if (feasible.size() > 1) {
                why << " (" << feasible.size() << " feasible VMs; others:";
                for (std::size_t i : feasible) {
                    if (i == chosen) continue;
                    why << " " << vms_[i].label() << "=" << fmt(bestFitScore(vms_[i], task.cpu, task.ram));
                }
                why << ")";
            }
            break;
        }
        case PlacementPolicy::LoadAware: {
            double best = loadAfter(vms_[chosen], task.cpu, task.ram);
            double bestMean = meanLoadAfter(vms_[chosen], task.cpu, task.ram);
            for (std::size_t i : feasible) {
                double s = loadAfter(vms_[i], task.cpu, task.ram);
                double m = meanLoadAfter(vms_[i], task.cpu, task.ram);
                if (s < best - kEps || (std::fabs(s - best) <= kEps && m < bestMean - kEps)) {
                    best = s;
                    bestMean = m;
                    chosen = i;
                }
            }
            r.score = best;
            const VM& vm = vms_[chosen];
            double c = static_cast<double>(vm.usedCpu() + task.cpu) / vm.totalCpu();
            double m = static_cast<double>(vm.usedRam() + task.ram) / vm.totalRam();
            why << vm.label() << " has the lowest load after placement: max(CPU " << pct(c)
                << ", RAM " << pct(m) << ") = " << pct(best);
            if (feasible.size() > 1) {
                why << " (" << feasible.size() << " feasible VMs; others:";
                for (std::size_t i : feasible) {
                    if (i == chosen) continue;
                    why << " " << vms_[i].label() << "=" << pct(loadAfter(vms_[i], task.cpu, task.ram));
                }
                why << ")";
            }
            break;
        }
    }

    VM& vm = vms_[chosen];
    if (!vm.reserve(task.cpu, task.ram)) {
        // Defensive: feasibility was checked above, so this indicates a logic error.
        r.outcome = PlacementOutcome::InvalidRequest;
        r.reason = "internal error: reservation on " + vm.label() + " refused";
        return r;
    }
    active_[task.id] = Allocation{chosen, task.cpu, task.ram};

    r.success = true;
    r.outcome = PlacementOutcome::Allocated;
    r.vmId = vm.id();
    r.reason = why.str();
    return r;
}

PlacementResult ResourceAllocator::failNeverFits(const Task& task) const {
    PlacementResult r;
    r.outcome = PlacementOutcome::NeverFits;

    int maxCpu = 0, maxRam = 0;
    double bestGap = 1e18;
    for (const VM& vm : vms_) {
        maxCpu = std::max(maxCpu, vm.totalCpu());
        maxRam = std::max(maxRam, vm.totalRam());
        double gap = shortfall(task.cpu, vm.totalCpu(), vm.totalCpu(), task.ram, vm.totalRam(), vm.totalRam());
        if (gap < bestGap - kEps) {
            bestGap = gap;
            r.closestVmId = vm.id();
            r.cpuDeficit = std::max(0, task.cpu - vm.totalCpu());
            r.ramDeficit = std::max(0, task.ram - vm.totalRam());
        }
    }

    bool cpuTooBig = task.cpu > maxCpu;
    bool ramTooBig = task.ram > maxRam;
    if (vms_.empty()) {
        r.bottleneck = Bottleneck::Both;
        r.reason = "no VMs are configured";
    } else if (cpuTooBig && ramTooBig) {
        r.bottleneck = Bottleneck::Both;
        r.reason = "needs " + demand(task) + " but the largest VM has only " +
                   std::to_string(maxCpu) + " CPU and " + std::to_string(maxRam) + " MB";
    } else if (cpuTooBig) {
        r.bottleneck = Bottleneck::Cpu;
        r.reason = "needs " + std::to_string(task.cpu) + " CPU but the largest VM has only " +
                   std::to_string(maxCpu);
    } else if (ramTooBig) {
        r.bottleneck = Bottleneck::Ram;
        r.reason = "needs " + std::to_string(task.ram) + " MB RAM but the largest VM has only " +
                   std::to_string(maxRam) + " MB";
    } else {
        r.bottleneck = Bottleneck::Mixed;
        r.reason = "no single VM has both " + std::to_string(task.cpu) + " CPU and " +
                   std::to_string(task.ram) + " MB (a task cannot be split across VMs)";
    }
    r.reason += "; it can never be placed in this configuration";
    return r;
}

PlacementResult ResourceAllocator::failNoCapacity(const Task& task) const {
    PlacementResult r;
    r.outcome = PlacementOutcome::NoCapacityNow;

    int eligible = 0, cpuOnly = 0, ramOnly = 0, both = 0, tooSmall = 0;
    int mostFreeCpu = 0, mostFreeRam = 0;
    double bestGap = 1e18;
    std::vector<std::string> details;

    for (const VM& vm : vms_) {
        if (!vm.canEverFit(task.cpu, task.ram)) {
            ++tooSmall;
            continue;
        }
        ++eligible;
        bool c = task.cpu > vm.freeCpu();
        bool m = task.ram > vm.freeRam();
        if (c && m) ++both;
        else if (c) ++cpuOnly;
        else ++ramOnly;
        mostFreeCpu = std::max(mostFreeCpu, vm.freeCpu());
        mostFreeRam = std::max(mostFreeRam, vm.freeRam());
        details.push_back(shortage(vm, task.cpu, task.ram));

        double gap = shortfall(task.cpu, vm.freeCpu(), vm.totalCpu(), task.ram, vm.freeRam(), vm.totalRam());
        if (gap < bestGap - kEps) {
            bestGap = gap;
            r.closestVmId = vm.id();
            r.cpuDeficit = std::max(0, task.cpu - vm.freeCpu());
            r.ramDeficit = std::max(0, task.ram - vm.freeRam());
        }
    }

    std::ostringstream os;
    if (cpuOnly == eligible) {
        r.bottleneck = Bottleneck::Cpu;
        os << "no VM currently has enough free CPU (needs " << task.cpu
           << ", most free on a suitable VM is " << mostFreeCpu << ")";
    } else if (ramOnly == eligible) {
        r.bottleneck = Bottleneck::Ram;
        os << "no VM currently has enough free RAM (needs " << task.ram
           << " MB, most free on a suitable VM is " << mostFreeRam << " MB)";
    } else if (both == eligible) {
        r.bottleneck = Bottleneck::Both;
        os << "no VM currently has enough free CPU or RAM (needs " << demand(task) << ")";
    } else {
        r.bottleneck = Bottleneck::Mixed;
        os << "no single VM has both " << demand(task) << " free right now";
    }
    os << ": ";
    for (std::size_t i = 0; i < details.size(); ++i) os << (i ? "; " : "") << details[i];
    if (tooSmall > 0) os << " [" << tooSmall << " VM(s) too small overall]";
    r.reason = os.str();
    return r;
}

ReleaseResult ResourceAllocator::release(int taskId) {
    ReleaseResult r;
    auto it = active_.find(taskId);
    if (it == active_.end()) {
        r.reason = "release refused: task " + std::to_string(taskId) +
                   " holds no allocation (unknown task or already released)";
        return r;
    }
    const Allocation a = it->second;
    VM& vm = vms_[a.vmIndex];
    r.vmId = vm.id();
    if (!vm.release(a.cpu, a.ram)) {
        r.reason = "release refused: " + vm.label() + " would exceed its capacity; state unchanged";
        return r;
    }
    active_.erase(it);
    r.success = true;
    r.cpu = a.cpu;
    r.ram = a.ram;
    r.reason = "released " + std::to_string(a.cpu) + " CPU / " + std::to_string(a.ram) + " MB to " +
               vm.label() + " (now " + std::to_string(vm.freeCpu()) + "/" +
               std::to_string(vm.totalCpu()) + " CPU, " + std::to_string(vm.freeRam()) + "/" +
               std::to_string(vm.totalRam()) + " MB free)";
    return r;
}

}  // namespace crs
