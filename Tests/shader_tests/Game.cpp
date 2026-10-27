//--------------------------------------------------------------------------------------
// Game.cpp
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "pch.h"
#include "Game.h"
#include "Processing.h"
#include "SharedDefinitions.h"
#include "TestParameters.h"
#include "bcnex.h"
#include "bc5blockcode.h"
#include "streamheader.h"
#include <filesystem>

extern void ExitGame() noexcept;

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// BC7 unshuffle shaders are mode-specific.
static std::wstring s_perModeUnshufflePSONames[9] =
{
    L"UnshuffleCSMode0.cso",
    L"UnshuffleCSMode1.cso",
    L"UnshuffleCSMode2.cso",
    L"UnshuffleCSMode3.cso",
    L"UnshuffleCSMode4.cso",
    L"UnshuffleCSMode5.cso",
    L"UnshuffleCSMode6.cso",
    L"UnshuffleCSMode7.cso",
    L"UnshuffleCSMode8.cso"
};

struct ShuffleShaderInfo
{
    std::wstring fileName;
    std::wstring signatureName;
    std::wstring pixName;
    uint32_t blockSizes;
    uint32_t blocksPerThread;
    uint32_t threadGroupSize;
};



#ifdef NEWBC1345
// BC1\3\4\5 streams take a headerless micro-shuffle id (linear only), or the format's EX id
// (GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX - _BC5_EX), whose stream header names the micro-shuffle and space curve; the
// shaders take the transform ID as a shader constant.
// Headerless transform IDs by format and microShuffleId (1, 2); 0 = no such pattern.
constexpr uint32_t gaclTransformIDs[4][2]
{
    { GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224,      GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44 },
    { GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224,   GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664 },
    { GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116,      0 },
    { GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116,   GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266 },
};

// The per-format shaders, by DispatchShader: the EX shaders (UnshuffleBC1Ex.hlsl - UnshuffleBC7Ex.hlsl) by
// GBaseFormat, BC7's also taking BC7 Join, then the headerless shaders (UnshuffleBC1Headerless.hlsl -
// UnshuffleBC5Headerless.hlsl) by GBaseFormat from gHeaderlessShaders. A stream of n blocks takes
// ceil((n / T + n % T) / threadGroupSize) thread groups, T the shader's blocks per thread. The BC4 EX shader decodes the
// streams of at least 65536 blocks whose header needs its general parse 4 blocks per thread, the spare groups exiting;
// curve-only BC7_EX streams decode 4 blocks per thread, the spare threads exiting.
constexpr uint32_t gHeaderlessShaders = GBaseFormat::BC7Join + 1;
static const ShuffleShaderInfo gShaderInfo[] = {                                            // data processed per thread group
    {L"UnshuffleBC1Ex.cso", L"BCnExCSRootSignature", L"BC1 Unshuffle", 8,  2, 128},                 // 2KB
    {L"UnshuffleBC3Ex.cso", L"BCnExCSRootSignature", L"BC3 Unshuffle", 16, 4, 256},                 // 16KB
    {L"UnshuffleBC4Ex.cso", L"BCnExCSRootSignature", L"BC4 Unshuffle", 8,  2, 256},                 // 4KB
    {L"UnshuffleBC5Ex.cso", L"BCnExCSRootSignature", L"BC5 Unshuffle", 16, 4, 128},                 // 8KB
    {L"UnshuffleBC7Ex.cso", L"BCnExCSRootSignature", L"BC7 Join Unshuffle", 16, 1, 256},            // 4KB
    {L"UnshuffleBC1Headerless.cso", L"BCnExCSRootSignature", L"BC1 Unshuffle", 8,  2, 128},         // 2KB
    {L"UnshuffleBC3Headerless.cso", L"BCnExCSRootSignature", L"BC3 Unshuffle", 16, 4, 256},         // 16KB
    {L"UnshuffleBC4Headerless.cso", L"BCnExCSRootSignature", L"BC4 Unshuffle", 8,  4, 256},         // 8KB
    {L"UnshuffleBC5Headerless.cso", L"BCnExCSRootSignature", L"BC5 Unshuffle", 16, 4, 128},         // 8KB
};

// The unified shader (UnshuffleBCnEx.hlsl: every id, 256-thread groups), kept for benchmarking: -unified
static const ShuffleShaderInfo gUnifiedShaderInfo[] = {
    {L"UnshuffleBCnEx.cso", L"BCnExCSRootSignature", L"BC1 Unshuffle", 8,  2, 256},
    {L"UnshuffleBCnEx.cso", L"BCnExCSRootSignature", L"BC3 Unshuffle", 16, 4, 256},
    {L"UnshuffleBCnEx.cso", L"BCnExCSRootSignature", L"BC4 Unshuffle", 8,  4, 256},
    {L"UnshuffleBCnEx.cso", L"BCnExCSRootSignature", L"BC5 Unshuffle", 16, 4, 256},
    {L"UnshuffleBCnEx.cso", L"BCnExCSRootSignature", L"BC7 Join Unshuffle", 16, 1, 256},
    {L"UnshuffleBCnEx.cso", L"BCnExCSRootSignature", L"BC1 Unshuffle", 8,  2, 256},
    {L"UnshuffleBCnEx.cso", L"BCnExCSRootSignature", L"BC3 Unshuffle", 16, 4, 256},
    {L"UnshuffleBCnEx.cso", L"BCnExCSRootSignature", L"BC4 Unshuffle", 8,  4, 256},
    {L"UnshuffleBCnEx.cso", L"BCnExCSRootSignature", L"BC5 Unshuffle", 16, 4, 256},
};
static_assert(_countof(gShaderInfo) == gHeaderlessShaders + GBaseFormat::BC5 + 1 &&
    _countof(gUnifiedShaderInfo) == _countof(gShaderInfo), "an EX shader per GBaseFormat below BC7, then a headerless shader per BC1\\3\\4\\5");

// The shader DirectStorage dispatches a transform id with, and so its geometry (bytes per block, blocks per thread,
// threads per group): its format's headerless shader for a headerless BC1\3\4\5 id, else its format's EX shader (the
// EX ids GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX - _BC7_EX, and BC7 Join)
static uint32_t DispatchShader(uint32_t transformId)
{
    switch (transformId)
    {
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44:
        return gHeaderlessShaders + GBaseFormat::BC1;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664:
        return gHeaderlessShaders + GBaseFormat::BC3;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116:
        return gHeaderlessShaders + GBaseFormat::BC4;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266:
        return gHeaderlessShaders + GBaseFormat::BC5;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX:
        return GBaseFormat::BC1;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_EX:
        return GBaseFormat::BC3;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_EX:
        return GBaseFormat::BC4;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_EX:
        return GBaseFormat::BC5;
    default:
        return GBaseFormat::BC7Join;    // GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN, _BC7_EX
    }
}
#else // Preview #1 interim shaders
static const ShuffleShaderInfo gShaderInfo[] = {
    {L"UnshuffleBC1.cso", L"BC1CSRootSignature", L"BC1 Unshuffle", 8,  2},
    {L"UnshuffleBC3.cso", L"BC3CSRootSignature", L"BC3 Unshuffle", 16, 4},
    {L"UnshuffleBC4.cso", L"BC4CSRootSignature", L"BC4 Unshuffle", 8,  4},
    {L"UnshuffleBC5.cso", L"BC5CSRootSignature", L"BC5 Unshuffle", 16, 4},
#endif


Game::Game() noexcept(false) : 
    m_uavHeapOffsetInBytes(0),
    m_allTestsPassed(false),
    m_minHWSupportedWaveSize(0),
    m_maxHWSupportedWaveSize(0),
    m_forceWarp(false),
    m_useUnifiedShader(false),
    m_DHIncrementSize(0),
    m_UAVOutputBufferGpuHandle{},
    m_offsetIntoHistogram(0u),
    m_offsetIntoBinning(0u),
    m_offsetIntoHeader(0u)
    #if RUN_TEST_FROM_TEXTURE_IN_DISK
    , m_inputPath(L"")
    #endif  
{
    m_deviceResources = std::make_unique<DX::DeviceResources>();
}

// Initialize the Direct3D resources required to run.
void Game::Initialize(LPWSTR lpCmdLine)
{
    int numArgs = 0;
    LPWSTR* argList = CommandLineToArgvW(lpCmdLine, &numArgs);

#ifdef CAPTURE_PIX
    PIXLoadLatestWinPixGpuCapturerLibrary();
#endif
    
    #if RUN_TEST_FROM_TEXTURE_IN_DISK
    if (!lpCmdLine || lpCmdLine[0] == L'\0' || !argList || numArgs == 0)
    {
        throw std::runtime_error("Did not provide any argument for the sample.");
    }
    #endif

    for (int i = 0; i < numArgs;)
    {
        LPWSTR arg = argList[i];

        #if RUN_TEST_FROM_TEXTURE_IN_DISK
        if (wcscmp(arg, L"/path") == 0 || wcscmp(arg, L"-path") == 0)
        {
            m_inputPath = argList[i + 1];
            i += 2;
        }
        else
        #endif
        if (wcscmp(arg, L"/warp") == 0 || wcscmp(arg, L"-warp") == 0)
        {
            m_forceWarp = true;
            i += 1;
        }
        else if (wcscmp(arg, L"/unified") == 0 || wcscmp(arg, L"-unified") == 0)
        {
            m_useUnifiedShader = true;
            i += 1;
        }
        else 
        {
            i += 1; // skip unrecognized argument (e.g. exe name, GTest flags)
        }
    }
    LocalFree(argList);

    m_deviceResources->CreateDeviceResources(m_forceWarp);

    auto device = m_deviceResources->GetD3DDevice();
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
    DX::ThrowIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1)));
    m_minHWSupportedWaveSize = options1.WaveLaneCountMin;
    m_maxHWSupportedWaveSize = options1.WaveLaneCountMax;
}

// Deterministic per-block, per-byte value. Uses a multiplicative hash so
// every block has varied, reproducible non-zero bit patterns without relying
// on a random seed. Same blockIdx+byteIdx always produces the same byte.
static uint8_t BlockByteHash(uint32_t blockIdx, uint32_t byteIdx)
{
    uint32_t x = blockIdx * 2654435761u ^ byteIdx * 1234567891u ^ 1u;
    x ^= x >> 16;
    x *= 0x45d9f3bu;
    x ^= x >> 16;
    return static_cast<uint8_t>(x);
}

void Game::CreateBC7RawFromMetadata(uint32_t widthPixels, uint32_t heightPixels, std::vector<uint8_t>& rawBC7)
{
    uint32_t widthInBlocks  = (widthPixels  + 3) / 4;
    uint32_t heightInBlocks = (heightPixels + 3) / 4;
    uint32_t totalBlocks    = widthInBlocks * heightInBlocks;

    rawBC7.resize(static_cast<size_t>(totalBlocks) * BC7_BYTES_PER_BLOCK);

    for (uint32_t blockIdx = 0; blockIdx < totalBlocks; ++blockIdx)
    {
        uint32_t mode  = blockIdx % (BC7_MODES_COUNT);
        uint8_t* block = rawBC7.data() + blockIdx * BC7_BYTES_PER_BLOCK;

        if (mode == 8)
        {
            for (uint32_t b = 0; b < BC7_BYTES_PER_BLOCK; ++b)  block[b] = 0u;
            continue;
        }

        // Fill all 16 bytes with varied, reproducible values so that
        // endpoints, indices, P-bits and rotation fields are non-zero.
        for (uint32_t b = 0; b < BC7_BYTES_PER_BLOCK; ++b)
            block[b] = BlockByteHash(blockIdx, b);

        // Fix byte 0: the mode indicator occupies bits [0..mode] of byte 0
        // (bit `mode` = 1, bits below it = 0). Clear those bits from the
        // hash value and OR in the correct mode indicator.
        uint32_t modeMask = (mode < 7u) ? ((2u << mode) - 1u) : 0xFFu;
        block[0] = static_cast<uint8_t>((block[0] & ~modeMask) | (1u << mode));
    }
}

void Game::CreateBCnRawFromMetadata(uint32_t widthPixels, uint32_t heightPixels, uint32_t elementSize, std::vector<uint8_t>& rawBCn)
{
    uint32_t widthInBlocks = (widthPixels + 3) / 4;
    uint32_t heightInBlocks = (heightPixels + 3) / 4;
    uint32_t totalBlocks = widthInBlocks * heightInBlocks;

    rawBCn.resize(static_cast<size_t>(totalBlocks) * elementSize);

    for (uint32_t blockIdx = 0; blockIdx < totalBlocks; ++blockIdx)
    {
        uint8_t* block = rawBCn.data() + blockIdx * elementSize;

        // Fill all 8/16 bytes with varied, reproducible values so that
        // endpoints, fields are non-zero.
        for (uint32_t b = 0; b < elementSize; ++b)
            block[b] = BlockByteHash(blockIdx, b);
    }
}

