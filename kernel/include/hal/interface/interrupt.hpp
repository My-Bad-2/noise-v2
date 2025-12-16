#pragma once

#include "cpu/exception.hpp"

namespace kernel::cpu {
enum class IrqStatus : uint8_t {
    Handled,     // no further work needed.
    Unhandled,   // may escalate or log
    Reschedule,  // current interrupt unblocked a thread; run scheduler
};

class IInterruptHandler {
   public:
    virtual ~IInterruptHandler()                     = default;
    virtual IrqStatus handle(arch::TrapFrame* frame) = 0;
    virtual const char* name() const                 = 0;
};
}  // namespace kernel::cpu