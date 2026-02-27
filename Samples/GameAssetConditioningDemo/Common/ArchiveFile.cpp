//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//

#include "ArchiveFile.h"

ArchiveFile::ArchiveFile(const wchar_t* filename)
    : std::ofstream(filename, std::ios::binary)
{
}

ArchiveFile::~ArchiveFile()
{
}

void ArchiveFile::WriteHeader(size_t fileCount)
{
    ArchiveFileHeader header = {};
    header.reserved = ATG::fourCC;
    header.fileCount = static_cast<uint32_t>(fileCount);
    this->write(reinterpret_cast<const char*>(&header), sizeof(ArchiveFileHeader));
}

void ArchiveFile::WriteMetadata(
    std::wstring name,
    DirectX::TexMetadata& metaData,
    uint64_t chunkOffset,
    std::vector<ChunkMetadata>& chunkMetadatas)
{
    ShuffledTextureMetadata shuffledTextureMetadata = {};
    shuffledTextureMetadata.reserved = ATG::fourCC;
    shuffledTextureMetadata.width = static_cast<uint32_t>(metaData.width);
    shuffledTextureMetadata.height = static_cast<uint32_t>(metaData.height);
    shuffledTextureMetadata.mipCount = static_cast<uint32_t>(metaData.mipLevels);
    shuffledTextureMetadata.layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    shuffledTextureMetadata.format = metaData.format;
    shuffledTextureMetadata.chunkCount = static_cast<uint32_t>(chunkMetadatas.size());
    shuffledTextureMetadata.offset = chunkOffset;

    for (uint32_t i = 0; i < shuffledTextureMetadata.chunkCount; i++)
    {
        shuffledTextureMetadata.loadSize += chunkMetadatas[i].compressedSize;
        shuffledTextureMetadata.uncompressedSize += chunkMetadatas[i].uncompressedSize;
    }

    if (name.size() > 127)
    {
        name = name.substr(0, 127);
    }
    name.copy(shuffledTextureMetadata.name, 127);
    shuffledTextureMetadata.name[name.size()] = L'\0';

    this->write(reinterpret_cast<const char*>(&shuffledTextureMetadata), sizeof(ShuffledTextureMetadata));
}
