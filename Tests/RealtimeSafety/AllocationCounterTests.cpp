// Self-tests for the allocation counter. If these fail, every real-time
// safety test built on top of the counter is meaningless.

#include "Support/AllocationCounter.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <thread>
#include <vector>

using aurel::test::ScopedAllocationCounter;

TEST_CASE ("AllocationCounter reports zero for allocation-free code", "[rt-safety]")
{
    std::size_t allocs = 0, frees = 0;
    volatile float acc = 0.0f;
    {
        const ScopedAllocationCounter counter;
        for (int i = 0; i < 1000; ++i)
            acc = acc + static_cast<float> (i) * 0.5f;
        allocs = counter.allocations();
        frees = counter.deallocations();
    }
    CHECK (allocs == 0);
    CHECK (frees == 0);
}

TEST_CASE ("AllocationCounter detects new/delete", "[rt-safety]")
{
    std::size_t allocs = 0, frees = 0;
    {
        const ScopedAllocationCounter counter;
        auto p = std::make_unique<int> (42);
        p.reset();
        allocs = counter.allocations();
        frees = counter.deallocations();
    }
    CHECK (allocs == 1);
    CHECK (frees == 1);
}

TEST_CASE ("AllocationCounter detects container growth and aligned new", "[rt-safety]")
{
    struct alignas (64) Aligned { float v[16]; };

    std::size_t allocs = 0;
    {
        const ScopedAllocationCounter counter;
        std::vector<float> v;
        v.push_back (1.0f);
        auto a = std::make_unique<Aligned>();
        allocs = counter.allocations();
    }
    CHECK (allocs >= 2);
}

TEST_CASE ("AllocationCounter only observes its own thread", "[rt-safety]")
{
    constexpr std::size_t workerAllocations = 50;
    std::size_t allocs = 0;
    {
        const ScopedAllocationCounter counter;
        // Constructing std::thread may allocate a small state block on *this*
        // thread; the worker's 50 allocations must not be attributed here.
        std::thread worker ([] {
            for (std::size_t i = 0; i < workerAllocations; ++i)
                auto p = std::make_unique<double> (1.0);
        });
        worker.join();
        allocs = counter.allocations();
    }
    CHECK (allocs < workerAllocations);
}

TEST_CASE ("AllocationCounter nests", "[rt-safety]")
{
    std::size_t outerAllocs = 0, innerAllocs = 0;
    {
        const ScopedAllocationCounter outer;
        auto a = std::make_unique<int> (1);
        {
            const ScopedAllocationCounter inner;
            auto b = std::make_unique<int> (2);
            innerAllocs = inner.allocations();
        }
        outerAllocs = outer.allocations();
    }
    CHECK (innerAllocs == 1);
    CHECK (outerAllocs == 2);
}
