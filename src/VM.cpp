#include "VM.h"

#include <stdexcept>
#include <utility>

namespace crs {

VM::VM(int id, std::string name, int totalCpu, int totalRam)
    : id_(id), name_(std::move(name)), totalCpu_(totalCpu), totalRam_(totalRam) {
    if (totalCpu <= 0 || totalRam <= 0) {
        throw std::invalid_argument("VM '" + name_ + "' must have positive CPU and RAM capacity");
    }
}

std::string VM::label() const {
    std::string id = "VM" + std::to_string(id_);
    return name_ == id ? id : id + "(" + name_ + ")";
}

bool VM::reserve(int cpu, int ram) {
    if (cpu < 0 || ram < 0) return false;
    if (!canFitNow(cpu, ram)) return false;  // never over-allocate
    usedCpu_ += cpu;
    usedRam_ += ram;
    return true;
}

bool VM::release(int cpu, int ram) {
    if (cpu < 0 || ram < 0) return false;
    if (cpu > usedCpu_ || ram > usedRam_) return false;  // would push free above total
    usedCpu_ -= cpu;
    usedRam_ -= ram;
    return true;
}

}  // namespace crs
