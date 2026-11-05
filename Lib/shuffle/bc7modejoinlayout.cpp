//-------------------------------------------------------------------------------------
// bc7modejoinlayout.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

// The BC7 Join fast paths compiled a second time with every field transform off. What remains is the pure bit layout
// (per mode and statics mask; mostly a bit permutation), which BC7_ModeJoin_Reverse probes to build its inverse. The
// field transforms are undone separately (bc7modejoinreverse.cpp).
//
// This is what keeps the CPU Join decoder in sync with the encoder: the decoder's inverse layout is derived from the
// encoder's own fast paths, so a Join layout change needs no CPU decoder change (the hand-written op lists that used to
// decode Join fell out of date with the fast paths).
//
// The field transforms are compile-time switches inside the fast paths, so a layout-only copy needs a second compile:
// this file turns the switches off and includes bc7modejoin.cpp, where the fast paths live, inside namespace
// BC7JoinLayout, which gives the copy its own symbols. Only the copy's BC7_ModeJoin_Shuffle_Fast and
// BC7_ModeJoin_FieldConfig are used. A new field-transform switch in bc7modejoin.cpp needs a matching line here;
// BC7_ModeJoin_ReverseSelfTest and the Join round-trip tests fail if the decoder and encoder drift apart.

#include "gacl.h"
#include "../BCnBlockDefs.h"
#include "bc7.h"
#include <chrono>
#include <cassert>
#include <algorithm>
#include <cmath>
#include <windows.h>
#include <intrin.h>

#undef GACL_IED
#undef GACL_IEDX
#undef GACL_IEB
#undef GACL_IEP
#undef GACL_IEPM
#undef GACL_IEPA
#undef GACL_IMS
#undef GACL_IMSM
#undef GACL_IMSP
#undef GACL_IEDL
#undef GACL_IIP
#undef GACL_IIPM
#undef GACL_IZB
#undef GACL_IXE
#undef GACL_IXEK2
#undef GACL_IXEK4
#undef GACL_IOF
#undef GACL_IOFM
#undef GACL_IOFA
#undef GACL_IAS
#undef GACL_JROT
#undef GACL_JBO
#undef GACL_JBOM
#undef GACL_JFP1
#undef GACL_JFP3
#undef GACL_JFP4
#undef GACL_JFP5
#undef GACL_JFP6
#undef GACL_JFP7

// bc7modejoin.cpp defaults to the production field transforms; the layout pass compiles them off
#define GACL_IED 0
#define GACL_IEDX 0
#define GACL_IEB 0
#define GACL_IEP 0
#define GACL_IEPA 0
#define GACL_IMS 0
#define GACL_IMSM 0xEF
#define GACL_IEDL 0
#define GACL_IIP 0
#define GACL_IIPM 7
#define GACL_IZB 0
#define GACL_IXE 0
#define GACL_IXEK2 0
#define GACL_IXEK4 0
#define GACL_IOF 0
#define GACL_IOFA 0
#define GACL_IAS 0
#define GACL_JBO 0
#define GACL_JBOM 0xFF
#define GACL_JFP1 0
#define GACL_JFP3 0
#define GACL_JFP4 0
#define GACL_JFP5 0
#define GACL_JFP6 0
#define GACL_JFP7 0

namespace BC7JoinLayout
{
#include "bc7modejoin.cpp"
}

void BC7_ModeJoin_LayoutShuffle(unsigned mode, const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    BC7JoinLayout::BC7_ModeJoin_Shuffle_Fast[mode](src, dest, metrics);
}

int BC7_ModeJoin_LayoutFieldConfig()
{
    return BC7JoinLayout::BC7_ModeJoin_FieldConfig();
}
