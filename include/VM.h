// VM.h — a virtual machine with finite CPU and RAM.
// Owner: Member 1
//
// Read access is public; mutation (reserve/release) is private and only
// ResourceAllocator is a friend, so no other module can change counters.
#pragma once

#include <string>

namespace crs {

class ResourceAllocator;

class VM {
public:
    VM(int id, std::string name, int totalCpu, int totalRam);  // throws std::invalid_argument

    int id() const { return id_; }
    const std::string& name() const { return name_; }
    // "VM2" or "VM2(medium)": the id used in event logs plus the configured name.
    std::string label() const;

    int totalCpu() const { return totalCpu_; }
    int totalRam() const { return totalRam_; }
    int usedCpu() const { return usedCpu_; }
    int usedRam() const { return usedRam_; }
    int freeCpu() const { return totalCpu_ - usedCpu_; }
    int freeRam() const { return totalRam_ - usedRam_; }

    double cpuUtilization() const { return static_cast<double>(usedCpu_) / totalCpu_; }
    double ramUtilization() const { return static_cast<double>(usedRam_) / totalRam_; }

    // Both resources must fit; neither may be ignored.
    bool canFitNow(int cpu, int ram) const { return cpu <= freeCpu() && ram <= freeRam(); }
    bool canEverFit(int cpu, int ram) const { return cpu <= totalCpu_ && ram <= totalRam_; }

private:
    friend class ResourceAllocator;

    // Return false (and change nothing) if the operation would break 0 <= used <= total.
    bool reserve(int cpu, int ram);
    bool release(int cpu, int ram);

    int id_;
    std::string name_;
    int totalCpu_;
    int totalRam_;
    int usedCpu_ = 0;
    int usedRam_ = 0;
};

}  // namespace crs
