//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//

#pragma once

#ifndef ARCHIVE_FILE_H
#define ARCHIVE_FILE_H

#include <d3d12.h>
#include <cstdint>
#include <fstream>

#include <DirectXTex.h>

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
                (static_cast<uint32_t>(static_cast<uint8_t>(ch0)) \
                | (static_cast<uint32_t>(static_cast<uint8_t>(ch1)) << 8) \
                | (static_cast<uint32_t>(static_cast<uint8_t>(ch2)) << 16) \
                | (static_cast<uint32_t>(static_cast<uint8_t>(ch3)) << 24))
#endif

namespace ATG
{
    const int32_t fourCC = MAKEFOURCC('S', 'T', 'M', 'D');
}

/*
    Archive file layout:

    [ArchiveFileHeader]
    [ShuffledTextureMetadata [fileCount]]
    [ChunkMetadata1 [ChunkCount1]]
    [ChunnkedCompressedData1 [ChunkCount1]]
    [ChunkMetadata2 [ChunkCount2]]
    [ChunnkedCompressedData2 [ChunkCount2]]
    [ChunkMetadata3 [ChunkCount3]]
    [ChunnkedCompressedData3 [ChunkCount3]]
       :
       :
*/

struct ArchiveFileHeader
{
    uint32_t    reserved;
    uint32_t    fileCount;
};

struct ShuffledTextureMetadata
{
    uint32_t             reserved;
    wchar_t              name[128]; // optional name for debug purpose
    uint32_t             width;
    uint32_t             height;
    uint32_t             mipCount;
    DXGI_FORMAT          format;
    D3D12_TEXTURE_LAYOUT layout;
    uint32_t             chunkCount;
    uint64_t             offset;           // binary offset
    uint64_t             loadSize;         // size on disc
    uint64_t             uncompressedSize; // size on memory
};

struct ChunkMetadata
{
    uint32_t    transformId;
    uint64_t    offset;
    uint64_t    compressedSize;
    uint64_t    uncompressedSize;
};

class ArchiveFile : public std::ofstream
{
public:
    ArchiveFile(const wchar_t* filename);
    ~ArchiveFile();

    void WriteHeader(size_t fileCount);
    void WriteMetadata(
        std::wstring name,
        DirectX::TexMetadata& metaData,
        uint64_t chunkOffset,
        std::vector<ChunkMetadata>& chunkMetadatas);
};

#endif // ARCHIVE_FILE_H