HRESULT(*Shuffle_BC1345[])(uint8_t* dest, const uint8_t* src, size_t size, size_t version) {
    &Shuffle_BC1,
    &Shuffle_BC3,
    &Shuffle_BC4,
    &Shuffle_BC5
};

#ifdef NEWBC1345
// Copies blocks into triplets (p, p + d, p + 2d) spread over the stream, so BCnEx_Encode can make room for its header
// by removing duplicates: the first copy is sourced forward, the third backward. Large 8-byte block streams need
// several header blocks.
static void AddDuplicateBlocks(std::vector<uint8_t>& raw, uint32_t elementSize)
{
    const size_t n = raw.size() / elementSize;
    const size_t triplets = (std::min)(size_t(32), n / 8);
    if (triplets == 0)
    {
        return;
    }
    const size_t stride = n / triplets;
    const size_t d = stride / 3;
    for (size_t t = 0; t < triplets; t++)
    {
        const size_t p = t * stride + stride / 4;
        const uint8_t* block = raw.data() + p * elementSize;
        memcpy(raw.data() + (p + d) * elementSize, block, elementSize);
        memcpy(raw.data() + (p + 2 * d) * elementSize, block, elementSize);
    }
}

// Numbers every block in its first index bytes (the hashed blocks repeat in large streams: 2048x2048 has 31
// duplicates), then makes up to eight blocks spread over the stream flat (zero indices, equal endpoint bytes 0 and 1 in
// each 8-byte half: the cheapest to code), each unique, so a stream without duplicates can only make room for its EX
// header by coding blocks in it
static void AddCodableBlocks(std::vector<uint8_t>& raw, uint32_t elementSize)
{
    const size_t n = raw.size() / elementSize;
    for (size_t b = 0; b < n; b++)
    {
        uint8_t* block = raw.data() + b * elementSize;
        block[2] = uint8_t(b);
        block[3] = uint8_t(b >> 8);
        block[4] = uint8_t(b >> 16);
    }
    const size_t count = (std::min)(size_t(8), n / 8);
    for (size_t j = 0; j < count; j++)
    {
        uint8_t* block = raw.data() + (j * (n / count) + n / (2 * count)) * elementSize;
        memset(block, 0, elementSize);
        for (uint32_t h = 0; h < elementSize; h += 8)
        {
            block[h] = block[h + 1] = uint8_t(120 + j + h);
        }
    }
}

// Numbers every block in its first index bytes (as AddCodableBlocks: no duplicates), then copies block 10,000 to 22,000
// and block 15,001 to 18,001. BCnEx_Encode removes both earlier copies, sourced forward: gap and source distance codes
// of 27, 29, 25 and 25 bits. With 8-byte blocks the first pair's don't fit a one-block header (1 + 7.25 bytes); with
// both pairs the codes fill two blocks (1 + 13.75 bytes), ending at header bit 118, past FastParseHeader's 96-bit window
// (bits 12 to 108 here), so the decoders must take ParseHeader. With 16-byte blocks one pair fits a one-block header.
static void AddFarDuplicates(std::vector<uint8_t>& raw, uint32_t elementSize)
{
    const size_t n = raw.size() / elementSize;
    for (size_t b = 0; b < n; b++)
    {
            uint8_t* block = raw.data() + b * elementSize;
            block[2] = uint8_t(b);
            block[3] = uint8_t(b >> 8);
            block[4] = uint8_t(b >> 16);
    }
    memcpy(raw.data() + 22000 * elementSize, raw.data() + 10000 * elementSize, elementSize);
    memcpy(raw.data() + 18001 * elementSize, raw.data() + 15001 * elementSize, elementSize);
}

// Encodes a BC1\3\4\5 block stream (already curved when useSpaceCurve) as an EX stream of the format (BCnEx_TransformId),
// without the predictionOff bits of the default index prediction and, with unshuffled, with BCNEX_UNSHUFFLED
static bool EncodeBCnEx(std::vector<uint8_t>& dest, const uint8_t* src, size_t sizeInBytes, uint32_t transformId,
    uint32_t elementSize, uint32_t widthPixels, bool useSpaceCurve, uint8_t predictionOff = 0, bool unshuffled = false)
{
    const size_t curveTiles = useSpaceCurve ? ((widthPixels + 3) / 4) / (elementSize == 16 ? 32 : 64) : 0;
    const uint8_t mask = uint8_t((BCnEx_DefaultPrediction(uint8_t(transformId)) & ~predictionOff) | (unshuffled ? BCNEX_UNSHUFFLED : 0));
    const int prediction = predictionOff || unshuffled ? int(mask) : -1;
    if ((useSpaceCurve && (curveTiles == 0 || curveTiles > 255)) ||
        !BCnEx_Encode(src, sizeInBytes, uint8_t(transformId), uint8_t(curveTiles), dest, nullptr, prediction))
    {
        std::cout << "BCnEx_Encode failed: transform " << transformId << ", " << sizeInBytes << " bytes" << std::endl;
        return false;
    }
    return true;
}

// Encodes a curved block stream as a curve-only EX stream (header parameter 2 BCNEX_UNSHUFFLED) of transformId's format
// (GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN for BC7: a BC7_EX stream), and checks that its header made room for itself by
// removing duplicates or, with coded, by coding blocks alone (the header blocks are the removed and coded blocks), and
// that BCnEx_Decode gives back raw, the linear stream
static bool EncodeBCnExCurveOnly(std::vector<uint8_t>& dest, const std::vector<uint8_t>& raw, const uint8_t* curved,
    uint32_t transformId, uint32_t elementSize, uint32_t widthPixels, bool coded)
{
    const uint8_t curveTiles = uint8_t(((widthPixels + 3) / 4) / (elementSize == 16 ? 32 : 64));
    size_t headerBlocks = 0, codedBlocks = 0;
    std::vector<uint8_t> decoded;
    if (!BCnEx_EncodeCurveOnly(curved, raw.size(), uint8_t(transformId), curveTiles, dest, &headerBlocks, &codedBlocks) ||
        dest.size() != raw.size() || dest[0] >> 3 != 3 || dest[1] != curveTiles || dest[2] != BCNEX_UNSHUFFLED ||
        (coded ? codedBlocks == 0 || codedBlocks != headerBlocks : codedBlocks == headerBlocks) ||
        !BCnEx_Decode(dest.data(), dest.size(), BCnEx_TransformId(uint8_t(transformId)), decoded) || decoded != raw)
    {
        std::cout << "Can't make or decode a curve-only EX stream: transform " << transformId << ", " << raw.size() <<
            " bytes, " << headerBlocks << " header blocks, " << codedBlocks << " coded" << std::endl;
        return false;
    }
    return true;
}

// Writes an EX stream of the format (linear, default index prediction) of the raw block stream's size
// whose header codes blocks of its own choosing, as no encoder would (a coded block must save room, and the longest
// bodies don't): the costliest block the format's code allows (every byte in its costliest bucket), starting at bit 31
// of a word so that its last bit is as many words past its first as a body can reach, after a block whose body cost
// puts it there and before one of the cheapest blocks, then more of the cheapest blocks until the header fits. The
// coded blocks are spread over the stream (several decoder thread groups), and the payload is the first raw blocks,
// shuffled. The bodies are blocks as the header codes them (index predicted); BCnEx_Decode gives the expected texture.
// With overrun, the first block's body cost instead puts the header's last bit one past its k blocks (in the payload's
// first byte), so that the header is valid but for its fit, and StreamHeader_Parse rejects it.
// With many, the header also removes two duplicates, sourced from blocks above coded blocks (in a large stream, the
// first decoder thread group is below every coded block and takes the decoders' short-header path, the others their
// general one), and codes at least 40 blocks: more positions than a wave of 32 lanes reads at once.
static bool EncodeBCnExLongCoded(std::vector<uint8_t>& dest, const std::vector<uint8_t>& raw, uint32_t transformId,
    uint32_t elementSize, GBaseFormat format, uint32_t microShuffleId, bool overrun, bool many)
{
    const size_t bs = elementSize, n = raw.size() / bs;
    const uint32_t* lengths = format == GBaseFormat::BC1 ? kBC1BlockCode : kBC5BlockCode;
    uint8_t headerCode = 0;
    while (headerCode < 8 && kStreamHeaderTransforms[headerCode] != transformId)
    {
        ++headerCode;
    }

    // per byte, each bucket's canonical code (streamheader.h: in (length, bucket) order, MSB first) and cost (the code
    // length plus the bits below the bucket's leading one)
    uint32_t len[16][9], code[16][9], cost[16][9], cheapest[16], costliest[16];
    uint32_t minCost = 0, maxCost = 0;
    for (size_t p = 0; p < bs; ++p)
    {
        cheapest[p] = costliest[p] = 0;
        for (uint32_t b = 0; b < 9; ++b)
        {
            len[p][b] = ((lengths[p] >> (3 * b)) & 7) + 1;
            cost[p][b] = len[p][b] + (b >= 2 ? b - 1 : 0);
            cheapest[p] = cost[p][b] < cost[p][cheapest[p]] ? b : cheapest[p];
            costliest[p] = cost[p][b] >= cost[p][costliest[p]] ? b : costliest[p];
        }
        uint32_t next = 0;
        for (uint32_t l = 1; l <= 8; ++l, next <<= 1)
        {
            for (uint32_t b = 0; b < 9; ++b)
            {
                if (len[p][b] == l)
                {
                    code[p][b] = next++;
                }
            }
        }
        minCost += cost[p][cheapest[p]];
        maxCost += cost[p][costliest[p]];
    }

    // reachable[p * 256 + c]: bytes p.. can cost exactly c bits (at most 16 x 15)
    std::vector<uint8_t> reachable((bs + 1) * 256, 0);
    reachable[bs * 256] = 1;
    for (size_t p = bs; p-- > 0; )
    {
        for (uint32_t c = 0; c < 256; ++c)
        {
            for (uint32_t b = 0; b < 9 && !reachable[p * 256 + c]; ++b)
            {
                reachable[p * 256 + c] = c >= cost[p][b] && reachable[(p + 1) * 256 + c - cost[p][b]];
            }
        }
    }

    // byte values from buckets: the bits below the leading one hashed, and the last one set in the costliest block
    auto makeBlock = [&](const uint32_t* buckets, uint32_t salt, bool lastBitSet, uint8_t* block)
    {
        for (size_t p = 0; p < bs; ++p)
        {
            const uint32_t b = buckets[p];
            uint32_t v = b < 2 ? b : (1u << (b - 1)) | (BlockByteHash(salt, uint32_t(p)) & ((1u << (b - 1)) - 1));
            if (lastBitSet && p + 1 == bs && b >= 2)
            {
                v |= 1u << (b - 2);
            }
            block[p] = uint8_t(v);
        }
    };
    auto expGolombBits = [](size_t v)
    {
        uint32_t b = 0;
        while (((v + 1) >> (b + 1)) != 0)
        {
            ++b;
        }
        return 2 * b + 1;
    };

    // k header blocks: the removed duplicates (with many), then the coded blocks: the filler, the costliest block, then
    // more of the cheapest
    uint32_t posBits = 1;
    while ((size_t(1) << posBits) < n)
    {
        ++posBits;
    }
    std::vector<size_t> removed, sources;
    if (many)
    {
        removed = { 1, n / 4 };
        sources = { n / 2 + 2, n / 8 + 1 };
    }
    auto gap = [&](size_t j)
    {
        return j == 0 ? removed[0] : removed[j] - removed[j - 1] - 1;
    };
    auto distance = [&](size_t j)
    {
        return sources[j] > removed[j] ? 2 * (sources[j] - removed[j] - 1) : 2 * (removed[j] - sources[j] - 1) + 1;
    };
    size_t entryBits = 0;
    for (size_t j = 0; j < removed.size(); ++j)
    {
        entryBits += expGolombBits(gap(j)) + expGolombBits(distance(j));
    }
    size_t codedCount = many ? 40 : 3, k = codedCount + removed.size(), fillerCost = 0;
    for (; k < n && k <= BCNEX_MAX_HEADER_BLOCKS; ++codedCount, ++k)
    {
        const size_t bodies = 8 * GACL_STREAMHEADER_FIXED_BYTES + expGolombBits(k - 1) + expGolombBits(codedCount) + entryBits + codedCount * posBits;
        if (overrun)
        {
            const size_t rest = bodies + maxCost + (codedCount - 2) * minCost;
            fillerCost = rest < k * bs * 8 + 1 ? k * bs * 8 + 1 - rest : 0;
            if (fillerCost >= minCost && fillerCost <= maxCost && reachable[fillerCost])
            {
                break;
            }
            continue;
        }
        fillerCost = minCost;
        while (fillerCost <= maxCost && ((bodies + fillerCost) % 32 != 31 || !reachable[fillerCost]))
        {
            ++fillerCost;
        }
        if (fillerCost <= maxCost && bodies + fillerCost + maxCost + (codedCount - 2) * minCost <= k * bs * 8)
        {
            break;
        }
    }
    if (k >= n || k > BCNEX_MAX_HEADER_BLOCKS || n < 8)
    {
        std::cout << "No room for a long coded block header in " << n << " blocks" << std::endl;
        return false;
    }

    std::vector<size_t> indices = { n / 8, n / 2, n / 2 + 1 };
    for (size_t j = 3; j < codedCount; ++j)
    {
        indices.push_back(n - codedCount + j);
    }
    std::vector<uint8_t> blocks(codedCount * bs);
    std::vector<uint32_t> buckets(codedCount * bs);
    for (size_t p = 0, c = fillerCost; p < bs; ++p)
    {
        uint32_t b = 0;
        while (c < cost[p][b] || !reachable[(p + 1) * 256 + c - cost[p][b]])
        {
            ++b;
        }
        buckets[p] = b;
        c -= cost[p][b];
        buckets[bs + p] = costliest[p];
        for (size_t j = 2; j < codedCount; ++j)
        {
            buckets[j * bs + p] = cheapest[p];
        }
    }
    for (size_t j = 0; j < codedCount; ++j)
    {
        makeBlock(&buckets[j * bs], uint32_t(indices[j]), j == 1, &blocks[j * bs]);
    }

    // the header, LSB first after its fixed bytes (no parameters): k - 1, the coded block count, the removed
    // duplicates (gap and distance codes), the coded positions, then the bodies, each byte's code then its bits below
    // the leading one. The payload is written first, so that an overrunning header's last bit replaces its first bit.
    dest.assign(raw.size(), 0);
    if (FAILED(Shuffle_BC1345[format](dest.data() + k * bs, raw.data(), (n - k) * bs, microShuffleId)))
    {
        std::cout << "Can't shuffle the payload of a long coded block header" << std::endl;
        return false;
    }
    dest[0] = headerCode;
    size_t bitPos = 8 * GACL_STREAMHEADER_FIXED_BYTES, longStart = 0;
    auto put = [&](uint64_t v, uint32_t bits)
    {
        for (uint32_t i = 0; i < bits; ++i, ++bitPos)
        {
            const uint8_t bit = uint8_t(1u << (bitPos & 7));
            dest[bitPos >> 3] = uint8_t(((v >> i) & 1) ? dest[bitPos >> 3] | bit : dest[bitPos >> 3] & ~bit);
        }
    };
    auto putExpGolomb = [&](size_t v)
    {
        const uint32_t b = (expGolombBits(v) - 1) / 2;
        put(0, b);
        put(1, 1);
        put(v + 1, b);
    };
    putExpGolomb(k - 1);
    putExpGolomb(codedCount);
    for (size_t j = 0; j < removed.size(); ++j)
    {
        putExpGolomb(gap(j));
        putExpGolomb(distance(j));
    }
    for (size_t i : indices)
    {
        put(i, posBits);
    }
    for (size_t j = 0; j < codedCount; ++j)
    {
        longStart = j == 1 ? bitPos : longStart;
        for (size_t p = 0; p < bs; ++p)
        {
            const uint32_t b = buckets[j * bs + p];
            for (uint32_t i = len[p][b]; i-- > 0; )
            {
                put(code[p][b] >> i, 1);
            }
            put(blocks[j * bs + p], b >= 2 ? b - 1 : 0);
        }
    }
    if (overrun ? bitPos != k * bs * 8 + 1 :
        (bitPos > k * bs * 8 || longStart % 32 != 31 || bitPos - longStart != size_t(maxCost) + (codedCount - 2) * minCost))
    {
        std::cout << "Can't write a long coded block header" << std::endl;
        return false;
    }

    // the parse must find these removed duplicates and coded blocks (with overrun, reject the header: it is valid but
    // for its fit, as the fit mutants of the shader tests show)
    StreamHeaderInfo info;
    const StreamHeaderBlockCode blockCode = { lengths, false };
    const bool parsed = StreamHeader_Parse(dest.data(), dest.size(), bs, info, &blockCode);
    if (overrun ? parsed : (!parsed || info.HeaderBlocks != k || info.Removed != removed || info.Sources != sources ||
        info.Coded != indices || info.CodedBlocks != blocks))
    {
        std::cout << "StreamHeader_Parse doesn't read the long coded block header back" << std::endl;
        return false;
    }
    return true;
}

