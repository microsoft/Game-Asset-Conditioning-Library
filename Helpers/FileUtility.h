//-------------------------------------------------------------------------------------
// FileUtility.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#include <vector>
#include <string>
#include <memory>

// If linking with ZLIB, files with the .gz suffix will try to be unzipped first
//#define ENABLE_ZLIB

namespace Utility
{
    using namespace std;

    typedef shared_ptr<vector<uint8_t>> ByteArray;
    extern ByteArray NullFile;

    bool DoesFileExist(const wstring& fileName);

    // Reads the entire contents of a binary file.  If the file with the same name except with an additional
    // ".gz" suffix exists, it will be loaded and decompressed instead.
    // This operation blocks until the entire file is read.
    ByteArray ReadFileSync(const wstring& fileName);

} // namespace Utility
