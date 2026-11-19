//-------------------------------------------------------------------------------------
// streamlayout.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#include "gacl.h"

#include <vector>

// The layout of a candidate stream, which the compression routines read through GACL_Compression_GetStreamLayout()
struct StreamLayout
{
    GACL_SHUFFLE_TRANSFORM Transform = GACL_SHUFFLE_TRANSFORM_NONE;
    bool Curved = false;
    std::vector<GACL_STREAM_SECTION> Sections;

    StreamLayout() = default;
    StreamLayout(GACL_SHUFFLE_TRANSFORM transform, bool curved) : Transform(transform), Curved(curved) {}

    // appends a section to the end of the stream; empty sections are left out
    void Add(size_t size, size_t elementSize, GACL_STREAM_SECTION_KIND kind)
    {
        if (size)
        {
            Sections.push_back({ Size(), size, elementSize, kind });
        }
    }

    size_t Size() const { return Sections.empty() ? 0 : Sections.back().Offset + Sections.back().Size; }
};

// Publishes a layout to GACL_Compression_GetStreamLayout() on the calling thread for the scope's lifetime (none for nullptr)
class StreamLayoutScope
{
public:
    explicit StreamLayoutScope(const StreamLayout* layout);
    ~StreamLayoutScope();
    StreamLayoutScope(const StreamLayoutScope&) = delete;
    StreamLayoutScope& operator=(const StreamLayoutScope&) = delete;

private:
    GACL_STREAM_LAYOUT m_view;
    const GACL_STREAM_LAYOUT* m_previous;
};

// Appends the sections of payloadBytes bytes of blocks shuffled by a BCn pattern (one of the v1 or v2 GACL_SHUFFLE_TRANSFORM_ZSTD_BC1-5
// ids): its fields, then the blocks it leaves unshuffled at the end (bcnex.cpp)
void StreamLayout_AddShuffled(StreamLayout& layout, uint8_t patternId, size_t payloadBytes);