// BC7 Join through the unified shader: a headerless GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN stream, or a BC7_EX stream
// whose header carries the statics masks, the space curve, the byte remap, removed duplicates and header-coded blocks.
// A truncated stream must decode as the CPU decodes it, or leave the output untouched (zero) when the CPU rejects it.
// With curveOnly, a curve-only BC7_EX stream instead. With a transformOverride, the CPU decode under that id
// (BCnEx_Decode) is the expected output, zero when it rejects the stream; with escape (the header's escape bit set), zero.
bool Game::RunBC7JoinTest(GTestParameters const& params)
{
    std::vector<uint8_t> bc7TextureRaw;
    CreateBC7RawFromMetadata(params.width, params.height, bc7TextureRaw);
    const size_t sizeInBytes = bc7TextureRaw.size();
    const size_t blockCount = sizeInBytes / BC7_BYTES_PER_BLOCK;

    if (params.BC7Join.curveOnly)
    {
        // The blocks are opaque to a curve-only stream: the header removes the texture's zero Mode 8 blocks as
        // duplicates or, with coded, codes blocks (AddCodableBlocks makes every block unique, and some codable). The
        // decoder reads the blocks as BC5 blocks, 4 per thread, so most of the thread groups dispatched here (one per
        // 256 blocks, as DirectStorage dispatches BC7_EX streams) are spare.
        if (params.BC7Join.coded)
        {
            AddCodableBlocks(bc7TextureRaw, BC7_BYTES_PER_BLOCK);
        }
        std::vector<uint8_t> curvedData(sizeInBytes), shuffledBuffer;
        if (!params.useSpaceCurve ||
            !GACL_Shuffle_ApplySpaceCurve(curvedData.data(), bc7TextureRaw.data(), sizeInBytes, BC7_BYTES_PER_BLOCK, params.width, true) ||
            !EncodeBCnExCurveOnly(shuffledBuffer, bc7TextureRaw, curvedData.data(), GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN,
                BC7_BYTES_PER_BLOCK, params.width, params.BC7Join.coded))
        {
            return false;
        }
        if (params.transformOverride != 0 &&
            !BCnEx_Decode(shuffledBuffer.data(), shuffledBuffer.size(), uint8_t(params.transformOverride), bc7TextureRaw))
        {
            bc7TextureRaw.assign(sizeInBytes, 0);
        }
        if (params.escape)
        {
            shuffledBuffer[0] |= GACL_STREAMHEADER_ESCAPE;
            std::vector<uint8_t> decoded;
            if (BCnEx_Decode(shuffledBuffer.data(), shuffledBuffer.size(), GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX, decoded))
            {
                std::cout << "BCnEx_Decode takes a header with its escape bit set" << std::endl;
                return false;
            }
            bc7TextureRaw.assign(sizeInBytes, 0);
        }

        CreateBC1345UnshufflePassD3D12Resources(shuffledBuffer, sizeInBytes);

        GTestParameters runParams = params;
        runParams.BC7Join.transformId = GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX;
        runParams.BC7Join.streamSize = uint32_t(sizeInBytes);

        UNSHUFFLE_PER_TEST_STATISTICS testStats = {};
        testStats.testName = params.testName;
        testStats.textureWidthPixels = static_cast<uint32_t>(params.width);

        TestGpuUnshuffling(runParams, bc7TextureRaw, testStats, sizeInBytes);

        return testStats.testPassed;
    }

    if (params.BC7Join.mode5)
    {
        // Modes 0-4 / 6 / 7 of three rounds of the modes in four become Mode 5: whole waves of Mode 5 blocks after the
        // decoder's mode sort, and mixed waves at the mode boundaries. Some hashed blocks differ in their mode bits
        // alone, so every block is numbered in its last three bytes (index bits in every mode) to keep it unique: only
        // the zero Mode 8 blocks repeat.
        for (size_t b = 0; b < blockCount; ++b)
        {
            const size_t mode = b % BC7_MODES_COUNT;
            if (mode == 8)
            {
                continue;
            }
            uint8_t* block = bc7TextureRaw.data() + b * BC7_BYTES_PER_BLOCK;
            if (mode != 5 && (b / BC7_MODES_COUNT) % 4 != 0)
            {
                block[0] = uint8_t((block[0] & ~0x3Fu) | 0x20u);
            }
            block[13] = uint8_t(b);
            block[14] = uint8_t(b >> 8);
            block[15] = uint8_t(b >> 16);
        }
    }

    if (params.BC7Join.coded || params.BC7Join.farDuplicate)
    {
        // The Mode 8 (invalid) blocks are all zero: make them unique, so there are no duplicates to remove
        for (size_t b = 8; b < blockCount; b += BC7_MODES_COUNT)
        {
            uint8_t* block = bc7TextureRaw.data() + b * BC7_BYTES_PER_BLOCK;
            for (uint32_t i = 1; i < BC7_BYTES_PER_BLOCK; ++i)
            {
                block[i] = BlockByteHash(uint32_t(b), i);
            }
        }
    }

    if (params.BC7Join.farDuplicate)
    {
        // One duplicate pair, far apart and far from block 0: block n / 2 (12,000 at most) copied 12,000 blocks on (or to
        // the last block). The encoder removes the earlier copy, sourcing it from the later, so the gap and the source
        // distance both have long codes in the one-block header (32 to 56 bits without the space curve), crossing its
        // 32-bit words.
        const size_t r = std::min<size_t>(blockCount / 2, 12000);
        const size_t s = std::min<size_t>(blockCount - 1, r + 12000);
        memcpy(bc7TextureRaw.data() + s * BC7_BYTES_PER_BLOCK, bc7TextureRaw.data() + r * BC7_BYTES_PER_BLOCK, BC7_BYTES_PER_BLOCK);
    }

    if (params.BC7Join.coded)
    {
        // Low-entropy blocks, which the header can code
        for (uint32_t i = 0; i < 16; ++i)
        {
            const size_t b = (2 * i + 1) * blockCount / 32;
            const uint32_t mode = i & 7u;
            uint8_t* block = bc7TextureRaw.data() + b * BC7_BYTES_PER_BLOCK;
            for (uint32_t j = 0; j < BC7_BYTES_PER_BLOCK; ++j)
            {
                block[j] = BlockByteHash(uint32_t(b), j) & 1u;
            }
            block[0] = uint8_t((block[0] << (mode + 1)) | (1u << mode));
        }
    }

    BC7TextureMetrics metrics = {};
    metrics.JoinRemap = params.BC7Join.remap ? 1 : 0;
    if (params.BC7Join.masks == 1)
    {
        metrics.Statics = 0x2d;
    }
    else if (params.BC7Join.masks == 2)
    {
        const uint8_t modeMasks[8] = { 0x00, 0x15, 0x2a, 0x3f, 0x0b, 0x34, 0x1c, 0xe3 };
        metrics.JoinModeMasks = true;
        memcpy(metrics.JoinModeMask, modeMasks, sizeof(modeMasks));
    }
    else if (params.BC7Join.masks == 3)
    {
        // Modes 5 / 6 with their alpha flags (bits 6-7) set, as for opaque content, and Mode 5's color channel flags
        // unlike Mode 6's
        const uint8_t modeMasks[8] = { 0x00, 0x15, 0x2a, 0x3f, 0x0b, 0xc1, 0xdc, 0xe3 };
        metrics.JoinModeMasks = true;
        memcpy(metrics.JoinModeMask, modeMasks, sizeof(modeMasks));
    }
    else if (params.BC7Join.masks == 4)
    {
        // opaque content: Modes 4-7 with only their alpha flags set, which interleave their color fields in descending order
        const uint8_t modeMasks[8] = { 0x00, 0x15, 0x2a, 0x3f, 0xc0, 0xc0, 0xc0, 0xc0 };
        metrics.JoinModeMasks = true;
        memcpy(metrics.JoinModeMask, modeMasks, sizeof(modeMasks));
    }
    else if (params.BC7Join.masks == 5)
    {
        metrics.Statics = 0xff;
    }

    std::vector<uint8_t> curvedData;
    const uint8_t* dataToShuffle = bc7TextureRaw.data();
    uint8_t curveWidthTiles = 0;
    if (params.useSpaceCurve)
    {
        curvedData.resize(sizeInBytes);
        if (!GACL_Shuffle_ApplySpaceCurve(curvedData.data(), bc7TextureRaw.data(), sizeInBytes, BC7_BYTES_PER_BLOCK, params.width, true))
        {
            std::cout << "The space curve doesn't apply to " << params.width << "x" << params.height << std::endl;
            return false;
        }
        dataToShuffle = curvedData.data();
        curveWidthTiles = uint8_t(((params.width + 3) / 4) / 32);
    }

    std::vector<uint8_t> shuffledBuffer;
    uint8_t transformId = 0;
    size_t coded = 0;
    if (!BC7_JoinStream_Encode(dataToShuffle, sizeInBytes, metrics, curveWidthTiles, shuffledBuffer, &transformId, &coded))
    {
        std::cout << "BC7_JoinStream_Encode failed: " << sizeInBytes << " bytes" << std::endl;
        return false;
    }
    const bool needsHeader = params.BC7Join.masks != 0 || params.useSpaceCurve || params.BC7Join.remap;
    if (shuffledBuffer.size() != sizeInBytes ||
        transformId != (needsHeader ? GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX : GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN) ||
        (params.BC7Join.coded && coded == 0))
    {
        std::cout << "Unexpected BC7 Join stream: transform " << uint32_t(transformId) << ", " << shuffledBuffer.size() <<
            " bytes, " << coded << " coded blocks" << std::endl;
        return false;
    }

    if (params.BC7Join.farDuplicate)
    {
        // the shape of the decoder's one-block BC7 Join parse
        StreamHeaderInfo info;
        std::vector<uint8_t> payload;
        if (!StreamHeader_Decode(shuffledBuffer.data(), shuffledBuffer.size(), BC7_BYTES_PER_BLOCK, info, payload, nullptr) ||
            info.HeaderBlocks != 1 || info.Removed.size() != 1 || !info.Coded.empty())
        {
            std::cout << "Expected a BC7 Join header of one block, holding one removed duplicate" << std::endl;
            return false;
        }
    }

    if (params.BC7Join.spareHeaderBlock)
    {
        // Rewrite the one-block header into two blocks, removing a later copy of its removed block from the same source
        StreamHeaderInfo info;
        std::vector<uint8_t> payload;
        if (!StreamHeader_Decode(shuffledBuffer.data(), shuffledBuffer.size(), BC7_BYTES_PER_BLOCK, info, payload, nullptr) ||
            info.HeaderBlocks != 1 || info.Removed.size() != 1)
        {
            std::cout << "Expected a BC7 Join header of one block, holding one removed duplicate" << std::endl;
            return false;
        }
        const size_t r0 = info.Removed[0], s0 = info.Sources[0];
        size_t r1 = r0 + 1;
        while (r1 < blockCount && (r1 == s0 || memcmp(payload.data() + r1 * BC7_BYTES_PER_BLOCK,
            payload.data() + s0 * BC7_BYTES_PER_BLOCK, BC7_BYTES_PER_BLOCK) != 0))
        {
            ++r1;
        }

        // the codes LSB first, as StreamHeader_Encode writes them: k - 1, the coded block count, then the removed
        // blocks' gaps and source distances
        std::vector<uint8_t> codes;
        size_t bitCount = 0;
        auto putExpGolomb = [&](size_t v)
        {
            unsigned b = 0;
            while (((v + 1) >> (b + 1)) != 0)
            {
                ++b;
            }
            const uint64_t code = (uint64_t(v + 1) << (b + 1)) | (1ull << b);
            for (unsigned i = 0; i < 2 * b + 1; ++i, ++bitCount)
            {
                if ((bitCount & 7) == 0)
                {
                    codes.push_back(0);
                }
                codes.back() |= uint8_t(((code >> i) & 1) << (bitCount & 7));
            }
        };
        auto sourceDistance = [](size_t r, size_t s) { return s > r ? 2 * (s - r - 1) : 2 * (r - s - 1) + 1; };
        putExpGolomb(1);
        putExpGolomb(0);
        putExpGolomb(r0);
        putExpGolomb(sourceDistance(r0, s0));
        putExpGolomb(r1 - r0 - 1);
        putExpGolomb(sourceDistance(r1, s0));

        size_t headSize = GACL_STREAMHEADER_FIXED_BYTES;
        for (const StreamHeaderParam& p : info.Params)
        {
            headSize += p.Value.size();
        }
        std::vector<uint8_t> rewritten(sizeInBytes, 0);
        StreamHeaderInfo check;
        if (r1 < blockCount && headSize + codes.size() <= 2 * BC7_BYTES_PER_BLOCK)
        {
            memcpy(rewritten.data(), shuffledBuffer.data(), headSize);
            memcpy(rewritten.data() + headSize, codes.data(), codes.size());
            size_t o = 2;
            for (size_t b = 0; b < blockCount; ++b)
            {
                if (b != r0 && b != r1)
                {
                    memcpy(rewritten.data() + o++ * BC7_BYTES_PER_BLOCK, payload.data() + b * BC7_BYTES_PER_BLOCK, BC7_BYTES_PER_BLOCK);
                }
            }
        }
        if (r1 == blockCount || !StreamHeader_Parse(rewritten.data(), rewritten.size(), BC7_BYTES_PER_BLOCK, check, nullptr) ||
            check.HeaderBlocks != 2 || check.Removed.size() != 2)
        {
            std::cout << "Can't rewrite the BC7 Join header into two blocks" << std::endl;
            return false;
        }
        shuffledBuffer.swap(rewritten);
    }

    // the CPU decoder is the reference for truncated streams, so check that it reverses complete ones
    std::vector<uint8_t> decoded;
    if (!BC7_JoinStream_Decode(shuffledBuffer.data(), shuffledBuffer.size(), transformId, decoded) || decoded != bc7TextureRaw)
    {
        std::cout << "BC7_JoinStream_Decode doesn't reverse the stream" << std::endl;
        return false;
    }

    const size_t streamSize = sizeInBytes - (params.BC7Join.truncation == 1 ? BC7_BYTES_PER_BLOCK : params.BC7Join.truncation == 2 ? 1 : 0);
    shuffledBuffer.resize(streamSize);
    std::vector<uint8_t> expected(sizeInBytes, 0);
    if (params.escape)
    {
        shuffledBuffer[0] |= GACL_STREAMHEADER_ESCAPE;
        if (BC7_JoinStream_Decode(shuffledBuffer.data(), streamSize, transformId, decoded))
        {
            std::cout << "BC7_JoinStream_Decode takes a header with its escape bit set" << std::endl;
            return false;
        }
    }
    else if (params.transformOverride != 0)
    {
        if (BCnEx_Decode(shuffledBuffer.data(), streamSize, uint8_t(params.transformOverride), decoded) && decoded.size() <= sizeInBytes)
        {
            std::copy(decoded.begin(), decoded.end(), expected.begin());
        }
    }
    else if (streamSize == sizeInBytes)
    {
        expected = bc7TextureRaw;
    }
    else if (BC7_JoinStream_Decode(shuffledBuffer.data(), streamSize, transformId, decoded) && decoded.size() <= sizeInBytes)
    {
        std::copy(decoded.begin(), decoded.end(), expected.begin());
    }

#if DUMP_SHUFFLED_BUFFER_TO_DISK
    std::stringstream dumpFileName;
    dumpFileName << params.testName << ".bin";
    std::ofstream(dumpFileName.str(), std::ios::binary).write(reinterpret_cast<const char*>(shuffledBuffer.data()), shuffledBuffer.size());
#endif

    // the output is the texture size, so writes past a truncated stream show
    CreateBC1345UnshufflePassD3D12Resources(shuffledBuffer, sizeInBytes);

    GTestParameters runParams = params;
    runParams.BC7Join.transformId = transformId;
    runParams.BC7Join.streamSize = uint32_t(streamSize);

    UNSHUFFLE_PER_TEST_STATISTICS testStats = {};
    testStats.testName = params.testName;
    testStats.textureWidthPixels = static_cast<uint32_t>(params.width);

    TestGpuUnshuffling(runParams, expected, testStats, sizeInBytes);

    return testStats.testPassed;
}
#endif

