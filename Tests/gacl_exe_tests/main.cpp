// gacl_exe_tests\main.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include <gtest/gtest.h>

int __cdecl main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    testing::GTEST_FLAG(catch_exceptions) = false;
    return RUN_ALL_TESTS();
}