//--------------------------------------------------------------------------------------
// Processing.cpp
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "pch.h"
#include "Processing.h"
#include "Utility.h"
#include "DirectXTex.h"
#include "zstd.h"

using namespace DirectX;
using namespace std;

bool IsBlockCompressed(DXGI_FORMAT Format)
{
    switch (Format)
    {
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return true;
    default:
        return false;
    }
}

void ProcessTexture(
    const std::wstring& inputFilePath,
    std::vector<uint8_t>& shuffledBuffer,
    std::vector<uint8_t>& originalBC7,
    size_t& bc7TextureSizeInBytes,
    size_t& bcTextureWidthInPixels, 
    size_t& bcTextureHeightInPixels,
    size_t& bcTextureSubresourceCount,
    DXGI_FORMAT& bcTextureFormat,
    GACL_SHUFFLE_TRANSFORM& bcnTransformId,
    ShuffleParameters const& shuffleParams)
{
    ScratchImage si;
    TexMetadata siMeta = {};

    HRESULT hr = S_OK;

    // The transform id parameter is in/out: on entry the caller provides the requested transform group
    // (GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED for the preview shaders, or ..._GROUP_ANY_EXPERIMENTAL when the
    // newer "x" shaders are built). On exit it holds the specific transform GACL_ShuffleCompress_BCn selected.
    GACL_SHUFFLE_TRANSFORM const requestedTransformGroup = bcnTransformId;
    bcnTransformId = GACL_SHUFFLE_TRANSFORM_NONE;

    if (Utility::ToLower(Utility::GetFileExtension(inputFilePath)) == L"dds")
    {
        hr = DirectX::LoadFromDDSFile(inputFilePath.c_str(), DDS_FLAGS::DDS_FLAGS_NONE, &siMeta, si);

        if (FAILED(hr))
        {
            throw std::runtime_error("Failed to load DDS file.");
        }

        if (!FAILED(hr))
        {
            if (!IsBlockCompressed(siMeta.format))
            {
                Utility::Printf(L"ERROR: File \"%ws\" is not in a block compressed texture.\n", inputFilePath.c_str());
                assert(false);
            }
        }
    }
    else
    {
        Utility::Printf(L"ERROR: File needs to be of DDS extension.");
        assert(false);
    }

    if (FAILED(hr))
    {
        Utility::Printf(L"ERROR: Failed to load \"%ws\" with HRESULT(0x%08X)\n", inputFilePath.c_str(), hr);
        assert(false);
    }

    BC7_ModeSplit_Validate_CopyBitOrders();

    DXGI_FORMAT const baseFmt = Utility::GetBaseFormat(siMeta.format);

    if (baseFmt == DXGI_FORMAT_BC7_TYPELESS)
    {
        // BC7 data
        uint8_t* src = si.GetPixels();
        size_t srcSize = si.GetPixelsSize();

        // Copy bc7 data (before shuffling)
        bc7TextureSizeInBytes = srcSize;
        originalBC7.resize(bc7TextureSizeInBytes);
        memcpy(originalBC7.data(), reinterpret_cast<void const*>(src), bc7TextureSizeInBytes);

        bcTextureWidthInPixels = si.GetMetadata().width;
        bcTextureHeightInPixels = si.GetMetadata().height;
        bcTextureSubresourceCount = si.GetMetadata().mipLevels;

        bcTextureFormat = siMeta.format;

        vector<uint8_t> curved(srcSize);

        if (shuffleParams.useSpaceCurve)
        {
            GACL_Shuffle_ApplySpaceCurve(curved.data(), src, srcSize, 16, bcTextureWidthInPixels, true);
            src = curved.data();
        }

        BC7TextureMetrics metrics = {};
        BC7_CollectTextureMetrics(src, srcSize, metrics);

        // Pack the parameters in a struct that is handed to the GACL lib to shuffle
        BC7ModeSplitShuffleOptions opt; 
        opt.ModeTransform = shuffleParams.modeTransform;
        opt.EndpointOrderStrategy = shuffleParams.endpointOrderStrategy;
        opt.RotationRegionSize = shuffleParams.ChunkSize;
        memcpy(opt.Patterns, shuffleParams.Patterns, std::size(opt.Patterns) * sizeof(BC7ModeSplitShufflePattern));

        // Call the GACL library to shuffle
        // Important:
        // - BC7_ModeSplit_Transform is an internal API within GACL, and it's called here since this is a lower level tests of the 
        // - shuffle feature itself. 
        // - Developers using the library are expected to call the simplified top level API, published as "GACL_ShuffleCompress_BCn"

        vector<uint8_t> splitModeA, splitModeB;
        BC7_ModeSplit_Transform(src, srcSize, splitModeA, splitModeB, opt, metrics);

        // Copy result into shuffled buffer
        size_t shuffledBufferSizeInBytes = splitModeB.size();
        shuffledBuffer.resize(shuffledBufferSizeInBytes);
        memcpy(shuffledBuffer.data(), &splitModeB[0], shuffledBufferSizeInBytes);

        // Report the BC7 mode-split transform so the caller can imply the format (BC7) and select the matching
        // unshuffle path. The curved variant is reported only when the space curve was actually applied above.
        bcnTransformId = shuffleParams.useSpaceCurve ?
            GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT_SC :
            GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT;
    }
    else
    {
        // BC1\3\4\5 path.
        //
        // Unlike the BC7 path (which calls the internal mode-split transform directly), this path drives the
        // shuffle through the public top-level API, GACL_ShuffleCompress_BCn(). That call shuffles AND compresses
        // the texture, trying the transforms in the requested group and returning the one that compressed best via
        // the in/out transform parameter. We keep that transform id so the GPU unshuffle pass can later select the
        // matching shader/transform.
        //
        // GACL_ShuffleCompress_BCn produces a compressed (single zstd frame) stream, whereas the GPU unshuffle pass
        // consumes the uncompressed shuffled bytes. We therefore decompress the stream here to recover them, which
        // mirrors the runtime load path (decompress on load, then reverse-transform on the GPU).
        uint32_t elementSize = 0;

        switch (baseFmt)
        {
        case DXGI_FORMAT_BC1_TYPELESS: elementSize = 8;  break;
        case DXGI_FORMAT_BC3_TYPELESS: elementSize = 16; break;
        case DXGI_FORMAT_BC4_TYPELESS: elementSize = 8;  break;
        case DXGI_FORMAT_BC5_TYPELESS: elementSize = 16; break;
        default:
            Utility::Printf(L"ERROR: Unsupported block compressed format for shuffle.\n");
            assert(false);
            return;
        }

        uint8_t* src = si.GetPixels();
        size_t srcSize = si.GetPixelsSize();

        // Copy original (unshuffled) data for later round trip validation.
        bc7TextureSizeInBytes = srcSize;
        originalBC7.resize(bc7TextureSizeInBytes);
        memcpy(originalBC7.data(), reinterpret_cast<void const*>(src), bc7TextureSizeInBytes);

        bcTextureWidthInPixels = si.GetMetadata().width;
        bcTextureHeightInPixels = si.GetMetadata().height;
        bcTextureSubresourceCount = si.GetMetadata().mipLevels;
        bcTextureFormat = siMeta.format;

        // GACL_ShuffleCompress_BCn applies the space curve internally, so we never curve at this layer. The only
        // thing we do here is a lightweight eligibility query (null src/dest) to decide whether curved transforms
        // should be offered at all: if the caller requested a curve and the layout is eligible, we point
        // CurvedTransforms.TextureData at the linear pixels and let the library curve them; otherwise we leave it
        // null so the library won't consider curved transforms.
        bool const useSC = shuffleParams.useSpaceCurve &&
            GACL_Shuffle_ApplySpaceCurve(nullptr, nullptr, srcSize, elementSize, bcTextureWidthInPixels, true);

        SHUFFLE_COMPRESS_PARAMETERS params = {};
        params.SizeInBytes = srcSize;
        params.Format = siMeta.format;
        params.TextureData = src;
        params.CompressSettings.Default.ZstdCompressionLevel = 0; // 0 => let GACL choose its default zstd settings
        params.CompressSettings.Default.TargetBlockSize = 0;
        params.CurvedTransforms.WidthInPixels = bcTextureWidthInPixels;
        params.CurvedTransforms.TextureData = useSC ? src : nullptr;
        params.CurvedTransforms.DataIsCurved = false;

        std::vector<uint8_t> compressed(srcSize);
        size_t compressedBytes = 0;

        // Let the library pick the best transform within the requested group.
        GACL_SHUFFLE_TRANSFORM transformId = requestedTransformGroup;

        HRESULT const scHr = GACL_ShuffleCompress_BCn(compressed.data(), &transformId, &compressedBytes, params);
        if (scHr != S_OK || compressedBytes == 0)
        {
            Utility::Printf(L"ERROR: GACL_ShuffleCompress_BCn failed (hr=0x%08X) for \"%ws\".\n", static_cast<unsigned>(scHr), inputFilePath.c_str());
            assert(false);
            return;
        }

        // Recover the uncompressed shuffled stream that the GPU unshuffle pass consumes.
        shuffledBuffer.resize(srcSize);
        size_t const decompressedBytes = ZSTD_decompress(shuffledBuffer.data(), srcSize, compressed.data(), compressedBytes);
        if (ZSTD_isError(decompressedBytes) || decompressedBytes != srcSize)
        {
            Utility::Printf(L"ERROR: Failed to decompress shuffled stream for \"%ws\".\n", inputFilePath.c_str());
            assert(false);
            return;
        }

        // Report the transform id the library used so the GPU pass can pick the matching unshuffle shader.
        bcnTransformId = transformId;
    }
}