#if RUN_TEST_FROM_TEXTURE_IN_DISK
extern const char* ToString(DXGI_FORMAT Format);
extern DXGI_FORMAT GetBaseFormat(DXGI_FORMAT Format);

bool Game::RunAllPermutationsForLoadedTexture()
{
    // Load dds texture contents from m_inputPath and fills out the out params widthPixels, heightPixels, format, bcTextureRaw
    std::vector<uint8_t> bcTextureRaw;
    uint32_t widthPixels, heightPixels;
    DXGI_FORMAT format;
    LoadTextureFromDisk(widthPixels, heightPixels, format, bcTextureRaw);

    size_t bcSizeInBytes = bcTextureRaw.size();
    DXGI_FORMAT baseFormat = GetBaseFormat(format);

    std::wcout << L"\n-- Loaded BC (raw) " << ToString(format) << " texture of resolution " << widthPixels << L"x" << heightPixels << std::endl;

    bool allPermutationPassed = true;

    if (baseFormat == DXGI_FORMAT_BC7_TYPELESS)
    {
        // Assume this scope is a for loop, going over every permutation
        for (uint32_t chunkIndex = 0; chunkIndex < std::size(g_testChunkSizes); ++chunkIndex)
        {
            for (uint32_t stratIndex = 0; stratIndex < std::size(g_endpointOrderStrategies); ++stratIndex)
            {
                for (uint32_t scIndex = 0; scIndex < std::size(g_useSpaceCurve); ++scIndex)
                {
                    // only run curved variants when the texture dimensions are space-curve eligible
                    if (g_useSpaceCurve[scIndex] && !GACL_Shuffle_ApplySpaceCurve(nullptr, nullptr, bcSizeInBytes, 16, widthPixels, true))
                    {
                        continue;
                    }

                    for (uint32_t patternSetIndex = 0; patternSetIndex < std::size(g_patternSets); ++patternSetIndex)
                    for (uint32_t ipIndex = 0; ipIndex < std::size(g_useIndexPrediction); ++ipIndex)
                    for (uint32_t slIndex = 0; slIndex < std::size(g_sectionLayouts); ++slIndex)
                    {
                        if (g_sectionLayouts[slIndex] && !g_useIndexPrediction[ipIndex])
                        {
                            continue;
                        }

                        BC7ModeSplitShufflePattern Patterns[8];
                        memcpy(Patterns, &g_patternSets[patternSetIndex][0], sizeof(Patterns));

                        // GACL Shuffle parameters for this run
                        ShuffleParameters shuffleParams = {};
                        shuffleParams.useSpaceCurve = static_cast<bool>(g_useSpaceCurve[scIndex]);
                        shuffleParams.useIndexPrediction = static_cast<bool>(g_useIndexPrediction[ipIndex]);
                        shuffleParams.sectionLayout = g_sectionLayouts[slIndex];
                        shuffleParams.endpointOrderStrategy = g_endpointOrderStrategies[stratIndex];
                        shuffleParams.ChunkSize = g_testChunkSizes[chunkIndex];
                        shuffleParams.Patterns = Patterns;


                        // Will hold the data for the buffer shuffled in ProcessTexture
                        std::vector<uint8_t> shuffledBuffer;

                        std::stringstream testName;
                        testName << "Res" << std::to_string(widthPixels) << "x" << std::to_string(heightPixels) <<
                            "_ChunkSize" << gs_chunkSizesName[chunkIndex] <<
                            "_Strat" << std::to_string(shuffleParams.endpointOrderStrategy) <<
                            "_SC" << (shuffleParams.useSpaceCurve ? "True" : "False") <<
                            "_PatternSet" << std::to_string(patternSetIndex) <<
                            "_IP" << (shuffleParams.useIndexPrediction ? "True" : "False") <<
                            "_SL" << std::to_string(shuffleParams.sectionLayout);

                        // Call that interfaces with gaclLib to shuffle the texture.
                        ShuffleBC7Texture(
                            shuffledBuffer,
                            bcTextureRaw.data(),
                            bcTextureRaw.size(),
                            widthPixels,
                            shuffleParams);

#if DUMP_SHUFFLED_BUFFER_TO_DISK
                        std::stringstream dumpFileName;
                        dumpFileName << testName.str() << ".bin";
                        std::ofstream(dumpFileName.str(), std::ios::binary).write(reinterpret_cast<const char*>(shuffledBuffer.data()), shuffledBuffer.size());
#endif

                        // Create all d3d12 resources for this test
                        CreateBC7UnshufflePassD3D12Resources(shuffledBuffer, bcTextureRaw.size());

                        UNSHUFFLE_PER_TEST_STATISTICS testStats = {};
                        testStats.testName = testName.str();
                        testStats.textureWidthPixels = static_cast<uint32_t>(widthPixels);

                        GTestParameters params = {
                            GBaseFormat::BC7,
                            testName.str(),
                            widthPixels,
                            heightPixels,
                            shuffleParams.useSpaceCurve,
                        };
                        params.BC7.ChunkSize = shuffleParams.ChunkSize;
                        params.BC7.endpointOrderStrategy = shuffleParams.endpointOrderStrategy;
                        params.BC7.patternIndex = patternSetIndex;
                        params.BC7.useIndexPrediction = shuffleParams.useIndexPrediction;
                        params.BC7.sectionLayout = shuffleParams.sectionLayout;


                        TestGpuUnshuffling(params, bcTextureRaw, testStats, bcSizeInBytes);

                        std::cout << "-- Finished test " << testName.str() << " -- Passed: " <<
                            (testStats.testPassed ? "true" : "false") << " - ShuffledSize: " << shuffledBuffer.size() << std::endl;

                        allPermutationPassed = allPermutationPassed && testStats.testPassed;
                    }
                }
            }
        }
    }
    else  // BC1, BC3, BC4, BC5
    {
        uint32_t transforms = 0;
        GBaseFormat baseFormatEnum;
        switch (baseFormat) {
            case DXGI_FORMAT_BC1_TYPELESS: baseFormatEnum = GBaseFormat::BC1; break;
            case DXGI_FORMAT_BC3_TYPELESS: baseFormatEnum = GBaseFormat::BC3; break;
            case DXGI_FORMAT_BC4_TYPELESS: baseFormatEnum = GBaseFormat::BC4; break;
            case DXGI_FORMAT_BC5_TYPELESS: baseFormatEnum = GBaseFormat::BC5; break;
            default:
                throw std::runtime_error("RunAllPermutationsForLoadedTexture: unsupported BC format.");
        }
        // headerless micro-shuffles (linear), then EX streams: linear, and curved when eligible
        for (uint32_t variant = 0; variant < 6; ++variant)
        {
            uint32_t microShuffleId = 1 + variant % 2;
            bool useHeader = variant >= 2;
            bool useSpaceCurve = variant >= 4;
            uint32_t elementSize = gShaderInfo[baseFormatEnum].blockSizes;
            if (gaclTransformIDs[baseFormatEnum][microShuffleId - 1] == 0)
            {
                continue;
            }

            std::vector<uint8_t> curvedData;
            if (useSpaceCurve)
            {
                if (GACL_Shuffle_ApplySpaceCurve(nullptr, nullptr, bcSizeInBytes, elementSize, widthPixels, true))
                {
                    curvedData.resize(bcSizeInBytes);
                    GACL_Shuffle_ApplySpaceCurve(curvedData.data(), bcTextureRaw.data(), bcSizeInBytes, elementSize, widthPixels, true);
                }
                else
                {
                    continue;
                }
            }

            // Will hold the data for the buffer shuffled in ProcessTexture
            std::vector<uint8_t> shuffledBuffer(bcSizeInBytes);
            const uint8_t* dataToShuffle = useSpaceCurve ? curvedData.data() : bcTextureRaw.data();

            if (useHeader)
            {
                if (!EncodeBCnEx(shuffledBuffer, dataToShuffle, bcSizeInBytes, gaclTransformIDs[baseFormatEnum][microShuffleId - 1],
                    elementSize, widthPixels, useSpaceCurve))
                {
                    continue;   // too few duplicate blocks for a header
                }
            }
            else
            {
                Shuffle_BC1345[static_cast<uint32_t>(baseFormatEnum)](shuffledBuffer.data(), dataToShuffle, bcSizeInBytes, microShuffleId);
            }

            std::stringstream testName;
            testName << "Res" << std::to_string(widthPixels) << "x" << std::to_string(heightPixels) <<
                "_SC" << (useSpaceCurve ? "True" : "False") <<
                (useHeader ? "_EX" : "") <<
                "_MicroPattern" << std::to_string(microShuffleId);

#if DUMP_SHUFFLED_BUFFER_TO_DISK
            std::stringstream dumpFileName;
            dumpFileName << testName.str() << ".bin";
            std::ofstream(dumpFileName.str(), std::ios::binary).write(reinterpret_cast<const char*>(shuffledBuffer.data()), shuffledBuffer.size());
#endif

            // Create all d3d12 resources for this test
            CreateBC1345UnshufflePassD3D12Resources(shuffledBuffer, bcTextureRaw.size());

            UNSHUFFLE_PER_TEST_STATISTICS testStats = {};
            testStats.testName = testName.str();
            testStats.textureWidthPixels = static_cast<uint32_t>(widthPixels);

            GTestParameters params = {
                baseFormatEnum,
                testName.str(),
                widthPixels,
                heightPixels,
                useSpaceCurve,
                {microShuffleId, useHeader}
            };


            TestGpuUnshuffling(params, bcTextureRaw, testStats, bcSizeInBytes);

            std::cout << "-- Finished test " << testName.str() << " -- Passed: " <<
                (testStats.testPassed ? "true" : "false") << " - ShuffledSize: " << shuffledBuffer.size() << std::endl;

            allPermutationPassed = allPermutationPassed && testStats.testPassed;
        }
    }
    std::wcout << L"\n-- All test passed: " << (allPermutationPassed ? L"true" : L"false") << std::endl;

    return allPermutationPassed;
}

