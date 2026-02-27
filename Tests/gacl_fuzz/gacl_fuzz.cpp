// gacl_fuzz.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define GACL_EXPERIMENTAL 1
#define GACL_INCLUDE_CLER 1

#include <iostream>
#include <gacl.h>
#include "DirectXTex.h"
#include "../../Helpers/FormatHelper.h"
#include <thread>

enum FuzzTarget 
{
    Shuffle_BC1,
    Shuffle_BC3,
    Shuffle_BC4,
    Shuffle_BC5,
    Shuffle_BC7,
    BLER,
    CLER,
};

int FuzzShuffleCompressBCn(const uint8_t* data, size_t size, DXGI_FORMAT format, size_t inWidth = 0)
{
    size_t elementSize = (format == DXGI_FORMAT_BC1_TYPELESS || format == DXGI_FORMAT_BC4_TYPELESS) ? 8 : 16;

    // since GACL will early out for any buffer that's not a multiple of the block size, we'll pad or truncate to a multiple
    std::vector<uint8_t> padded;
    if (size < elementSize)
    {
        padded.resize(elementSize);
        memcpy_s(padded.data(), elementSize, data, size);
        data = padded.data();
        size = elementSize;
    }
    else
    {
        // GACL will early out if the buffer isn't a multiple of element size, so truncate
        size &= ~(elementSize-1);
    }

    std::vector<uint8_t> dest(size);
    size_t destBytes = 0;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL;

    if (size % (16 * 1024) == 0)
    {
        // if the size would allow for curved transforms, call shuffle+compress with additional curve params
        SHUFFLE_COMPRESS_PARAMETERS params = { size, format, data, {3}, {128, data} };
        GACL_ShuffleCompress_BCn(dest.data(), &tid, &destBytes, params);
    }
    else
    {
        SHUFFLE_COMPRESS_PARAMETERS params = { size, format, data, {3} };
        GACL_ShuffleCompress_BCn(dest.data(), &tid, &destBytes, params);
    }
    return 0;
}

int FuzzBlockLevelEntropyReduction(const uint8_t* data, size_t size)
{
    // rather than reject inputs that aren't an even number of blocks, we'll buffer or truncate.

    size_t bcSize = size < 16 ? 16 : size & ~15ull;

    DirectX::ScratchImage si;
    DirectX::ScratchImage decoded;
    HRESULT hr = si.Initialize2D(DXGI_FORMAT_BC7_UNORM_SRGB, bcSize / 4, 4, 1, 1);
    memcpy_s(si.GetPixels(), si.GetPixelsSize(), data, std::min<size_t>(size, bcSize));

    const DirectX::Image* src = si.GetImage(0, 0, 0);
    hr = DirectX::Decompress(src, 1, si.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, decoded);

    std::vector<uint8_t> blockGrouped(decoded.GetPixelsSize());
    GACL_RDO_R8G8B8A8LinearToBlockGrouped(blockGrouped.data(), decoded.GetPixels(), src->rowPitch, src->width, src->height);
    GACL_RDO_BlockLevelEntropyReduce(uint32_t(bcSize / 16), si.GetPixels(), 16, blockGrouped.data(), 0.5f);
    return 0;
}


int FuzzComponentLevelEntropyReduction(const uint8_t* data, size_t size)
{
    // rather than reject inputs that aren't an even number of blocks, we'll buffer or truncate.

    size_t bcSize = size < 8 ? 8 : size & ~7ull;

    DirectX::ScratchImage si;
    DirectX::ScratchImage decoded;
    HRESULT hr = si.Initialize2D(DXGI_FORMAT_BC1_UNORM_SRGB, bcSize / 2, 4, 1, 1);
    memcpy_s(si.GetPixels(), si.GetPixelsSize(), data, std::min<size_t>(size, bcSize));

    const DirectX::Image* src = si.GetImage(0, 0, 0);
    hr = DirectX::Decompress(src, 1, si.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, decoded);

    RDOOptions rdoOptions;
    rdoOptions.isGammaFormat = true;
    rdoOptions.iterations = 1;
    rdoOptions.minClusters = 0;
    rdoOptions.maxClusters = -1;
    rdoOptions.metric = RDOLossMetric::LPIPS;
    rdoOptions.lossMax = 0.1f;
    rdoOptions.lossMin = 0.05f;
    rdoOptions.usePlusPlus = true;

    GACL_RDO_ComponentLevelEntropyReduce(8, si.GetPixels(), decoded.GetPixels(), uint32_t(src->width), uint32_t(src->height), DXGI_FORMAT_BC1_UNORM_SRGB, rdoOptions);
    return 0;
}




