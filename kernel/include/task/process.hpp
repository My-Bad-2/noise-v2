#pragma once

#include <cstdint>
#include <cstddef>
#include "memory/pagemap.hpp"
#include "sched/thread.hpp"

namespace kernel::sched {
struct Process {
    size_t pid;
    memory::PageMap map;
};
}  // namespace kernel::sched