// Note: Loading only Mip 0 of the texture pointed by m_inputPath
void Game::LoadTextureFromDisk(uint32_t& widthPixels, uint32_t& heightPixels, DXGI_FORMAT& format, std::vector<uint8_t>& bcTextureRaw)
{
    DirectX::TexMetadata metadata = {};
    DirectX::ScratchImage scratchImage;

    HRESULT hr = DirectX::LoadFromDDSFile(m_inputPath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, scratchImage);
    if (FAILED(hr))
    {
        throw std::runtime_error("LoadTextureFromDisk: failed to load DDS file.");
    }

    DXGI_FORMAT baseFormat = GetBaseFormat(metadata.format);

    if (baseFormat != DXGI_FORMAT_BC1_TYPELESS &&
        baseFormat != DXGI_FORMAT_BC3_TYPELESS && 
        baseFormat != DXGI_FORMAT_BC4_TYPELESS && 
        baseFormat != DXGI_FORMAT_BC5_TYPELESS && 
        baseFormat != DXGI_FORMAT_BC7_TYPELESS)
    {
        throw std::runtime_error("LoadTextureFromDisk: texture is not BC1\\3\\4\\5\\7 format.");
    }

    // Use the top mip level (mip=0, item=0, slice=0)
    const DirectX::Image* img = scratchImage.GetImage(0, 0, 0);
    if (!img)
    {
        throw std::runtime_error("LoadTextureFromDisk: failed to retrieve image data.");
    }

    widthPixels  = static_cast<uint32_t>(img->width);
    heightPixels = static_cast<uint32_t>(img->height);
    format = img->format;

    bcTextureRaw.resize(img->slicePitch);
    memcpy(bcTextureRaw.data(), img->pixels, img->slicePitch);
}
#endif



bool Game::RunTestWithParams(GTestParameters const& params)
{
#ifdef NEWBC1345
    if (params.baseFormat == GBaseFormat::BC7Join)
    {
        return RunBC7JoinTest(params);
    }
#endif
    if (params.baseFormat == GBaseFormat::BC7)
    {
        // Method that creates the raw contents of a bc7 texture of widthPixels x heightPixels.
        // The texture will be created block by block. And it will cycle through each bc7 mode foreach new block.
        // So if the texture has 6 blocks, the first will be mode 0, second mode 1, then mode 2, etc.
        // If the texture has more than 9 blocks, it wraps around and starts again from mode 0. this way the texture is representative of every mode.
        // The color contents of the texture do not matter. All that matter is that each block is constructed conforming to the bc7 specification.
        std::vector<uint8_t> bc7TextureRaw;
        CreateBC7RawFromMetadata(params.width, params.height, bc7TextureRaw);

        BC7ModeSplitShufflePattern Patterns[8];
        memcpy(Patterns, &g_patternSets[params.BC7.patternIndex][0], sizeof(Patterns));

        ShuffleParameters shuffleParams = {};
        shuffleParams.useSpaceCurve = static_cast<bool>(params.useSpaceCurve);
        shuffleParams.useIndexPrediction = params.BC7.useIndexPrediction;
        shuffleParams.sectionLayout = params.BC7.sectionLayout;
        shuffleParams.endpointOrderStrategy = params.BC7.endpointOrderStrategy;
        shuffleParams.ChunkSize = params.BC7.ChunkSize;
        shuffleParams.Patterns = Patterns;

        size_t bc7SizeInBytes = bc7TextureRaw.size();

        // Will hold the data for the buffer shuffled in ProcessTexture
        std::vector<uint8_t> shuffledBuffer;

        // Call that interfaces with gaclLib to shuffle the texture.
        ShuffleBC7Texture(
            shuffledBuffer,
            bc7TextureRaw.data(),
            bc7TextureRaw.size(),
            params.width,
            shuffleParams);

#if DUMP_SHUFFLED_BUFFER_TO_DISK
        std::stringstream dumpFileName;
        dumpFileName << params.testName << ".bin";
        std::ofstream(dumpFileName.str(), std::ios::binary).write(reinterpret_cast<const char*>(shuffledBuffer.data()), shuffledBuffer.size());
#endif

        // Create all d3d12 resources for this test
        CreateBC7UnshufflePassD3D12Resources(shuffledBuffer, bc7SizeInBytes);

        UNSHUFFLE_PER_TEST_STATISTICS testStats = {};
        testStats.testName = params.testName;
        testStats.textureWidthPixels = static_cast<uint32_t>(params.width);

        TestGpuUnshuffling(params, bc7TextureRaw, testStats, bc7SizeInBytes);

        return testStats.testPassed;
    }
    else
    {
        uint32_t elementSize = gShaderInfo[params.baseFormat].blockSizes;
        std::vector<uint8_t> bcnTextureRaw;
        CreateBCnRawFromMetadata(params.width, params.height, elementSize, bcnTextureRaw);

#ifdef NEWBC1345
        const bool useHeader = params.BC1345.useHeader;
        if (useHeader && !params.BC1345.longCoded)
        {
            if (params.BC1345.farDuplicates)
            {
                AddFarDuplicates(bcnTextureRaw, elementSize);
            }
            else if (params.BC1345.coded)
            {
                AddCodableBlocks(bcnTextureRaw, elementSize);
            }
            else
            {
                AddDuplicateBlocks(bcnTextureRaw, elementSize);
            }
        }
#endif

        size_t sizeInBytes = bcnTextureRaw.size();

        std::vector<uint8_t> curvedData(bcnTextureRaw.size());
        std::vector<uint8_t> shuffledBuffer(bcnTextureRaw.size());
        const uint8_t* dataToShuffle = bcnTextureRaw.data();

        if (params.useSpaceCurve)
        {
            GACL_Shuffle_ApplySpaceCurve(curvedData.data(), bcnTextureRaw.data(), sizeInBytes, elementSize, params.width, true);
            dataToShuffle = curvedData.data();
        }

#ifdef NEWBC1345
        if (useHeader && params.BC1345.curveOnly)
        {
            if (!EncodeBCnExCurveOnly(shuffledBuffer, bcnTextureRaw, dataToShuffle, gaclTransformIDs[params.baseFormat][0],
                    elementSize, params.width, params.BC1345.coded))
            {
                return false;
            }
        }
        else if (useHeader && params.BC1345.longCoded)
        {
            // an overrunning header is rejected: the output stays zero
            std::vector<uint8_t> decoded;
            const bool overrun = params.BC1345.overrun;
            if (!EncodeBCnExLongCoded(shuffledBuffer, bcnTextureRaw, gaclTransformIDs[params.baseFormat][params.BC1345.microShuffleId - 1],
                    elementSize, params.baseFormat, params.BC1345.microShuffleId, overrun, params.BC1345.manyCoded) ||
                BCnEx_Decode(shuffledBuffer.data(), shuffledBuffer.size(), BCnEx_TransformId(uint8_t(gaclTransformIDs[params.baseFormat][0])),
                    decoded) == overrun)
            {
                std::cout << "Can't make or decode an EX stream with a long coded block" << std::endl;
                return false;
            }
            if (overrun)
            {
                decoded.assign(sizeInBytes, 0);
            }
            bcnTextureRaw.swap(decoded);
        }
        else if (useHeader)
        {
            if (!EncodeBCnEx(shuffledBuffer, dataToShuffle, sizeInBytes, gaclTransformIDs[params.baseFormat][params.BC1345.microShuffleId - 1],
                elementSize, params.width, params.useSpaceCurve, params.BC1345.predictionOff, params.BC1345.unshuffled))
            {
                return false;
            }

            // without duplicates to remove, the header made room for itself by coding blocks
            StreamHeaderInfo info;
            const StreamHeaderBlockCode blockCode = { params.baseFormat == GBaseFormat::BC1 ? kBC1BlockCode : kBC5BlockCode, false };
            if (params.BC1345.coded &&
                (!StreamHeader_Parse(shuffledBuffer.data(), shuffledBuffer.size(), elementSize, info, &blockCode) ||
                 info.Coded.empty() || !info.Removed.empty()))
            {
                std::cout << "Expected an EX stream with coded blocks and no removed duplicates" << std::endl;
                return false;
            }

            // the far duplicates removed, both pairs in two header blocks with 8-byte blocks, the first in one otherwise
            const std::vector<size_t> farRemoved = { 10000, 15001 }, farSources = { 22000, 18001 };
            const size_t farK = elementSize == 8 ? 2 : 1;
            if (params.BC1345.farDuplicates &&
                (!StreamHeader_Parse(shuffledBuffer.data(), shuffledBuffer.size(), elementSize, info, nullptr) ||
                 info.HeaderBlocks != farK || !info.Coded.empty() ||
                 info.Removed != std::vector<size_t>(farRemoved.begin(), farRemoved.begin() + farK) ||
                 info.Sources != std::vector<size_t>(farSources.begin(), farSources.begin() + farK)))
            {
                std::cout << "Expected an EX stream header of " << farK << " blocks, removing the far duplicates" << std::endl;
                return false;
            }
        }
        else
#endif
        {
            Shuffle_BC1345[params.baseFormat](shuffledBuffer.data(), dataToShuffle, sizeInBytes, params.BC1345.microShuffleId);
        }

#ifdef NEWBC1345
        // under another format's EX id, the CPU decode under that id: zero when it rejects the stream
        if (params.transformOverride != 0 &&
            !BCnEx_Decode(shuffledBuffer.data(), shuffledBuffer.size(), uint8_t(params.transformOverride), bcnTextureRaw))
        {
            bcnTextureRaw.assign(sizeInBytes, 0);
        }
        // with the header's escape bit set, rejected: zero
        if (params.escape)
        {
            shuffledBuffer[0] |= GACL_STREAMHEADER_ESCAPE;
            std::vector<uint8_t> decoded;
            if (BCnEx_Decode(shuffledBuffer.data(), shuffledBuffer.size(), BCnEx_TransformId(uint8_t(gaclTransformIDs[params.baseFormat][0])), decoded))
            {
                std::cout << "BCnEx_Decode takes a header with its escape bit set" << std::endl;
                return false;
            }
            bcnTextureRaw.assign(sizeInBytes, 0);
        }
#endif

#if DUMP_SHUFFLED_BUFFER_TO_DISK
        std::stringstream dumpFileName;
        dumpFileName << params.testName << ".bin";
        std::ofstream(dumpFileName.str(), std::ios::binary).write(reinterpret_cast<const char*>(shuffledBuffer.data()), shuffledBuffer.size());
#endif

        CreateBC1345UnshufflePassD3D12Resources(shuffledBuffer, sizeInBytes);

        UNSHUFFLE_PER_TEST_STATISTICS testStats = {};
        testStats.testName = params.testName;
        testStats.textureWidthPixels = static_cast<uint32_t>(params.width);

        TestGpuUnshuffling(params, bcnTextureRaw, testStats, sizeInBytes);

        return testStats.testPassed;
    }
}

