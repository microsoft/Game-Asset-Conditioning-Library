//-------------------------------------------------------------------------------------
// Deflate.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#define NOMINMAX

#include "gacl.h"
#include "DirectXTex.h"
#include "Deflate.h"

#include <zlib.h>

#include <algorithm>
#include <climits>
#include <iterator>
#include <memory>
#include <new>
#include <vector>

/*  -deflate: GACL's compression routines (shuffle.h) replaced with zlib, for platforms whose decompression hardware only decodes
    DEFLATE.

    Each stream GACL compresses (each candidate of a shuffle+compress request, or -cr's data) becomes one zlib stream: RFC 1950
    wrapping RFC 1951 DEFLATE, at level 9 with a 32KB window, which any compliant inflate decodes.  DEFLATE codes each block with
    Huffman tables fitted to that block, so the encoder ends a block (Z_BLOCK: new tables, the 32KB history kept) where a shuffled
    stream's content changes, at the sections GACL_Compression_GetStreamLayout() reports, and splits sections into equal runs of
    whole elements per format:

        Format    Curved streams                    Linear streams
        BC1       each section, in runs <= 32KB     each section
        BC3       each section, in runs <= 32KB     each section, endpoint sections in 2 runs
        BC4       each section, in runs <= 32KB     each section, endpoint sections in 4 runs
        BC5       each section, in runs <= 32KB     each section, endpoint sections in 2 runs
        BC7       each section, in runs <= 32KB     each section

    Curved streams (EX and BC7 Split streams in space curve order) are series of 16KB micro-tiles of screen-adjacent blocks.  An EX
    header stays in the block of the section after it, and the blocks a shuffle pattern leaves unshuffled at the end of a stream
    in the block of its last section, so an unshuffled stream (one section of blocks, after any header) is only split when it is
    curved.  Other formats, and -cr data, have no forced ends.  zlib also ends blocks itself, whenever its symbol buffer fills.

    The rules were picked by a zlib sweep of InsectsDemo's BC1/3/4/5/7 mip 0 textures and 1000 CitySample virtual texture tile
    groups (4x4 tiles) per format, as the best of simple rules over both sets.  */

namespace
{
    // How a stream's sections are split into DEFLATE blocks: each endpoint section into EndpointRuns equal runs, each index
    // section into IndexRuns, and (MaxBytes not 0) any section into the fewest equal runs of at most MaxBytes
    struct BlockRule
    {
        size_t EndpointRuns;
        size_t IndexRuns;
        size_t MaxBytes;
    };

    struct FormatRule
    {
        DXGI_FORMAT Typeless;
        BlockRule Curved;
        BlockRule Linear;
    };

    const FormatRule c_formatRules[] =
    {
        { DXGI_FORMAT_BC1_TYPELESS, { 1, 1, 32 * 1024 }, { 1, 1, 0 } },
        { DXGI_FORMAT_BC3_TYPELESS, { 1, 1, 32 * 1024 }, { 2, 1, 0 } },
        { DXGI_FORMAT_BC4_TYPELESS, { 1, 1, 32 * 1024 }, { 4, 1, 0 } },
        { DXGI_FORMAT_BC5_TYPELESS, { 1, 1, 32 * 1024 }, { 2, 1, 0 } },
        { DXGI_FORMAT_BC7_TYPELESS, { 1, 1, 32 * 1024 }, { 1, 1, 0 } },
    };

    const int c_level = 9;
    const int c_windowBits = 15;        // DEFLATE's largest window (32KB), with the zlib header and trailer
    const int c_memLevel = 9;

    // zlib counts bytes in 32 bit types on Windows
    const size_t c_maxStreamBytes = size_t(1) << 31;

    struct DeflateContext
    {
        z_stream Stream{};
        size_t LayoutBytes = 0;         // the size of the stream BlockEnds is for
        std::vector<size_t> BlockEnds;  // forced block ends, increasing, inside (0, LayoutBytes)
    };

    std::vector<size_t> GetBlockEnds(const GACL_STREAM_LAYOUT& layout, const BlockRule& rule, size_t streamBytes)
    {
        std::vector<size_t> ends;
        bool first = true;
        GACL_STREAM_SECTION_KIND previous = GACL_STREAM_SECTION_KIND_HEADER;
        for (size_t s = 0; s < layout.SectionCount; s++)
        {
            const GACL_STREAM_SECTION& section = layout.Sections[s];

            // an EX header joins the section after it, and a shuffle pattern's leftover blocks the section before them
            const bool leftover = section.Kind == GACL_STREAM_SECTION_KIND_BLOCKS &&
                (previous == GACL_STREAM_SECTION_KIND_ENDPOINTS || previous == GACL_STREAM_SECTION_KIND_INDICES);
            previous = section.Kind;
            if (section.Kind == GACL_STREAM_SECTION_KIND_HEADER || leftover)
            {
                continue;
            }

            if (!first)
            {
                ends.push_back(section.Offset);
            }
            first = false;

            size_t runs = section.Kind == GACL_STREAM_SECTION_KIND_ENDPOINTS ? rule.EndpointRuns :
                section.Kind == GACL_STREAM_SECTION_KIND_INDICES ? rule.IndexRuns : 1;
            if (rule.MaxBytes != 0)
            {
                runs = std::max(runs, (section.Size + rule.MaxBytes - 1) / rule.MaxBytes);
            }
            const size_t elementSize = std::max<size_t>(section.ElementSize, 1);
            const size_t elements = section.Size / elementSize;
            for (size_t r = 1; r < runs && elements > 0; r++)
            {
                ends.push_back(section.Offset + elementSize * (elements * r / runs));
            }
        }

        std::sort(ends.begin(), ends.end());
        ends.erase(std::unique(ends.begin(), ends.end()), ends.end());
        ends.erase(std::remove_if(ends.begin(), ends.end(), [streamBytes](size_t e) { return e == 0 || e >= streamBytes; }), ends.end());
        return ends;
    }

