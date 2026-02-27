//-------------------------------------------------------------------------------------
// FileUtility.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include "FileUtility.h"
#include <fstream>

using namespace std;
using namespace Utility;

namespace Utility
{
    ByteArray NullFile = make_shared<vector<uint8_t> > (vector<uint8_t>() );
}

bool Utility::DoesFileExist(const wstring& fileName)
{
    struct _stat64 fileStat;
    return _wstat64(fileName.c_str(), &fileStat) >= 0;
}

static ByteArray ReadFileHelper(const wstring& fileName)
{
    struct _stat64 fileStat;
    int fileExists = _wstat64(fileName.c_str(), &fileStat);
    if (fileExists == -1)
        return NullFile;

    ifstream file( fileName, ios::in | ios::binary );
    if (!file)
        return NullFile;

    Utility::ByteArray byteArray = make_shared<vector<uint8_t> >( fileStat.st_size );
    file.read( (char*)byteArray->data(), byteArray->size() );
    file.close();

    return byteArray;
}

ByteArray Utility::ReadFileSync( const wstring& fileName)
{
    return ReadFileHelper(fileName);
}