void Game::TestGpuUnshuffling(GTestParameters params, std::vector<uint8_t> const& BCnOriginalData, UNSHUFFLE_PER_TEST_STATISTICS& testStats, size_t sizeInBytes)
{
#ifdef CAPTURE_PIX
    PIXCaptureParameters pixCaptureParams = {};
    wchar_t captureName[200];
    const wchar_t* formatNames[] = {L"BC1", L"BC3", L"BC4", L"BC5", L"B7J", L"BC7"};
    if (params.baseFormat == GBaseFormat::BC7)
    {
        swprintf_s(captureName, _countof(captureName), L"FCap_%s_%ux%u_sc%d_p%d_cs%d_eos%d.wpix", 
            formatNames[params.baseFormat], params.width, params.height, params.useSpaceCurve, 
            params.BC7.patternIndex, params.BC7.ChunkSize, params.BC7.endpointOrderStrategy);
    }
    else
    {
        swprintf_s(captureName, _countof(captureName), L"FCap_%s_%ux%u_sc%d_p%d.wpix", 
            formatNames[params.baseFormat], params.width, params.height, params.useSpaceCurve, 
            params.BC1345.microShuffleId);
    }
    pixCaptureParams.GpuCaptureParameters.FileName = captureName;
    CAPTURE_SELECT
    PIXBeginCapture(PIX_CAPTURE_GPU, &pixCaptureParams);
    #endif

    // Add unshuffling shader commands to the command list
    Unshuffle(params, sizeInBytes);

    #ifdef CAPTURE_PIX
    CAPTURE_SELECT
    {
    PIXEndCapture(false /*Discard param not supported on Windows*/);
    }
    #endif

    // Copy unshuffled data back to CPU from GPU
    std::vector<uint8_t> unshuffledData(sizeInBytes);
    UnshuffleCPUReadback(unshuffledData, sizeInBytes);

    // Compare the unshuffled data with the expected data
    if (params.baseFormat == GBaseFormat::BC7)
    {
        CompareAgainstOriginalBC7Data(BCnOriginalData, unshuffledData, testStats);
    }
    else
    {
        testStats.testPassed = 
            (BCnOriginalData.size() == unshuffledData.size()) &&
            (0 == memcmp(BCnOriginalData.data(), unshuffledData.data(), sizeInBytes));
    }

    // Reset d3d12 resources before next test
    ResetD3D12Resources();
}

void Game::Unshuffle(GTestParameters params, size_t sizeInBytes)
{
    m_deviceResources->ResetCommandList();
    auto commandList = m_deviceResources->GetCommandList();

    if (params.baseFormat == GBaseFormat::BC7)
    {
        uint32_t numBlocks = static_cast<uint32_t>(sizeInBytes) / BC7_BYTES_PER_BLOCK;
        uint32_t histogramCount = (numBlocks + TG_THREAD_COUNT - 1) / TG_THREAD_COUNT;

        D3D12_RESOURCE_BARRIER barriers[3] = {};

        // BC7 Prepass
        {
            PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"BC7 Prepass");

            commandList->SetPipelineState(m_prepassCSPipelineState.Get());

            commandList->SetComputeRootSignature(m_prepassCSRootSignature.Get());

            commandList->SetComputeRoot32BitConstant(0, numBlocks, 0);
            commandList->SetComputeRoot32BitConstant(0, m_offsetIntoHistogram, 1);

            commandList->SetComputeRootShaderResourceView(1, m_shuffledBuffer->GetGPUVirtualAddress());

            commandList->SetComputeRootUnorderedAccessView(2, m_scratchBuffer->GetGPUVirtualAddress());

            commandList->Dispatch(histogramCount, 1, 1);

            PIXEndEvent(commandList);
        }

        barriers[0] = CD3DX12_RESOURCE_BARRIER::UAV(m_scratchBuffer.Get());
        commandList->ResourceBarrier(1, barriers);

        // BC7 Compute Prefix-Sum
        {
            PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"BC7 Prefix-Sum");

            commandList->SetPipelineState(m_prefixSumCSPipelineState.Get());

            commandList->SetComputeRootSignature(m_prefixSumCSRootSignature.Get());

            commandList->SetComputeRoot32BitConstant(0, histogramCount, 0);
            commandList->SetComputeRoot32BitConstant(0, m_offsetIntoHistogram, 1);

            commandList->SetComputeRootUnorderedAccessView(1, m_scratchBuffer->GetGPUVirtualAddress());

            commandList->Dispatch(1, BC7_MODES_COUNT, 1);

            PIXEndEvent(commandList);
        }

        barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_unshufflePassIndirectBuffer.Get(),
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barriers[1] = CD3DX12_RESOURCE_BARRIER::UAV(m_scratchBuffer.Get());
        commandList->ResourceBarrier(2, barriers);

        // BC7 Mode Binning Pass
        {
            PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"BC7 ModeBinning");

            commandList->SetPipelineState(m_modeBinningCSPipelineState.Get());

            commandList->SetComputeRootSignature(m_modeBinningCSRootSignature.Get());

            commandList->SetComputeRoot32BitConstant(0, histogramCount, 0);
            commandList->SetComputeRoot32BitConstant(0, numBlocks, 1);
            commandList->SetComputeRoot32BitConstant(0, m_offsetIntoHistogram, 2);
            commandList->SetComputeRoot32BitConstant(0, m_offsetIntoBinning, 3);

            commandList->SetComputeRootShaderResourceView(1, m_shuffledBuffer->GetGPUVirtualAddress());
            commandList->SetComputeRootUnorderedAccessView(2, m_scratchBuffer->GetGPUVirtualAddress());
            commandList->SetComputeRootUnorderedAccessView(3, m_outBCnBuffer->GetGPUVirtualAddress());
            commandList->SetComputeRootUnorderedAccessView(4, m_unshufflePassIndirectBuffer->GetGPUVirtualAddress());

            commandList->Dispatch(histogramCount, 1, 1);

            PIXEndEvent(commandList);
        }

        barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_unshufflePassIndirectBuffer.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        barriers[1] = CD3DX12_RESOURCE_BARRIER::UAV(m_scratchBuffer.Get());
        barriers[2] = CD3DX12_RESOURCE_BARRIER::UAV(m_outBCnBuffer.Get());
        commandList->ResourceBarrier(3, barriers);

        // BC7 Compute Unshuffle
        {
            PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"BC7 Unshuffle");

            commandList->SetComputeRootSignature(m_unshuffleCSRootSignature.Get());

            commandList->SetComputeRoot32BitConstant(0, m_offsetIntoBinning, 0);

            commandList->SetComputeRootShaderResourceView(1, m_shuffledBuffer->GetGPUVirtualAddress());
            commandList->SetComputeRootUnorderedAccessView(2, m_scratchBuffer->GetGPUVirtualAddress());
            commandList->SetComputeRootUnorderedAccessView(3, m_outBCnBuffer->GetGPUVirtualAddress());

            for (uint32_t i = 0; i < BC7_MODES_COUNT; ++i)
            {
                commandList->SetPipelineState(m_unshuffleBinnedCSPSO[i].Get());

                commandList->ExecuteIndirect(m_unshufflePassCmdSignature.Get(), 1,
                    m_unshufflePassIndirectBuffer.Get(), i * sizeof(UnshufflePassIndirectBuffer), nullptr, 0);
            }
        }
    }
    else
    {
        PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, gShaderInfo[params.baseFormat].pixName.data());

#ifdef NEWBC1345
        // a truncated BC7 Join stream is shorter than the output (sizeInBytes)
        const uint32_t streamSize = params.baseFormat == GBaseFormat::BC7Join ? params.BC7Join.streamSize : uint32_t(sizeInBytes);
        uint32_t tID = (params.baseFormat == GBaseFormat::BC7Join) ? params.BC7Join.transformId :
            params.BC1345.useHeader ? BCnEx_TransformId(uint8_t(gaclTransformIDs[params.baseFormat][0])) :
            gaclTransformIDs[params.baseFormat][params.BC1345.microShuffleId - 1];
        if (params.transformOverride != 0)
        {
            tID = params.transformOverride;
        }
        // DirectStorage's dispatch for the transform id: its shader, blocks per thread and thread group size
        const uint32_t dispatchShader = DispatchShader(tID);
        const ShuffleShaderInfo& geometry = (m_useUnifiedShader ? gUnifiedShaderInfo : gShaderInfo)[dispatchShader];
        const uint32_t blocksPerThread = geometry.blocksPerThread;
        const uint32_t threadGroupSize = geometry.threadGroupSize;
#else
        const uint32_t streamSize = uint32_t(sizeInBytes);
        const uint32_t dispatchShader = params.baseFormat;
        const ShuffleShaderInfo& geometry = gShaderInfo[params.baseFormat];
        const uint32_t blocksPerThread = geometry.blocksPerThread;
        const uint32_t threadGroupSize = TG_THREAD_COUNT;
#endif
        uint32_t totalBlocks = streamSize / geometry.blockSizes;
        uint32_t numPairsOrQuads = totalBlocks / blocksPerThread;
        uint32_t numStragglers = totalBlocks % blocksPerThread;
        uint32_t numThreadsRequired = numPairsOrQuads + numStragglers;

        uint32_t numThreadGroupsX = (numThreadsRequired + (threadGroupSize - 1)) / threadGroupSize;


#ifdef NEWBC1345
        uint32_t shaderConstants[4] = { streamSize, 0, tID,  params.width};
#else
        uint32_t shaderConstants[2] = { streamSize, 0 };
#endif

        commandList->SetPipelineState(m_bcnCSPipelineState[dispatchShader].Get());

        commandList->SetComputeRootSignature(m_bcnCSRootSignature[dispatchShader].Get());

        commandList->SetComputeRootShaderResourceView(1, m_shuffledBuffer->GetGPUVirtualAddress());
#ifdef USE_ROOT_DESCRIPTORS
        commandList->SetComputeRootUnorderedAccessView(2, m_outBCnBuffer->GetGPUVirtualAddress());
#else
        ID3D12DescriptorHeap* dheaps[] = { m_uavSrvCbvHeap.Get() };
        commandList->SetDescriptorHeaps(1, dheaps);
        commandList->SetComputeRootDescriptorTable(2, m_UAVOutputBufferGpuHandle);
#endif
#ifdef NEWBC1345
        commandList->SetComputeRoot32BitConstants(0, 4, &shaderConstants, 0);
#else
        commandList->SetComputeRoot32BitConstants(0, 2, &shaderConstants, 0);
