//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//

#include "pch.h"

#include "..\\Common\\ArchiveFile.h"
#include "Condition.h"

using winrt::check_hresult;

static std::wstring to_lowercase(const std::wstring& input)
{
    std::wstring result = input;
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) { return std::tolower(c); });
    return result;
}

static void ShowHelpText()
{
    std::cout << "This sample shows how to use the Game Asset Conditioning Library (GACL) with DirectStorage."
        << std::endl
        << std::endl;
    std::cout << "USAGE: Condition.exe -in:<src directory> -out:<dst directory> [options]"
        << std::endl
        << std::endl;
    std::cout << "OPTIONS: -in:<src directory> = specify source directory." << std::endl;
    std::cout << "         -out:<dst directory> = specify destination directory." << std::endl;
    std::cout << "         -list:<file name> = specify image file list.(optional)" << std::endl;
    std::cout << "         -zstdLevel:<n> = specify Zstd compression level (1 to 22). If not specified, the size will be adjusted automatically.(optional)" << std::endl;
    std::cout << "         -zstdBlocksize:<n> = specify Zstd block size in bytes (1340 to 131072). If not specified, the size will be 8192 as default.(optional)" << std::endl;
    std::cout << "         -nc = disable compression.(optional)" << std::endl;
    std::cout << "         -ns = disable shuffle.(optional)" << std::endl;
    std::cout << "         -chunksizeKB:<n> = set the chunk size for parallel loading to n KBytes (64 to 1024). Default is 512.(optional)" << std::endl << std::endl;

    std::cout << "NOTE: You need to place DDS files encoded with BCn in the source directory. Files in other formats will be ignored."
        << std::endl;
    std::cout << "      When the list option is given, input image files are retrieved from the specified list file."
        << std::endl;
    std::cout << "      The list file is a text file that contains image file names and parameters(-zstd, -nc, -ns, -chunksizeKB) per line."
        << std::endl;
    std::cout << "      This sample only support top level mipmap and array. Other surfaces will be ignored."
        << std::endl;
    std::cout << "      The specified chunk size is aligned to a multiple of the BCn block size."
        << std::endl;
}

