#pragma once

// RAII guard that enables flush-to-zero (FTZ) and denormals-are-zero (DAZ)
// for the current thread and restores the previous mode on destruction.
//
// Denormal floats appear when recursive filters / reverbs decay towards
// silence and can cost 10-100x per operation on x86. The engine's top-level
// process call holds one of these for the whole block.
//
// x86/x64 only (SSE MXCSR). On other architectures this is currently a no-op;
// ARM64 support (FPCR.FZ) will be added if/when that platform is targeted.

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
    #include <xmmintrin.h>
    #define AUREL_DSP_HAS_SSE_CSR 1
#else
    #define AUREL_DSP_HAS_SSE_CSR 0
#endif

namespace aurel::dsp
{

class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() noexcept
    {
#if AUREL_DSP_HAS_SSE_CSR
        saved = _mm_getcsr();
        _mm_setcsr (saved | kFlushToZero | kDenormalsAreZero);
#endif
    }

    ~ScopedFlushDenormals() noexcept
    {
#if AUREL_DSP_HAS_SSE_CSR
        _mm_setcsr (saved);
#endif
    }

    ScopedFlushDenormals (const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;

    static constexpr bool isSupported() noexcept { return AUREL_DSP_HAS_SSE_CSR != 0; }

private:
    static constexpr unsigned int kFlushToZero = 0x8000u;      // MXCSR bit 15
    static constexpr unsigned int kDenormalsAreZero = 0x0040u; // MXCSR bit 6
    unsigned int saved = 0;
};

} // namespace aurel::dsp
