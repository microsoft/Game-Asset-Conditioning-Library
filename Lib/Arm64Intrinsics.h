#pragma once

#include <cstdint>
#include <intrin.h>

namespace GaclArm64
{
    inline std::uint64_t ParallelDeposit(std::uint64_t value, std::uint64_t mask)
    {
        std::uint64_t result = 0;
        while (mask != 0)
        {
            const std::uint64_t bit = mask & (0ULL - mask);
            if ((value & 1) != 0)
                result |= bit;
            value >>= 1;
            mask &= mask - 1;
        }
        return result;
    }

    inline std::uint64_t ParallelExtract(std::uint64_t value, std::uint64_t mask)
    {
        std::uint64_t result = 0;
        std::uint64_t outputBit = 1;
        while (mask != 0)
        {
            const std::uint64_t bit = mask & (0ULL - mask);
            if ((value & bit) != 0)
                result |= outputBit;
            outputBit <<= 1;
            mask &= mask - 1;
        }
        return result;
    }

    inline std::uint32_t ParallelDeposit32(std::uint32_t value, std::uint32_t mask)
    {
        return static_cast<std::uint32_t>(ParallelDeposit(value, mask));
    }

    inline std::uint32_t ParallelExtract32(std::uint32_t value, std::uint32_t mask)
    {
        return static_cast<std::uint32_t>(ParallelExtract(value, mask));
    }
}

#if defined(_M_ARM64) && defined(USE_SOFT_INTRINSICS)
// Retain the SDK's SSE types and emulate the unsupported AVX/FMA operations.
#define SIMDE_NO_NATIVE
#define SIMDE_X86_SSE_NATIVE
#define SIMDE_X86_SSE2_NATIVE
#define SIMDE_X86_AVX_ENABLE_NATIVE_ALIASES
#define SIMDE_X86_AVX2_ENABLE_NATIVE_ALIASES
#define SIMDE_X86_FMA_ENABLE_NATIVE_ALIASES
#include <simde/x86/avx2.h>
#include <simde/x86/fma.h>

#define _pdep_u64 GaclArm64::ParallelDeposit
#define _pext_u64 GaclArm64::ParallelExtract
#define _pdep_u32 GaclArm64::ParallelDeposit32
#define _pext_u32 GaclArm64::ParallelExtract32
#endif