int wmain(int argc, wchar_t* argv[])
{
    std::wcout << L"GameAssetConditioningDemo : Condition\n\n";

    if (argc < 2)
    {
        ShowHelpText();
        return 1;
    }

    Parameter param;
    std::wstring inputFilePath = L".\\";
    std::wstring outputFilePath = L".\\";
    std::wstring listFileName;

    ParseParameter(argc, argv, param);

    for (int i = 1; i < argc; i++)
    {
        std::wstring cmd = to_lowercase(argv[i]);

        if (cmd.starts_with(L"-in:"))
        {
            inputFilePath = cmd.substr(4);
        }
        else if (cmd.starts_with(L"-out:"))
        {
            outputFilePath = cmd.substr(5);
        }
        else if (cmd.starts_with(L"-list:"))
        {
            listFileName = cmd.substr(6);
        }
    }

    if (!std::filesystem::exists(outputFilePath))
    {
        std::filesystem::create_directory(outputFilePath);
    }

    // Enumerate input files.
    std::vector<std::wstring> inputFiles;
    std::vector<Parameter> parameters;

    if (listFileName.empty())
    {
        FindFiles(inputFilePath, inputFiles);
    }
    else
    {
        LoadFileList(listFileName, inputFilePath, inputFiles, parameters);
    }

    if (inputFiles.empty())
    {
        debugPrint(L"** Error : No input files found in %s\n", inputFilePath.c_str());
        return 1;
    }

    // Initialize COM library to facilitate WIC usage
    check_hresult(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    // Determine output file name.
    std::filesystem::path fileName = L"shuffledTextures.bin";
    std::filesystem::path path = outputFilePath / fileName;
    std::wstring outFileName = path.wstring();

    // Write archive header.
    ArchiveFile outFile(outFileName.c_str());
    if (!outFile)
    {
        debugPrint(L"Failed to open archive file.\n");
        return 1;
    }

    outFile.WriteHeader(inputFiles.size());

    // Write conditioned data.
    DirectX::ScratchImage bcImage;
    uint64_t fileOffset = sizeof(ArchiveFileHeader) + sizeof(ShuffledTextureMetadata) * inputFiles.size();

    for (size_t i = 0; i < inputFiles.size(); i++)
    {
        HRESULT hr = S_OK;

        // Use per-file parameter if a list file is specified.
        if (!listFileName.empty())
        {
            param = parameters[i];
        }

        debugPrint(L"--- Processing file %d of %d --- ", i + 1, static_cast<int>(inputFiles.size()));

        // Load input image.
        hr = LoadInputImage(bcImage, inputFiles[i].c_str());
        if (FAILED(hr))
        {
            debugPrint(L"** Error : Failed to load image. 0x%08x\n\n", hr);
            return 1;
        }

        DirectX::TexMetadata inputMetaData = bcImage.GetMetadata();
        if (!DirectX::IsCompressed(inputMetaData.format))
        {
            debugPrint(L"** Error : Input image isn't BCn format.\n\n", hr);
            return 1;
        }

        debugPrint(L"format: %s, Zstd compression level: %d, Zstd block size: %d %s %s\n", DXGIFormatToString(inputMetaData.format),
            param.zstdCompressionLevel,
            param.zstdBlockSize,
            param.bShuffle ? L"" : L"(No Shuffle)",
            param.bCompress ? L"" : L"(No Zstd Compression)");

        if (inputMetaData.mipLevels != 1)
        {
            debugPrint(L"Input image has mipmaps, this sample process top level only.\n");
        }
        if (inputMetaData.arraySize != 1)
        {
            debugPrint(L"Input image is an array texture, this sample process single images only.\n");
        }

        // Shuffle and compress.
        auto startTimeShuffle = std::chrono::high_resolution_clock::now();
        std::vector<uint8_t> compressed;
        std::vector<ChunkMetadata> chunkMetadatas;

        hr = ShuffleAndCompress(bcImage, param, compressed, chunkMetadatas);
        if (FAILED(hr))
        {
            debugPrint(L"** Error : Shuffling is failed.\n\n");
            return 1;
        }

        auto endTimeShuffle = std::chrono::high_resolution_clock::now();
        float durationMS = std::chrono::duration<float, std::milli>(endTimeShuffle - startTimeShuffle).count();

        debugPrint(L"Completed: [ original: %zu compressed: %zu (%.2f%%, %.2f ms) ]\n",
            bcImage.GetPixelsSize(), compressed.size(), 100.0f * compressed.size() / bcImage.GetPixelsSize(), durationMS);

        // Write to file.
        outFile.seekp(sizeof(ArchiveFileHeader) + sizeof(ShuffledTextureMetadata) * i, std::ios::beg);

        outFile.WriteMetadata(inputFiles[i], inputMetaData, fileOffset, chunkMetadatas);

        outFile.seekp(fileOffset, std::ios::beg);
        outFile.write(reinterpret_cast<const char*>(chunkMetadatas.data()), sizeof(ChunkMetadata) * chunkMetadatas.size());
        outFile.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());

        fileOffset += sizeof(ChunkMetadata) * chunkMetadatas.size() + compressed.size();

        debugPrint(L"\n");
    }

    debugPrint(L"%zu textures saved: %s\n", inputFiles.size(), outFileName.c_str());

    return 0;
}

HRESULT LoadInputImage(DirectX::ScratchImage& image, const wchar_t* pFileName)
{
    debugPrint(L"Loading: %s\n", pFileName);

    // This sample only supports DDS input files.
    return DirectX::LoadFromDDSFile(pFileName, DirectX::DDS_FLAGS_NONE, nullptr, image);
}

