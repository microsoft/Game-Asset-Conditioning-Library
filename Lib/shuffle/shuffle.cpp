//-------------------------------------------------------------------------------------
// shuffle.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------
#define ZSTD_STATIC_LINKING_ONLY

#include "gacl.h"
#include "../BCnBlockDefs.h"
#include "../helpers/FormatHelper.h"
#include "../helpers/Utility.h"
#include "../ThirdParty/zstd/lib/zstd.h"

#include <thread>
#include <vector>
#include <atomic>


/* Applies or reverses a screen space adjacency curve to texture data to improve compression later */

_Success_(dest != nullptr && src != nullptr)
bool GACL_Shuffle_ApplySpaceCurve(
    _Out_writes_bytes_opt_(size) uint8_t* dest,
    _In_reads_opt_(size) const uint8_t* src,
    size_t size,
    size_t elementSizeBytes,
    size_t widthInPixels,
    bool forward
)
{
    const size_t widthInElements = (widthInPixels + 3) / 4;
    const size_t pitchBytes = elementSizeBytes * widthInElements;
    
    const size_t heightInElements = (size + pitchBytes - 1) / pitchBytes;
    
    // 16KB micro tiles in z-order, applicable when height\width in tiles is power of 2
    if ((elementSizeBytes == 8 || elementSizeBytes == 16) &&
        size > 16ull * 1024 &&
        _mm_popcnt_u64(widthInElements) == 1 && widthInElements >= (elementSizeBytes == 8 ? 64u: 32u) &&
        _mm_popcnt_u64(heightInElements) == 1 && heightInElements >= 32u)
    {
        if (dest != nullptr && src != nullptr)
        {
            // 32 element * 32\64 element micro tile  
            const size_t tileSizeBytes = 16ull * 1024;
            const size_t tiles = size / tileSizeBytes;

            const size_t tileWidthElements = elementSizeBytes == 16 ? 32 : 64;
            const size_t widthInTiles = widthInElements / tileWidthElements;
            const size_t heightInTiles = tiles / widthInTiles;

            // default mask 
            size_t maskX = 0xAAAAAAAA;
            size_t maskY = 0x55555555;
            if (widthInTiles > heightInTiles)
            {
                size_t smallDimMask = (heightInTiles * heightInTiles) - 1;
                maskY &= smallDimMask;
                maskX |= ~smallDimMask;
            }
            else if (widthInTiles < heightInTiles)
            {
                size_t smallDimMask = (widthInTiles * widthInTiles) - 1;
                maskY |= ~smallDimMask;
                maskX &= smallDimMask;
            }

            for (size_t t = 0; t < tiles; t++)
            {
                size_t tx = _pext_u64(t, maskX);
                size_t ty = _pext_u64(t, maskY);

                const size_t firstTileByte =
                    ty * (tileSizeBytes * widthInTiles) +
                    tx * (tileWidthElements * elementSizeBytes);

                for (size_t r = 0; r < 32; r++)
                {
                    const size_t firstRowByte = firstTileByte + r * pitchBytes;

                    if (forward)
                    {
                        memcpy(dest + tileSizeBytes * t + 512 * r, src + firstRowByte, 512);
                    }
                    else
                    {
                        memcpy(dest + firstRowByte, src + tileSizeBytes * t + 512 * r, 512);
                    }
                }
            }
        }
        return true;
    }
    else
    {
        if (dest != nullptr && src != nullptr)
        {
            memcpy(dest, src, size);
        }
        return false;
    }
}


/*  Default zstd compression init, which will ensure >=btopt strategy and 3 byte matching, along with target block size */

