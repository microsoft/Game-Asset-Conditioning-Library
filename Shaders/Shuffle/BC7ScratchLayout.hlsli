//--------------------------------------------------------------------------------------
// BC7ScratchLayout.hlsli
//
// Byte layout of the BC7 split scratch header (CommonHeader). Macro only, so it can be
// included by both the shaders and the C++ hosts, which static_assert their mirrors of
// CommonHeader against it.
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#define GACL_BC7_PER_MODE_OFFSET     16u    // CommonHeader.perModeData
#define GACL_BC7_PER_MODE_STRIDE     32u    // sizeof(PerModeData)
#define GACL_BC7_PACKED_DATA_OFFSET  12u    // PerModeData.packedData
#define GACL_BC7_SC_WIDTH_OFFSET    304u    // CommonHeader.spaceCurveWidthInPixels
#define GACL_BC7_HEADER_SIZE        308u    // sizeof(CommonHeader)
