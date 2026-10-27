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
#include "bcnex.h"

#include <intrin.h>
#include <immintrin.h>

#include <zstd.h>

#include <thread>
#include <vector>
#include <atomic>


/* Applies or reverses a screen space adjacency curve to texture data to improve compression later */

_Success_(dest != nullptr && src != nullptr)
bool GACL_Shuffle_ApplySpaceCurve(
    _Out_writes_bytes_opt_(sizeBytes) uint8_t* dest,
    _In_reads_opt_(sizeBytes) const uint8_t* src,
    size_t sizeBytes,
    size_t elementSizeBytes,
    size_t widthInPixels,
    bool forward
)
{
    const size_t widthInElements = (widthInPixels + 3) / 4;
    const size_t pitchBytes = elementSizeBytes * widthInElements;

    const size_t heightInElements = (sizeBytes + pitchBytes - 1) / pitchBytes;

    // 16KB micro tiles in z-order, applicable when height\width in tiles is power of 2 and the data is whole rows
    if ((elementSizeBytes == 8 || elementSizeBytes == 16) &&
        sizeBytes > 16ull * 1024 && sizeBytes % pitchBytes == 0 &&
        _mm_popcnt_u64(widthInElements) == 1 && widthInElements >= (elementSizeBytes == 8 ? 64u: 32u) &&
        _mm_popcnt_u64(heightInElements) == 1 && heightInElements >= 32u)
    {
        if (dest != nullptr && src != nullptr)
        {
            // 32 element * 32\64 element micro tile
            const size_t tileSizeBytes = 16ull * 1024;
            const size_t tiles = sizeBytes / tileSizeBytes;

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
            memcpy(dest, src, sizeBytes);
        }
        return false;
    }
}