HRESULT GACL_Compression_DefaultInitRoutine(
    _Out_ void** ccContext,
    _Out_ size_t* destBytesRequired,
    _In_ const SHUFFLE_COMPRESS_PARAMETERS* params
)
{
    *destBytesRequired = 0;
    // Minimum recommended compression settings = 3-byte minimum match size, which requires btopt or higher strategy
    // Scale the compression level up if the minimum is not met

    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    if (nullptr == cctx)
    {
        return E_OUTOFMEMORY;
    }

    int requestedCompressionLevel = int(params->CompressSettings.Default.ZstdCompressionLevel);
    int requestedTargetBlockSize = int(params->CompressSettings.Default.TargetBlockSize);

    ZSTD_compressionParameters zParams;
    
    if (requestedCompressionLevel)
    {
        zParams = ZSTD_getCParams(requestedCompressionLevel, params->SizeInBytes, 0);
    }
    else
    {
        if (params->SizeInBytes <= 16 * 1024)
        {
            zParams = ZSTD_getCParams(12, params->SizeInBytes, 0);  // min size to enforce btopt & 3 byte matching
        }
        else if (params->SizeInBytes <= 256 * 1024)
        {
            zParams = ZSTD_getCParams(14, params->SizeInBytes, 0);  // min size to enforce btopt & 3 byte matching
        }
        else    // >256KB
        {
            zParams = ZSTD_getCParams(18, params->SizeInBytes, 0);  // min size to enforce btopt & 3 byte matching
        }
    }
    size_t status = ZSTD_CCtx_setCParams(cctx, zParams);

    if (!ZSTD_isError(status))
    {
        status = ZSTD_CCtx_setParameter(cctx, ZSTD_c_targetCBlockSize, (requestedTargetBlockSize ? requestedTargetBlockSize : GACL_ZSTD_TARGET_COMPRESSED_BLOCK_SIZE));
    }

    if (!ZSTD_isError(status))
    {
        status = ZSTD_compressBound(params->SizeInBytes);
    }

    if (ZSTD_isError(status))
    {
        std::wstring message = Utility::UTF8ToWideString(ZSTD_getErrorName(status));
        Utility::Printf(GACL_Logging_Priority_High, message.c_str());
        if (cctx) ZSTD_freeCCtx(cctx);
        return E_FAIL;
    }
    else
    {
        *reinterpret_cast<ZSTD_CCtx**>(ccContext) = cctx;
        *destBytesRequired = status;
        return S_OK;
    }

}


/*  Default zstd compression handler  */

HRESULT GACL_Compression_DefaultCompressRoutine(
    _In_ void* context,
    _Out_writes_(*destBytes) void* dest,
    _Inout_ size_t* destBytes,
    _In_reads_(srcBytes) const void* src,
    size_t srcBytes
)
{
    ZSTD_CCtx* cctx = reinterpret_cast<ZSTD_CCtx*>(context);

    size_t status = ZSTD_compress2(cctx, dest, *destBytes, src, srcBytes);

    if (ZSTD_isError(status))
    {
        std::wstring message = Utility::UTF8ToWideString(ZSTD_getErrorName(status));
        Utility::Printf(GACL_Logging_Priority_High, message.c_str());
        *destBytes = 0;
        return E_FAIL;
    }
    else
    {
        *destBytes = status;
        return S_OK;
    }
}


/*  Default zstd compression cleanup */

HRESULT GACL_Compression_DefaultCleanupRoutine(
    _In_ void* pContext
)
{
    ZSTD_freeCCtx(reinterpret_cast<ZSTD_CCtx*>(pContext));
    return S_OK;
}

PGACL_COMPRESSION_INITROUTINE GACL_Compression_InitRoutine = &GACL_Compression_DefaultInitRoutine;
PGACL_COMPRESSION_COMPRESSROUTINE GACL_Compression_CompressRoutine = &GACL_Compression_DefaultCompressRoutine;
PGACL_COMPRESSION_CLEANUPROUTINE GACL_Compression_CleanupRoutine = &GACL_Compression_DefaultCleanupRoutine;


typedef HRESULT (*PSHUFFLE_FUNCTION)(uint8_t* dest, const uint8_t* src, size_t size, size_t version);

HRESULT Shuffle_BC1(uint8_t* dest, const uint8_t* src, size_t size, size_t version);
HRESULT Shuffle_BC3(uint8_t* dest, const uint8_t* src, size_t size, size_t version);
HRESULT Shuffle_BC4(uint8_t* dest, const uint8_t* src, size_t size, size_t version);
HRESULT Shuffle_BC5(uint8_t* dest, const uint8_t* src, size_t size, size_t version);
HRESULT ShuffleCompress_BC7(_Out_writes_all_(params.SizeInBytes) uint8_t* dest, _Inout_ GACL_SHUFFLE_TRANSFORM* destTransformId, _Out_ size_t* destBytesWritten, SHUFFLE_COMPRESS_PARAMETERS& params);


