#pragma once

// Real-time-safety instrumentation for the test binary.
//
// The test executable replaces the global operator new / delete family (see
// AllocationCounter.cpp). While a ScopedAllocationCounter is alive on a
// thread, every heap allocation and deallocation made *by that thread*
// through operator new/delete is counted. Audio-path tests wrap rendering in
// a counter and require both counts to be zero.
//
// Limitations (documented, deliberate):
//  - Direct malloc/free calls are not intercepted; AUREL code must not call
//    them anyway, and third-party code in the audio path is reviewed.
//  - Only the current thread is observed. That matches the real-time
//    question being asked: "does the audio thread touch the heap?"

#include <cstddef>

namespace aurel::test
{

class ScopedAllocationCounter
{
public:
    ScopedAllocationCounter() noexcept;
    ~ScopedAllocationCounter() noexcept;

    ScopedAllocationCounter (const ScopedAllocationCounter&) = delete;
    ScopedAllocationCounter& operator= (const ScopedAllocationCounter&) = delete;

    [[nodiscard]] std::size_t allocations() const noexcept;
    [[nodiscard]] std::size_t deallocations() const noexcept;

private:
    ScopedAllocationCounter* previous;
    std::size_t allocCount = 0;
    std::size_t freeCount = 0;

    friend void noteAllocation() noexcept;
    friend void noteDeallocation() noexcept;
};

void noteAllocation() noexcept;
void noteDeallocation() noexcept;

} // namespace aurel::test
