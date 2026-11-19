//-------------------------------------------------------------------------------------
// Deflate.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------
#pragma once

namespace gacl
{
    //  Replaces GACL's zstd compression routines with zlib ones (-deflate), for platforms whose decompression hardware only
    //  decodes DEFLATE: each stream compresses to one standard zlib stream, with DEFLATE blocks ended at the stream's section
    //  boundaries (see Deflate.cpp).

    void Deflate_InstallRoutines();
}
