// ml_rdo_tests\main.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#if GACL_INCLUDE_CLER

#include <gtest/gtest.h>
int __cdecl main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    testing::GTEST_FLAG(catch_exceptions) = false;
    return RUN_ALL_TESTS();
}
#endif