#ifdef __cplusplus
#define FUZZ_EXPORT extern "C" __declspec(dllexport)
#else
#define FUZZ_EXPORT __declspec(dllexport)
#endif
FUZZ_EXPORT int __cdecl LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    static struct Init{
        const char* Cmdline;
        FuzzTarget Target;
        bool ExplicitTarget = true;

        Init()
        {
            Cmdline = GetCommandLineA();

            if (strstr(Cmdline, "--BC1") || strstr(Cmdline, "--bc1"))
            {
                Target = FuzzTarget::Shuffle_BC1;
            }
            else if (strstr(Cmdline, "--BC3") || strstr(Cmdline, "--bc3"))
            {
                Target = FuzzTarget::Shuffle_BC3;
            }
            else if (strstr(Cmdline, "--BC4") || strstr(Cmdline, "--bc4"))
            {
                Target = FuzzTarget::Shuffle_BC4;
            }
            else if (strstr(Cmdline, "--BC5") || strstr(Cmdline, "--bc5"))
            {
                Target = FuzzTarget::Shuffle_BC5;
            }
            else if (strstr(Cmdline, "--BC7") || strstr(Cmdline, "--bc7"))
            {
                Target = FuzzTarget::Shuffle_BC7;
            }
            else if (strstr(Cmdline, "--BLER") || strstr(Cmdline, "--bler"))
            {
                Target = FuzzTarget::BLER;
            }
            else if (strstr(Cmdline, "--CLER") || strstr(Cmdline, "--cler"))
            {
                Target = FuzzTarget::CLER;
            }
            else
            {
                Target = FuzzTarget::Shuffle_BC7;   // default
                ExplicitTarget = false;
            }
        }
    } i;
    
    // Fuzzer might be run with random inputs, or with a corpus of texture files.
    // For the second case, we want to consume the texture data, and want to skip past the header
    constexpr uint32_t DDS_MAGIC = 0x20534444;
    if (size > 124 && *reinterpret_cast<const uint32_t*>(data) == DDS_MAGIC)
    {
        DirectX::TexMetadata siMeta;
        DirectX::ScratchImage si;
        if (SUCCEEDED(DirectX::LoadFromDDSMemory(data, size, DirectX::DDS_FLAGS_NONE, &siMeta, si)))
        {
            if (i.ExplicitTarget)
            {
                int64_t hdrSize = si.GetPixels() - data;
                size -= hdrSize;
                data = si.GetPixels();
            }
            else
            {
                FuzzShuffleCompressBCn(data, si.GetPixelsSize(), siMeta.format, si.GetImage(0, 0, 0)->width);
                return 0;
            }
        }
    }

    switch (i.Target)
    {
    case Shuffle_BC1:
        return FuzzShuffleCompressBCn(data, size, DXGI_FORMAT_BC1_TYPELESS);

    case Shuffle_BC3:
        return FuzzShuffleCompressBCn(data, size, DXGI_FORMAT_BC3_TYPELESS);

    case Shuffle_BC4:
        return FuzzShuffleCompressBCn(data, size, DXGI_FORMAT_BC4_TYPELESS);

    case Shuffle_BC5:
        return FuzzShuffleCompressBCn(data, size, DXGI_FORMAT_BC5_TYPELESS);

    case Shuffle_BC7:
        return FuzzShuffleCompressBCn(data, size, DXGI_FORMAT_BC7_TYPELESS);

    case BLER:
        return FuzzBlockLevelEntropyReduction(data, size);

    case CLER:
        return FuzzComponentLevelEntropyReduction(data, size);
    }
     
    return 0;
}
