// gacl_lib_tests\main.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include <gtest/gtest.h>
#include "../ContentInput.h"
void RegisterLibraryContent(const std::filesystem::path& input);

int __cdecl main(int argc, char** argv)
{
    try
    {
        const auto input = ContentInput::ParseDirectory(argc, argv);
        RegisterLibraryContent(input);
        if (!input.empty()) testing::GTEST_FLAG(filter) = "ContentInput.*";
        testing::InitGoogleTest(&argc, argv);
        testing::GTEST_FLAG(catch_exceptions) = false;
        return ContentInput::RunTests(!input.empty());
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}