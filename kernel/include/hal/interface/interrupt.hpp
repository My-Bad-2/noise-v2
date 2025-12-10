#pragma once

#include <utility>
#include "cpu/exception.hpp"

namespace kernel::cpu {
/**
 * @brief Status code returned by interrupt handlers.
 *
 * Used by the dispatcher/scheduler to decide follow-up actions:
 *  - `Handled`    : no further work needed.
 *  - `Unhandled`  : may escalate or log.
 *  - `Reschedule` : current interrupt unblocked work; scheduler should run.
 */
enum class IrqStatus : uint8_t { Handled, Unhandled, Reschedule };

/**
 * @brief Abstract interface for interrupt/exception handlers.
 *
 * Handlers receive a full `TrapFrame` and may inspect/modify it before
 * returning control. The `name()` method is intended for diagnostics.
 */
class IInterruptHandler {
   public:
    virtual ~IInterruptHandler()                     = default;
    virtual IrqStatus handle(arch::TrapFrame* frame) = 0;
    virtual const char* name() const                 = 0;
};
}  // namespace kernel::cpu