/*  As above, but this function will apply the same 16KB encoded data space curve to decoded pixel data, or original art data loaded from another source */
_Success_(dest != nullptr && src != nullptr)
bool GACL_Shuffle_ApplySpaceCurveDecoded(
    _Out_writes_bytes_opt_(sizeBytes) uint8_t* dest,
    _In_reads_opt_(sizeBytes) const uint8_t* src,
    size_t sizeBytes,
    size_t encodedElementSizeBytes,
    size_t decodedPixelSizeBytes,
    size_t widthInPixels,
    bool forward
)
{
    const size_t sizeInPixels = sizeBytes / decodedPixelSizeBytes;
    const size_t heightInPixels = sizeInPixels / widthInPixels;

    const size_t rowPitchBytes = widthInPixels * decodedPixelSizeBytes;
    const size_t tileRowPitchBytes = rowPitchBytes * 128;

    const size_t widthInElements = (widthInPixels + 3) / 4;
    const size_t heightInElements = (heightInPixels + 3) / 4;

    // 16KB micro tiles in z-order, applicable when height\width in tiles is power of 2
    if ((encodedElementSizeBytes == 8 || encodedElementSizeBytes == 16 ) &&
        widthInElements * heightInElements * 16 * decodedPixelSizeBytes == sizeBytes &&
        _mm_popcnt_u64(widthInElements) == 1 && widthInElements >= (encodedElementSizeBytes == 8 ? 64u : 32u) &&
        _mm_popcnt_u64(heightInElements) == 1 && heightInElements >= 32u)
    {
        if (dest != nullptr && src != nullptr)
        {
            // 32 element * 32\64 element micro tile
            const size_t encodedTileSizeBytes = 16ull * 1024;
            const size_t tileSizeInElements = encodedTileSizeBytes / encodedElementSizeBytes;
            const size_t tiles = (widthInElements * heightInElements) / tileSizeInElements;

            const size_t tileWidthElements = encodedElementSizeBytes == 16 ? 32 : 64;
            const size_t widthInTiles = widthInElements / tileWidthElements;
            const size_t heightInTiles = tiles / widthInTiles;

            const size_t tilePitchBytes = tileWidthElements * 4 * decodedPixelSizeBytes;

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

                const size_t decodedTileSizeBytes = tileSizeInElements * 16 * decodedPixelSizeBytes;

                const size_t firstTileByte =
                    ty * (tileRowPitchBytes) +
                    tx * (tilePitchBytes);

                for (size_t r = 0; r < 128; r++)
                {
                    const size_t firstRowByte = firstTileByte + r * rowPitchBytes;

                    if (forward)
                    {
                        memcpy(dest + decodedTileSizeBytes * t + tilePitchBytes * r, src + firstRowByte, tilePitchBytes);
                    }
                    else
                    {
                        memcpy(dest + firstRowByte, src + decodedTileSizeBytes * t + tilePitchBytes * r, tilePitchBytes);
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
            memcpy(dest, src, sizeBytes);
        }
        return false;
    }
}


/*  The single point at which GACL's window-size policy is applied.  Every GACL compression
    path routes through here (including the BC7 mode-split section layout trials in
    bc7modesplit.cpp), so changing that policy - for example to require an exact window
    rather than a maximum - is one edit rather than a search.

    This caps only; it never raises a smaller window.  zstd reduces windowLog to fit the source
    at compression time and never grows it, so the cap reaches the frame as an upper bound.  */

void GACL_ApplyWindowPolicy(ZSTD_compressionParameters& zParams)
{
    if (zParams.windowLog > GACL_ZSTD_MAX_WINDOW_LOG)
    {
        zParams.windowLog = GACL_ZSTD_MAX_WINDOW_LOG;
    }
}


namespace
{
    /*  Selects the compression parameters GACL uses for a given input size.  Level 0 means "let
        GACL choose"; the chosen levels are the minimum that enforce btopt strategy and 3 byte
        matching for each size bucket.  */

    ZSTD_compressionParameters GACL_SelectCompressionParameters(size_t srcBytes, int requestedCompressionLevel)
    {
        if (requestedCompressionLevel)
        {
            return ZSTD_getCParams(requestedCompressionLevel, srcBytes, 0);
        }

        if (srcBytes <= 16 * 1024)
        {
            return ZSTD_getCParams(12, srcBytes, 0);  // min size to enforce btopt & 3 byte matching
        }
        else if (srcBytes <= 256 * 1024)
        {
            return ZSTD_getCParams(14, srcBytes, 0);  // min size to enforce btopt & 3 byte matching
        }

        return ZSTD_getCParams(18, srcBytes, 0);      // min size to enforce btopt & 3 byte matching
    }

    /*  Applies GACL's parameters and target block size to a context.  */

    size_t GACL_ConfigureCompressionContext(ZSTD_CCtx* cctx, size_t srcBytes, int requestedCompressionLevel, int requestedTargetBlockSize)
    {
        ZSTD_compressionParameters zParams = GACL_SelectCompressionParameters(srcBytes, requestedCompressionLevel);

        GACL_ApplyWindowPolicy(zParams);

        size_t status = ZSTD_CCtx_setCParams(cctx, zParams);

        if (!ZSTD_isError(status))
        {
            status = ZSTD_CCtx_setParameter(cctx, ZSTD_c_targetCBlockSize, (requestedTargetBlockSize ? requestedTargetBlockSize : GACL_ZSTD_TARGET_COMPRESSED_BLOCK_SIZE));
        }

        return status;
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

    size_t status = GACL_ConfigureCompressionContext(
        cctx,
        params->SizeInBytes,
        int(params->CompressSettings.Default.ZstdCompressionLevel),
        int(params->CompressSettings.Default.TargetBlockSize));

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


/*  Compress arbitrary data with GACL's constrained zstd settings, no shuffle transform applied  */

_Success_(return == S_OK)
GACL_API HRESULT GACL_Compression_CompressBuffer(
    _Out_writes_bytes_to_opt_(sizeInBytes, *destBytesWritten) uint8_t * dest,
    size_t sizeInBytes,
    _Out_ size_t * destBytesWritten,
    _In_reads_bytes_(sizeInBytes) const uint8_t * src,
    _In_opt_ const GACL_COMPRESS_BUFFER_PARAMETERS * params
)
{
    if (destBytesWritten)
    {
        // Report "nothing usable" up front so that every failure and the S_FALSE path leave the caller
        // with outputs it can act on without inspecting the HRESULT first.
        *destBytesWritten = 0;
    }

    if (dest == nullptr || destBytesWritten == nullptr || sizeInBytes == 0 || src == nullptr)
    {
        return E_INVALIDARG;
    }



    // Drive the shared compression hooks, so a caller that has replaced them gets one handler for
    // texture and non-texture payloads alike. DXGI_FORMAT_UNKNOWN is the signal to a custom handler
    // that this is not a BCn stream; the default handler only reads SizeInBytes and CompressSettings.
    SHUFFLE_COMPRESS_PARAMETERS shuffleParams = {};
    shuffleParams.SizeInBytes = sizeInBytes;
    shuffleParams.Format = DXGI_FORMAT_UNKNOWN;
    shuffleParams.TextureData = src;
    if (params != nullptr)
    {
        shuffleParams.CompressSettings.Default.ZstdCompressionLevel = params->ZstdCompressionLevel;
        shuffleParams.CompressSettings.Default.TargetBlockSize = params->TargetBlockSize;
    }

    void* cc = nullptr;
    size_t compressBound = 0;
    HRESULT hr = GACL_Compression_InitRoutine(&cc, &compressBound, &shuffleParams);
    if (FAILED(hr))
    {
        return hr;
    }
    if (cc == nullptr || compressBound == 0)
    {
        if (cc != nullptr)
        {
            GACL_Compression_CleanupRoutine(cc);
        }
        return E_FAIL;
    }

    // zstd may expand incompressible input past srcBytes, and the documented contract is that
    // streams that are not compressible will not copy to the output, and S_FALSE retuned
    std::vector<uint8_t> scratch(compressBound);
    size_t compressedBytes = compressBound;

    hr = GACL_Compression_CompressRoutine(cc, scratch.data(), &compressedBytes, src, sizeInBytes);

    const HRESULT cleanupHr = GACL_Compression_CleanupRoutine(cc);
    if (FAILED(hr))
    {
        return hr;
    }
    if (FAILED(cleanupHr))
    {
        return cleanupHr;
    }

    // Match GACL_ShuffleCompress_BCn: a result that is not smaller than the input is reported as
    // S_FALSE with nothing written, so the caller stores the original bytes.
    if (compressedBytes == 0 || compressedBytes >= sizeInBytes)
    {
        return S_FALSE;
    }

    memcpy_s(dest, sizeInBytes, scratch.data(), compressedBytes);

    *destBytesWritten = compressedBytes;
    return S_OK;
}


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
    GACL_SHUFFLE_TRANSFORM tid1, tid2;
    size_t variants = 1;


    switch (baseFormat)
    {
    case DXGI_FORMAT_BC1_TYPELESS:
        pShuffle = &Shuffle_BC1;
        tid1 = GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224;
        tid2 = GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44;
        variants = 2;
        break;

    case DXGI_FORMAT_BC3_TYPELESS:
        pShuffle = &Shuffle_BC3;
        tid1 = GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224;
        tid2 = GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664;
        variants = 2;
        break;

    case DXGI_FORMAT_BC4_TYPELESS:
        pShuffle = &Shuffle_BC4;
        tid1 = GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116;
        tid2 = GACL_SHUFFLE_TRANSFORM_NONE;
        break;

    case DXGI_FORMAT_BC5_TYPELESS:
        pShuffle = &Shuffle_BC5;
        tid1 = GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116;
        tid2 = GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266;
        variants = 2;
        break;

    case DXGI_FORMAT_BC7_TYPELESS:

        return ShuffleCompress_BC7(dest, destTransformId, destBytesWritten, params);

    default:
        *destTransformId = GACL_SHUFFLE_TRANSFORM_NONE;
        return E_INVALIDARG;
    }

    const GACL_SHUFFLE_TRANSFORM requested = *destTransformId;
    const bool anySupported = requested >= GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED;
    const bool anyExperimental = requested >= GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL;
    const GACL_SHUFFLE_TRANSFORM ex = GACL_SHUFFLE_TRANSFORM(BCnEx_TransformId(uint8_t(tid1)));     // the format's EX id
    const bool includeEx = anyExperimental || requested == ex;

    // curved EX streams record the texture width in 16KB micro-tiles (32 16-byte or 64 8-byte blocks wide)
    size_t curveTiles = 0;
    if (dataForCurvedTransforms != nullptr &&
        GACL_Shuffle_ApplySpaceCurve(nullptr, nullptr, params.SizeInBytes, elementSize, params.CurvedTransforms.WidthInPixels, true))
    {
        curveTiles = ((params.CurvedTransforms.WidthInPixels + 3) / 4) / (elementSize == 16 ? 32 : 64);
        curveTiles = curveTiles <= 255 ? curveTiles : 0;
    }

    struct Candidate
    {
        const wchar_t* Label;
        GACL_SHUFFLE_TRANSFORM Id;          // transform id of the result
        GACL_SHUFFLE_TRANSFORM Pattern;     // headerless shuffle pattern, NONE = not shuffled
        size_t Version;                     // pShuffle version of the pattern
        const uint8_t* Src;
        uint8_t CurveTiles;                 // EX curve width parameter
        bool Included;
        bool NoHeaderRoom = false;
        size_t HeaderBlocks = 0;
        size_t CodedBlocks = 0;             // header blocks the EX header codes
        std::vector<uint8_t> Data;
        int Prediction = -1;                // EX index prediction mask, -1 = the transform's default
    };

    const uint8_t* const linear = params.TextureData;
    const uint8_t* const curved = dataForCurvedTransforms;
    const bool v2 = variants >= 2;
    const uint8_t exTiles = uint8_t(curveTiles);

    // EX adds a header to the shuffle; linear streams only gain from it when it also adds index prediction. The
    // curve-only EX stream (no shuffle, no prediction) replaces the former ZSTD_SC transform.
    std::vector<Candidate> candidates =
    {
        { L"Compressed",                  GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY, GACL_SHUFFLE_TRANSFORM_NONE, 0, linear, 0, linear && (anySupported || requested == GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY) },
        { L"Compressed EX SC",            ex, GACL_SHUFFLE_TRANSFORM_NONE, 0, curved, exTiles, exTiles && includeEx },
        { L"Shuffle compressed",          tid1, tid1, 1, linear, 0, linear && (anySupported || requested == tid1) },
        // [experimental - preview does not include v2 shuffle]
        { L"Shuffle compressed v2",       tid2, tid2, 2, linear, 0, v2 && linear && (anyExperimental || requested == tid2) },
        { L"Shuffle compressed EX",       ex, tid1, 1, linear, 0, linear && includeEx && (BCnEx_Predicted(uint8_t(tid1)) || requested == ex) },
        { L"Shuffle compressed EX v2",    ex, tid2, 2, linear, 0, v2 && linear && includeEx && (BCnEx_Predicted(uint8_t(tid2)) || requested == ex) },
        { L"Shuffle compressed EX SC",    ex, tid1, 1, curved, exTiles, exTiles && includeEx },
        { L"Shuffle compressed EX v2 SC", ex, tid2, 2, curved, exTiles, v2 && exTiles && includeEx },
    };

    // per-stream index prediction (header parameter 2, BCNEX_PARAM_PREDICTION): the BC1 / BC3 EX candidates are
    // also tried without color prediction, which loses on some textures; linear ones only while they still predict
    // something (BC3 alpha)
    static const wchar_t* const kNoColourLabels[2][2] =
    {
        { L"Shuffle compressed EX no color prediction", L"Shuffle compressed EX SC no color prediction" },
        { L"Shuffle compressed EX v2 no color prediction", L"Shuffle compressed EX v2 SC no color prediction" },
    };
    for (size_t i = 0, count = candidates.size(); i < count; i++)
    {
        Candidate c = candidates[i];
        const uint8_t defaultMask = BCnEx_DefaultPrediction(uint8_t(c.Pattern));
        const uint8_t mask = uint8_t(defaultMask & ~BCNEX_PREDICT_COLOUR);
        if (c.Id != ex || !c.Included || mask == defaultMask || (c.CurveTiles == 0 && mask == 0))
        {
            continue;
        }
        c.Label = kNoColourLabels[c.Version - 1][c.CurveTiles != 0];
        c.Prediction = mask;
        candidates.push_back(c);
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

    for (Candidate& candidate : candidates)
    {
        if (!candidate.Included)
        {
            continue;
        }
        tasks.push_back(std::thread([&, pc = &candidate] {
            Candidate& c = *pc;
            if (c.Id == ex)
            {
                std::vector<uint8_t> stream;
                c.NoHeaderRoom = c.Pattern == GACL_SHUFFLE_TRANSFORM_NONE ?
                    !BCnEx_EncodeCurveOnly(c.Src, params.SizeInBytes, uint8_t(tid1), c.CurveTiles, stream, &c.HeaderBlocks, &c.CodedBlocks) :
                    !BCnEx_Encode(c.Src, params.SizeInBytes, uint8_t(c.Pattern), c.CurveTiles, stream, &c.HeaderBlocks, c.Prediction, &c.CodedBlocks);
                if (!c.NoHeaderRoom)
                {
#if _DEBUG || GACL_EXCHK
                    // checked against the candidate's own source, still curved: curved candidates encode the curved-space
                    // data, which can differ from TextureData (RDO applied in curved space) or be the only input
                    std::vector<uint8_t> back;
                    uint8_t backCurveTiles = 0;
                    if (!BCnEx_Decode(stream.data(), stream.size(), uint8_t(c.Id), back, false, nullptr, &backCurveTiles) ||
                        backCurveTiles != c.CurveTiles || back.size() != params.SizeInBytes || memcmp(back.data(), c.Src, back.size()) != 0)
                    {
                        Utility::Printf(GACL_Logging_Priority_High, L"%s round trip failed.\n", c.Label);
                        lastError = E_FAIL;
                    }
#endif
                    compressTask(c.Data, stream.data(), stream.size());
                }
            }
            else if (c.Pattern != GACL_SHUFFLE_TRANSFORM_NONE)
            {
                std::vector<uint8_t> shuffled(params.SizeInBytes);
                pShuffle(shuffled.data(), c.Src, params.SizeInBytes, c.Version);
                compressTask(c.Data, shuffled.data(), params.SizeInBytes);
            }
            else
            {
                compressTask(c.Data, c.Src, params.SizeInBytes);
            }
            }));
    }

    if (tasks.size() > 0)
    {
        for (auto& task : tasks) task.join();

        if (SUCCEEDED(status) && SUCCEEDED(lastError.load()))
        {
            const bool defaultCompression = GACL_Compression_InitRoutine == &GACL_Compression_DefaultInitRoutine &&
                GACL_Compression_CompressRoutine == &GACL_Compression_DefaultCompressRoutine &&
                GACL_Compression_CleanupRoutine == &GACL_Compression_DefaultCleanupRoutine;

            // Coded blocks slow the GPU decode (UnshuffleBCnEx.hlsl): BC1 streams with them take about four times a
            // headerless stream's latency. So at or below the default zstd level (and with custom compression routines,
            // which have no zstd level) a BC1 stream with coded blocks must be at least 1% smaller than the best stream
            // without them, else that stream (or the uncompressed texture) ships; above it the smallest stream wins.
            const int requestedZstdLevel = int(params.CompressSettings.Default.ZstdCompressionLevel);
            const int defaultZstdLevel = params.SizeInBytes <= 16 * 1024 ? 12 : (params.SizeInBytes <= 256 * 1024 ? 14 : 18);     // as GACL_Compression_DefaultInitRoutine
            const bool gateCoded = baseFormat == DXGI_FORMAT_BC1_TYPELESS && (!defaultCompression || requestedZstdLevel <= defaultZstdLevel);
            size_t uncodedSize = params.SizeInBytes;
            for (const Candidate& c : candidates)
            {
                if (c.Included && c.Data.size() > 0 && c.CodedBlocks == 0 && c.Data.size() < uncodedSize)
                {
                    uncodedSize = c.Data.size();
                }
            }
            auto gated = [&](const Candidate& c) { return gateCoded && c.CodedBlocks > 0 && c.Data.size() * 100 > uncodedSize * 99; };

            const Candidate* best = nullptr;
            size_t bestSize = params.SizeInBytes;

            for (const Candidate& c : candidates)
            {
                if (c.Included && c.Data.size() > 0 && c.Data.size() < bestSize && !gated(c))
                {
                    best = &c;
                    bestSize = c.Data.size();
                }
            }

            wchar_t compressionStr[20] = L"(custom)";

            if (defaultCompression)
            {
                swprintf_s(compressionStr, _countof(compressionStr), L"(zstd-%d)", params.CompressSettings.Default.ZstdCompressionLevel);
            }

            Utility::Printf(L"Original texture                   %zu%wc  \n", params.SizeInBytes, best == nullptr ? L'*' : L' ');
            for (const Candidate& c : candidates)
            {
                if (!c.Included)
                {
                    continue;
                }
                if (c.NoHeaderRoom)
                {
                    Utility::Printf(L"%-24s %s  - (no duplicate blocks to make room for the stream header)\n", c.Label, compressionStr);
                }
                else if (c.Id == ex)
                {
                    Utility::Printf(L"%-24s %s  %zu%wc  (header %zu blocks%s)\n", c.Label, compressionStr, c.Data.size(), best == &c ? L'*' : L' ', c.HeaderBlocks,
                        gated(c) ? L"; its coded blocks save under 1%, not used at this zstd level" : L"");
                }
                else
                {
                    Utility::Printf(L"%-24s %s  %zu%wc  \n", c.Label, compressionStr, c.Data.size(), best == &c ? L'*' : L' ');
                }
            }

            if (best == nullptr)
            {
                Utility::Printf(L"Shuffle compression %s did not yield a smaller size\n", compressionStr);
                *destTransformId = GACL_SHUFFLE_TRANSFORM_NONE;
                return S_FALSE;
            }

            memcpy_s(dest, params.SizeInBytes, best->Data.data(), best->Data.size());
            *destTransformId = best->Id;
            *destBytesWritten = best->Data.size();
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

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44:
        return L"gacl.bc12";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224:
        return L"gacl.bc31";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664:
        return L"gacl.bc32";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116:
        return L"gacl.bc41";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116:
        return L"gacl.bc51";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266:
        return L"gacl.bc52";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN:
        return L"gacl.bc7j";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX:
        return L"gacl.bc1ex";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_EX:
        return L"gacl.bc3ex";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_EX:
        return L"gacl.bc4ex";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_EX:
        return L"gacl.bc5ex";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX:
        return L"gacl.bc7ex";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT:
        return L"gacl.bc7s";

    case GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY:
        return L"zstd";   // No shuffle, compressed only

    default:
        return L"unsupported";

    }
}