#endif
        commandList->Dispatch(numThreadGroupsX, 1, 1);

        PIXEndEvent(commandList);
    }

    // Send commands to the GPU
    PIXBeginEvent(PIX_COLOR_DEFAULT, L"ECL Submission");
    auto cmdQueue = m_deviceResources->GetCommandQueue();
    commandList->Close();
    ID3D12CommandList* cmdLists[] = { commandList };
    cmdQueue->ExecuteCommandLists(1, cmdLists);
    PIXEndEvent();
}

void Game::UnshuffleCPUReadback(std::vector<uint8_t>& BCnUnshuffledData, size_t bcSizeInBytes)
{
    m_deviceResources->ResetCommandListToReadbackAllocator();
    auto commandList = m_deviceResources->GetCommandList();
    
    D3D12_RESOURCE_BARRIER barriers[1];
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_outBCnBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(1, barriers);
    
    // Copy from output to readback
    commandList->CopyBufferRegion(m_BCnReadback.Get(), 0ull, m_outBCnBuffer.Get(), m_uavHeapOffsetInBytes, bcSizeInBytes);
    
    // Send commands to the GPU
    auto cmdQueue = m_deviceResources->GetCommandQueue();
    commandList->Close();
    ID3D12CommandList* cmdLists[] = { commandList };
    cmdQueue->ExecuteCommandLists(1, cmdLists);
    
    // Signal the readback queue and then wait for it on the CPU
    m_deviceResources->WaitForReadbackFence();
    
    // Get data from readback
    uint8_t* m_pReadbackBufferData = nullptr;
    DX::ThrowIfFailed(m_BCnReadback->Map(0, nullptr, reinterpret_cast<void**>(&m_pReadbackBufferData)));
    memcpy(&BCnUnshuffledData[0], m_pReadbackBufferData, bcSizeInBytes);
    m_BCnReadback->Unmap(0, nullptr);
}

void Game::CreateBC1345UnshufflePassD3D12Resources(std::vector<uint8_t>& shuffledBuffer, size_t sizeInBytes)
{
    auto device = m_deviceResources->GetD3DDevice();

    // Create shader RootSignature and PSO

    static bool oneTime = true;
    if (oneTime)
    {
        oneTime = false;

#ifdef NEWBC1345
        const ShuffleShaderInfo* shaderInfo = m_useUnifiedShader ? gUnifiedShaderInfo : gShaderInfo;
        std::wcout << L"-- BC1\\3\\4\\5 and BC7 Join unshuffle: " <<
            (m_useUnifiedShader ? L"unified shader (UnshuffleBCnEx.cso)" :
                L"per-format shaders (UnshuffleBC1Ex - BC7Ex.cso, UnshuffleBC1Headerless - BC5Headerless.cso)") << std::endl;
        static_assert(_countof(gShaderInfo) == _countof(m_bcnCSPipelineState), "a pipeline per shader");
#else
        const ShuffleShaderInfo* shaderInfo = gShaderInfo;
#endif
        for (size_t i = 0; i < _countof(gShaderInfo); i++)
        {
            auto csBlob = DX::ReadData(shaderInfo[i].fileName.data());
            DX::ThrowIfFailed(device->CreateRootSignature(0, csBlob.data(), csBlob.size(), IID_PPV_ARGS(m_bcnCSRootSignature[i].ReleaseAndGetAddressOf())));
            m_bcnCSRootSignature[i]->SetName(shaderInfo[i].signatureName.data());

            D3D12_COMPUTE_PIPELINE_STATE_DESC computePSODesc = {};
            computePSODesc.CS = { csBlob.data(), csBlob.size() };
            computePSODesc.pRootSignature = m_bcnCSRootSignature[i].Get();
            DX::ThrowIfFailed(device->CreateComputePipelineState(&computePSODesc, IID_PPV_ARGS(m_bcnCSPipelineState[i].ReleaseAndGetAddressOf())));
        }
    }


#ifndef USE_ROOT_DESCRIPTORS
    // Create DescriptorHeap
    D3D12_DESCRIPTOR_HEAP_DESC dHeapDesc = {};
    dHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    dHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    dHeapDesc.NumDescriptors = 1; // output buffer UAV only.
    DX::ThrowIfFailed(device->CreateDescriptorHeap(&dHeapDesc, IID_PPV_ARGS(m_uavSrvCbvHeap.ReleaseAndGetAddressOf())));
    m_DHIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
#endif


    D3D12_HEAP_PROPERTIES defaultHeapProperties = {};
    defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    uint32_t shuffledBufferSizeBytes = uint32_t(shuffledBuffer.size() + 3) & ~0b11ul; // round up to DWORD

    // OUTPUT RESOURCE
    {
        D3D12_RESOURCE_DESC outBCnBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        D3D12_RESOURCE_ALLOCATION_INFO heapAllocInfo = device->GetResourceAllocationInfo(0, 1, &outBCnBufferDesc);

        D3D12_HEAP_DESC outputHeapDesc = {};
        outputHeapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        outputHeapDesc.Properties = defaultHeapProperties;
        outputHeapDesc.SizeInBytes = heapAllocInfo.SizeInBytes;

        DX::ThrowIfFailed(device->CreateHeap(&outputHeapDesc, IID_PPV_ARGS(m_outputHeap.ReleaseAndGetAddressOf())));

        DX::ThrowIfFailed(device->CreatePlacedResource(
            m_outputHeap.Get(),
            0,
            &outBCnBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(m_outBCnBuffer.ReleaseAndGetAddressOf())));
        m_outBCnBuffer->SetName(L"OutBCnBuffer");
    }

    // SAMPLE-ONLY RESOURCES
    {
        D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        // Shuffled buffer related resources (input)
        D3D12_RESOURCE_DESC shuffledBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(shuffledBufferSizeBytes);

        // Intermediate resource with buffer data
        DX::ThrowIfFailed(device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &shuffledBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(m_shuffledBufferUpload.ReleaseAndGetAddressOf())));
        m_shuffledBufferUpload->SetName(L"ShuffledBufferUpload");

        // Resource with buffer data on GPU readable memory
        DX::ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &shuffledBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(m_shuffledBuffer.ReleaseAndGetAddressOf())));
        m_shuffledBuffer->SetName(L"ShuffledBuffer");

        // Readback Resource (for reading and comparing output data after unshuffling
        CD3DX12_HEAP_PROPERTIES readbackHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        D3D12_RESOURCE_DESC readbackBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes);

        DX::ThrowIfFailed(device->CreateCommittedResource(
            &readbackHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &readbackBufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(m_BCnReadback.ReleaseAndGetAddressOf())));
        m_BCnReadback->SetName(L"BCnReadback");
    }


    // Create Descriptors
#ifndef USE_ROOT_DESCRIPTORS
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle;
        //D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;

        auto uavSrvCbvDescHeapStart = m_uavSrvCbvHeap->GetCPUDescriptorHandleForHeapStart();

        // Unshuffled output buffer UAV
        uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        uavDesc.Buffer.FirstElement = static_cast<uint32_t>(m_uavHeapOffsetInBytes) / sizeof(uint32_t);
        uavDesc.Buffer.NumElements = static_cast<uint32_t>(sizeInBytes) / sizeof(uint32_t);
        uavDesc.Buffer.StructureByteStride = 0;
        handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(uavSrvCbvDescHeapStart, UAV_OUTPUT_BUFFER, m_DHIncrementSize);
        device->CreateUnorderedAccessView(m_outBCnBuffer.Get(), nullptr, &uavDesc, handle);

        m_UAVOutputBufferGpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_uavSrvCbvHeap->GetGPUDescriptorHandleForHeapStart(), UAV_OUTPUT_BUFFER, m_DHIncrementSize);
    }
#endif

    // Upload shuffled data to GPU
    {
        m_deviceResources->ResetCommandList();
        auto commandList = m_deviceResources->GetCommandList();

        // Here we would need to upload the data we got from the shuffled buffer to the gpu
        uint8_t* data = nullptr;
        DX::ThrowIfFailed(m_shuffledBufferUpload->Map(0, nullptr, reinterpret_cast<void**>(&data)));
        memcpy(data, shuffledBuffer.data(), shuffledBuffer.size());
        m_shuffledBufferUpload->Unmap(0, nullptr);

        // Once uploaded, transition resources to correct states and perform the copy
        {
            D3D12_RESOURCE_BARRIER barriers[1] = {};
            barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_shuffledBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1, barriers);

            commandList->CopyResource(m_shuffledBuffer.Get(), m_shuffledBufferUpload.Get());

            barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_shuffledBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            commandList->ResourceBarrier(1, barriers);
        }

        commandList->Close();
        ID3D12CommandList* cmdLists[] = { commandList };
        auto commandQueue = m_deviceResources->GetCommandQueue();
        commandQueue->ExecuteCommandLists(1, cmdLists);

        m_deviceResources->WaitForGpu();
    }
}

