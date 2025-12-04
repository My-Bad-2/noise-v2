#include "libs/log.hpp"
#include "memory/vmm.hpp"
#include "memory/heap.hpp"
#include "libs/math.hpp"

#define SLAB_MIN_SIZE 16
#define SLAB_MAX_SIZE 2048

#define CACHE_LINE_SIZE 64

#define SLAB_MAGIC       0x51AB51ABu
#define LARGE_SLAB_MAGIC 0xB166B166u

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// NOLINTBEGIN(performance-no-int-to-ptr)
namespace kernel::memory {

}  // namespace kernel::memory
// NOLINTEND(performance-no-int-to-ptr)