    HRESULT DeflateInit(void** ccContext, size_t* destBytesRequired, const SHUFFLE_COMPRESS_PARAMETERS* params)
    {
        if (ccContext == nullptr || destBytesRequired == nullptr || params == nullptr)
        {
            return E_INVALIDARG;
        }
        *ccContext = nullptr;
        *destBytesRequired = 0;
        if (params->SizeInBytes > c_maxStreamBytes)
        {
            return E_INVALIDARG;
        }

        std::unique_ptr<DeflateContext> context;
        try
        {
            context = std::make_unique<DeflateContext>();
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
        const int initResult = deflateInit2(&context->Stream, c_level, Z_DEFLATED, c_windowBits, c_memLevel, Z_DEFAULT_STRATEGY);
        if (initResult != Z_OK)
        {
            return initResult == Z_MEM_ERROR ? E_OUTOFMEMORY : E_FAIL;
        }

        const GACL_STREAM_LAYOUT* layout = GACL_Compression_GetStreamLayout();
        const DXGI_FORMAT typeless = DirectX::MakeTypeless(params->Format);
        const auto rule = std::find_if(std::begin(c_formatRules), std::end(c_formatRules), [typeless](const FormatRule& r) { return r.Typeless == typeless; });
        if (layout != nullptr && rule != std::end(c_formatRules))
        {
            try
            {
                for (size_t s = 0; s < layout->SectionCount; s++)
                {
                    context->LayoutBytes += layout->Sections[s].Size;
                }
                context->BlockEnds = GetBlockEnds(*layout, layout->Curved ? rule->Curved : rule->Linear, context->LayoutBytes);
            }
            catch (const std::bad_alloc&)
            {
                deflateEnd(&context->Stream);
                return E_OUTOFMEMORY;
            }
        }

        // deflateBound() without a stream is zlib's bound for any settings, and each forced end can add at most a stored block's
        // 5 byte header
        *destBytesRequired = size_t(deflateBound(Z_NULL, uLong(std::max<size_t>(params->SizeInBytes, 1)))) + 5 * context->BlockEnds.size() + 64;
        *ccContext = context.release();
        return S_OK;
    }

    HRESULT DeflateCompress(void* ccContext, void* dest, size_t* destBytes, const void* src, size_t srcBytes)
    {
        DeflateContext* context = static_cast<DeflateContext*>(ccContext);
        if (context == nullptr || destBytes == nullptr || dest == nullptr || (src == nullptr && srcBytes != 0))
        {
            return E_INVALIDARG;
        }
        const size_t capacity = *destBytes;
        *destBytes = 0;
        if (srcBytes > c_maxStreamBytes)
        {
            return E_INVALIDARG;
        }

        z_stream& stream = context->Stream;
        if (deflateReset(&stream) != Z_OK)
        {
            return E_FAIL;
        }

        // the block ends are for the stream the layout describes
        const std::vector<size_t> noEnds;
        const std::vector<size_t>& ends = srcBytes == context->LayoutBytes ? context->BlockEnds : noEnds;

        const Bytef* input = static_cast<const Bytef*>(src);
        stream.next_out = static_cast<Bytef*>(dest);
        stream.avail_out = uInt(std::min<size_t>(capacity, UINT_MAX));
        size_t offset = 0;
        for (size_t k = 0; k <= ends.size(); k++)
        {
            const bool last = k == ends.size();
            const size_t end = last ? srcBytes : ends[k];
            stream.next_in = const_cast<Bytef*>(input + offset);
            stream.avail_in = uInt(end - offset);

            // Z_BLOCK ends the DEFLATE block and keeps the window; with output space left, all input is consumed
            const int result = deflate(&stream, last ? Z_FINISH : Z_BLOCK);
            if (last ? result != Z_STREAM_END : (result != Z_OK || stream.avail_in != 0 || stream.avail_out == 0))
            {
                return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            }
            offset = end;
        }

        *destBytes = size_t(stream.total_out);
        return S_OK;
    }

    HRESULT DeflateCleanup(void* ccContext)
    {
        DeflateContext* context = static_cast<DeflateContext*>(ccContext);
        if (context != nullptr)
        {
            deflateEnd(&context->Stream);
            delete context;
        }
        return S_OK;
    }
}

void gacl::Deflate_InstallRoutines()
{
    GACL_Compression_InitRoutine = &DeflateInit;
    GACL_Compression_CompressRoutine = &DeflateCompress;
    GACL_Compression_CleanupRoutine = &DeflateCleanup;
}