void Game::CreateBC7UnshufflePassD3D12Resources(std::vector<uint8_t>& shuffledBuffer, size_t bc7SizeInBytes)
{
    auto device = m_deviceResources->GetD3DDevice();

    // Create RootSignature and PSO
    {
        // PREPASS
        auto csBlobPrepass = DX::ReadData(L"PrepassB.cso");
        DX::ThrowIfFailed(
            device->CreateRootSignature(0, csBlobPrepass.data(), csBlobPrepass.size(), IID_PPV_ARGS(m_prepassCSRootSignature.ReleaseAndGetAddressOf())));
        m_prepassCSRootSignature->SetName(L"PrepassCSRootSignature");

        D3D12_COMPUTE_PIPELINE_STATE_DESC prepassComputePSODesc = {};
        prepassComputePSODesc.CS = { csBlobPrepass.data(), csBlobPrepass.size() };
        prepassComputePSODesc.pRootSignature = m_prepassCSRootSignature.Get();

        DX::ThrowIfFailed(device->CreateComputePipelineState(&prepassComputePSODesc, IID_PPV_ARGS(m_prepassCSPipelineState.ReleaseAndGetAddressOf())));
        
        // Mode Binning
        std::vector<uint8_t> csModeBinningBlob;
        if (m_minHWSupportedWaveSize == 4)
        {
            csModeBinningBlob = DX::ReadData(L"BinningPassCS_MinWave4.cso");
        }
        else if (m_minHWSupportedWaveSize == 8)
        {
            csModeBinningBlob = DX::ReadData(L"BinningPassCS_MinWave8.cso");
        }
        else if (m_minHWSupportedWaveSize == 16)
        {
            csModeBinningBlob = DX::ReadData(L"BinningPassCS_MinWave16.cso");
        }
        else if (m_minHWSupportedWaveSize == 32)
        {
            csModeBinningBlob = DX::ReadData(L"BinningPassCS_MinWave32.cso");
        }
        else if (m_minHWSupportedWaveSize == 64)
        {
            csModeBinningBlob = DX::ReadData(L"BinningPassCS_MinWave64.cso");
        }
        else if (m_minHWSupportedWaveSize == 128)
        {
            csModeBinningBlob = DX::ReadData(L"BinningPassCS_MinWave128.cso");
        }
        else 
        {
            throw std::runtime_error("Unsupported wave size.");
        }

        DX::ThrowIfFailed(
            device->CreateRootSignature(0, csModeBinningBlob.data(), csModeBinningBlob.size(), IID_PPV_ARGS(m_modeBinningCSRootSignature.ReleaseAndGetAddressOf())));
        m_modeBinningCSRootSignature->SetName(L"ModeBinningCSRootSignature");

        D3D12_COMPUTE_PIPELINE_STATE_DESC modeBinningPSODesc = {};
        modeBinningPSODesc.CS = { csModeBinningBlob.data(), csModeBinningBlob.size() };
        modeBinningPSODesc.pRootSignature = m_modeBinningCSRootSignature.Get();

        DX::ThrowIfFailed(device->CreateComputePipelineState(&modeBinningPSODesc, IID_PPV_ARGS(m_modeBinningCSPipelineState.ReleaseAndGetAddressOf())));
       
        // PREFIX-SUM PASS
        std::vector<uint8_t> csPrefixSumBlob;
        if (m_minHWSupportedWaveSize == 4)
        {
            csPrefixSumBlob = DX::ReadData(L"PrefixSumPassCS_MinWave4.cso");
        }
        else if (m_minHWSupportedWaveSize == 8)
        {
            csPrefixSumBlob = DX::ReadData(L"PrefixSumPassCS_MinWave8.cso");
        }
        else if (m_minHWSupportedWaveSize == 16)
        {
            csPrefixSumBlob = DX::ReadData(L"PrefixSumPassCS_MinWave16.cso");
        }
        else if (m_minHWSupportedWaveSize == 32)
        {
            csPrefixSumBlob = DX::ReadData(L"PrefixSumPassCS_MinWave32.cso");
        }
        else if (m_minHWSupportedWaveSize == 64)
        {
            csPrefixSumBlob = DX::ReadData(L"PrefixSumPassCS_MinWave64.cso");
        }
        else if (m_minHWSupportedWaveSize == 128)
        {
            csPrefixSumBlob = DX::ReadData(L"PrefixSumPassCS_MinWave128.cso");
        }
        else
        {
            assert(false); // Size not supported
        }
        DX::ThrowIfFailed(
            device->CreateRootSignature(0, csPrefixSumBlob.data(), csPrefixSumBlob.size(), IID_PPV_ARGS(m_prefixSumCSRootSignature.ReleaseAndGetAddressOf())));
        m_prefixSumCSRootSignature->SetName(L"PrefixSumCSRootSignature");

        D3D12_COMPUTE_PIPELINE_STATE_DESC prefixsumPSODesc = {};
        prefixsumPSODesc.CS = { csPrefixSumBlob.data(), csPrefixSumBlob.size() };
        prefixsumPSODesc.pRootSignature = m_prefixSumCSRootSignature.Get();

        DX::ThrowIfFailed(device->CreateComputePipelineState(&prefixsumPSODesc, IID_PPV_ARGS(m_prefixSumCSPipelineState.ReleaseAndGetAddressOf())));

        // UNSHUFFLE PASS
        auto csBlob = DX::ReadData(s_perModeUnshufflePSONames[0].c_str());
        DX::ThrowIfFailed(
            device->CreateRootSignature(0, csBlob.data(), csBlob.size(), IID_PPV_ARGS(m_unshuffleCSRootSignature.ReleaseAndGetAddressOf())));
        m_unshuffleCSRootSignature->SetName(L"UnshuffleCSRootSignature");

        D3D12_COMPUTE_PIPELINE_STATE_DESC computePSODesc = {};
        computePSODesc.pRootSignature = m_unshuffleCSRootSignature.Get();

        for (uint32_t i = 0; i < BC7_MODES_COUNT; ++i)
        {
            csBlob = DX::ReadData(s_perModeUnshufflePSONames[i].c_str());
            computePSODesc.CS = { csBlob.data(), csBlob.size() };
            DX::ThrowIfFailed(device->CreateComputePipelineState(&computePSODesc, IID_PPV_ARGS(m_unshuffleBinnedCSPSO[i].ReleaseAndGetAddressOf())));
        }
    }

    // Shuffled buffer data. Since this is going to be accessed as a raw buffer which needs to be 
    // dword aligned, size is aligned to the next dword boundary
    uint32_t shuffledBufferSizeBytes = static_cast<uint32_t>(shuffledBuffer.size());
    uint32_t rem = shuffledBufferSizeBytes % 4;
    if (rem != 0) shuffledBufferSizeBytes += (4 - rem);

    uint32_t blockCount = static_cast<uint32_t>(bc7SizeInBytes) / BC7_BYTES_PER_BLOCK;

    uint32_t modeBinningBufferSizeInBytes = blockCount * sizeof(uint32_t);

    uint32_t histogramCount = (blockCount + TG_THREAD_COUNT - 1) / TG_THREAD_COUNT;
    uint32_t histogramEntrySize = BC7_MODES_COUNT * sizeof(uint32_t);
    uint32_t histogramBufferSizeInBytes = histogramCount * histogramEntrySize;
    
    uint32_t headerSizeBytes = sizeof(CommonHeader);
    rem = headerSizeBytes % 4;
    if (rem != 0) headerSizeBytes += (4 - rem);

    // These offsets are passed to shaders in order to fetch from the correct memory boundary
    // Right now there is no alignment between these other than dword alignment
    m_offsetIntoHeader = 0;
    m_offsetIntoHistogram = headerSizeBytes;
    m_offsetIntoBinning = headerSizeBytes + histogramBufferSizeInBytes;

    // This is the size of the buffer holding the intermediate resources (minus the indirect arguments)
    uint32_t scratchBufferSizeBytes = histogramBufferSizeInBytes + modeBinningBufferSizeInBytes + headerSizeBytes;

    uint32_t indirectArgBufferSize = BC7_MODES_COUNT * sizeof(UnshufflePassIndirectBuffer);

    // Resource Description struct for all intermediates
    D3D12_RESOURCE_DESC scratchBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(scratchBufferSizeBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    D3D12_RESOURCE_DESC indirectBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indirectArgBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    std::vector<D3D12_RESOURCE_ALLOCATION_INFO> allocInfoList;
    allocInfoList.push_back(device->GetResourceAllocationInfo(0, 1, &scratchBufferDesc));
    allocInfoList.push_back(device->GetResourceAllocationInfo(0, 1, &indirectBufferDesc));

    uint64_t intermediateResourcesOffsetsIntoHeap[4];
    uint64_t heapSizeBytes = 0u;
    for (size_t i = 0; i < allocInfoList.size(); ++i)
    {
        intermediateResourcesOffsetsIntoHeap[i] = heapSizeBytes;
        heapSizeBytes += allocInfoList[i].SizeInBytes;
    }

    D3D12_HEAP_PROPERTIES defaultHeapProperties = {};
    defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_HEAP_DESC heapDesc = {};
    heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heapDesc.Flags = D3D12_HEAP_FLAG_NONE;
    heapDesc.Properties = defaultHeapProperties;
    heapDesc.SizeInBytes = heapSizeBytes;

    DX::ThrowIfFailed(device->CreateHeap(&heapDesc, IID_PPV_ARGS(m_intermediateHeap.ReleaseAndGetAddressOf())));
    
    m_uavHeapOffsetInBytes = 0;

    // OUTPUT RESOURCE
    {
        size_t outputHeapSize = bc7SizeInBytes + m_uavHeapOffsetInBytes;

        D3D12_RESOURCE_DESC outBC7BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(outputHeapSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        
        D3D12_RESOURCE_ALLOCATION_INFO heapAllocInfo = device->GetResourceAllocationInfo(0, 1, &outBC7BufferDesc);
        
        D3D12_HEAP_DESC outputHeapDesc = {};
        outputHeapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        outputHeapDesc.Properties = defaultHeapProperties;
        outputHeapDesc.SizeInBytes = heapAllocInfo.SizeInBytes;

        DX::ThrowIfFailed(device->CreateHeap(&outputHeapDesc, IID_PPV_ARGS(m_outputHeap.ReleaseAndGetAddressOf())));

        DX::ThrowIfFailed(device->CreatePlacedResource(
            m_outputHeap.Get(),
            0,
            &outBC7BufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(m_outBCnBuffer.ReleaseAndGetAddressOf())));
        m_outBCnBuffer->SetName(L"OutBC7Buffer");
    }
    
    // SAMPLE-ONLY RESOURCES
    {
        D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        // Shuffled buffer related resources (input)
        D3D12_RESOURCE_DESC shuffledBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(shuffledBufferSizeBytes);

        // Intermediate resource with buffer data
        DX::ThrowIfFailed(device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &shuffledBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(m_shuffledBufferUpload.ReleaseAndGetAddressOf())));
        m_shuffledBufferUpload->SetName(L"ShuffledBufferUpload");

        // Resource with buffer data on GPU readable memory
        DX::ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &shuffledBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(m_shuffledBuffer.ReleaseAndGetAddressOf())));
        m_shuffledBuffer->SetName(L"ShuffledBuffer");

        // Readback Resource (for reading and comparing BC7 output data after unshuffling
        CD3DX12_HEAP_PROPERTIES readbackHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        D3D12_RESOURCE_DESC readbackBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bc7SizeInBytes);

        DX::ThrowIfFailed(device->CreateCommittedResource(
            &readbackHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &readbackBufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(m_BCnReadback.ReleaseAndGetAddressOf())));
        m_BCnReadback->SetName(L"BC7Readback");
    }

    // INTERMEDIATE RESOURCES
    {
        // Buffer that contains header, binning buffer, and histogram buffer
        DX::ThrowIfFailed(device->CreatePlacedResource(
            m_intermediateHeap.Get(),
            intermediateResourcesOffsetsIntoHeap[0],
            &scratchBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(m_scratchBuffer.ReleaseAndGetAddressOf())));
        m_scratchBuffer->SetName(L"ScratchBuffer");

        DX::ThrowIfFailed(device->CreatePlacedResource(
            m_intermediateHeap.Get(),
            intermediateResourcesOffsetsIntoHeap[1],
            &indirectBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(m_unshufflePassIndirectBuffer.ReleaseAndGetAddressOf())));
    }

    // Execute Indirect command signature
    {
        D3D12_INDIRECT_ARGUMENT_DESC arguments[1] = {};
        arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC cmdSigDesc = {};
        cmdSigDesc.NumArgumentDescs = std::size(arguments);
        cmdSigDesc.pArgumentDescs = arguments;
        cmdSigDesc.ByteStride = sizeof(UnshufflePassIndirectBuffer);

        DX::ThrowIfFailed(device->CreateCommandSignature(&cmdSigDesc, nullptr, IID_PPV_ARGS(m_unshufflePassCmdSignature.ReleaseAndGetAddressOf())));
    }

    // Upload shuffled data to GPU
    {
        m_deviceResources->ResetCommandList();
        auto commandList = m_deviceResources->GetCommandList();

        // Here we would need to upload the data we got from the shuffled buffer to the gpu
        uint8_t* data = nullptr;
        DX::ThrowIfFailed(m_shuffledBufferUpload->Map(0, nullptr, reinterpret_cast<void**>(&data)));
        memcpy(data, shuffledBuffer.data(), shuffledBuffer.size());
        m_shuffledBufferUpload->Unmap(0, nullptr);

        // Once uploaded, transition resources to correct states and perform the copy
        {
            D3D12_RESOURCE_BARRIER barriers[1] = {};
            barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_shuffledBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1, barriers);
            
            commandList->CopyResource(m_shuffledBuffer.Get(), m_shuffledBufferUpload.Get());

            barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_shuffledBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            commandList->ResourceBarrier(1, barriers);
        }

        commandList->Close();
        ID3D12CommandList* cmdLists[] = { commandList };
        auto commandQueue = m_deviceResources->GetCommandQueue();
        commandQueue->ExecuteCommandLists(1, cmdLists);

        m_deviceResources->WaitForGpu();
    }
}

void Game::CompareAgainstOriginalBC7Data(std::vector<uint8_t> const& BC7OriginalData, 
    std::vector<uint8_t> const& BC7UnshuffledData, UNSHUFFLE_PER_TEST_STATISTICS& stats)
{
    // Compare m_OriginalBC7Buffer vs m_BC7UnshuffledData block per block. Gather stats and log at the end

    uint32_t blockIndex = 0;

    uint32_t modeCounts[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint32_t modePassCounts[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint32_t modeFailCounts[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    do
    {
        uint32_t blockBaseAddress = blockIndex * 16;
        uint8_t modeByte = BC7OriginalData[blockBaseAddress];

        DWORD mode;
        if (!_BitScanForward(&mode, modeByte))
        {
            mode = 8;
        }

        bool pass = true;
        for (uint8_t byte = 0; byte < 16 && pass; ++byte)
        {
            if (BC7OriginalData[blockBaseAddress + byte] != BC7UnshuffledData[blockBaseAddress + byte])
            {
                pass = false;
            }
        }

        modeCounts[mode]++;
        if (pass) 
        {
            modePassCounts[mode]++;
        }
        else 
        {
            modeFailCounts[mode]++;
        }

        blockIndex++;
    } 
    while (blockIndex * 16 < BC7OriginalData.size());

    uint32_t modesEvaluated = 0;
    for (uint8_t i = 0; i < 9; ++i)  modesEvaluated += modeCounts[i];
    assert(modesEvaluated == blockIndex);

    stats.blockCount = modesEvaluated;

    bool passed = true;
    for (size_t i = 0; i < 9; ++i) 
    {
        passed = passed && (modeFailCounts[i] == 0);

        stats.perModeFailCount[i] = modeFailCounts[i];
        stats.perModePassCount[i] = modePassCounts[i];
    }

    stats.testPassed = passed;
}

void Game::ResetD3D12Resources() 
{
    // Prepass
    m_prepassCSRootSignature.Reset();
    m_prepassCSPipelineState.Reset();

    // PrefixSum
    m_prefixSumCSRootSignature.Reset();
    m_prefixSumCSPipelineState.Reset();

    // Mode Binning
    m_modeBinningCSRootSignature.Reset();
    m_modeBinningCSPipelineState.Reset();

    // Unshuffle
    m_unshuffleCSRootSignature.Reset();
    for (uint32_t i = 0; i < BC7_MODES_COUNT; ++i) 
    {
        m_unshuffleBinnedCSPSO[i].Reset();
    }
    m_unshufflePassCmdSignature.Reset();

    // Resources that need allocation
    m_outBCnBuffer.Reset();
    m_scratchBuffer.Reset();
    m_unshufflePassIndirectBuffer.Reset();

    // And their heaps
    m_intermediateHeap.Reset();
    m_outputHeap.Reset();

    // Inputs and Aux
    m_shuffledBuffer.Reset();
    m_shuffledBufferUpload.Reset();
    m_BCnReadback.Reset();
}