HRESULT ShuffleAndCompress(DirectX::ScratchImage& srcForNonCurveTransforms, Parameter& param,
    std::vector<uint8_t>& buffer, std::vector<ChunkMetadata>& chunkMetadatas)
{
    debugPrint(L"Shuffling and compressing ...\n");

    DirectX::TexMetadata metaData = srcForNonCurveTransforms.GetMetadata();

    const size_t elementSize = DirectX::BytesPerBlock(metaData.format);
    const size_t blocksX = (metaData.width + 3) / 4;
    const size_t blocksY = (metaData.height + 3) / 4;
    const size_t mipSize = elementSize * blocksX * blocksY;

    const size_t bytesPerRow = blocksX * elementSize;
    size_t rowsPerChunk = (param.chunkSize + bytesPerRow - 1) / bytesPerRow;

    // Align to 4-row boundaries for BCn formats
    rowsPerChunk = ((rowsPerChunk + 3) / 4) * 4;

    const uint32_t chunkCount = static_cast<uint32_t>((blocksY + rowsPerChunk - 1) / rowsPerChunk);

    debugPrint(L"Texture info: %zux%zu blocks, %zu bytes per row, %zu rows per chunk, %u chunks\n",
        blocksX, blocksY, bytesPerRow, rowsPerChunk, chunkCount);

    // Clear the output buffer and reserve space
    buffer.clear();
    buffer.reserve(mipSize);
    chunkMetadatas.clear();
    chunkMetadatas.reserve(chunkCount);

    size_t fileOffset = 0;
    HRESULT hr = S_OK;

    for (uint32_t i = 0; i < chunkCount; i++)
    {
        // Calculate starting row for this chunk (in block units)
        size_t startRow = i * rowsPerChunk;
        size_t endRow = std::min(startRow + rowsPerChunk, blocksY);
        size_t actualRows = endRow - startRow;

        // Calculate actual chunk size in bytes
        uint64_t sourceSize = actualRows * bytesPerRow;
        uint64_t offset = startRow * bytesPerRow;

        ChunkMetadata chunkMeta = {};

        chunkMeta.offset = fileOffset;
        chunkMeta.uncompressedSize = sourceSize;

        if (param.bCompress)
        {
            std::vector<uint8_t> chunkBuffer(sourceSize);
            size_t chunkCompressedBytes = 0;

            SHUFFLE_COMPRESS_PARAMETERS shuffleParam = {};
            shuffleParam.SizeInBytes = sourceSize;
            shuffleParam.Format = metaData.format;
            shuffleParam.TextureData = srcForNonCurveTransforms.GetPixels() + offset;
            // if TargetBlockSize and ZstdCompressionLevel are 0, GACL will automatically determine the default values.
            shuffleParam.CompressSettings.Default.TargetBlockSize = param.zstdBlockSize;
            shuffleParam.CompressSettings.Default.ZstdCompressionLevel = param.zstdCompressionLevel;
            GACL_SHUFFLE_TRANSFORM transformId = param.bShuffle ? GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED : GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY;

            hr = GACL_ShuffleCompress_BCn(
                chunkBuffer.data(),
                &transformId,
                &chunkCompressedBytes,
                shuffleParam
            );

            if (hr == S_OK)
            {
                // Append compressed chunk to output buffer
                buffer.insert(buffer.end(), chunkBuffer.begin(), chunkBuffer.begin() + chunkCompressedBytes);
                chunkMeta.compressedSize = chunkCompressedBytes;
                chunkMeta.transformId = GetTransformType(transformId);

                debugPrint(L"  Chunk %d/%d: rows %zu-%zu, %llu bytes -> %zu bytes, transform=%d\n",
                    i + 1, chunkCount, startRow, endRow, sourceSize, chunkCompressedBytes, chunkMeta.transformId);
            }
            else
            {
                // Compression failed, use original data
                debugPrint(L"  Chunk %d/%d: Shuffle+Zstd compression did not reduce size, using original BC data.\n", i + 1, chunkCount);

                // Append uncompressed chunk
                buffer.insert(buffer.end(),
                    srcForNonCurveTransforms.GetPixels() + offset,
                    srcForNonCurveTransforms.GetPixels() + offset + sourceSize);
                chunkMeta.compressedSize = sourceSize;
                chunkMeta.transformId = DSTORAGE_GACL_SHUFFLE_TRANSFORM_NONE;
            }
        }
        else
        {
            // Compression disabled, use original data
            debugPrint(L"  Chunk %d/%d: Zstd compression disabled, using original BC data.\n", i + 1, chunkCount);
            // Append uncompressed chunk
            buffer.insert(buffer.end(),
                srcForNonCurveTransforms.GetPixels() + offset,
                srcForNonCurveTransforms.GetPixels() + offset + sourceSize);
            chunkMeta.compressedSize = sourceSize;
            chunkMeta.transformId = DSTORAGE_GACL_SHUFFLE_TRANSFORM_NONE;
        }

        chunkMetadatas.push_back(chunkMeta);
        fileOffset += chunkMeta.compressedSize;
    }

    return hr;
}

