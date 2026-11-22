#include "Support/AllocationCounter.h"

#include <cstdlib>
#include <new>

namespace aurel::test
{

namespace
{
    // Plain pointer with constant initialisation: safe to touch from
    // operator new even during static initialisation / thread start-up.
    thread_local ScopedAllocationCounter* activeCounter = nullptr;
} // namespace

ScopedAllocationCounter::ScopedAllocationCounter() noexcept : previous (activeCounter)
{
    activeCounter = this;
}

ScopedAllocationCounter::~ScopedAllocationCounter() noexcept
{
    activeCounter = previous;
}

std::size_t ScopedAllocationCounter::allocations() const noexcept { return allocCount; }
std::size_t ScopedAllocationCounter::deallocations() const noexcept { return freeCount; }

void noteAllocation() noexcept
{
    for (auto* c = activeCounter; c != nullptr; c = c->previous)
        ++c->allocCount;
}

void noteDeallocation() noexcept
{
    for (auto* c = activeCounter; c != nullptr; c = c->previous)
        ++c->freeCount;
}

} // namespace aurel::test

// --- Global replacements ----------------------------------------------------

namespace
{
    void* allocate (std::size_t size) noexcept
    {
        aurel::test::noteAllocation();
        return std::malloc (size == 0 ? 1 : size);
    }

    void* allocateAligned (std::size_t size, std::align_val_t align) noexcept
    {
        aurel::test::noteAllocation();
        const auto a = static_cast<std::size_t> (align);
        const auto n = size == 0 ? a : size;
#if defined(_MSC_VER)
        return _aligned_malloc (n, a);
#else
        return std::aligned_alloc (a, (n + a - 1) / a * a);
#endif
    }

    void release (void* p) noexcept
    {
        if (p == nullptr)
            return;
        aurel::test::noteDeallocation();
        std::free (p);
    }

    void releaseAligned (void* p) noexcept
    {
        if (p == nullptr)
            return;
        aurel::test::noteDeallocation();
#if defined(_MSC_VER)
        _aligned_free (p);
#else
        std::free (p);
#endif
    }
} // namespace

void* operator new (std::size_t n)
{
    if (auto* p = allocate (n))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t n)
{
    if (auto* p = allocate (n))
        return p;
    throw std::bad_alloc();
}

void* operator new (std::size_t n, const std::nothrow_t&) noexcept { return allocate (n); }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { return allocate (n); }

void* operator new (std::size_t n, std::align_val_t a)
{
    if (auto* p = allocateAligned (n, a))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t n, std::align_val_t a)
{
    if (auto* p = allocateAligned (n, a))
        return p;
    throw std::bad_alloc();
}

void* operator new (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocateAligned (n, a); }
void* operator new[] (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocateAligned (n, a); }

void operator delete (void* p) noexcept { release (p); }
void operator delete[] (void* p) noexcept { release (p); }
void operator delete (void* p, std::size_t) noexcept { release (p); }
void operator delete[] (void* p, std::size_t) noexcept { release (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept { release (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { release (p); }

void operator delete (void* p, std::align_val_t) noexcept { releaseAligned (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { releaseAligned (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { releaseAligned (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { releaseAligned (p); }
void operator delete (void* p, std::align_val_t, const std::nothrow_t&) noexcept { releaseAligned (p); }
void operator delete[] (void* p, std::align_val_t, const std::nothrow_t&) noexcept { releaseAligned (p); }