HRESULT GACL_ShuffleCompress_BCn(
    _Out_writes_(params.SizeInBytes) uint8_t* dest,
    _Inout_ GACL_SHUFFLE_TRANSFORM* destTransformId,
    _Out_ size_t* destBytesWritten,
    _In_ SHUFFLE_COMPRESS_PARAMETERS& params
)
{
    HRESULT status = S_OK;
    *destBytesWritten = 0;

    DXGI_FORMAT baseFormat = Utility::GetBaseFormat(params.Format);
    size_t elementSize;

    switch (baseFormat)
    {
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC4_TYPELESS:
        elementSize = 8;
        break;
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC7_TYPELESS:
        elementSize = 16;
        break;
    default:
        return E_INVALIDARG;
    }

    if (params.TextureData == nullptr && params.CurvedTransforms.TextureData == nullptr ||
        params.SizeInBytes % elementSize != 0 ||
        dest == nullptr ||
        destBytesWritten == nullptr)
        return E_INVALIDARG;

    std::vector<uint8_t> curvedData;
    const uint8_t* dataForCurvedTransforms = params.CurvedTransforms.TextureData;

    if (dataForCurvedTransforms != nullptr && !params.CurvedTransforms.DataIsCurved && baseFormat != DXGI_FORMAT_BC7_TYPELESS)
    {
        curvedData.resize(params.SizeInBytes);
        if (GACL_Shuffle_ApplySpaceCurve(curvedData.data(), dataForCurvedTransforms, params.SizeInBytes, elementSize, params.CurvedTransforms.WidthInPixels, true))
        {
            dataForCurvedTransforms = curvedData.data();
        }
        else
        {
            *destTransformId = GACL_SHUFFLE_TRANSFORM_NONE;
            return E_INVALIDARG;
            // Note to developers: if you prefer to have Shuffle+Compress to continue on with other possible
            // transforms, rather than erroring out due to requesting curved transforms for a texture of unsupported 
            // dimensions, simply comment out the above to lines, and uncomment the below:
            // dataForCurvedTransforms = nullptr;
        }
    }

    PSHUFFLE_FUNCTION pShuffle = nullptr;
    GACL_SHUFFLE_TRANSFORM tid1, tid1sc, tid2, tid2sc;
    size_t variants = 1;


    switch (baseFormat)
    {
    case DXGI_FORMAT_BC1_TYPELESS:
        pShuffle = &Shuffle_BC1;
        tid1 = GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224;
        tid1sc = GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224_SC;
        tid2 = GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44;
        tid2sc = GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44_SC;
        variants = 2;
        break;

    case DXGI_FORMAT_BC3_TYPELESS:
        pShuffle = &Shuffle_BC3;
        tid1 = GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224;
        tid1sc = GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224_SC;
        tid2 = GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664;
        tid2sc = GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664_SC;
        variants = 2;
        break;

    case DXGI_FORMAT_BC4_TYPELESS:
        pShuffle = &Shuffle_BC4;
        tid1 = GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116;
        tid1sc = GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116_SC;
        tid2 = GACL_SHUFFLE_TRANSFORM_NONE;
        tid2sc = GACL_SHUFFLE_TRANSFORM_NONE;
        break;

    case DXGI_FORMAT_BC5_TYPELESS:
        pShuffle = &Shuffle_BC5;
        tid1 = GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116;
        tid1sc = GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116_SC;
        tid2 = GACL_SHUFFLE_TRANSFORM_NONE;
        tid2sc = GACL_SHUFFLE_TRANSFORM_NONE;
        break;

    case DXGI_FORMAT_BC7_TYPELESS:
        
        return ShuffleCompress_BC7(dest, destTransformId, destBytesWritten, params);

    default:
        *destTransformId = GACL_SHUFFLE_TRANSFORM_NONE;
        return E_INVALIDARG;
    }


    std::vector<std::thread> tasks;
    std::atomic<HRESULT> lastError = S_OK;

    auto compressTask = [&](std::vector<uint8_t>& dest, const uint8_t* src, size_t srcBytes)
        {
            void* cc = nullptr;
            size_t compressedBytes;
            HRESULT hr = GACL_Compression_InitRoutine(&cc, &compressedBytes, &params);

            if (SUCCEEDED(hr) && cc != nullptr)
            {
                //// if using the default compression path, retrieve the compression level, it's fine that we do this multiple times and overwrite the value
                //// the default compress init is deterministic and will return the same value each time.

                dest.resize(compressedBytes);           // maximum possible size required
                hr = GACL_Compression_CompressRoutine(cc, dest.data(), &compressedBytes, src, srcBytes);

                if (SUCCEEDED(hr))
                {
                    dest.resize(compressedBytes);       // actual compressed size
                }
                else
                {
                    dest.resize(0);
                    lastError = hr;
                }

                hr = GACL_Compression_CleanupRoutine(cc);
                if (!SUCCEEDED(hr)) lastError = hr;
            }
            else
            {
                lastError = hr;
            }
        };


    // uncurved compress only
    std::vector<uint8_t> result_zstd_only;
    bool incZstdOnly = false;
    if (params.TextureData != nullptr && (*destTransformId >= GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED || *destTransformId == GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY))
    {
        incZstdOnly = true;
        tasks.push_back(std::thread([&] {
            compressTask(result_zstd_only, params.TextureData, params.SizeInBytes);
            }));
    }

    // curved zstd only  [experimental - preview does not include a standalone reverse-curve shader]
    std::vector<uint8_t> result_zstd_sc;
    bool inclZstdSc = false;
    if (dataForCurvedTransforms != nullptr && (*destTransformId >= GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL || *destTransformId == GACL_SHUFFLE_TRANSFORM_ZSTD_SC))
    {
        inclZstdSc = true;
        tasks.push_back(std::thread([&] {
            compressTask(result_zstd_sc, dataForCurvedTransforms, params.SizeInBytes);
            }));
    }

    // uncurved v1 shuffle
    std::vector<uint8_t> result_v1;
    bool incResultV1 = false;
    if (params.TextureData != nullptr && (*destTransformId >= GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED || *destTransformId == tid1))
    {
        incResultV1 = true;
        tasks.push_back(std::thread([&] {
            std::vector<uint8_t> shuffled(params.SizeInBytes);
            pShuffle(shuffled.data(), params.TextureData, params.SizeInBytes, 1);
            compressTask(result_v1, shuffled.data(), params.SizeInBytes);
            }));
    }

    bool incResultV1sc = false;
    // curved v1 shuffle  [experimental - preview does not include reverse-curve support]
    std::vector<uint8_t> result_v1sc;
    if (dataForCurvedTransforms != nullptr && (*destTransformId == GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL || *destTransformId == tid1sc))
    {
        incResultV1sc = true;
        tasks.push_back(std::thread([&] {
            std::vector<uint8_t> shuffled(params.SizeInBytes);
            pShuffle(shuffled.data(), dataForCurvedTransforms, params.SizeInBytes, 1);
            compressTask(result_v1sc, shuffled.data(), params.SizeInBytes);
            }));
    }

    std::vector<uint8_t> result_v2;
    std::vector<uint8_t> result_v2sc;
    bool incResultV2 = false;
    bool incResultV2sc = false;
    if (variants >= 2)
    {
        // uncurved v2 shuffle  [experimental - preview does not include v2 shuffle]
        if (params.TextureData != nullptr && (*destTransformId == GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL || *destTransformId == tid2))
        {
            incResultV2 = true;
            tasks.push_back(std::thread([&] {
                std::vector<uint8_t> shuffled(params.SizeInBytes);
                pShuffle(shuffled.data(), params.TextureData, params.SizeInBytes, 2);
                compressTask(result_v2, shuffled.data(), params.SizeInBytes);
                }));
        }

        // curved v2 shuffle  [experimental - preview does not include v2 shuffle]
        if (dataForCurvedTransforms != nullptr && (*destTransformId == GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL || *destTransformId == tid2sc))
        {
            incResultV2sc = true;
            tasks.push_back(std::thread([&] {
                std::vector<uint8_t> shuffled(params.SizeInBytes);
                pShuffle(shuffled.data(), dataForCurvedTransforms, params.SizeInBytes, 2);
                compressTask(result_v2sc, shuffled.data(), params.SizeInBytes);
                }));
        }
    }

    if (tasks.size() > 0)
    {
        for (auto& task : tasks) task.join();

        if (SUCCEEDED(status) && SUCCEEDED(lastError.load()))
        {
            std::vector<uint8_t>* bestData = nullptr;;
            size_t bestSize = params.SizeInBytes;

            for (auto d : { &result_zstd_only, &result_zstd_sc, &result_v1, &result_v1sc, &result_v2, &result_v2sc })
            {
                if (d->size() > 0 && d->size() < bestSize)
                {
                    bestData = d;
                    bestSize = bestData->size();
                }
            }

            wchar_t compressionStr[20] = L"(custom)";

            if (GACL_Compression_InitRoutine == &GACL_Compression_DefaultInitRoutine &&
                GACL_Compression_CompressRoutine == &GACL_Compression_DefaultCompressRoutine &&
                GACL_Compression_CleanupRoutine == &GACL_Compression_DefaultCleanupRoutine)
            {
                swprintf_s(compressionStr, _countof(compressionStr), L"(zstd-%d)", params.CompressSettings.Default.ZstdCompressionLevel);
            }

            Utility::Printf(L"Original texture                   %u%wc  \n", params.SizeInBytes, bestData == nullptr ? L'*' : L' ');
            if (incZstdOnly)
            {
                Utility::Printf(L"Compressed               %s  %u%wc  \n", compressionStr, result_zstd_only.size(), bestData == &result_zstd_only ? L'*' : L' ');
            }
            if (inclZstdSc)
            {
                Utility::Printf(L"Compressed SC            %s  %u%wc  \n", compressionStr, result_zstd_sc.size(), bestData == &result_zstd_sc ? L'*' : L' ');
            }
            if (incResultV1)
            {
                Utility::Printf(L"Shuffle compressed       %s  %u%wc  \n", compressionStr, result_v1.size(), bestData == &result_v1 ? L'*' : L' ');
            }
            if (incResultV1sc)
            {
                Utility::Printf(L"Shuffle compressed SC    %s  %u%wc  \n", compressionStr, result_v1sc.size(), bestData == &result_v1sc ? L'*' : L' ');
            }
            if (incResultV2)
            {
                Utility::Printf(L"Shuffle compressed v2    %s  %u%wc  \n", compressionStr, result_v2.size(), bestData == &result_v1 ? L'*' : L' ');
            }
            if (incResultV2sc)
            {
                Utility::Printf(L"Shuffle compressed v2 SC %s  %u%wc  \n", compressionStr, result_v2sc.size(), bestData == &result_v2sc ? L'*' : L' ');
            }

            if (bestData == nullptr)
            {
                Utility::Printf(L"Shuffle compression %s did not yield a smaller size\n", compressionStr);
                *destTransformId = GACL_SHUFFLE_TRANSFORM_NONE;
                return S_FALSE;
            }

            if (bestData == &result_zstd_only)
            {
                memcpy_s(dest, params.SizeInBytes, result_zstd_only.data(), result_zstd_only.size());
                *destTransformId = GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY;
                *destBytesWritten = result_zstd_only.size();
            }
            else if (bestData == &result_zstd_sc)
            {
                memcpy_s(dest, params.SizeInBytes, result_zstd_sc.data(), result_zstd_sc.size());
                *destTransformId = GACL_SHUFFLE_TRANSFORM_ZSTD_SC;
                *destBytesWritten = result_zstd_sc.size();
            }
            else if (bestData == &result_v1)
            {
                memcpy_s(dest, params.SizeInBytes, result_v1.data(), result_v1.size());
                *destTransformId = tid1;
                *destBytesWritten = result_v1.size();
            }
            else if (bestData == &result_v1sc)
            {
                memcpy_s(dest, params.SizeInBytes, result_v1sc.data(), result_v1sc.size());
                *destTransformId = tid1sc;
                *destBytesWritten = result_v1sc.size();
            }
            else if (bestData == &result_v2)
            {
                memcpy_s(dest, params.SizeInBytes, result_v2.data(), result_v2.size());
                *destTransformId = tid2;
                *destBytesWritten = result_v2.size();
            }
            else
            {
                memcpy_s(dest, params.SizeInBytes, result_v2sc.data(), result_v2sc.size());
                *destTransformId = tid2sc;
                *destBytesWritten = result_v2sc.size();
            }
            return status;
        }
        else
        {
            Utility::Printf(GACL_Logging_Priority_High, L"Compression fault.\n");
            *destTransformId = GACL_SHUFFLE_TRANSFORM_NONE;
            return status;
        }
    }
    else
    {
        *destTransformId = GACL_SHUFFLE_TRANSFORM_NONE;
        return E_INVALIDARG;
    }

}

const wchar_t* GACL_ShuffleCompress_GetFileExtensionForTransform(
    GACL_SHUFFLE_TRANSFORM transformId
)
{
    switch (transformId)
    {
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224:
        return L"gacl.bc11";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224_SC:
        return L"gacl.bc11s";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44:
        return L"gacl.bc12";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44_SC:
        return L"gacl.bc12s";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224:
        return L"gacl.bc31";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224_SC:
        return L"gacl.bc31s";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664:
        return L"gacl.bc32";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664_SC:
        return L"gacl.bc32s";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116:
        return L"gacl.bc41";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116_SC:
        return L"gacl.bc41s";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116:
        return L"gacl.bc51";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116_SC:
        return L"gacl.bc51s";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN:
        return L"gacl.bc7j";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN_SC:
        return L"gacl.bc7js";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT:
        return L"gacl.bc7s";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT_SC:
        return L"gacl.bc7ss";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY:
        return L"zstd";   // No shuffle, compressed only

    case GACL_SHUFFLE_TRANSFORM_ZSTD_SC:
        return L"gacl.zstds";

    default:
        return L"unsupported";

    }
}