void ParseParameter(int argc, wchar_t* argv[], Parameter& param)
{
    for (int i = 1; i < argc; i++)
    {
        std::wstring cmd = to_lowercase(argv[i]);

        if (cmd == L"-ns")
        {
            param.bShuffle = false;
        }
        else if (cmd == L"-nc")
        {
            param.bCompress = false;
        }
        else if (cmd.starts_with(L"-zstdlevel:"))
        {
            std::wstring zstdLevelStr = cmd.substr(11);
            param.zstdCompressionLevel = std::clamp(std::stoi(zstdLevelStr), 1, 22);
        }
        else if (cmd.starts_with(L"-zstdblock:"))
        {
            std::wstring zstdBlockSizeStr = cmd.substr(11);
            param.zstdBlockSize = std::clamp(std::stoi(zstdBlockSizeStr), ZSTD_TARGETCBLOCKSIZE_MIN, ZSTD_TARGETCBLOCKSIZE_MAX);
        }
        else if (cmd.starts_with(L"-chunksizekb:"))
        {
            std::wstring chunkSizeStr = cmd.substr(13);
            param.chunkSize = std::clamp(std::stoi(chunkSizeStr), 64, 1024) * 1024;
        }
    }
}

DSTORAGE_GACL_SHUFFLE_TRANSFORM_TYPE GetTransformType(GACL_SHUFFLE_TRANSFORM gaclShuffleTransform)
{
    DSTORAGE_GACL_SHUFFLE_TRANSFORM_TYPE ret = DSTORAGE_GACL_SHUFFLE_TRANSFORM_NONE;

    switch (gaclShuffleTransform)
    {
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224:
        ret = DSTORAGE_GACL_SHUFFLE_TRANSFORM_BC1;
        break;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224:
        ret = DSTORAGE_GACL_SHUFFLE_TRANSFORM_BC3;
        break;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116:
        ret = DSTORAGE_GACL_SHUFFLE_TRANSFORM_BC4;
        break;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116:
        ret = DSTORAGE_GACL_SHUFFLE_TRANSFORM_BC5;
        break;
    default:
        ret = DSTORAGE_GACL_SHUFFLE_TRANSFORM_NONE;
        break;
    }

    return ret;
}

void FindFiles(std::wstring sourcePath, std::vector<std::wstring>& results)
{
    const std::wstring extension = L".dds";

    for (const auto& entry : std::filesystem::directory_iterator(sourcePath))
    {
        if (std::filesystem::is_regular_file(entry.status()))
        {
            const auto& file_path = entry.path();
            const auto& ext = to_lowercase(file_path.extension().wstring());

            if (ext == extension)
            {
                results.emplace_back(entry.path().wstring());
            }
        }
    }
}

void LoadFileList(std::wstring listFileName, std::wstring sourcePath,
    std::vector<std::wstring>& files, std::vector<Parameter>& parameters)
{
    std::wifstream inFile(listFileName);
    if (!inFile)
    {
        debugPrint(L"Failed to open list file.\n");
        return;
    }

    std::wstring line;
    while (std::getline(inFile, line))
    {
        if (!line.empty())
        {
            // Parse line into tokens, handling quoted strings
            std::wregex re(L"(\"([^\"]+)\"|(\\S+))");

            std::wsregex_iterator it(line.begin(), line.end(), re);
            std::wsregex_iterator end;

            std::vector<std::wstring> tokens;
            tokens.push_back(L"Condition.exe"); // dummy argv[0]
            for (; it != end; ++it) {
                if ((*it)[1].matched) {
                    tokens.push_back((*it)[1]);
                }
                else {
                    tokens.push_back((*it)[2]);
                }
            }

            if (tokens.size() > 1)
            {
                std::vector<wchar_t*> argv;
                argv.reserve(tokens.size() + 1);
                for (auto& s : tokens) {
                    argv.push_back(const_cast<wchar_t*>(s.c_str()));
                }
                argv.push_back(nullptr);

                Parameter param;
                ParseParameter((int)tokens.size(), const_cast<wchar_t**>(argv.data()), param);

                // Remove quatation mark and combine with input file path.
                std::filesystem::path path = sourcePath;
                std::wstring str = tokens.back();
                str.erase(std::remove(str.begin(), str.end(), L'"'), str.end());
                path /= str;

                files.emplace_back(path.wstring());
                parameters.emplace_back(param);
            }
        }
    }
}
