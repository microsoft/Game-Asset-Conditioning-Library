//-------------------------------------------------------------------------------------
// bc7modesplit.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include "gacl.h"
#include "../BCnBlockDefs.h"
#include "bc7.h"

#include <windows.h>

#include <algorithm>
#include <cassert>

namespace
{

enum CopyBitDestination
{
    Mode = 0,      // transformed bits
    Color = 2,      // Low entropy bytes
    Misc = 3,      // High entroy bytes
    Scraps = 4,      // High entropy spare bits
};

struct CopyBitsOrder
{
    CopyBitDestination DestStream;
    uint8_t DestBitOffset;
    uint8_t CopyBitCount;
};

struct CopyBitsSequence
{
    size_t OpCount;
    CopyBitsOrder Ops[124];
};



void GetBC_ModeSplit_CopyBitsOrderMode0(CopyBitsSequence& sequence, BC7ModeSplitShuffleOptions opt, BC7TextureMetrics metrics)
{
    UNREFERENCED_PARAMETER(metrics);
    memset(&sequence, 0, sizeof(sequence));

    //  Bit Offset  Bits or ordering and count
    //  0           1 bit Mode (1)
    //  1           4 bits Partition
    //  5           4 bits R0   4 bits R1   4 bits R2   4 bits R3   4 bits R4   4 bits R5
    //  29          4 bits G0   4 bits G1   4 bits G2   4 bits G3   4 bits G4   4 bits G5
    //  53          4 bits B0   4 bits B1   4 bits B2   4 bits B3   4 bits B4   4 bits B5
    //  77          1 bit P0    1 bit P1    1 bit P2    1 bit P3    1 bit P4    1 bit P5
    //  83          45 bits Index

    constexpr size_t kModeBits = 1;
    constexpr size_t kChannels = 6;

    if (opt.Patterns[0] == BC7ModeSplitShufflePattern::ColorPlane4bit || opt.Patterns[0] == BC7ModeSplitShufflePattern::EndpointPair4bit)  // experimental pattern IDs: 1, 4
    {
        // Groups 6 fields of 4 bits in two optional ways to make 24bit (3 byte) sequences
        // 1) groups colors, which works well for some textures with low channel correlation 
        // 4) group by endpoint RGB which works will when there is high correlation betwwen channels

        uint8_t ColorGrouping[] =
        {
            0,   4,   8,   12,  16, 20,
            24,  28,  32,  36,  40, 44,
            48,  52,  56,  60,  64, 68,
        };

        uint8_t EndpointGrouping[] =
        {
            0,   4,   24,  28,  48, 52,
            8,   12,  32,  36,  56, 60,
            16,  20,  40,  44,  64, 68,
        };

        enum {
            R0 = 0, R1, R2, R3, R4, R5,
            G0, G1, G2, G3, G4, G5,
            B0, B1, B2, B3, B4, B5,
        };

        uint8_t* grouping = (opt.Patterns[0] == BC7ModeSplitShufflePattern::ColorPlane4bit) ? ColorGrouping : EndpointGrouping;

        CopyBitsOrder ops[] = {
            {   //  Mode (1)
                CopyBitDestination::Mode,
                0,
                1
            },

            {   //  Partition bits 
                CopyBitDestination::Misc,
                0,
                4
            },

            {   CopyBitDestination::Color,                grouping[R0],                4 },
            {   CopyBitDestination::Color,                grouping[R1],                4 },
            {   CopyBitDestination::Color,                grouping[R2],                4 },
            {   CopyBitDestination::Color,                grouping[R3],                4 },
            {   CopyBitDestination::Color,                grouping[R4],                4 },
            {   CopyBitDestination::Color,                grouping[R5],                4 },

            {   CopyBitDestination::Color,                grouping[G0],                4 },
            {   CopyBitDestination::Color,                grouping[G1],                4 },
            {   CopyBitDestination::Color,                grouping[G2],                4 },
            {   CopyBitDestination::Color,                grouping[G3],                4 },
            {   CopyBitDestination::Color,                grouping[G4],                4 },
            {   CopyBitDestination::Color,                grouping[G5],                4 },

            {   CopyBitDestination::Color,                grouping[B0],                4 },
            {   CopyBitDestination::Color,                grouping[B1],                4 },
            {   CopyBitDestination::Color,                grouping[B2],                4 },
            {   CopyBitDestination::Color,                grouping[B3],                4 },
            {   CopyBitDestination::Color,                grouping[B4],                4 },
            {   CopyBitDestination::Color,                grouping[B5],                4 },

            // above covers 1 mode bit, 4 partition,  72 color bits, = 77 bits

            //  p bits and index are 77-127 = 51 bits
            {   CopyBitDestination::Misc,                   4,                44 },

            //  remainder =  51 - 44 = 7 spare bits for the scrap stream
            {   CopyBitDestination::Scraps,                 0,                7 },
        };

        sequence.OpCount = ARRAYSIZE(ops);
        memcpy(sequence.Ops, ops, sizeof(ops));
    }
    else if (opt.Patterns[0] == BC7ModeSplitShufflePattern::EndpointPairSignificantBitInderleaved)
    {
        //  The three endpoint pairs  (E0\E1, E2\E3, E4\E5) are encoded into three different 3 byte fields in the same pattern used for several other modes

        //  24 bits of RGB 0\1:    bits 0 - 23
        //                                                                                                        Most significant 
        //  R0'0 R1'0 G0'0 G1'0 B0'0 B1'0   R0'1 R1'1 G0'1 G1'1 B0'1 B1'1   R0'2 R1'2 G0'2 G1'2 B0'2 B1'2   R0'3 R1'3 G0'3 G1'3 B0'3 B1'3   
        //
        //  24 bits of RGB 2\3:    bits 24 - 47
        //                                                                                                        Most significant 
        //  R2'0 R3'0 G2'0 G3'0 B2'0 B3'0   R2'1 R3'1 G2'1 G3'1 B2'1 B3'1   R2'2 R3'2 G2'2 G3'2 B2'2 B3'2   R2'3 R3'3 G2'3 G3'3 B2'3 B3'3   
        //
        //  24 bits of RGB 2\3:    bits 48 - 71
        //                                                                                                        Most significant 
        //  R4'0 R5'0 G4'0 G5'0 B4'0 B5'0   R4'1 R5'1 G4'1 G5'1 B4'1 B5'1   R4'2 R5'2 G4'2 G5'2 B4'2 B5'2   R4'3 R5'3 G4'3 G5'3 B4'3 B5'3   
        //

        enum ChannelOrder
        {
            R0 = 0,
            R1 = 1,
            G0 = 2,
            G1 = 3,
            B0 = 4,
            B1 = 5,

            R2 = 24,
            R3 = 25,
            G2 = 26,
            G3 = 27,
            B2 = 28,
            B3 = 29,

            R4 = 48,
            R5 = 49,
            G4 = 50,
            G5 = 51,
            B4 = 52,
            B5 = 53,
        };


        CopyBitsOrder ops[] = {
            {   // Mode [1]
                CopyBitDestination::Mode,
                0,
                kModeBits
            },
            {   // Partition 
                CopyBitDestination::Misc,
                0,
                4
            },

            { CopyBitDestination::Color, R0 + (kChannels * 0), 1 },  // R0'0
            { CopyBitDestination::Color, R0 + (kChannels * 1), 1 },  // R0'1
            { CopyBitDestination::Color, R0 + (kChannels * 2), 1 },  // R0'2
            { CopyBitDestination::Color, R0 + (kChannels * 3), 1 },  // R0'3

            { CopyBitDestination::Color, R1 + (kChannels * 0), 1 },  // R1'0
            { CopyBitDestination::Color, R1 + (kChannels * 1), 1 },  // R1'1
            { CopyBitDestination::Color, R1 + (kChannels * 2), 1 },  // R1'2
            { CopyBitDestination::Color, R1 + (kChannels * 3), 1 },  // R1'3

            { CopyBitDestination::Color, R2 + (kChannels * 0), 1 },  // R2'0
            { CopyBitDestination::Color, R2 + (kChannels * 1), 1 },  // R2'1
            { CopyBitDestination::Color, R2 + (kChannels * 2), 1 },  // R2'2
            { CopyBitDestination::Color, R2 + (kChannels * 3), 1 },  // R2'3

            { CopyBitDestination::Color, R3 + (kChannels * 0), 1 },  // R3'0
            { CopyBitDestination::Color, R3 + (kChannels * 1), 1 },  // R3'1
            { CopyBitDestination::Color, R3 + (kChannels * 2), 1 },  // R3'2
            { CopyBitDestination::Color, R3 + (kChannels * 3), 1 },  // R3'3

            { CopyBitDestination::Color, R4 + (kChannels * 0), 1 },  // R4'0
            { CopyBitDestination::Color, R4 + (kChannels * 1), 1 },  // R4'1
            { CopyBitDestination::Color, R4 + (kChannels * 2), 1 },  // R4'2
            { CopyBitDestination::Color, R4 + (kChannels * 3), 1 },  // R4'3

            { CopyBitDestination::Color, R5 + (kChannels * 0), 1 },  // R5'0
            { CopyBitDestination::Color, R5 + (kChannels * 1), 1 },  // R5'1
            { CopyBitDestination::Color, R5 + (kChannels * 2), 1 },  // R5'2
            { CopyBitDestination::Color, R5 + (kChannels * 3), 1 },  // R5'3


            { CopyBitDestination::Color, G0 + (kChannels * 0), 1 },  // G0'0
            { CopyBitDestination::Color, G0 + (kChannels * 1), 1 },  // G0'1
            { CopyBitDestination::Color, G0 + (kChannels * 2), 1 },  // G0'2
            { CopyBitDestination::Color, G0 + (kChannels * 3), 1 },  // G0'3

            { CopyBitDestination::Color, G1 + (kChannels * 0), 1 },  // G1'0
            { CopyBitDestination::Color, G1 + (kChannels * 1), 1 },  // G1'1
            { CopyBitDestination::Color, G1 + (kChannels * 2), 1 },  // G1'2
            { CopyBitDestination::Color, G1 + (kChannels * 3), 1 },  // G1'3

            { CopyBitDestination::Color, G2 + (kChannels * 0), 1 },  // G2'0
            { CopyBitDestination::Color, G2 + (kChannels * 1), 1 },  // G2'1
            { CopyBitDestination::Color, G2 + (kChannels * 2), 1 },  // G2'2
            { CopyBitDestination::Color, G2 + (kChannels * 3), 1 },  // G2'3

            { CopyBitDestination::Color, G3 + (kChannels * 0), 1 },  // G3'0
            { CopyBitDestination::Color, G3 + (kChannels * 1), 1 },  // G3'1
            { CopyBitDestination::Color, G3 + (kChannels * 2), 1 },  // G3'2
            { CopyBitDestination::Color, G3 + (kChannels * 3), 1 },  // G3'3

            { CopyBitDestination::Color, G4 + (kChannels * 0), 1 },  // G4'0
            { CopyBitDestination::Color, G4 + (kChannels * 1), 1 },  // G4'1
            { CopyBitDestination::Color, G4 + (kChannels * 2), 1 },  // G4'2
            { CopyBitDestination::Color, G4 + (kChannels * 3), 1 },  // G4'3

            { CopyBitDestination::Color, G5 + (kChannels * 0), 1 },  // G5'0
            { CopyBitDestination::Color, G5 + (kChannels * 1), 1 },  // G5'1
            { CopyBitDestination::Color, G5 + (kChannels * 2), 1 },  // G5'2
            { CopyBitDestination::Color, G5 + (kChannels * 3), 1 },  // G5'3


            { CopyBitDestination::Color, B0 + (kChannels * 0), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 1), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 2), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 3), 1 },  // B0'0

            { CopyBitDestination::Color, B1 + (kChannels * 0), 1 },  // B1'0
            { CopyBitDestination::Color, B1 + (kChannels * 1), 1 },  // B1'1
            { CopyBitDestination::Color, B1 + (kChannels * 2), 1 },  // B1'2
            { CopyBitDestination::Color, B1 + (kChannels * 3), 1 },  // B1'3

            { CopyBitDestination::Color, B2 + (kChannels * 0), 1 },  // B2'0
            { CopyBitDestination::Color, B2 + (kChannels * 1), 1 },  // B2'1
            { CopyBitDestination::Color, B2 + (kChannels * 2), 1 },  // B2'2
            { CopyBitDestination::Color, B2 + (kChannels * 3), 1 },  // B2'3

            { CopyBitDestination::Color, B3 + (kChannels * 0), 1 },  // B3'0
            { CopyBitDestination::Color, B3 + (kChannels * 1), 1 },  // B3'1
            { CopyBitDestination::Color, B3 + (kChannels * 2), 1 },  // B3'2
            { CopyBitDestination::Color, B3 + (kChannels * 3), 1 },  // B3'3

            { CopyBitDestination::Color, B4 + (kChannels * 0), 1 },  // B4'0
            { CopyBitDestination::Color, B4 + (kChannels * 1), 1 },  // B4'1
            { CopyBitDestination::Color, B4 + (kChannels * 2), 1 },  // B4'2
            { CopyBitDestination::Color, B4 + (kChannels * 3), 1 },  // B4'3

            { CopyBitDestination::Color, B5 + (kChannels * 0), 1 },  // B5'0
            { CopyBitDestination::Color, B5 + (kChannels * 1), 1 },  // B5'1
            { CopyBitDestination::Color, B5 + (kChannels * 2), 1 },  // B5'2
            { CopyBitDestination::Color, B5 + (kChannels * 3), 1 },  // B5'3

            // above is 1 mode bits, 4 partition, 72 color bits, 51 bits remain
            // misc has 4 partition bits.

            {   // P bits + some index, to reach 48 bits
                CopyBitDestination::Misc,
                4,
                44,

            },
            {   // remaining index to scraps
                CopyBitDestination::Scraps,
                0,
                7
            }
        };
        assert(_countof(ops) <= _countof(sequence.Ops));
        sequence.OpCount = _countof(ops);
        memcpy(sequence.Ops, ops, sizeof(ops));
    }
    else
    {
        assert(false);
    }

    return;
}

void GetBC_ModeSplit_CopyBitsOrderMode1(CopyBitsSequence& sequence, BC7ModeSplitShuffleOptions opt, BC7TextureMetrics metrics)
{
    UNREFERENCED_PARAMETER(metrics);
    memset(&sequence, 0, sizeof(sequence));

    //  Bit Offset  Bits or ordering and count
    //  0           2 bits Mode (10)
    //  2           6 bits Partition
    //  8           6 bits R0   6 bits R1   6 bits R2   6 bits R3
    //  32          6 bits G0   6 bits G1   6 bits G2   6 bits G3
    //  56          6 bits B0   6 bits B1   6 bits B2   6 bits B3
    //  80          1 bit P0    1 bit P1
    //  82          46 bits Index

    constexpr size_t kModeBits = 2;
    constexpr size_t kChannels = 6;

    if (opt.Patterns[1] == BC7ModeSplitShufflePattern::EndpointQuadSignificantBitInderleaved) // experimental ID 5
    {
        //  This shuffle treats 0/1 endpoints as a group and 2/3 endpoints as a different group that vary independenty.
        //  Interleaves bits within each endpoint pair, reversing the second one to group most significat bits in the center.
        //  Boundary between endpoint pairs is positioned mid-byte

        //  36 bits of RGB 0\1:    bits 0 - 35
        //                                                                                                                3                               4                      Most significant 5
        //  R0'0 R1'0 G0'0 G1'0 B0'0 B1'0   R0'1 R1'1 G0'1 G1'1 B0'1 B1'1   R0'2 R1'2 G0'2 G1'2 B0'2 B1'2   R0'3 R1'3 G0'3 G1'3 B0'3 B1'3   R0'4 R1'4 G0'4 G1'4 B0'4 B1'4   R0'5 R1'5 G0'5 G1'5 B0'5 B1'5 
        //
        //  followed by reversed 36 bits of RGB 2\3:    bits 36 - 71 
        //    
        //       Most significant 5                     4                                   4
        //  B2'5 B3'5 G2'5 G3'5 R2'5 R3'5   B2'4 B3'4 G2'4 G3'4 R2'4 R3'4   B2'3 B3'3 G2'3 G3'3 R2'3 R3'3   B2'2 B3'2 G2'2 G3'2 R2'2 R3'2   B2'1 B3'1 G2'1 G3'1 R2'1 R3'1   B2'0 B3'0 G2'0 G3'0 R2'0 R3'0   
        //


        enum ChannelOrder
        {
            R0 = 0,
            R1 = 1,
            G0 = 2,
            G1 = 3,
            B0 = 4,
            B1 = 5,

            R2 = 70,
            R3 = 71,
            G2 = 68,
            G3 = 69,
            B2 = 66,
            B3 = 67,
        };

        CopyBitsOrder ops[] = {
            {   // Mode [10]
                CopyBitDestination::Mode,
                0,
                kModeBits
            },
            {   // Partition 
                CopyBitDestination::Misc,
                0,
                6
            },


            { CopyBitDestination::Color, R0 + (kChannels * 0), 1 },  // R0'0
            { CopyBitDestination::Color, R0 + (kChannels * 1), 1 },  // R0'1
            { CopyBitDestination::Color, R0 + (kChannels * 2), 1 },  // R0'2
            { CopyBitDestination::Color, R0 + (kChannels * 3), 1 },  // R0'3
            { CopyBitDestination::Color, R0 + (kChannels * 4), 1 },  // R0'4
            { CopyBitDestination::Color, R0 + (kChannels * 5), 1 },  // R0'5

            { CopyBitDestination::Color, R1 + (kChannels * 0), 1 },  // R1'0
            { CopyBitDestination::Color, R1 + (kChannels * 1), 1 },  // R1'1
            { CopyBitDestination::Color, R1 + (kChannels * 2), 1 },  // R1'2
            { CopyBitDestination::Color, R1 + (kChannels * 3), 1 },  // R1'3
            { CopyBitDestination::Color, R1 + (kChannels * 4), 1 },  // R1'4
            { CopyBitDestination::Color, R1 + (kChannels * 5), 1 },  // R1'5


            { CopyBitDestination::Color, R2 - (kChannels * 0), 1 },  // R2'0
            { CopyBitDestination::Color, R2 - (kChannels * 1), 1 },  // R2'1
            { CopyBitDestination::Color, R2 - (kChannels * 2), 1 },  // R2'2
            { CopyBitDestination::Color, R2 - (kChannels * 3), 1 },  // R2'3
            { CopyBitDestination::Color, R2 - (kChannels * 4), 1 },  // R2'4
            { CopyBitDestination::Color, R2 - (kChannels * 5), 1 },  // R2'5

            { CopyBitDestination::Color, R3 - (kChannels * 0), 1 },  // R3'0
            { CopyBitDestination::Color, R3 - (kChannels * 1), 1 },  // R3'1
            { CopyBitDestination::Color, R3 - (kChannels * 2), 1 },  // R3'2
            { CopyBitDestination::Color, R3 - (kChannels * 3), 1 },  // R3'3
            { CopyBitDestination::Color, R3 - (kChannels * 4), 1 },  // R3'4
            { CopyBitDestination::Color, R3 - (kChannels * 5), 1 },  // R3'5


            { CopyBitDestination::Color, G0 + (kChannels * 0), 1 },  // G0'0
            { CopyBitDestination::Color, G0 + (kChannels * 1), 1 },  // G0'1
            { CopyBitDestination::Color, G0 + (kChannels * 2), 1 },  // G0'2
            { CopyBitDestination::Color, G0 + (kChannels * 3), 1 },  // G0'3
            { CopyBitDestination::Color, G0 + (kChannels * 4), 1 },  // G0'4
            { CopyBitDestination::Color, G0 + (kChannels * 5), 1 },  // G0'5

            { CopyBitDestination::Color, G1 + (kChannels * 0), 1 },  // G1'0
            { CopyBitDestination::Color, G1 + (kChannels * 1), 1 },  // G1'1
            { CopyBitDestination::Color, G1 + (kChannels * 2), 1 },  // G1'2
            { CopyBitDestination::Color, G1 + (kChannels * 3), 1 },  // G1'3
            { CopyBitDestination::Color, G1 + (kChannels * 4), 1 },  // G1'4
            { CopyBitDestination::Color, G1 + (kChannels * 5), 1 },  // G1'5


            { CopyBitDestination::Color, G2 - (kChannels * 0), 1 },  // G2'0
            { CopyBitDestination::Color, G2 - (kChannels * 1), 1 },  // G2'1
            { CopyBitDestination::Color, G2 - (kChannels * 2), 1 },  // G2'2
            { CopyBitDestination::Color, G2 - (kChannels * 3), 1 },  // G2'3
            { CopyBitDestination::Color, G2 - (kChannels * 4), 1 },  // G2'4
            { CopyBitDestination::Color, G2 - (kChannels * 5), 1 },  // G2'5

            { CopyBitDestination::Color, G3 - (kChannels * 0), 1 },  // G3'0
            { CopyBitDestination::Color, G3 - (kChannels * 1), 1 },  // G3'1
            { CopyBitDestination::Color, G3 - (kChannels * 2), 1 },  // G3'2
            { CopyBitDestination::Color, G3 - (kChannels * 3), 1 },  // G3'3
            { CopyBitDestination::Color, G3 - (kChannels * 4), 1 },  // G3'4
            { CopyBitDestination::Color, G3 - (kChannels * 5), 1 },  // G3'5


            { CopyBitDestination::Color, B0 + (kChannels * 0), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 1), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 2), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 3), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 4), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 5), 1 },  // B0'0

            { CopyBitDestination::Color, B1 + (kChannels * 0), 1 },  // B1'0
            { CopyBitDestination::Color, B1 + (kChannels * 1), 1 },  // B1'1
            { CopyBitDestination::Color, B1 + (kChannels * 2), 1 },  // B1'2
            { CopyBitDestination::Color, B1 + (kChannels * 3), 1 },  // B1'3
            { CopyBitDestination::Color, B1 + (kChannels * 4), 1 },  // B1'4
            { CopyBitDestination::Color, B1 + (kChannels * 5), 1 },  // B1'5


            { CopyBitDestination::Color, B2 - (kChannels * 0), 1 },  // B2'0
            { CopyBitDestination::Color, B2 - (kChannels * 1), 1 },  // B2'1
            { CopyBitDestination::Color, B2 - (kChannels * 2), 1 },  // B2'2
            { CopyBitDestination::Color, B2 - (kChannels * 3), 1 },  // B2'3
            { CopyBitDestination::Color, B2 - (kChannels * 4), 1 },  // B2'4
            { CopyBitDestination::Color, B2 - (kChannels * 5), 1 },  // B2'5

            { CopyBitDestination::Color, B3 - (kChannels * 0), 1 },  // B3'0
            { CopyBitDestination::Color, B3 - (kChannels * 1), 1 },  // B3'1
            { CopyBitDestination::Color, B3 - (kChannels * 2), 1 },  // B3'2
            { CopyBitDestination::Color, B3 - (kChannels * 3), 1 },  // B3'3
            { CopyBitDestination::Color, B3 - (kChannels * 4), 1 },  // B3'4
            { CopyBitDestination::Color, B3 - (kChannels * 5), 1 },  // B3'5


            // above is 2 mode bits, 6 partition, 72 color bits, 56 bits remain
            // misc has 6 partition bits.


            {   // P bits + some index, to reach 48 bits
                CopyBitDestination::Misc,
                6,
                42,

            },
            {   // remaining index to scraps
                CopyBitDestination::Scraps,
                0,
                6
            }
        };
        assert(_countof(ops) <= _countof(sequence.Ops));
        sequence.OpCount = _countof(ops);
        memcpy(sequence.Ops, ops, sizeof(ops));
    }
    else if (opt.Patterns[1] == BC7ModeSplitShufflePattern::EndpointPairSignificantBitInderleaved)
    {
        //  This shuffle interleaves bits in significance order from each endpoint pair
        //  
        //  36 bits of RGB 0\1:    bits 4 - 39, padded with 4 other bits to make 5 bytes
        //                                                                                                                                                                                            (Most significant)
        //  [ 4 bits partition ]  R0'0 R1'0 G0'0 G1'0 B0'0 B1'0    R0'1 R1'1 G0'1 G1'1 B0'1 B1'1    R0'2 R1'2 G0'2 G1'2 B0'2 B1'2    R0'3 R1'3 G0'3 G1'3 B0'3 B1'3    R0'4 R1'4 G0'4 G1'4 B0'4 B1'4   R0'5 R1'5 G0'5 G1'5 B0'5 B1'5 
        //
        //  36 bits of RGB 2\3:    bits 44 - 79, padded with 4 other bits to make 5 bytes
        //                                                                                                                                                                                            (Most significant)
        //  [ 2 part, 2 pbits ]   R2'0 R3'0 G2'0 G3'0 B2'0 B3'0    R2'1 R3'1 G2'1 G3'1 B2'1 B3'1    R2'2 R3'2 G2'2 G3'2 B2'2 B3'2    R2'3 R3'3 G2'3 G3'3 B2'3 B3'3    R2'4 R3'4 G2'4 G3'4 B2'4 B3'4   R2'5 R3'5 G2'5 G3'5 B2'5 B3'5 
        //

        enum ChannelOrder
        {
            R0 = 4,
            R1 = 5,
            G0 = 6,
            G1 = 7,
            B0 = 8,
            B1 = 9,

            R2 = 44,
            R3 = 45,
            G2 = 46,
            G3 = 47,
            B2 = 48,
            B3 = 49,
            //R0 = 4,
            //R3 = 5,
            //G0 = 6,
            //G3 = 7,
            //B0 = 8,
            //B3 = 9,

            //R1 = 44,
            //R2 = 45,
            //G1 = 46,
            //G2 = 47,
            //B1 = 48,
            //B2 = 49,
        };

        CopyBitsOrder ops[] = {
            {   //  Mode (10)
               CopyBitDestination::Mode,
               0,
               kModeBits
           },
           {   //  Partition bits 0-3
               CopyBitDestination::Color,
               0,
               4
           },
           {   //  Partition bits 4-5             
               CopyBitDestination::Color,
               40,
               2
           },


            { CopyBitDestination::Color, R0 + (kChannels * 0), 1 },  // R0'0
            { CopyBitDestination::Color, R0 + (kChannels * 1), 1 },  // R0'1
            { CopyBitDestination::Color, R0 + (kChannels * 2), 1 },  // R0'2
            { CopyBitDestination::Color, R0 + (kChannels * 3), 1 },  // R0'3
            { CopyBitDestination::Color, R0 + (kChannels * 4), 1 },  // R0'4
            { CopyBitDestination::Color, R0 + (kChannels * 5), 1 },  // R0'5

            { CopyBitDestination::Color, R1 + (kChannels * 0), 1 },  // R1'0
            { CopyBitDestination::Color, R1 + (kChannels * 1), 1 },  // R1'1
            { CopyBitDestination::Color, R1 + (kChannels * 2), 1 },  // R1'2
            { CopyBitDestination::Color, R1 + (kChannels * 3), 1 },  // R1'3
            { CopyBitDestination::Color, R1 + (kChannels * 4), 1 },  // R1'4
            { CopyBitDestination::Color, R1 + (kChannels * 5), 1 },  // R1'5

            { CopyBitDestination::Color, R2 + (kChannels * 0), 1 },  // R2'0
            { CopyBitDestination::Color, R2 + (kChannels * 1), 1 },  // R2'1
            { CopyBitDestination::Color, R2 + (kChannels * 2), 1 },  // R2'2
            { CopyBitDestination::Color, R2 + (kChannels * 3), 1 },  // R2'3
            { CopyBitDestination::Color, R2 + (kChannels * 4), 1 },  // R2'4
            { CopyBitDestination::Color, R2 + (kChannels * 5), 1 },  // R2'5

            { CopyBitDestination::Color, R3 + (kChannels * 0), 1 },  // R3'0
            { CopyBitDestination::Color, R3 + (kChannels * 1), 1 },  // R3'1
            { CopyBitDestination::Color, R3 + (kChannels * 2), 1 },  // R3'2
            { CopyBitDestination::Color, R3 + (kChannels * 3), 1 },  // R3'3
            { CopyBitDestination::Color, R3 + (kChannels * 4), 1 },  // R3'4
            { CopyBitDestination::Color, R3 + (kChannels * 5), 1 },  // R3'5


            { CopyBitDestination::Color, G0 + (kChannels * 0), 1 },  // G0'0
            { CopyBitDestination::Color, G0 + (kChannels * 1), 1 },  // G0'1
            { CopyBitDestination::Color, G0 + (kChannels * 2), 1 },  // G0'2
            { CopyBitDestination::Color, G0 + (kChannels * 3), 1 },  // G0'3
            { CopyBitDestination::Color, G0 + (kChannels * 4), 1 },  // G0'4
            { CopyBitDestination::Color, G0 + (kChannels * 5), 1 },  // G0'5

            { CopyBitDestination::Color, G1 + (kChannels * 0), 1 },  // G1'0
            { CopyBitDestination::Color, G1 + (kChannels * 1), 1 },  // G1'1
            { CopyBitDestination::Color, G1 + (kChannels * 2), 1 },  // G1'2
            { CopyBitDestination::Color, G1 + (kChannels * 3), 1 },  // G1'3
            { CopyBitDestination::Color, G1 + (kChannels * 4), 1 },  // G1'4
            { CopyBitDestination::Color, G1 + (kChannels * 5), 1 },  // G1'5

            { CopyBitDestination::Color, G2 + (kChannels * 0), 1 },  // G2'0
            { CopyBitDestination::Color, G2 + (kChannels * 1), 1 },  // G2'1
            { CopyBitDestination::Color, G2 + (kChannels * 2), 1 },  // G2'2
            { CopyBitDestination::Color, G2 + (kChannels * 3), 1 },  // G2'3
            { CopyBitDestination::Color, G2 + (kChannels * 4), 1 },  // G2'4
            { CopyBitDestination::Color, G2 + (kChannels * 5), 1 },  // G2'5

            { CopyBitDestination::Color, G3 + (kChannels * 0), 1 },  // G3'0
            { CopyBitDestination::Color, G3 + (kChannels * 1), 1 },  // G3'1
            { CopyBitDestination::Color, G3 + (kChannels * 2), 1 },  // G3'2
            { CopyBitDestination::Color, G3 + (kChannels * 3), 1 },  // G3'3
            { CopyBitDestination::Color, G3 + (kChannels * 4), 1 },  // G3'4
            { CopyBitDestination::Color, G3 + (kChannels * 5), 1 },  // G3'5


            { CopyBitDestination::Color, B0 + (kChannels * 0), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 1), 1 },  // B0'1
            { CopyBitDestination::Color, B0 + (kChannels * 2), 1 },  // B0'2
            { CopyBitDestination::Color, B0 + (kChannels * 3), 1 },  // B0'3
            { CopyBitDestination::Color, B0 + (kChannels * 4), 1 },  // B0'4
            { CopyBitDestination::Color, B0 + (kChannels * 5), 1 },  // B0'5

            { CopyBitDestination::Color, B1 + (kChannels * 0), 1 },  // B1'0
            { CopyBitDestination::Color, B1 + (kChannels * 1), 1 },  // B1'1
            { CopyBitDestination::Color, B1 + (kChannels * 2), 1 },  // B1'2
            { CopyBitDestination::Color, B1 + (kChannels * 3), 1 },  // B1'3
            { CopyBitDestination::Color, B1 + (kChannels * 4), 1 },  // B1'4
            { CopyBitDestination::Color, B1 + (kChannels * 5), 1 },  // B1'5

            { CopyBitDestination::Color, B2 + (kChannels * 0), 1 },  // B2'0
            { CopyBitDestination::Color, B2 + (kChannels * 1), 1 },  // B2'1
            { CopyBitDestination::Color, B2 + (kChannels * 2), 1 },  // B2'2
            { CopyBitDestination::Color, B2 + (kChannels * 3), 1 },  // B2'3
            { CopyBitDestination::Color, B2 + (kChannels * 4), 1 },  // B2'4
            { CopyBitDestination::Color, B2 + (kChannels * 5), 1 },  // B2'5

            { CopyBitDestination::Color, B3 + (kChannels * 0), 1 },  // B3'0
            { CopyBitDestination::Color, B3 + (kChannels * 1), 1 },  // B3'1
            { CopyBitDestination::Color, B3 + (kChannels * 2), 1 },  // B3'2
            { CopyBitDestination::Color, B3 + (kChannels * 3), 1 },  // B3'3
            { CopyBitDestination::Color, B3 + (kChannels * 4), 1 },  // B3'4
            { CopyBitDestination::Color, B3 + (kChannels * 5), 1 },  // B3'5

            {   //  P bits              
               CopyBitDestination::Color,
               42,
               2
            },

            // Above is 82 bits, 46 remain

            { CopyBitDestination::Misc, 0, 40 },
            { CopyBitDestination::Scraps, 0, 6 },
        };
        assert(_countof(ops) <= _countof(sequence.Ops));
        sequence.OpCount = _countof(ops);
        memcpy(sequence.Ops, ops, sizeof(ops));
    }
    else
    {
        assert(false);
    }

    return;
}

void GetBC_ModeSplit_CopyBitsOrderMode2(CopyBitsSequence& sequence, BC7ModeSplitShuffleOptions opt, BC7TextureMetrics metrics)
{
    UNREFERENCED_PARAMETER(metrics);
    memset(&sequence, 0, sizeof(sequence));

    //  Bit Offset  Bits or ordering and count
    //  0           3 bits Mode (100)
    //  3           6 bits Partition
    //  9           5 bits R0   5 bits R1   5 bits R2   5 bits R3   5 bits R4   5 bits R5
    //  39          5 bits G0   5 bits G1   5 bits G2   5 bits G3   5 bits R4   5 bits R5
    //  69          5 bits B0   5 bits B1   5 bits B2   5 bits B3   5 bits R4   5 bits R5
    //  99          29 bits Index

    constexpr size_t kModeBits = 3;
    constexpr size_t kChannels = 6;

    if (opt.Patterns[2] == BC7ModeSplitShufflePattern::ColorPlane4bit || opt.Patterns[2] == BC7ModeSplitShufflePattern::EndpointPair4bit)  // experimental IDs 2, 4
    {
        // Groups 6 fields of 4 bits in two optional ways to make 24bit (3 byte) sequences
        // 2) groups colors, which works well for some textures with low channel correlation 
        // 4) group by endpoint RGB which works will when there is high correlation betwwen channels
        //
        // in both cases, the least significant bits are carved off to a different location, so color data may match across modes 0\2

        uint8_t ColorGrouping[] =
        {
            0,   4,   8,   12,  16, 20,
            24,  28,  32,  36,  40, 44,
            48,  52,  56,  60,  64, 68,
        };

        uint8_t EndpointGrouping[] =
        {
            0,   4,   24,  28,  48, 52,
            8,   12,  32,  36,  56, 60,
            16,  20,  40,  44,  64, 68,
        };

        enum {
            R0 = 0, R1, R2, R3, R4, R5,
            G0, G1, G2, G3, G4, G5,
            B0, B1, B2, B3, B4, B5,
        };

        uint8_t* grouping = (opt.Patterns[2] == BC7ModeSplitShufflePattern::ColorPlane4bit) ? ColorGrouping : EndpointGrouping;

        CopyBitsOrder ops[] = {
            {   //  Mode (100)
                CopyBitDestination::Mode,
                0,
                kModeBits
            },

            {   //  Partition bits 
                CopyBitDestination::Misc,
                18,
                6
            },

            { CopyBitDestination::Misc, R0, 1 }, { CopyBitDestination::Color, grouping[R0], 4 },
            { CopyBitDestination::Misc, R1, 1 }, { CopyBitDestination::Color, grouping[R1], 4 },
            { CopyBitDestination::Misc, R2, 1 }, { CopyBitDestination::Color, grouping[R2], 4 },
            { CopyBitDestination::Misc, R3, 1 }, { CopyBitDestination::Color, grouping[R3], 4 },
            { CopyBitDestination::Misc, R4, 1 }, { CopyBitDestination::Color, grouping[R4], 4 },
            { CopyBitDestination::Misc, R5, 1 }, { CopyBitDestination::Color, grouping[R5], 4 },

            { CopyBitDestination::Misc, G0, 1 }, { CopyBitDestination::Color, grouping[G0], 4 },
            { CopyBitDestination::Misc, G1, 1 }, { CopyBitDestination::Color, grouping[G1], 4 },
            { CopyBitDestination::Misc, G2, 1 }, { CopyBitDestination::Color, grouping[G2], 4 },
            { CopyBitDestination::Misc, G3, 1 }, { CopyBitDestination::Color, grouping[G3], 4 },
            { CopyBitDestination::Misc, G4, 1 }, { CopyBitDestination::Color, grouping[G4], 4 },
            { CopyBitDestination::Misc, G5, 1 }, { CopyBitDestination::Color, grouping[G5], 4 },

            { CopyBitDestination::Misc, B0, 1 }, { CopyBitDestination::Color, grouping[B0], 4 },
            { CopyBitDestination::Misc, B1, 1 }, { CopyBitDestination::Color, grouping[B1], 4 },
            { CopyBitDestination::Misc, B2, 1 }, { CopyBitDestination::Color, grouping[B2], 4 },
            { CopyBitDestination::Misc, B3, 1 }, { CopyBitDestination::Color, grouping[B3], 4 },
            { CopyBitDestination::Misc, B4, 1 }, { CopyBitDestination::Color, grouping[B4], 4 },
            { CopyBitDestination::Misc, B5, 1 }, { CopyBitDestination::Color, grouping[B5], 4 },

            // above covers 3 mode bits, 6 partition,  90 color bits, = 99 bits.....   29 bits remain


            //  Misc already contains the 18 least significant color bits & 6 partition, add 24 to complete an 6 byte block
            {   CopyBitDestination::Misc,                   24,                24 },

            //  remainder =  47 - 42 = 5 spare bits for the scrap stream
            {   CopyBitDestination::Scraps,                 0,                5 },
        };

        assert(_countof(ops) <= _countof(sequence.Ops));
        sequence.OpCount = _countof(ops);
        memcpy(sequence.Ops, ops, sizeof(ops));
    }
    else if (opt.Patterns[2] == BC7ModeSplitShufflePattern::EndpointPairSignificantBitInderleaved)
    {
        //  This shuffle interleaves bits in significance order from each endpoint pair
        //  
        //  30 bits of RGB 0\1:    bits 2 - 31, padded with 2 other bits to make 4 bytes
        //                                                                                                                                                                 (Most significant)
        //  [ 2 bits partition ]  R0'0 R1'0 G0'0 G1'0 B0'0 B1'0    R0'1 R1'1 G0'1 G1'1 B0'1 B1'1    R0'2 R1'2 G0'2 G1'2 B0'2 B1'2    R0'3 R1'3 G0'3 G1'3 B0'3 B1'3    R0'4 R1'4 G0'4 G1'4 B0'4 B1'4   
        //
        //  30 bits of RGB 2\3:    bits 34 - 63, padded with 2 other bits to make 4 bytes
        //                                                                                                                                                                 (Most significant)
        //  [ 2 bits partition ]  R2'0 R3'0 G2'0 G3'0 B2'0 B3'0    R2'1 R3'1 G2'1 G3'1 B2'1 B3'1    R2'2 R3'2 G2'2 G3'2 B2'2 B3'2    R2'3 R3'3 G2'3 G3'3 B2'3 B3'3    R2'4 R3'4 G2'4 G3'4 B2'4 B3'4   
        //
        //  30 bits of RGB 4\5:    bits 66 - 95, padded with 2 other bits to make 4 bytes
        //                                                                                                                                                                 (Most significant)
        //  [ 2 bits partition ]  R4'0 R5'0 G4'0 G5'0 B4'0 B5'0    R4'1 R5'1 G4'1 G5'1 B4'1 B5'1    R4'2 R5'2 G4'2 G5'2 B4'2 B5'2    R4'3 R5'3 G4'3 G5'3 B4'3 B5'3    R4'4 R5'4 G4'4 G5'4 B4'4 B5'4   
        //

        enum ChannelOrder
        {
            R0 = 2,
            R1 = 3,
            G0 = 4,
            G1 = 5,
            B0 = 6,
            B1 = 7,

            R2 = 34,
            R3 = 35,
            G2 = 36,
            G3 = 37,
            B2 = 38,
            B3 = 39,

            R4 = 66,
            R5 = 67,
            G4 = 68,
            G5 = 69,
            B4 = 70,
            B5 = 71,
        };

        CopyBitsOrder ops[] = {
            {   //  Mode (100)
               CopyBitDestination::Mode,
               0,
               kModeBits
           },
           {   //  Partition bits 0-1
               CopyBitDestination::Color,
               0,
               2
           },
           {   //  Partition bits 2-3
               CopyBitDestination::Color,
               32,
               2
           },
           {   //  Partition bits 4-5             
               CopyBitDestination::Color,
               64,
               2
           },


            { CopyBitDestination::Color, R0 + (kChannels * 0), 1 },  // R0'0
            { CopyBitDestination::Color, R0 + (kChannels * 1), 1 },  // R0'1
            { CopyBitDestination::Color, R0 + (kChannels * 2), 1 },  // R0'2
            { CopyBitDestination::Color, R0 + (kChannels * 3), 1 },  // R0'3
            { CopyBitDestination::Color, R0 + (kChannels * 4), 1 },  // R0'4

            { CopyBitDestination::Color, R1 + (kChannels * 0), 1 },  // R1'0
            { CopyBitDestination::Color, R1 + (kChannels * 1), 1 },  // R1'1
            { CopyBitDestination::Color, R1 + (kChannels * 2), 1 },  // R1'2
            { CopyBitDestination::Color, R1 + (kChannels * 3), 1 },  // R1'3
            { CopyBitDestination::Color, R1 + (kChannels * 4), 1 },  // R1'4

            { CopyBitDestination::Color, R2 + (kChannels * 0), 1 },  // R2'0
            { CopyBitDestination::Color, R2 + (kChannels * 1), 1 },  // R2'1
            { CopyBitDestination::Color, R2 + (kChannels * 2), 1 },  // R2'2
            { CopyBitDestination::Color, R2 + (kChannels * 3), 1 },  // R2'3
            { CopyBitDestination::Color, R2 + (kChannels * 4), 1 },  // R2'4

            { CopyBitDestination::Color, R3 + (kChannels * 0), 1 },  // R3'0
            { CopyBitDestination::Color, R3 + (kChannels * 1), 1 },  // R3'1
            { CopyBitDestination::Color, R3 + (kChannels * 2), 1 },  // R3'2
            { CopyBitDestination::Color, R3 + (kChannels * 3), 1 },  // R3'3
            { CopyBitDestination::Color, R3 + (kChannels * 4), 1 },  // R3'4

            { CopyBitDestination::Color, R4 + (kChannels * 0), 1 },  // R4'0
            { CopyBitDestination::Color, R4 + (kChannels * 1), 1 },  // R4'1
            { CopyBitDestination::Color, R4 + (kChannels * 2), 1 },  // R4'2
            { CopyBitDestination::Color, R4 + (kChannels * 3), 1 },  // R4'3
            { CopyBitDestination::Color, R4 + (kChannels * 4), 1 },  // R4'4

            { CopyBitDestination::Color, R5 + (kChannels * 0), 1 },  // R5'0
            { CopyBitDestination::Color, R5 + (kChannels * 1), 1 },  // R5'1
            { CopyBitDestination::Color, R5 + (kChannels * 2), 1 },  // R5'2
            { CopyBitDestination::Color, R5 + (kChannels * 3), 1 },  // R5'3
            { CopyBitDestination::Color, R5 + (kChannels * 4), 1 },  // R5'4


            { CopyBitDestination::Color, G0 + (kChannels * 0), 1 },  // G0'0
            { CopyBitDestination::Color, G0 + (kChannels * 1), 1 },  // G0'1
            { CopyBitDestination::Color, G0 + (kChannels * 2), 1 },  // G0'2
            { CopyBitDestination::Color, G0 + (kChannels * 3), 1 },  // G0'3
            { CopyBitDestination::Color, G0 + (kChannels * 4), 1 },  // G0'4

            { CopyBitDestination::Color, G1 + (kChannels * 0), 1 },  // G1'0
            { CopyBitDestination::Color, G1 + (kChannels * 1), 1 },  // G1'1
            { CopyBitDestination::Color, G1 + (kChannels * 2), 1 },  // G1'2
            { CopyBitDestination::Color, G1 + (kChannels * 3), 1 },  // G1'3
            { CopyBitDestination::Color, G1 + (kChannels * 4), 1 },  // G1'4

            { CopyBitDestination::Color, G2 + (kChannels * 0), 1 },  // G2'0
            { CopyBitDestination::Color, G2 + (kChannels * 1), 1 },  // G2'1
            { CopyBitDestination::Color, G2 + (kChannels * 2), 1 },  // G2'2
            { CopyBitDestination::Color, G2 + (kChannels * 3), 1 },  // G2'3
            { CopyBitDestination::Color, G2 + (kChannels * 4), 1 },  // G2'4

            { CopyBitDestination::Color, G3 + (kChannels * 0), 1 },  // G3'0
            { CopyBitDestination::Color, G3 + (kChannels * 1), 1 },  // G3'1
            { CopyBitDestination::Color, G3 + (kChannels * 2), 1 },  // G3'2
            { CopyBitDestination::Color, G3 + (kChannels * 3), 1 },  // G3'3
            { CopyBitDestination::Color, G3 + (kChannels * 4), 1 },  // G3'4

            { CopyBitDestination::Color, G4 + (kChannels * 0), 1 },  // G4'0
            { CopyBitDestination::Color, G4 + (kChannels * 1), 1 },  // G4'1
            { CopyBitDestination::Color, G4 + (kChannels * 2), 1 },  // G4'2
            { CopyBitDestination::Color, G4 + (kChannels * 3), 1 },  // G4'3
            { CopyBitDestination::Color, G4 + (kChannels * 4), 1 },  // G4'4

            { CopyBitDestination::Color, G5 + (kChannels * 0), 1 },  // G5'0
            { CopyBitDestination::Color, G5 + (kChannels * 1), 1 },  // G5'1
            { CopyBitDestination::Color, G5 + (kChannels * 2), 1 },  // G5'2
            { CopyBitDestination::Color, G5 + (kChannels * 3), 1 },  // G5'3
            { CopyBitDestination::Color, G5 + (kChannels * 4), 1 },  // G5'4


            { CopyBitDestination::Color, B0 + (kChannels * 0), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 1), 1 },  // B0'1
            { CopyBitDestination::Color, B0 + (kChannels * 2), 1 },  // B0'2
            { CopyBitDestination::Color, B0 + (kChannels * 3), 1 },  // B0'3
            { CopyBitDestination::Color, B0 + (kChannels * 4), 1 },  // B0'4

            { CopyBitDestination::Color, B1 + (kChannels * 0), 1 },  // B1'0
            { CopyBitDestination::Color, B1 + (kChannels * 1), 1 },  // B1'1
            { CopyBitDestination::Color, B1 + (kChannels * 2), 1 },  // B1'2
            { CopyBitDestination::Color, B1 + (kChannels * 3), 1 },  // B1'3
            { CopyBitDestination::Color, B1 + (kChannels * 4), 1 },  // B1'4

            { CopyBitDestination::Color, B2 + (kChannels * 0), 1 },  // B2'0
            { CopyBitDestination::Color, B2 + (kChannels * 1), 1 },  // B2'1
            { CopyBitDestination::Color, B2 + (kChannels * 2), 1 },  // B2'2
            { CopyBitDestination::Color, B2 + (kChannels * 3), 1 },  // B2'3
            { CopyBitDestination::Color, B2 + (kChannels * 4), 1 },  // B2'4

            { CopyBitDestination::Color, B3 + (kChannels * 0), 1 },  // B3'0
            { CopyBitDestination::Color, B3 + (kChannels * 1), 1 },  // B3'1
            { CopyBitDestination::Color, B3 + (kChannels * 2), 1 },  // B3'2
            { CopyBitDestination::Color, B3 + (kChannels * 3), 1 },  // B3'3
            { CopyBitDestination::Color, B3 + (kChannels * 4), 1 },  // B3'4

            { CopyBitDestination::Color, B4 + (kChannels * 0), 1 },  // B4'0
            { CopyBitDestination::Color, B4 + (kChannels * 1), 1 },  // B4'1
            { CopyBitDestination::Color, B4 + (kChannels * 2), 1 },  // B4'2
            { CopyBitDestination::Color, B4 + (kChannels * 3), 1 },  // B4'3
            { CopyBitDestination::Color, B4 + (kChannels * 4), 1 },  // B4'4

            { CopyBitDestination::Color, B5 + (kChannels * 0), 1 },  // B5'0
            { CopyBitDestination::Color, B5 + (kChannels * 1), 1 },  // B5'1
            { CopyBitDestination::Color, B5 + (kChannels * 2), 1 },  // B5'2
            { CopyBitDestination::Color, B5 + (kChannels * 3), 1 },  // B5'3
            { CopyBitDestination::Color, B5 + (kChannels * 4), 1 },  // B5'4

            // Above is 99 bits, 29 remain

            { CopyBitDestination::Misc, 0, 24 },
            { CopyBitDestination::Scraps, 0, 5 },
        };
        assert(_countof(ops) <= _countof(sequence.Ops));
        sequence.OpCount = _countof(ops);
        memcpy(sequence.Ops, ops, sizeof(ops));
    }
    else
    {
        assert(false);
    }

    return;
}

void GetBC_ModeSplit_CopyBitsOrderMode3(CopyBitsSequence& sequence, BC7ModeSplitShuffleOptions opt, BC7TextureMetrics metrics)
{
    UNREFERENCED_PARAMETER(metrics);
    memset(&sequence, 0, sizeof(sequence));

    //  Bit Offset  Bits or ordering and count
    //  0           4 bits Mode (1000)
    //  4           6 bits Partition
    //  10          7 bits R0   7 bits R1   7 bits R2   7 bits R3
    //  38          7 bits G0   7 bits G1   7 bits G2   7 bits G3
    //  66          7 bits B0   7 bits B1   7 bits B2   7 bits B3
    //  94          1 bit P0    1 bit P1    1 bit P2    1 bit P3
    //  98          30 bits Index
    constexpr size_t kModeBits = 4;
    constexpr size_t kChannels = 6;

    if (opt.Patterns[3] == BC7ModeSplitShufflePattern::EndpointQuadSignificantBitInderleaved) // matches experimental ID 11 
    {
        //  This shuffle treats 0/1 endpoints as a group and 2/3 endpoints as a different group that vary independenty.
        //  the most significant bits are grouped in the middle, specifically at a 4-bit offset into a byte.

        //  42 bits of RGB 0\1:  padded with P0+P1   bits 0 - 43
        //                                                                                                                3                               4                                                     Most significant 7
        //  P0 P1   R0'0 R1'0 G0'0 G1'0 B0'0 B1'0   R0'1 R1'1 G0'1 G1'1 B0'1 B1'1   R0'2 R1'2 G0'2 G1'2 B0'2 B1'2   R0'3 R1'3 G0'3 G1'3 B0'3 B1'3   R0'4 R1'4 G0'4 G1'4 B0'4 B1'4   R0'5 R1'5 G0'5 G1'5 B0'5 B1'5   R0'6 R1'6 G0'6 G1'6 B0'6 B1'6   
        //
        //  followed by reversed 42 bits of RGB 2\3:    bits 44 - 87 
        //    
        //       Most significant 7                                    4                                         3
        //  B2'6 B3'6 G2'6 G3'6 R2'6 R3'6   B2'5 B3'5 G2'5 G3'5 R2'5 R3'5   B2'4 B3'4 G2'4 G3'4 R2'4 R3'4   B2'3 B3'3 G2'3 G3'3 R2'3 R3'3   B2'2 B3'2 G2'2 G3'2 R2'2 R3'2   B2'1 B3'1 G2'1 G3'1 R2'1 R3'1   B2'0 B3'0 G2'0 G3'0 R2'0 R3'0   P2 P3
        //

        enum ChannelOrder
        {
            R0 = 2,
            R1 = 3,
            G0 = 4,
            G1 = 5,
            B0 = 6,
            B1 = 7,

            R2 = 84,
            R3 = 85,
            G2 = 82,
            G3 = 83,
            B2 = 80,
            B3 = 81,
        };

        CopyBitsOrder ops[] = {
            {   // Mode [1000]
                CopyBitDestination::Mode,
                0,
                kModeBits
            },
            {   // Partition 
                CopyBitDestination::Misc,
                0,
                6
            },


            { CopyBitDestination::Color, R0 + (kChannels * 0), 1 },  // R0'0
            { CopyBitDestination::Color, R0 + (kChannels * 1), 1 },  // R0'1
            { CopyBitDestination::Color, R0 + (kChannels * 2), 1 },  // R0'2
            { CopyBitDestination::Color, R0 + (kChannels * 3), 1 },  // R0'3
            { CopyBitDestination::Color, R0 + (kChannels * 4), 1 },  // R0'4
            { CopyBitDestination::Color, R0 + (kChannels * 5), 1 },  // R0'5
            { CopyBitDestination::Color, R0 + (kChannels * 6), 1 },  // R0'6

            { CopyBitDestination::Color, R1 + (kChannels * 0), 1 },  // R1'0
            { CopyBitDestination::Color, R1 + (kChannels * 1), 1 },  // R1'1
            { CopyBitDestination::Color, R1 + (kChannels * 2), 1 },  // R1'2
            { CopyBitDestination::Color, R1 + (kChannels * 3), 1 },  // R1'3
            { CopyBitDestination::Color, R1 + (kChannels * 4), 1 },  // R1'4
            { CopyBitDestination::Color, R1 + (kChannels * 5), 1 },  // R1'5
            { CopyBitDestination::Color, R1 + (kChannels * 6), 1 },  // R1'6


            { CopyBitDestination::Color, R2 - (kChannels * 0), 1 },  // R2'0
            { CopyBitDestination::Color, R2 - (kChannels * 1), 1 },  // R2'1
            { CopyBitDestination::Color, R2 - (kChannels * 2), 1 },  // R2'2
            { CopyBitDestination::Color, R2 - (kChannels * 3), 1 },  // R2'3
            { CopyBitDestination::Color, R2 - (kChannels * 4), 1 },  // R2'4
            { CopyBitDestination::Color, R2 - (kChannels * 5), 1 },  // R2'5
            { CopyBitDestination::Color, R2 - (kChannels * 6), 1 },  // R2'6

            { CopyBitDestination::Color, R3 - (kChannels * 0), 1 },  // R3'0
            { CopyBitDestination::Color, R3 - (kChannels * 1), 1 },  // R3'1
            { CopyBitDestination::Color, R3 - (kChannels * 2), 1 },  // R3'2
            { CopyBitDestination::Color, R3 - (kChannels * 3), 1 },  // R3'3
            { CopyBitDestination::Color, R3 - (kChannels * 4), 1 },  // R3'4
            { CopyBitDestination::Color, R3 - (kChannels * 5), 1 },  // R3'5
            { CopyBitDestination::Color, R3 - (kChannels * 6), 1 },  // R3'6


            { CopyBitDestination::Color, G0 + (kChannels * 0), 1 },  // G0'0
            { CopyBitDestination::Color, G0 + (kChannels * 1), 1 },  // G0'1
            { CopyBitDestination::Color, G0 + (kChannels * 2), 1 },  // G0'2
            { CopyBitDestination::Color, G0 + (kChannels * 3), 1 },  // G0'3
            { CopyBitDestination::Color, G0 + (kChannels * 4), 1 },  // G0'4
            { CopyBitDestination::Color, G0 + (kChannels * 5), 1 },  // G0'5
            { CopyBitDestination::Color, G0 + (kChannels * 6), 1 },  // G0'6

            { CopyBitDestination::Color, G1 + (kChannels * 0), 1 },  // G1'0
            { CopyBitDestination::Color, G1 + (kChannels * 1), 1 },  // G1'1
            { CopyBitDestination::Color, G1 + (kChannels * 2), 1 },  // G1'2
            { CopyBitDestination::Color, G1 + (kChannels * 3), 1 },  // G1'3
            { CopyBitDestination::Color, G1 + (kChannels * 4), 1 },  // G1'4
            { CopyBitDestination::Color, G1 + (kChannels * 5), 1 },  // G1'5
            { CopyBitDestination::Color, G1 + (kChannels * 6), 1 },  // G1'6

            { CopyBitDestination::Color, G2 - (kChannels * 0), 1 },  // G2'0
            { CopyBitDestination::Color, G2 - (kChannels * 1), 1 },  // G2'1
            { CopyBitDestination::Color, G2 - (kChannels * 2), 1 },  // G2'2
            { CopyBitDestination::Color, G2 - (kChannels * 3), 1 },  // G2'3
            { CopyBitDestination::Color, G2 - (kChannels * 4), 1 },  // G2'4
            { CopyBitDestination::Color, G2 - (kChannels * 5), 1 },  // G2'5
            { CopyBitDestination::Color, G2 - (kChannels * 6), 1 },  // G2'6

            { CopyBitDestination::Color, G3 - (kChannels * 0), 1 },  // G3'0
            { CopyBitDestination::Color, G3 - (kChannels * 1), 1 },  // G3'1
            { CopyBitDestination::Color, G3 - (kChannels * 2), 1 },  // G3'2
            { CopyBitDestination::Color, G3 - (kChannels * 3), 1 },  // G3'3
            { CopyBitDestination::Color, G3 - (kChannels * 4), 1 },  // G3'4
            { CopyBitDestination::Color, G3 - (kChannels * 5), 1 },  // G3'5
            { CopyBitDestination::Color, G3 - (kChannels * 6), 1 },  // G3'6


            { CopyBitDestination::Color, B0 + (kChannels * 0), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 1), 1 },  // B0'1
            { CopyBitDestination::Color, B0 + (kChannels * 2), 1 },  // B0'2
            { CopyBitDestination::Color, B0 + (kChannels * 3), 1 },  // B0'3
            { CopyBitDestination::Color, B0 + (kChannels * 4), 1 },  // B0'4
            { CopyBitDestination::Color, B0 + (kChannels * 5), 1 },  // B0'5
            { CopyBitDestination::Color, B0 + (kChannels * 6), 1 },  // B0'6

            { CopyBitDestination::Color, B1 + (kChannels * 0), 1 },  // B1'0
            { CopyBitDestination::Color, B1 + (kChannels * 1), 1 },  // B1'1
            { CopyBitDestination::Color, B1 + (kChannels * 2), 1 },  // B1'2
            { CopyBitDestination::Color, B1 + (kChannels * 3), 1 },  // B1'3
            { CopyBitDestination::Color, B1 + (kChannels * 4), 1 },  // B1'4
            { CopyBitDestination::Color, B1 + (kChannels * 5), 1 },  // B1'5
            { CopyBitDestination::Color, B1 + (kChannels * 6), 1 },  // B1'6


            { CopyBitDestination::Color, B2 - (kChannels * 0), 1 },  // B2'0
            { CopyBitDestination::Color, B2 - (kChannels * 1), 1 },  // B2'1
            { CopyBitDestination::Color, B2 - (kChannels * 2), 1 },  // B2'2
            { CopyBitDestination::Color, B2 - (kChannels * 3), 1 },  // B2'3
            { CopyBitDestination::Color, B2 - (kChannels * 4), 1 },  // B2'4
            { CopyBitDestination::Color, B2 - (kChannels * 5), 1 },  // B2'5
            { CopyBitDestination::Color, B2 - (kChannels * 6), 1 },  // B2'6

            { CopyBitDestination::Color, B3 - (kChannels * 0), 1 },  // B3'0
            { CopyBitDestination::Color, B3 - (kChannels * 1), 1 },  // B3'1
            { CopyBitDestination::Color, B3 - (kChannels * 2), 1 },  // B3'2
            { CopyBitDestination::Color, B3 - (kChannels * 3), 1 },  // B3'3
            { CopyBitDestination::Color, B3 - (kChannels * 4), 1 },  // B3'4
            { CopyBitDestination::Color, B3 - (kChannels * 5), 1 },  // B3'5
            { CopyBitDestination::Color, B3 - (kChannels * 6), 1 },  // B3'6

            { CopyBitDestination::Color, 0, 2 },   // P0, P1
            { CopyBitDestination::Color, 86, 2 },  // P3, P3

            // above is 4 mode bits, 6 partition, 84 color bits, 4 p bits, 30 bits remain
            // misc has 6 partition bits.


            {   //  fill misc to a byte-round 32 bits... 
                CopyBitDestination::Misc,
                6,
                26,

            },
            {   // remaining index to scraps
                CopyBitDestination::Scraps,
                0,
                4
            }
        };
        assert(_countof(ops) <= _countof(sequence.Ops));
        sequence.OpCount = _countof(ops);
        memcpy(sequence.Ops, ops, sizeof(ops));
    }
    else if (opt.Patterns[3] == BC7ModeSplitShufflePattern::EndpointPairSignificantBitInderleaved)
    {
        //  This shuffle interleaves bits in significance order from each endpoint pair
        //  
        //  42 bits of RGB 0\1:    bits 6 - 47, padded with 6 other bits to make 6 bytes
        //                                                                                                                                                                                                                             (Most significant)
        //  [ 6 bits partition ]  R0'0 R1'0 G0'0 G1'0 B0'0 B1'0    R0'1 R1'1 G0'1 G1'1 B0'1 B1'1    R0'2 R1'2 G0'2 G1'2 B0'2 B1'2    R0'3 R1'3 G0'3 G1'3 B0'3 B1'3    R0'4 R1'4 G0'4 G1'4 B0'4 B1'4   R0'5 R1'5 G0'5 G1'5 B0'5 B1'5    R0'6 R1'6 G0'6 G1'6 B0'6 B1'6
        //
        //  42 bits of RGB 2\3:    bits 54 - 95, padded with 6 other bits to make 5 bytes
        //                                                                                                                                                                                                                             (Most significant)
        //  [ 4 pbits, 2 idx ]    R2'0 R3'0 G2'0 G3'0 B2'0 B3'0    R2'1 R3'1 G2'1 G3'1 B2'1 B3'1    R2'2 R3'2 G2'2 G3'2 B2'2 B3'2    R2'3 R3'3 G2'3 G3'3 B2'3 B3'3    R2'4 R3'4 G2'4 G3'4 B2'4 B3'4   R2'5 R3'5 G2'5 G3'5 B2'5 B3'5    R2'6 R3'6 G2'6 G3'6 B2'6 B3'6 
        //

        enum ChannelOrder
        {
            R0 = 6,
            R1 = 7,
            G0 = 8,
            G1 = 9,
            B0 = 10,
            B1 = 11,

            R2 = 54,
            R3 = 55,
            G2 = 56,
            G3 = 57,
            B2 = 58,
            B3 = 59,

            //R0 = 6,
            //R3 = 7,
            //G0 = 8,
            //G3 = 9,
            //B0 = 10,
            //B3 = 11,

            //R1 = 54,
            //R2 = 55,
            //G1 = 56,
            //G2 = 57,
            //B1 = 58,
            //B2 = 59,
        };

        CopyBitsOrder ops[] = {
            {   //  Mode (1000)
               CopyBitDestination::Mode,
               0,
               kModeBits
            },
            {   //  Partition 
               CopyBitDestination::Color,
               0,
               6
            },

            { CopyBitDestination::Color, R0 + (kChannels * 0), 1 },  // R0'0
            { CopyBitDestination::Color, R0 + (kChannels * 1), 1 },  // R0'1
            { CopyBitDestination::Color, R0 + (kChannels * 2), 1 },  // R0'2
            { CopyBitDestination::Color, R0 + (kChannels * 3), 1 },  // R0'3
            { CopyBitDestination::Color, R0 + (kChannels * 4), 1 },  // R0'4
            { CopyBitDestination::Color, R0 + (kChannels * 5), 1 },  // R0'5
            { CopyBitDestination::Color, R0 + (kChannels * 6), 1 },  // R0'6

            { CopyBitDestination::Color, R1 + (kChannels * 0), 1 },  // R1'0
            { CopyBitDestination::Color, R1 + (kChannels * 1), 1 },  // R1'1
            { CopyBitDestination::Color, R1 + (kChannels * 2), 1 },  // R1'2
            { CopyBitDestination::Color, R1 + (kChannels * 3), 1 },  // R1'3
            { CopyBitDestination::Color, R1 + (kChannels * 4), 1 },  // R1'4
            { CopyBitDestination::Color, R1 + (kChannels * 5), 1 },  // R1'5
            { CopyBitDestination::Color, R1 + (kChannels * 6), 1 },  // R1'6

            { CopyBitDestination::Color, R2 + (kChannels * 0), 1 },  // R2'0
            { CopyBitDestination::Color, R2 + (kChannels * 1), 1 },  // R2'1
            { CopyBitDestination::Color, R2 + (kChannels * 2), 1 },  // R2'2
            { CopyBitDestination::Color, R2 + (kChannels * 3), 1 },  // R2'3
            { CopyBitDestination::Color, R2 + (kChannels * 4), 1 },  // R2'4
            { CopyBitDestination::Color, R2 + (kChannels * 5), 1 },  // R2'5
            { CopyBitDestination::Color, R2 + (kChannels * 6), 1 },  // R2'6

            { CopyBitDestination::Color, R3 + (kChannels * 0), 1 },  // R3'0
            { CopyBitDestination::Color, R3 + (kChannels * 1), 1 },  // R3'1
            { CopyBitDestination::Color, R3 + (kChannels * 2), 1 },  // R3'2
            { CopyBitDestination::Color, R3 + (kChannels * 3), 1 },  // R3'3
            { CopyBitDestination::Color, R3 + (kChannels * 4), 1 },  // R3'4
            { CopyBitDestination::Color, R3 + (kChannels * 5), 1 },  // R3'5
            { CopyBitDestination::Color, R3 + (kChannels * 6), 1 },  // R3'6


            { CopyBitDestination::Color, G0 + (kChannels * 0), 1 },  // G0'0
            { CopyBitDestination::Color, G0 + (kChannels * 1), 1 },  // G0'1
            { CopyBitDestination::Color, G0 + (kChannels * 2), 1 },  // G0'2
            { CopyBitDestination::Color, G0 + (kChannels * 3), 1 },  // G0'3
            { CopyBitDestination::Color, G0 + (kChannels * 4), 1 },  // G0'4
            { CopyBitDestination::Color, G0 + (kChannels * 5), 1 },  // G0'5
            { CopyBitDestination::Color, G0 + (kChannels * 6), 1 },  // G0'6

            { CopyBitDestination::Color, G1 + (kChannels * 0), 1 },  // G1'0
            { CopyBitDestination::Color, G1 + (kChannels * 1), 1 },  // G1'1
            { CopyBitDestination::Color, G1 + (kChannels * 2), 1 },  // G1'2
            { CopyBitDestination::Color, G1 + (kChannels * 3), 1 },  // G1'3
            { CopyBitDestination::Color, G1 + (kChannels * 4), 1 },  // G1'4
            { CopyBitDestination::Color, G1 + (kChannels * 5), 1 },  // G1'5
            { CopyBitDestination::Color, G1 + (kChannels * 6), 1 },  // G1'6

            { CopyBitDestination::Color, G2 + (kChannels * 0), 1 },  // G2'0
            { CopyBitDestination::Color, G2 + (kChannels * 1), 1 },  // G2'1
            { CopyBitDestination::Color, G2 + (kChannels * 2), 1 },  // G2'2
            { CopyBitDestination::Color, G2 + (kChannels * 3), 1 },  // G2'3
            { CopyBitDestination::Color, G2 + (kChannels * 4), 1 },  // G2'4
            { CopyBitDestination::Color, G2 + (kChannels * 5), 1 },  // G2'5
            { CopyBitDestination::Color, G2 + (kChannels * 6), 1 },  // G2'6

            { CopyBitDestination::Color, G3 + (kChannels * 0), 1 },  // G3'0
            { CopyBitDestination::Color, G3 + (kChannels * 1), 1 },  // G3'1
            { CopyBitDestination::Color, G3 + (kChannels * 2), 1 },  // G3'2
            { CopyBitDestination::Color, G3 + (kChannels * 3), 1 },  // G3'3
            { CopyBitDestination::Color, G3 + (kChannels * 4), 1 },  // G3'4
            { CopyBitDestination::Color, G3 + (kChannels * 5), 1 },  // G3'5
            { CopyBitDestination::Color, G3 + (kChannels * 6), 1 },  // G3'6


            { CopyBitDestination::Color, B0 + (kChannels * 0), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 1), 1 },  // B0'1
            { CopyBitDestination::Color, B0 + (kChannels * 2), 1 },  // B0'2
            { CopyBitDestination::Color, B0 + (kChannels * 3), 1 },  // B0'3
            { CopyBitDestination::Color, B0 + (kChannels * 4), 1 },  // B0'4
            { CopyBitDestination::Color, B0 + (kChannels * 5), 1 },  // B0'5
            { CopyBitDestination::Color, B0 + (kChannels * 6), 1 },  // B0'6

            { CopyBitDestination::Color, B1 + (kChannels * 0), 1 },  // B1'0
            { CopyBitDestination::Color, B1 + (kChannels * 1), 1 },  // B1'1
            { CopyBitDestination::Color, B1 + (kChannels * 2), 1 },  // B1'2
            { CopyBitDestination::Color, B1 + (kChannels * 3), 1 },  // B1'3
            { CopyBitDestination::Color, B1 + (kChannels * 4), 1 },  // B1'4
            { CopyBitDestination::Color, B1 + (kChannels * 5), 1 },  // B1'5
            { CopyBitDestination::Color, B1 + (kChannels * 6), 1 },  // B1'6

            { CopyBitDestination::Color, B2 + (kChannels * 0), 1 },  // B2'0
            { CopyBitDestination::Color, B2 + (kChannels * 1), 1 },  // B2'1
            { CopyBitDestination::Color, B2 + (kChannels * 2), 1 },  // B2'2
            { CopyBitDestination::Color, B2 + (kChannels * 3), 1 },  // B2'3
            { CopyBitDestination::Color, B2 + (kChannels * 4), 1 },  // B2'4
            { CopyBitDestination::Color, B2 + (kChannels * 5), 1 },  // B2'5
            { CopyBitDestination::Color, B2 + (kChannels * 6), 1 },  // B2'6

            { CopyBitDestination::Color, B3 + (kChannels * 0), 1 },  // B3'0
            { CopyBitDestination::Color, B3 + (kChannels * 1), 1 },  // B3'1
            { CopyBitDestination::Color, B3 + (kChannels * 2), 1 },  // B3'2
            { CopyBitDestination::Color, B3 + (kChannels * 3), 1 },  // B3'3
            { CopyBitDestination::Color, B3 + (kChannels * 4), 1 },  // B3'4
            { CopyBitDestination::Color, B3 + (kChannels * 5), 1 },  // B3'5
            { CopyBitDestination::Color, B3 + (kChannels * 6), 1 },  // B3'6

            {   //  P bits + 2 index              
               CopyBitDestination::Color,
               48,
               6
            },

            // Above is 100 bits, 28 remain

            { CopyBitDestination::Misc, 0, 24 },
            { CopyBitDestination::Scraps, 0, 4 },
        };
        assert(_countof(ops) <= _countof(sequence.Ops));
        sequence.OpCount = _countof(ops);
        memcpy(sequence.Ops, ops, sizeof(ops));
    }
    else
    {
        assert(false);
    }

    return;
}

void GetBC_ModeSplit_CopyBitsOrderMode4(CopyBitsSequence& sequence, BC7ModeSplitShuffleOptions opt, BC7TextureMetrics metrics)
{
    memset(&sequence, 0, sizeof(sequence));

    //  Bit Offset  Bits or ordering and count
    //  0           5 bits Mode (10000)
    //  5           2 bits Rotation
    //  7           1 bit Idx Mode
    //  8           5 bits R0   5 bits R1
    //  18          5 bits G0   5 bits G1
    //  28          5 bits B0   5 bits B1
    //  38          6 bits A0   6 bits A1
    //  50          31 bits Index Data
    //  81          47 bits Index Data

    // We work off the RGBA derotated form:

    //  Bit Offset  Bits or ordering and count
    //  0           5 bits Mode (10000)
    //  5           2 bits Rotation
    //  7           1 bit Idx Mode
    //  8           5 bits R0   5 bits R1
    //  18          5 bits G0   5 bits G1
    //  28          5 bits B0   5 bits B1
    //  38          1 bit ex0
    //  39          5 bits A0
    //  44                      1 bit ex1
    //  45                      5 bits A1
    //  50          31 bits Index Data
    //  81          47 bits Index Data

    constexpr size_t kModeBits = 5;
    constexpr size_t kChannels = 8;

    if (opt.Patterns[4] == BC7ModeSplitShufflePattern::StableIsland) // matches experimental ID 13
    {
        // This mode is the "stable island" dynamic mode that depends on including a "statics" header to mark channels.  Bits are then arranged around this stable island within the color stream
        // the pattern looks different based on the number of static fields identified.  



        //  40 color bits
        // byte         0                        1                        2                      3                      4                     
        //     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39   non-static  req'for 3 bytes, 4 bytes,  5 bytes, 
        // 0 statics   5th           -           3rd         -          1st          -          2nd          -          4th                  8           24           32         40       
        // 1           5th           -           3rd         - 0  1  2  3  4  *  *  *-          2nd          -          4th                  7           19           27         35        
        // 2           5th           -           3rd         - 0  1  2  3  4  0  1  2- 3  4  *  *  *  *  *  *-          4th                  6           14           12         30        
        // 3           5th           -           3rd         - 0  1  2  3  4  0  1  2- 3  4  0  1  2  3  4  *-          4th                  5            9           17         25        
        // 4           4th           - 0  1  2  3  4  0  1  2- 3  4  0  1  2  3  4  0- 1  2  3  4  *  *  *  *-          5th                  4            4           12         20        
        // 5           5th           - 0  1  2  3  4  0  1  2- 3  4  0  1  2  3  4  0- 1  2  3  4  0  1  2  3- 4  *  *  *  *  *  *  *        3                         7         15         
        // 6           5th           - 0  1  2  3  4  0  1  2- 3  4  0  1  2  3  4  0- 1  2  3  4  0  1  2  3- 4  0  1  2  3  4  *  *        2                         2         10         
        // 7   0  1  2  3  4  0  1  2- 3  4  0  1  2  3  4  0- 1  2  3  4  0  1  2  3- 4  0  1  2  3  4  0  1- 2  3  4  *  *  *  *  *        1                                    5
        // 8   0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  0  1  2  3  4        0  

        static CopyBitsOrder ops[60] = {
            {   //  Mode (10000)
               CopyBitDestination::Mode,
               0,
               kModeBits                    // 5
           },
           {   //  Rotation 0               // this will trail the first 31 bits if index data
               CopyBitDestination::Misc,
               31,
               1
           },
           {   //  Rotation 1               
               CopyBitDestination::Scraps,
               0,
               1
           },
           {   //  idx mode                 // this will trail the second 47 bits if index data
               CopyBitDestination::Misc,
               79,
               1
           },
        };

        uint64_t placedBits = 0;

        size_t OrderCount = 4;
        size_t staticFields = __popcnt64(size_t(metrics.M[4].Statics | metrics.M[4].LowEntropy));   // <-- TODO modify to have a low entropy region
        size_t staticBits = staticFields * 5;
        size_t otherFields = 8 - staticFields;

        size_t staticBitStarts[] = { 16, 16, 16, 16, 8, 8, 8, 0, 0 };

        size_t otherByteOrder[][5] = {
            {2, 3, 1, 4, 0},      // 0 statics
            {2, 3, 1, 4, 0},      // 1 statics
            {2, 3, 1, 4, 0},      // 2 statics
            {2, 3, 1, 4, 0},      // 3 statics
            {1, 2, 3, 0, 4},      // 4 statics
            {1, 2, 3, 4, 0},      // 5 statics
            {1, 2, 3, 4, 0},      // 6 statics
            {0, 1, 2, 3, 4},      // 7 statics
            {0, 1, 2, 3, 4},      // 8 statics
        };

        //                    R0 R1
        uint8_t order[] = { 6, 7, 4, 5, 2, 3, 0, 1 };   // A0, A1, B0, B1, G0, G1, R0, R1
        //uint8_t order[] = { 6, 7, 4, 5, 0, 1, 2, 3 };   // B0, B1, A0, A1, G0, G1, R0, R1

        // adjust ordering based on static fields
        for (uint8_t e = 0; e < 8; e++)
        {
            if ((metrics.M[4].Statics | metrics.M[4].LowEntropy) & (1u << e))
            {
                // move other endpoints up in the order, to account for the removal of this static field from the normal order
                for (uint8_t ee = 0; ee < 8; ee++)
                {
                    if (order[ee] > order[e]) order[ee]--;
                }
                order[e] = 0;
            }
        }

        size_t nextStaticBit = staticBitStarts[staticFields];
        size_t nextScrapBit = 1;

        for (uint8_t e = 0; e < 8; e++)
        {

            if (e >= 6)
            {
                // process the low extra bit of the alpha channel to account for derotation
                ops[OrderCount].DestStream = CopyBitDestination::Scraps;
                ops[OrderCount].CopyBitCount = 1;
                ops[OrderCount].DestBitOffset = (uint8_t)nextScrapBit;
                nextScrapBit++;
                OrderCount++;
            }
            if ((metrics.M[4].Statics | metrics.M[4].LowEntropy) & (1u << e))
            {
                ops[OrderCount].DestStream = CopyBitDestination::Color;
                ops[OrderCount].CopyBitCount = 5;
                ops[OrderCount].DestBitOffset = (uint8_t)nextStaticBit; assert(nextStaticBit <= 0xff);

                placedBits |= 0x1full << nextStaticBit;

                nextStaticBit += 5;
                OrderCount++;
            }
            else
            {
                for (uint8_t b = 0; b < 5; b++)
                {
                    const size_t bi = uint8_t(4u - b);
                    const size_t destLinear = staticBits + order[e] + bi * otherFields;
                    const size_t destLinearByte = destLinear / 8;
                    const size_t destLinearBit = destLinear % 8;
                    const size_t destRemappedByte = otherByteOrder[staticFields][destLinearByte];
                    const size_t destRemapped = 8 * destRemappedByte + destLinearBit;

                    ops[OrderCount].DestStream = CopyBitDestination::Color;
                    ops[OrderCount].CopyBitCount = 1;
                    ops[OrderCount].DestBitOffset = uint8_t(destRemapped);  assert(destRemapped <= 0xff);

                    placedBits |= 0x1ull << (destRemapped);

                    OrderCount++;
                }
            }
        }

        assert(placedBits == 0x000000FFFFFFFFFFull);

        //   above acounts for 8 bytes, 2 bits.  Mode (5), Rot(2), idx (1), color(40+2) bits = 50 bits
        //   78 bits remain...   misc has bits 2 bits (31\79)
        //   scraps have 3 bits  

        ops[OrderCount].DestStream = CopyBitDestination::Misc;
        ops[OrderCount].DestBitOffset = 0;
        ops[OrderCount].CopyBitCount = 31;
        OrderCount++;

        ops[OrderCount].DestStream = CopyBitDestination::Misc;
        ops[OrderCount].DestBitOffset = 32;
        ops[OrderCount].CopyBitCount = 47;
        OrderCount++;

        assert(OrderCount <= _countof(sequence.Ops));
        sequence.OpCount = OrderCount;
        memcpy(sequence.Ops, ops, sizeof(CopyBitsOrder) * OrderCount);
    }
    else if (opt.Patterns[4] == BC7ModeSplitShufflePattern::EndpointPairSignificantBitInderleaved) // matches experimental ID 3
    {
        //  This shuffle treats 0/1 endpoints as a unit, and interleaves bits in significant bit order
        //  the most significant bits are placed at a 

        //  40 bits of RGBA 0\1:    bits 0 - 39
        //                  byte 0                                     byte 1                                 byte 2                                   byte 3                                  byte 4   (Most significant)
        //  R0'0 R1'0 G0'0 G1'0 B0'0 B1'0 A0'0 A1'0  R0'1 R1'1 G0'1 G1'1 B0'1 B1'1 A0'1 A1'1   R0'2 R1'2 G0'2 G1'2 B0'2 B1'2 A0'2 A1'2   R0'3 R1'3 G0'3 G1'3 B0'3 B1'3 A0'3 A1'3   R0'4 R1'4 G0'4 G1'4 B0'4 B1'4 A0'4 A1'4  
        //

        enum ChannelOrder
        {
            R0 = 0,
            R1 = 1,
            G0 = 2,
            G1 = 3,
            B0 = 4,
            B1 = 5,
            A0 = 6,
            A1 = 7
        };

        CopyBitsOrder ops[] = {
            {   //  Mode (10000)
               CopyBitDestination::Mode,
               0,
               5
           },
           {   //  Rotation 0               // this will trail the first 31 bits if index data
               CopyBitDestination::Misc,
               31,
               1
           },
           {   //  Rotation 1               
               CopyBitDestination::Scraps,
               0,
               1
           },
           {   //  idx mode                 // this will trail the second 47 bits if index data
               CopyBitDestination::Misc,
               79,
               1
           },


            { CopyBitDestination::Color, R0 + (kChannels * 0), 1 },  // R0'0
            { CopyBitDestination::Color, R0 + (kChannels * 1), 1 },  // R0'1
            { CopyBitDestination::Color, R0 + (kChannels * 2), 1 },  // R0'2
            { CopyBitDestination::Color, R0 + (kChannels * 3), 1 },  // R0'3
            { CopyBitDestination::Color, R0 + (kChannels * 4), 1 },  // R0'4

            { CopyBitDestination::Color, R1 + (kChannels * 0), 1 },  // R1'0
            { CopyBitDestination::Color, R1 + (kChannels * 1), 1 },  // R1'1
            { CopyBitDestination::Color, R1 + (kChannels * 2), 1 },  // R1'2
            { CopyBitDestination::Color, R1 + (kChannels * 3), 1 },  // R1'3
            { CopyBitDestination::Color, R1 + (kChannels * 4), 1 },  // R1'4


            { CopyBitDestination::Color, G0 + (kChannels * 0), 1 },  // G0'0
            { CopyBitDestination::Color, G0 + (kChannels * 1), 1 },  // G0'1
            { CopyBitDestination::Color, G0 + (kChannels * 2), 1 },  // G0'2
            { CopyBitDestination::Color, G0 + (kChannels * 3), 1 },  // G0'3
            { CopyBitDestination::Color, G0 + (kChannels * 4), 1 },  // G0'4

            { CopyBitDestination::Color, G1 + (kChannels * 0), 1 },  // G1'0
            { CopyBitDestination::Color, G1 + (kChannels * 1), 1 },  // G1'1
            { CopyBitDestination::Color, G1 + (kChannels * 2), 1 },  // G1'2
            { CopyBitDestination::Color, G1 + (kChannels * 3), 1 },  // G1'3
            { CopyBitDestination::Color, G1 + (kChannels * 4), 1 },  // G1'4


            { CopyBitDestination::Color, B0 + (kChannels * 0), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 1), 1 },  // B0'1
            { CopyBitDestination::Color, B0 + (kChannels * 2), 1 },  // B0'2
            { CopyBitDestination::Color, B0 + (kChannels * 3), 1 },  // B0'3
            { CopyBitDestination::Color, B0 + (kChannels * 4), 1 },  // B0'4

            { CopyBitDestination::Color, B1 + (kChannels * 0), 1 },  // B1'0
            { CopyBitDestination::Color, B1 + (kChannels * 1), 1 },  // B1'1
            { CopyBitDestination::Color, B1 + (kChannels * 2), 1 },  // B1'2
            { CopyBitDestination::Color, B1 + (kChannels * 3), 1 },  // B1'3
            { CopyBitDestination::Color, B1 + (kChannels * 4), 1 },  // B1'4

            { CopyBitDestination::Scraps, 1, 1 },  // ex0

            { CopyBitDestination::Color, A0 + (kChannels * 0), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 1), 1 },  // A0'1
            { CopyBitDestination::Color, A0 + (kChannels * 2), 1 },  // A0'2
            { CopyBitDestination::Color, A0 + (kChannels * 3), 1 },  // A0'3
            { CopyBitDestination::Color, A0 + (kChannels * 4), 1 },  // A0'4

            { CopyBitDestination::Scraps, 2, 1 },  // ex1

            { CopyBitDestination::Color, A1 + (kChannels * 0), 1 },  // A1'0
            { CopyBitDestination::Color, A1 + (kChannels * 1), 1 },  // A1'1
            { CopyBitDestination::Color, A1 + (kChannels * 2), 1 },  // A1'2
            { CopyBitDestination::Color, A1 + (kChannels * 3), 1 },  // A1'3
            { CopyBitDestination::Color, A1 + (kChannels * 4), 1 },  // A1'4


            { CopyBitDestination::Misc, 0, 31 },
            { CopyBitDestination::Misc, 32, 47 },
        };
        assert(_countof(ops) <= _countof(sequence.Ops));
        sequence.OpCount = _countof(ops);
        memcpy(sequence.Ops, ops, sizeof(ops));
    }
    else
    {
        assert(false);
    }

    return;
}

void GetBC_ModeSplit_CopyBitsOrderMode5(CopyBitsSequence& sequence, BC7ModeSplitShuffleOptions opt, BC7TextureMetrics metrics)
{
    memset(&sequence, 0, sizeof(sequence));

    //  Bit Offset  Bits or ordering and count
    //  0           6 bits Mode (100000)
    //  6           2 bits Rotation
    //  8           7 bits R0   7 bits R1
    //  22          7 bits G0   7 bits G1
    //  36          7 bits B0   7 bits B1
    //  50          8 bits A0   8 bits A1
    //  66          31 bits Colour Index
    //  97          31 bits Alpha Index

    // We work off the RGBA derotated form:

    //  Bit Offset  Bits or ordering and count
    //  0           6 bits Mode (100000)
    //  6           2 bits Rotation
    //  8           7 bits R0   7 bits R1
    //  22          7 bits G0   7 bits G1
    //  36          7 bits B0   7 bits B1
    //  50          1 bit ex0   
    //  51          7 bits A0   
    //  58                      1 bit ex1
    //  59                      7 bits A1
    //  66          31 bits Colour Index
    //  97          31 bits Alpha Index

    constexpr size_t kModeBits = 6;
    constexpr size_t kChannels = 8;

    if (opt.Patterns[5] == BC7ModeSplitShufflePattern::StableIsland) // matches experimental ID 13
    {
        // This mode is the "stable island" dynamic mode that depends on including a "statics" header to mark channels.  Bits are then arranged around this stable island within the color stream
        // the pattern looks different based on the number of static fields identified.  

        //  56 color bits
        // byte         0                        1                        2                      3                      4                       5                        6
        //     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55   non-static  req'for 3 bytes, 4 bytes,  5 bytes,  6 bytes
        // 0 statics   7th           -          5th          -           3rd         -          1st          -          2nd          -          4th          -           6th                 8           24           32         40        48
        // 1           7th           -          5th          -           3rd         - 0  1  2  3  4  5  6  *-          2nd          -          4th          -           6th                 7           17           25         33        41
        // 2           7th           -          5th          -           3rd         - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  *  *-          4th          -           6th                 6           10           18         26        34
        // 3           6th           -          4th          - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  *  *  *-          5th          -           7th                 5            3           11         19        27
        // 4           7th           -          5th          - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  *  *  *  *-           6th                 4                         4         12        20
        // 5           6th           - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  *  *  *  *  *-           7th                 3                                   5         13
        // 6           7th           - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  0  1  2  3  4- 5  6  *  *  *  *  *  *        2                                              6
        // 7   0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  0  1  2  3  4- 5  6  0  1  2  3  4  5- 6  *  *  *  *  *  *  *        1  
        // 8   0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  0  1  2  3  4_ 5  6  0  1  2  3  4  5- 6  0  1  2  3  4  5  6        0  


        static CopyBitsOrder ops[70] = {
             {   //  Mode (100000)
                CopyBitDestination::Mode,
                0,
                kModeBits               // 6
            },
            {   //  Rotation
                CopyBitDestination::Misc,
                31,
                1
            },
            {   //  Rotation
                CopyBitDestination::Misc,
                63,
                1
            }
        };

        uint64_t placedBits = 0;

        size_t OrderCount = 3;
        size_t staticFields = __popcnt64(size_t(metrics.M[5].Statics | metrics.M[5].LowEntropy));       // <-- TODO should we instead bitwise wrap low entropy bits, rather than counting as statics?
        size_t staticBits = staticFields * 7;
        size_t otherFields = 8 - staticFields;



        size_t staticBitStarts[] = { 24, 24, 24, 16, 16, 8, 8, 0, 0 };
        size_t otherBitStarts[] = { 24, 31, 38, 37, 44, 43, 50, 49, 56 };

        size_t otherByteOrder[][7] = {
            {3, 4, 2, 5, 1, 6, 0},      // 0 statics
            {3, 4, 2, 5, 1, 6, 0},      // 1 statics
            {3, 4, 2, 5, 1, 6, 0},      // 2 statics
            {2, 3, 4, 1, 5, 0, 6},      // 3 statics
            {2, 3, 4, 5, 1, 6, 0},      // 4 statics
            {1, 2, 3, 4, 5, 0, 6},      // 5 statics
            {1, 2, 3, 4, 5, 6, 0},      // 6 statics
            {0, 1, 2, 3, 4, 5, 6},      // 7 statics
            {0, 1, 2, 3, 4, 5, 6},      // 8 statics
        };

        //                    R0 R1
        uint8_t order[] = { 6, 7, 4, 5, 2, 3, 0, 1 };   // A0, A1, B0, B1, G0, G1, R0, R1
        //uint8_t order[] = { 6, 7, 4, 5, 0, 1, 2, 3 };   // B0, B1, A0, A1, G0, G1, R0, R1

        // adjust ordering based on static fields
        for (uint8_t e = 0; e < 8; e++)
        {
            if ((metrics.M[5].Statics | metrics.M[5].LowEntropy) & (1u << e))
            {
                for (uint8_t ee = 0; ee < 8; ee++)
                {
                    if (order[ee] > order[e]) order[ee]--;
                }
                order[e] = 0;
            }
        }

        size_t nextStaticBit = staticBitStarts[staticFields];

        for (uint8_t e = 0; e < 8; e++)
        {
            if (e >= 6)
            {
                // process the low extra bit of the alpha channel to account for derotation
                ops[OrderCount].DestStream = CopyBitDestination::Scraps;
                ops[OrderCount].CopyBitCount = 1;
                ops[OrderCount].DestBitOffset = (e == 6u) ? 0u : 1u;
                OrderCount++;
            }
            if ((metrics.M[5].Statics | metrics.M[5].LowEntropy) & (1u << e))
            {
                ops[OrderCount].DestStream = CopyBitDestination::Color;
                ops[OrderCount].CopyBitCount = 7;
                ops[OrderCount].DestBitOffset = (uint8_t)nextStaticBit; assert(nextStaticBit <= 0xff);

                placedBits |= 0x7full << nextStaticBit;

                nextStaticBit += 7;
                OrderCount++;
            }
            else
            {
                for (uint8_t b = 0; b < 7; b++)
                {
                    const size_t bi = uint8_t(6u - b);
                    const size_t destLinear = staticBits + order[e] + bi * otherFields;
                    const size_t destLinearByte = destLinear / 8;
                    const size_t destLinearBit = destLinear % 8;
                    const size_t destRemappedByte = otherByteOrder[staticFields][destLinearByte];
                    const size_t destRemapped = 8 * destRemappedByte + destLinearBit;

                    ops[OrderCount].DestStream = CopyBitDestination::Color;
                    ops[OrderCount].CopyBitCount = 1;
                    ops[OrderCount].DestBitOffset = (uint8_t)destRemapped; assert(destRemapped <= 0xff);

                    placedBits |= 0x1ull << (destRemapped);

                    OrderCount++;
                }
            }
        }

        assert(placedBits == 0x00FFFFFFFFFFFFFFull);

        //   Index
        ops[OrderCount].DestStream = CopyBitDestination::Misc;
        ops[OrderCount].DestBitOffset = 0;
        ops[OrderCount].CopyBitCount = 31;
        OrderCount++;

        ops[OrderCount].DestStream = CopyBitDestination::Misc;
        ops[OrderCount].DestBitOffset = 32;
        ops[OrderCount].CopyBitCount = 31;
        OrderCount++;

        assert(OrderCount <= _countof(sequence.Ops));
        sequence.OpCount = OrderCount;
        memcpy(sequence.Ops, ops, sizeof(CopyBitsOrder) * OrderCount);
    }
    else if (opt.Patterns[5] == BC7ModeSplitShufflePattern::EndpointPairSignificantBitInderleaved) // matches experimental ID 3
    {
        //  This shuffle treats 0/1 endpoints as a unit, and interleaves bits in significant bit order
        //  the most significant bits are placed at a 

        //  56 bits of RGBA 0\1:    bits 0 - 55
        //                  byte 0                                     byte 1                                 byte 2                                   byte 3                                  byte 4                                    byte 5                                  (Most significant)
        //  R0'0 R1'0 G0'0 G1'0 B0'0 B1'0 A0'0 A1'0  R0'1 R1'1 G0'1 G1'1 B0'1 B1'1 A0'1 A1'1   R0'2 R1'2 G0'2 G1'2 B0'2 B1'2 A0'2 A1'2   R0'3 R1'3 G0'3 G1'3 B0'3 B1'3 A0'3 A1'3   R0'4 R1'4 G0'4 G1'4 B0'4 B1'4 A0'4 A1'4  R0'5 R1'5 G0'5 G1'5 B0'5 B1'5 A0'5 A1'5  R0'6 R1'6 G0'6 G1'6 B0'6 B1'6 A0'6 A1'6
        //
        //

        enum ChannelOrder
        {
            R0 = 0,
            R1 = 1,
            G0 = 2,
            G1 = 3,
            B0 = 4,
            B1 = 5,
            A0 = 6,
            A1 = 7
        };

        CopyBitsOrder ops[] = {
            {   //  Mode (100000)
               CopyBitDestination::Mode,
               0,
               6
           },
           {   //  Rotation 0               // this will trail the first 31 bits if index data
               CopyBitDestination::Misc,
               31,
               1
           },
           {   //  Rotation 1               // this will trail the second 31 bits if index data  
               CopyBitDestination::Misc,
               63,
               1
           },


            { CopyBitDestination::Color, R0 + (kChannels * 0), 1 },  // R0'0
            { CopyBitDestination::Color, R0 + (kChannels * 1), 1 },  // R0'1
            { CopyBitDestination::Color, R0 + (kChannels * 2), 1 },  // R0'2
            { CopyBitDestination::Color, R0 + (kChannels * 3), 1 },  // R0'3
            { CopyBitDestination::Color, R0 + (kChannels * 4), 1 },  // R0'4
            { CopyBitDestination::Color, R0 + (kChannels * 5), 1 },  // R0'5
            { CopyBitDestination::Color, R0 + (kChannels * 6), 1 },  // R0'6

            { CopyBitDestination::Color, R1 + (kChannels * 0), 1 },  // R1'0
            { CopyBitDestination::Color, R1 + (kChannels * 1), 1 },  // R1'1
            { CopyBitDestination::Color, R1 + (kChannels * 2), 1 },  // R1'2
            { CopyBitDestination::Color, R1 + (kChannels * 3), 1 },  // R1'3
            { CopyBitDestination::Color, R1 + (kChannels * 4), 1 },  // R1'4
            { CopyBitDestination::Color, R1 + (kChannels * 5), 1 },  // R1'5
            { CopyBitDestination::Color, R1 + (kChannels * 6), 1 },  // R1'6


            { CopyBitDestination::Color, G0 + (kChannels * 0), 1 },  // G0'0
            { CopyBitDestination::Color, G0 + (kChannels * 1), 1 },  // G0'1
            { CopyBitDestination::Color, G0 + (kChannels * 2), 1 },  // G0'2
            { CopyBitDestination::Color, G0 + (kChannels * 3), 1 },  // G0'3
            { CopyBitDestination::Color, G0 + (kChannels * 4), 1 },  // G0'4
            { CopyBitDestination::Color, G0 + (kChannels * 5), 1 },  // G0'5
            { CopyBitDestination::Color, G0 + (kChannels * 6), 1 },  // G0'6

            { CopyBitDestination::Color, G1 + (kChannels * 0), 1 },  // G1'0
            { CopyBitDestination::Color, G1 + (kChannels * 1), 1 },  // G1'1
            { CopyBitDestination::Color, G1 + (kChannels * 2), 1 },  // G1'2
            { CopyBitDestination::Color, G1 + (kChannels * 3), 1 },  // G1'3
            { CopyBitDestination::Color, G1 + (kChannels * 4), 1 },  // G1'4
            { CopyBitDestination::Color, G1 + (kChannels * 5), 1 },  // G1'5
            { CopyBitDestination::Color, G1 + (kChannels * 6), 1 },  // G1'6


            { CopyBitDestination::Color, B0 + (kChannels * 0), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 1), 1 },  // B0'1
            { CopyBitDestination::Color, B0 + (kChannels * 2), 1 },  // B0'2
            { CopyBitDestination::Color, B0 + (kChannels * 3), 1 },  // B0'3
            { CopyBitDestination::Color, B0 + (kChannels * 4), 1 },  // B0'4
            { CopyBitDestination::Color, B0 + (kChannels * 5), 1 },  // B0'5
            { CopyBitDestination::Color, B0 + (kChannels * 6), 1 },  // B0'6

            { CopyBitDestination::Color, B1 + (kChannels * 0), 1 },  // B1'0
            { CopyBitDestination::Color, B1 + (kChannels * 1), 1 },  // B1'1
            { CopyBitDestination::Color, B1 + (kChannels * 2), 1 },  // B1'2
            { CopyBitDestination::Color, B1 + (kChannels * 3), 1 },  // B1'3
            { CopyBitDestination::Color, B1 + (kChannels * 4), 1 },  // B1'4
            { CopyBitDestination::Color, B1 + (kChannels * 5), 1 },  // B1'4
            { CopyBitDestination::Color, B1 + (kChannels * 6), 1 },  // B1'4

            { CopyBitDestination::Scraps, 0, 1 },  // ex0

            { CopyBitDestination::Color, A0 + (kChannels * 0), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 1), 1 },  // A0'1
            { CopyBitDestination::Color, A0 + (kChannels * 2), 1 },  // A0'2
            { CopyBitDestination::Color, A0 + (kChannels * 3), 1 },  // A0'3
            { CopyBitDestination::Color, A0 + (kChannels * 4), 1 },  // A0'4
            { CopyBitDestination::Color, A0 + (kChannels * 5), 1 },  // A0'5
            { CopyBitDestination::Color, A0 + (kChannels * 6), 1 },  // A0'6

            { CopyBitDestination::Scraps, 1, 1 },  // ex1

            { CopyBitDestination::Color, A1 + (kChannels * 0), 1 },  // A1'0
            { CopyBitDestination::Color, A1 + (kChannels * 1), 1 },  // A1'1
            { CopyBitDestination::Color, A1 + (kChannels * 2), 1 },  // A1'2
            { CopyBitDestination::Color, A1 + (kChannels * 3), 1 },  // A1'3
            { CopyBitDestination::Color, A1 + (kChannels * 4), 1 },  // A1'4
            { CopyBitDestination::Color, A1 + (kChannels * 5), 1 },  // A1'5
            { CopyBitDestination::Color, A1 + (kChannels * 6), 1 },  // A1'6


            { CopyBitDestination::Misc, 0, 31 },
            { CopyBitDestination::Misc, 32, 31 },
        };
        assert(_countof(ops) <= _countof(sequence.Ops));
        sequence.OpCount = _countof(ops);
        memcpy(sequence.Ops, ops, sizeof(ops));
    }
    else
    {
        assert(false);
    }

    return;
}

void GetBC_ModeSplit_CopyBitsOrderMode6(CopyBitsSequence& sequence, BC7ModeSplitShuffleOptions opt, BC7TextureMetrics metrics)
{
    memset(&sequence, 0, sizeof(sequence));

    //  Bit Offset  Bits or ordering and count
    //  0           7 bits Mode (1000000)
    //  7           7 bits R0   7 bits R1
    //  21          7 bits G0   7 bits G1
    //  35          7 bits B0   7 bits B1
    //  49          7 bits A0   7 bits A1
    //  63          1 bit P0    1 bit P1
    //  65          63 bits Index

    constexpr size_t kModeBits = 7;
    constexpr size_t kChannels = 8;

    if (opt.Patterns[6] == BC7ModeSplitShufflePattern::StableIsland) // matches experimental ID 13
    {
        // This mode is the "stable island" dynamic mode that depends on including a "statics" header to mark channels.  Bits are then arranged around this stable island within the color stream
        // the pattern looks different based on the number of static fields identified.  

        //  56 color bits
        // byte         0                        1                        2                      3                      4                       5                        6
        //     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55   non-static  req'for 3 bytes, 4 bytes,  5 bytes,  6 bytes
        // 0 statics   7th           -          5th          -           3rd         -          1st          -          2nd          -          4th          -           6th                 8           24           32         40        48
        // 1           7th           -          5th          -           3rd         - 0  1  2  3  4  5  6  *-          2nd          -          4th          -           6th                 7           17           25         33        41
        // 2           7th           -          5th          -           3rd         - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  *  *-          4th          -           6th                 6           10           18         26        34
        // 3           6th           -          4th          - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  *  *  *-          5th          -           7th                 5            3           11         19        27
        // 4           7th           -          5th          - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  *  *  *  *-           6th                 4                         4         12        20
        // 5           6th           - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  *  *  *  *  *-           7th                 3                                   5         13
        // 6           7th           - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  0  1  2  3  4- 5  6  *  *  *  *  *  *        2                                              6
        // 7   0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  0  1  2  3  4- 5  6  0  1  2  3  4  5- 6  *  *  *  *  *  *  *        1  
        // 8   0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  0  1  2  3  4_ 5  6  0  1  2  3  4  5- 6  0  1  2  3  4  5  6        0  


        static CopyBitsOrder ops[70] = {
             {   //  Mode (1000000)
                CopyBitDestination::Mode,
                0,
                kModeBits
            },
        };

        uint64_t placedBits = 0;

        size_t OrderCount = 1;
        size_t staticFields = __popcnt64(size_t(metrics.M[6].Statics | metrics.M[6].LowEntropy));       // <-- TODO should we instead bitwise wrap low entropy bits, rather than counting as statics?
        size_t staticBits = staticFields * 7;
        size_t otherFields = 8 - staticFields;



        size_t staticBitStarts[] = { 24, 24, 24, 16, 16, 8, 8, 0, 0 };
        size_t otherBitStarts[] = { 24, 31, 38, 37, 44, 43, 50, 49, 56 };

        size_t otherByteOrder[][7] = {
            {3, 4, 2, 5, 1, 6, 0},      // 0 statics
            {3, 4, 2, 5, 1, 6, 0},      // 1 statics
            {3, 4, 2, 5, 1, 6, 0},      // 2 statics
            {2, 3, 4, 1, 5, 0, 6},      // 3 statics
            {2, 3, 4, 5, 1, 6, 0},      // 4 statics
            {1, 2, 3, 4, 5, 0, 6},      // 5 statics
            {1, 2, 3, 4, 5, 6, 0},      // 6 statics
            {0, 1, 2, 3, 4, 5, 6},      // 7 statics
            {0, 1, 2, 3, 4, 5, 6},      // 8 statics
        };

        //                    R0 R1
        uint8_t order[] = { 6, 7, 4, 5, 2, 3, 0, 1 };   // A0, A1, B0, B1, G0, G1, R0, R1
        //uint8_t order[] = { 6, 7, 4, 5, 0, 1, 2, 3 };   // B0, B1, A0, A1, G0, G1, R0, R1

        // adjust ordering based on static fields
        for (uint8_t e = 0; e < 8; e++)
        {
            if ((metrics.M[6].Statics | metrics.M[6].LowEntropy) & (1u << e))
            {
                for (uint8_t ee = 0; ee < 8; ee++)
                {
                    if (order[ee] > order[e]) order[ee]--;
                }
                order[e] = 0;
            }
        }

        size_t nextStaticBit = staticBitStarts[staticFields];

        for (uint8_t e = 0; e < 8; e++)
        {
            if ((metrics.M[6].Statics | metrics.M[6].LowEntropy) & (1u << e))
            {
                ops[OrderCount].DestStream = CopyBitDestination::Color;
                ops[OrderCount].CopyBitCount = 7;
                ops[OrderCount].DestBitOffset = (uint8_t)nextStaticBit; assert(nextStaticBit <= 0xff);

                placedBits |= 0x7full << nextStaticBit;

                nextStaticBit += 7;
                OrderCount++;
            }
            else
            {
                for (uint8_t b = 0; b < 7; b++)
                {
                    const size_t bi = uint8_t(6u - b);
                    const size_t destLinear = staticBits + order[e] + bi * otherFields;
                    const size_t destLinearByte = destLinear / 8;
                    const size_t destLinearBit = destLinear % 8;
                    const size_t destRemappedByte = otherByteOrder[staticFields][destLinearByte];
                    const size_t destRemapped = 8 * destRemappedByte + destLinearBit;

                    ops[OrderCount].DestStream = CopyBitDestination::Color;
                    ops[OrderCount].CopyBitCount = 1;
                    ops[OrderCount].DestBitOffset = (uint8_t)destRemapped; assert(destRemapped <= 0xff);

                    placedBits |= 0x1ull << (destRemapped);

                    OrderCount++;
                }
            }
        }

        assert(placedBits == 0x00FFFFFFFFFFFFFFull);

        //  P0
        ops[OrderCount].DestStream = CopyBitDestination::Misc;
        ops[OrderCount].DestBitOffset = 63;
        ops[OrderCount].CopyBitCount = 1;
        OrderCount++;

        //  P1
        ops[OrderCount].DestStream = CopyBitDestination::Scraps;
        ops[OrderCount].DestBitOffset = 0;
        ops[OrderCount].CopyBitCount = 1;
        OrderCount++;

        //   Index
        ops[OrderCount].DestStream = CopyBitDestination::Misc;          // <-- todo try to encode significant bit order
        ops[OrderCount].DestBitOffset = 0;
        ops[OrderCount].CopyBitCount = 63;
        OrderCount++;

        assert(OrderCount <= _countof(sequence.Ops));
        sequence.OpCount = OrderCount;
        memcpy(sequence.Ops, ops, sizeof(CopyBitsOrder) * OrderCount);
    }
    else if (opt.Patterns[6] == BC7ModeSplitShufflePattern::EndpointPairSignificantBitInderleaved) // matches experimental ID 3
    {
        //  This shuffle treats 0/1 endpoints as a unit, and interleaves bits in significant bit order
        //  the most significant bits are placed at a byte boundary, and channel bits are interleaved.  Matches across modes are possible

        //  56 bits of RGBA 0\1:    bits 0 - 55
        //                  byte 0                                     byte 1                                 byte 2                                   byte 3                                  byte 4                                    byte 5                                  (Most significant)
        //  R0'0 R1'0 G0'0 G1'0 B0'0 B1'0 A0'0 A1'0  R0'1 R1'1 G0'1 G1'1 B0'1 B1'1 A0'1 A1'1   R0'2 R1'2 G0'2 G1'2 B0'2 B1'2 A0'2 A1'2   R0'3 R1'3 G0'3 G1'3 B0'3 B1'3 A0'3 A1'3   R0'4 R1'4 G0'4 G1'4 B0'4 B1'4 A0'4 A1'4  R0'5 R1'5 G0'5 G1'5 B0'5 B1'5 A0'5 A1'5  R0'6 R1'6 G0'6 G1'6 B0'6 B1'6 A0'6 A1'6
        //
        //

        enum ChannelOrder
        {
            R0 = 0,
            R1 = 1,
            G0 = 2,
            G1 = 3,
            B0 = 4,
            B1 = 5,
            A0 = 6,
            A1 = 7
        };

        CopyBitsOrder ops[] = {
            {   //  Mode (100000)
               CopyBitDestination::Mode,
               0,
               kModeBits
            },


            { CopyBitDestination::Color, R0 + (kChannels * 0), 1 },  // R0'0
            { CopyBitDestination::Color, R0 + (kChannels * 1), 1 },  // R0'1
            { CopyBitDestination::Color, R0 + (kChannels * 2), 1 },  // R0'2
            { CopyBitDestination::Color, R0 + (kChannels * 3), 1 },  // R0'3
            { CopyBitDestination::Color, R0 + (kChannels * 4), 1 },  // R0'4
            { CopyBitDestination::Color, R0 + (kChannels * 5), 1 },  // R0'5
            { CopyBitDestination::Color, R0 + (kChannels * 6), 1 },  // R0'6

            { CopyBitDestination::Color, R1 + (kChannels * 0), 1 },  // R1'0
            { CopyBitDestination::Color, R1 + (kChannels * 1), 1 },  // R1'1
            { CopyBitDestination::Color, R1 + (kChannels * 2), 1 },  // R1'2
            { CopyBitDestination::Color, R1 + (kChannels * 3), 1 },  // R1'3
            { CopyBitDestination::Color, R1 + (kChannels * 4), 1 },  // R1'4
            { CopyBitDestination::Color, R1 + (kChannels * 5), 1 },  // R1'5
            { CopyBitDestination::Color, R1 + (kChannels * 6), 1 },  // R1'6


            { CopyBitDestination::Color, G0 + (kChannels * 0), 1 },  // G0'0
            { CopyBitDestination::Color, G0 + (kChannels * 1), 1 },  // G0'1
            { CopyBitDestination::Color, G0 + (kChannels * 2), 1 },  // G0'2
            { CopyBitDestination::Color, G0 + (kChannels * 3), 1 },  // G0'3
            { CopyBitDestination::Color, G0 + (kChannels * 4), 1 },  // G0'4
            { CopyBitDestination::Color, G0 + (kChannels * 5), 1 },  // G0'5
            { CopyBitDestination::Color, G0 + (kChannels * 6), 1 },  // G0'6

            { CopyBitDestination::Color, G1 + (kChannels * 0), 1 },  // G1'0
            { CopyBitDestination::Color, G1 + (kChannels * 1), 1 },  // G1'1
            { CopyBitDestination::Color, G1 + (kChannels * 2), 1 },  // G1'2
            { CopyBitDestination::Color, G1 + (kChannels * 3), 1 },  // G1'3
            { CopyBitDestination::Color, G1 + (kChannels * 4), 1 },  // G1'4
            { CopyBitDestination::Color, G1 + (kChannels * 5), 1 },  // G1'5
            { CopyBitDestination::Color, G1 + (kChannels * 6), 1 },  // G1'6


            { CopyBitDestination::Color, B0 + (kChannels * 0), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 1), 1 },  // B0'1
            { CopyBitDestination::Color, B0 + (kChannels * 2), 1 },  // B0'2
            { CopyBitDestination::Color, B0 + (kChannels * 3), 1 },  // B0'3
            { CopyBitDestination::Color, B0 + (kChannels * 4), 1 },  // B0'4
            { CopyBitDestination::Color, B0 + (kChannels * 5), 1 },  // B0'5
            { CopyBitDestination::Color, B0 + (kChannels * 6), 1 },  // B0'6

            { CopyBitDestination::Color, B1 + (kChannels * 0), 1 },  // B1'0
            { CopyBitDestination::Color, B1 + (kChannels * 1), 1 },  // B1'1
            { CopyBitDestination::Color, B1 + (kChannels * 2), 1 },  // B1'2
            { CopyBitDestination::Color, B1 + (kChannels * 3), 1 },  // B1'3
            { CopyBitDestination::Color, B1 + (kChannels * 4), 1 },  // B1'4
            { CopyBitDestination::Color, B1 + (kChannels * 5), 1 },  // B1'4
            { CopyBitDestination::Color, B1 + (kChannels * 6), 1 },  // B1'4


            { CopyBitDestination::Color, A0 + (kChannels * 0), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 1), 1 },  // A0'1
            { CopyBitDestination::Color, A0 + (kChannels * 2), 1 },  // A0'2
            { CopyBitDestination::Color, A0 + (kChannels * 3), 1 },  // A0'3
            { CopyBitDestination::Color, A0 + (kChannels * 4), 1 },  // A0'4
            { CopyBitDestination::Color, A0 + (kChannels * 5), 1 },  // A0'5
            { CopyBitDestination::Color, A0 + (kChannels * 6), 1 },  // A0'6


            { CopyBitDestination::Color, A1 + (kChannels * 0), 1 },  // A1'0
            { CopyBitDestination::Color, A1 + (kChannels * 1), 1 },  // A1'1
            { CopyBitDestination::Color, A1 + (kChannels * 2), 1 },  // A1'2
            { CopyBitDestination::Color, A1 + (kChannels * 3), 1 },  // A1'3
            { CopyBitDestination::Color, A1 + (kChannels * 4), 1 },  // A1'4
            { CopyBitDestination::Color, A1 + (kChannels * 5), 1 },  // A1'5
            { CopyBitDestination::Color, A1 + (kChannels * 6), 1 },  // A1'6

            { CopyBitDestination::Misc, 63, 1 },   // P0
            { CopyBitDestination::Scraps, 0, 1 },  // P1

            { CopyBitDestination::Misc, 0, 63 },
        };
        assert(_countof(ops) <= _countof(sequence.Ops));
        sequence.OpCount = _countof(ops);
        memcpy(sequence.Ops, ops, sizeof(ops));
    }
    else
    {
        assert(false);
    }

    return;
}

void GetBC_ModeSplit_CopyBitsOrderMode7(CopyBitsSequence& sequence, BC7ModeSplitShuffleOptions opt, BC7TextureMetrics metrics)
{
    UNREFERENCED_PARAMETER(metrics);
    memset(&sequence, 0, sizeof(sequence));

    // BC7 mode 7 element
    // 
    //  Bit Offset  Bits or ordering and count
    //  0           8 bits Mode (10000000)
    //  6           6 bits Partition
    //  14          5 bits R0   5 bits R1   5 bits R2   5 bits R3
    //  34          5 bits G0   5 bits G1   5 bits G2   5 bits G3
    //  54          5 bits B0   5 bits B1   5 bits B2   5 bits B3
    //  74          5 bits A0   5 bits A1   5 bits A2   5 bits A3
    //  94          1 bit P0    1 bit P1    1 bit P2    1 bit P2
    //  98          30 bits Index

    constexpr size_t kModeBits = 8;
    constexpr size_t kChannels = 8;

    if (opt.Patterns[7] == BC7ModeSplitShufflePattern::EndpointQuadSignificantBitInderleaved)
    {
        //  This swizzle treats 0/1 endpoints as a group and 2/3 endpoints as a different group that vary independenty.
        //  the most significant bits are grouped in the middle.  Other possibly stable data is used to pad the endpoints pair
        //  data, to push the edge to mid-byte.

        //  40 bits of RGBA 0\1:    bits 4 - 43, with a 4 bit leading pad
        //                                                                                                                                                                                   Most significant 5
        //  [ 4 partition bits] R0'0 R1'0 G0'0 G1'0 A0'0 A1'0 B0'0 B1'0   R0'1 R1'1 G0'1 G1'1 A0'1 A1'1 B0'1 B1'1   R0'2 R1'2 G0'2 G1'2 A0'2 A1'2 B0'2 B1'2   R0'3 R1'3 G0'3 G1'3 A0'3 A1'3 B0'3 B1'3   R0'4 R1'4 G0'4 G1'4 A0'4 A1'4 B0'4 B1'4    
        //
        //  followed by reversed 40 bits of RGBA 2\3:    bits 44 - 83, padded with 4 bits 
        //    
        //       Most significant 5                                    4                                         3
        //  B2'4 B3'4 A0'4 A1'4 G2'4 G3'4 R2'4 R3'4   B2'3 B3'3 A0'3 A1'3 G2'3 G3'3 R2'3 R3'3   B2'2 B3'2 A0'2 A1'2 G2'2 G3'2 R2'2 R3'2   B2'1 B3'1 A0'1 A1'1 G2'1 G3'1 R2'1 R3'1   B2'0 B3'0 A0'0 A1'0 G2'0 G3'0 R2'0 R3'0   [2 partition, 2 P bits]
        //

        enum ChannelOrder
        {
            R0 = 4,
            R1 = 5,
            G0 = 6,
            G1 = 7,
            B0 = 8,
            B1 = 9,
            A0 = 10,
            A1 = 11,

            R2 = 82,
            R3 = 83,
            G2 = 80,
            G3 = 81,
            B2 = 78,
            B3 = 79,
            A2 = 76,
            A3 = 77,
        };

        CopyBitsOrder ops[] = {
            {   // Mode [10000000]
                CopyBitDestination::Mode,
                0,
                kModeBits
            },
            {   // Partition bits 0-3
                CopyBitDestination::Color,
                0,
                4
            },
            {   // Partition bits 4-5
                CopyBitDestination::Color,
                84,
                2
            },


            { CopyBitDestination::Color, R0 + (kChannels * 0), 1 },  // R0'0
            { CopyBitDestination::Color, R0 + (kChannels * 1), 1 },  // R0'1
            { CopyBitDestination::Color, R0 + (kChannels * 2), 1 },  // R0'2
            { CopyBitDestination::Color, R0 + (kChannels * 3), 1 },  // R0'3
            { CopyBitDestination::Color, R0 + (kChannels * 4), 1 },  // R0'4

            { CopyBitDestination::Color, R1 + (kChannels * 0), 1 },  // R1'0
            { CopyBitDestination::Color, R1 + (kChannels * 1), 1 },  // R1'1
            { CopyBitDestination::Color, R1 + (kChannels * 2), 1 },  // R1'2
            { CopyBitDestination::Color, R1 + (kChannels * 3), 1 },  // R1'3
            { CopyBitDestination::Color, R1 + (kChannels * 4), 1 },  // R1'4


            { CopyBitDestination::Color, R2 - (kChannels * 0), 1 },  // R2'0
            { CopyBitDestination::Color, R2 - (kChannels * 1), 1 },  // R2'1
            { CopyBitDestination::Color, R2 - (kChannels * 2), 1 },  // R2'2
            { CopyBitDestination::Color, R2 - (kChannels * 3), 1 },  // R2'3
            { CopyBitDestination::Color, R2 - (kChannels * 4), 1 },  // R2'4

            { CopyBitDestination::Color, R3 - (kChannels * 0), 1 },  // R3'0
            { CopyBitDestination::Color, R3 - (kChannels * 1), 1 },  // R3'1
            { CopyBitDestination::Color, R3 - (kChannels * 2), 1 },  // R3'2
            { CopyBitDestination::Color, R3 - (kChannels * 3), 1 },  // R3'3
            { CopyBitDestination::Color, R3 - (kChannels * 4), 1 },  // R3'4


            { CopyBitDestination::Color, G0 + (kChannels * 0), 1 },  // G0'0
            { CopyBitDestination::Color, G0 + (kChannels * 1), 1 },  // G0'1
            { CopyBitDestination::Color, G0 + (kChannels * 2), 1 },  // G0'2
            { CopyBitDestination::Color, G0 + (kChannels * 3), 1 },  // G0'3
            { CopyBitDestination::Color, G0 + (kChannels * 4), 1 },  // G0'4

            { CopyBitDestination::Color, G1 + (kChannels * 0), 1 },  // G1'0
            { CopyBitDestination::Color, G1 + (kChannels * 1), 1 },  // G1'1
            { CopyBitDestination::Color, G1 + (kChannels * 2), 1 },  // G1'2
            { CopyBitDestination::Color, G1 + (kChannels * 3), 1 },  // G1'3
            { CopyBitDestination::Color, G1 + (kChannels * 4), 1 },  // G1'4


            { CopyBitDestination::Color, G2 - (kChannels * 0), 1 },  // G2'0
            { CopyBitDestination::Color, G2 - (kChannels * 1), 1 },  // G2'1
            { CopyBitDestination::Color, G2 - (kChannels * 2), 1 },  // G2'2
            { CopyBitDestination::Color, G2 - (kChannels * 3), 1 },  // G2'3
            { CopyBitDestination::Color, G2 - (kChannels * 4), 1 },  // G2'4

            { CopyBitDestination::Color, G3 - (kChannels * 0), 1 },  // G3'0
            { CopyBitDestination::Color, G3 - (kChannels * 1), 1 },  // G3'1
            { CopyBitDestination::Color, G3 - (kChannels * 2), 1 },  // G3'2
            { CopyBitDestination::Color, G3 - (kChannels * 3), 1 },  // G3'3
            { CopyBitDestination::Color, G3 - (kChannels * 4), 1 },  // G3'4


            { CopyBitDestination::Color, B0 + (kChannels * 0), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 1), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 2), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 3), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 4), 1 },  // B0'0

            { CopyBitDestination::Color, B1 + (kChannels * 0), 1 },  // B1'0
            { CopyBitDestination::Color, B1 + (kChannels * 1), 1 },  // B1'1
            { CopyBitDestination::Color, B1 + (kChannels * 2), 1 },  // B1'2
            { CopyBitDestination::Color, B1 + (kChannels * 3), 1 },  // B1'3
            { CopyBitDestination::Color, B1 + (kChannels * 4), 1 },  // B1'4


            { CopyBitDestination::Color, B2 - (kChannels * 0), 1 },  // B2'0
            { CopyBitDestination::Color, B2 - (kChannels * 1), 1 },  // B2'1
            { CopyBitDestination::Color, B2 - (kChannels * 2), 1 },  // B2'2
            { CopyBitDestination::Color, B2 - (kChannels * 3), 1 },  // B2'3
            { CopyBitDestination::Color, B2 - (kChannels * 4), 1 },  // B2'4

            { CopyBitDestination::Color, B3 - (kChannels * 0), 1 },  // B3'0
            { CopyBitDestination::Color, B3 - (kChannels * 1), 1 },  // B3'1
            { CopyBitDestination::Color, B3 - (kChannels * 2), 1 },  // B3'2
            { CopyBitDestination::Color, B3 - (kChannels * 3), 1 },  // B3'3
            { CopyBitDestination::Color, B3 - (kChannels * 4), 1 },  // B3'4

            { CopyBitDestination::Color, A0 + (kChannels * 0), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 1), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 2), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 3), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 4), 1 },  // A0'0

            { CopyBitDestination::Color, A1 + (kChannels * 0), 1 },  // A1'0
            { CopyBitDestination::Color, A1 + (kChannels * 1), 1 },  // A1'1
            { CopyBitDestination::Color, A1 + (kChannels * 2), 1 },  // A1'2
            { CopyBitDestination::Color, A1 + (kChannels * 3), 1 },  // A1'3
            { CopyBitDestination::Color, A1 + (kChannels * 4), 1 },  // A1'4


            { CopyBitDestination::Color, A2 - (kChannels * 0), 1 },  // A2'0
            { CopyBitDestination::Color, A2 - (kChannels * 1), 1 },  // A2'1
            { CopyBitDestination::Color, A2 - (kChannels * 2), 1 },  // A2'2
            { CopyBitDestination::Color, A2 - (kChannels * 3), 1 },  // A2'3
            { CopyBitDestination::Color, A2 - (kChannels * 4), 1 },  // A2'4

            { CopyBitDestination::Color, A3 - (kChannels * 0), 1 },  // A3'0
            { CopyBitDestination::Color, A3 - (kChannels * 1), 1 },  // A3'1
            { CopyBitDestination::Color, A3 - (kChannels * 2), 1 },  // A3'2
            { CopyBitDestination::Color, A3 - (kChannels * 3), 1 },  // A3'3
            { CopyBitDestination::Color, A3 - (kChannels * 4), 1 },  // A3'4

            {   // P bits 0-1
                CopyBitDestination::Color,
                86,
                2
            },

            // above is 8 mode bits, 6 partition, 80 color bits, 2 pbits.   2 pbits, 30 index bits remain

            {   // P bits + index
                CopyBitDestination::Misc,
                0,
                32,

            },
        };
        assert(_countof(ops) <= _countof(sequence.Ops));
        sequence.OpCount = _countof(ops);
        memcpy(sequence.Ops, ops, sizeof(ops));
    }
    else if (opt.Patterns[7] == BC7ModeSplitShufflePattern::EndpointQuadSignificantBitInderleavedAlt) // matches experimental ID 4
    {
        //  This swizzle treats 0/1 endpoints as a group and 2/3 endpoints as a different group that vary independenty.
        //  the most significant bits are grouped in the middle.  Alt implies byte aligned endpoint pair edge.

        //  40 bits of RGBA 0\1:    bits 0 - 39
        //                                                                                                                3                               4                                    Most significant 5
        //  R0'0 R1'0 G0'0 G1'0 A0'0 A1'0 B0'0 B1'0   R0'1 R1'1 G0'1 G1'1 A0'1 A1'1 B0'1 B1'1   R0'2 R1'2 G0'2 G1'2 A0'2 A1'2 B0'2 B1'2   R0'3 R1'3 G0'3 G1'3 A0'3 A1'3 B0'3 B1'3   R0'4 R1'4 G0'4 G1'4 A0'4 A1'4 B0'4 B1'4    
        //
        //  followed by reversed 40 bits of RGBA 2\3:    bits 40 - 79 
        //    
        //       Most significant 5                                    4                                         3
        //  B2'4 B3'4 A0'4 A1'4 G2'4 G3'4 R2'4 R3'4   B2'3 B3'3 A0'3 A1'3 G2'3 G3'3 R2'3 R3'3   B2'2 B3'2 A0'2 A1'2 G2'2 G3'2 R2'2 R3'2   B2'1 B3'1 A0'1 A1'1 G2'1 G3'1 R2'1 R3'1   B2'0 B3'0 A0'0 A1'0 G2'0 G3'0 R2'0 R3'0   
        //

        enum ChannelOrder
        {
            R0 = 0,
            R1 = 1,
            G0 = 2,
            G1 = 3,
            //A0 = 4,
            //A1 = 5,
            //B0 = 6,
            //B1 = 7,
            B0 = 4,
            B1 = 5,
            A0 = 6,
            A1 = 7,

            R2 = 78,
            R3 = 79,
            G2 = 76,
            G3 = 77,
            //A2 = 74,
            //A3 = 75,
            //B2 = 72,
            //B3 = 73,
            B2 = 74,
            B3 = 75,
            A2 = 72,
            A3 = 73,
        };

        CopyBitsOrder ops[] = {
            {   // Mode [10000000]
                CopyBitDestination::Mode,
                0,
                kModeBits
            },
            {   // Partition 
                CopyBitDestination::Misc,
                0,
                6
            },


            { CopyBitDestination::Color, R0 + (kChannels * 0), 1 },  // R0'0
            { CopyBitDestination::Color, R0 + (kChannels * 1), 1 },  // R0'1
            { CopyBitDestination::Color, R0 + (kChannels * 2), 1 },  // R0'2
            { CopyBitDestination::Color, R0 + (kChannels * 3), 1 },  // R0'3
            { CopyBitDestination::Color, R0 + (kChannels * 4), 1 },  // R0'4

            { CopyBitDestination::Color, R1 + (kChannels * 0), 1 },  // R1'0
            { CopyBitDestination::Color, R1 + (kChannels * 1), 1 },  // R1'1
            { CopyBitDestination::Color, R1 + (kChannels * 2), 1 },  // R1'2
            { CopyBitDestination::Color, R1 + (kChannels * 3), 1 },  // R1'3
            { CopyBitDestination::Color, R1 + (kChannels * 4), 1 },  // R1'4


            { CopyBitDestination::Color, R2 - (kChannels * 0), 1 },  // R2'0
            { CopyBitDestination::Color, R2 - (kChannels * 1), 1 },  // R2'1
            { CopyBitDestination::Color, R2 - (kChannels * 2), 1 },  // R2'2
            { CopyBitDestination::Color, R2 - (kChannels * 3), 1 },  // R2'3
            { CopyBitDestination::Color, R2 - (kChannels * 4), 1 },  // R2'4

            { CopyBitDestination::Color, R3 - (kChannels * 0), 1 },  // R3'0
            { CopyBitDestination::Color, R3 - (kChannels * 1), 1 },  // R3'1
            { CopyBitDestination::Color, R3 - (kChannels * 2), 1 },  // R3'2
            { CopyBitDestination::Color, R3 - (kChannels * 3), 1 },  // R3'3
            { CopyBitDestination::Color, R3 - (kChannels * 4), 1 },  // R3'4


            { CopyBitDestination::Color, G0 + (kChannels * 0), 1 },  // G0'0
            { CopyBitDestination::Color, G0 + (kChannels * 1), 1 },  // G0'1
            { CopyBitDestination::Color, G0 + (kChannels * 2), 1 },  // G0'2
            { CopyBitDestination::Color, G0 + (kChannels * 3), 1 },  // G0'3
            { CopyBitDestination::Color, G0 + (kChannels * 4), 1 },  // G0'4

            { CopyBitDestination::Color, G1 + (kChannels * 0), 1 },  // G1'0
            { CopyBitDestination::Color, G1 + (kChannels * 1), 1 },  // G1'1
            { CopyBitDestination::Color, G1 + (kChannels * 2), 1 },  // G1'2
            { CopyBitDestination::Color, G1 + (kChannels * 3), 1 },  // G1'3
            { CopyBitDestination::Color, G1 + (kChannels * 4), 1 },  // G1'4


            { CopyBitDestination::Color, G2 - (kChannels * 0), 1 },  // G2'0
            { CopyBitDestination::Color, G2 - (kChannels * 1), 1 },  // G2'1
            { CopyBitDestination::Color, G2 - (kChannels * 2), 1 },  // G2'2
            { CopyBitDestination::Color, G2 - (kChannels * 3), 1 },  // G2'3
            { CopyBitDestination::Color, G2 - (kChannels * 4), 1 },  // G2'4

            { CopyBitDestination::Color, G3 - (kChannels * 0), 1 },  // G3'0
            { CopyBitDestination::Color, G3 - (kChannels * 1), 1 },  // G3'1
            { CopyBitDestination::Color, G3 - (kChannels * 2), 1 },  // G3'2
            { CopyBitDestination::Color, G3 - (kChannels * 3), 1 },  // G3'3
            { CopyBitDestination::Color, G3 - (kChannels * 4), 1 },  // G3'4


            { CopyBitDestination::Color, B0 + (kChannels * 0), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 1), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 2), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 3), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 4), 1 },  // B0'0

            { CopyBitDestination::Color, B1 + (kChannels * 0), 1 },  // B1'0
            { CopyBitDestination::Color, B1 + (kChannels * 1), 1 },  // B1'1
            { CopyBitDestination::Color, B1 + (kChannels * 2), 1 },  // B1'2
            { CopyBitDestination::Color, B1 + (kChannels * 3), 1 },  // B1'3
            { CopyBitDestination::Color, B1 + (kChannels * 4), 1 },  // B1'4


            { CopyBitDestination::Color, B2 - (kChannels * 0), 1 },  // B2'0
            { CopyBitDestination::Color, B2 - (kChannels * 1), 1 },  // B2'1
            { CopyBitDestination::Color, B2 - (kChannels * 2), 1 },  // B2'2
            { CopyBitDestination::Color, B2 - (kChannels * 3), 1 },  // B2'3
            { CopyBitDestination::Color, B2 - (kChannels * 4), 1 },  // B2'4

            { CopyBitDestination::Color, B3 - (kChannels * 0), 1 },  // B3'0
            { CopyBitDestination::Color, B3 - (kChannels * 1), 1 },  // B3'1
            { CopyBitDestination::Color, B3 - (kChannels * 2), 1 },  // B3'2
            { CopyBitDestination::Color, B3 - (kChannels * 3), 1 },  // B3'3
            { CopyBitDestination::Color, B3 - (kChannels * 4), 1 },  // B3'4

            { CopyBitDestination::Color, A0 + (kChannels * 0), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 1), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 2), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 3), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 4), 1 },  // A0'0

            { CopyBitDestination::Color, A1 + (kChannels * 0), 1 },  // A1'0
            { CopyBitDestination::Color, A1 + (kChannels * 1), 1 },  // A1'1
            { CopyBitDestination::Color, A1 + (kChannels * 2), 1 },  // A1'2
            { CopyBitDestination::Color, A1 + (kChannels * 3), 1 },  // A1'3
            { CopyBitDestination::Color, A1 + (kChannels * 4), 1 },  // A1'4


            { CopyBitDestination::Color, A2 - (kChannels * 0), 1 },  // A2'0
            { CopyBitDestination::Color, A2 - (kChannels * 1), 1 },  // A2'1
            { CopyBitDestination::Color, A2 - (kChannels * 2), 1 },  // A2'2
            { CopyBitDestination::Color, A2 - (kChannels * 3), 1 },  // A2'3
            { CopyBitDestination::Color, A2 - (kChannels * 4), 1 },  // A2'4

            { CopyBitDestination::Color, A3 - (kChannels * 0), 1 },  // A3'0
            { CopyBitDestination::Color, A3 - (kChannels * 1), 1 },  // A3'1
            { CopyBitDestination::Color, A3 - (kChannels * 2), 1 },  // A3'2
            { CopyBitDestination::Color, A3 - (kChannels * 3), 1 },  // A3'3
            { CopyBitDestination::Color, A3 - (kChannels * 4), 1 },  // A3'4

            // above is 8 mode bits, 6 partition, 80 color bits.   4 pbits, 30 index bits remain
            // misc has 6 partition bits.

            {   // P bits + index
                CopyBitDestination::Misc,
                6,
                34,

            },
        };
        assert(_countof(ops) <= _countof(sequence.Ops));
        sequence.OpCount = _countof(ops);
        memcpy(sequence.Ops, ops, sizeof(ops));
    }
    else if (opt.Patterns[7] == BC7ModeSplitShufflePattern::EndpointPairSignificantBitInderleaved)
    {
        //  This swizzle treats 0/1 endpoints as a group and 2/3 endpoints as a different group that vary independenty.
        //  Each pair is encoded independently.  Since Partition is sometimes low entropy, that is included in Color.

        //  40 bits of RGBA 0\1:    bits 0 - 39
        //                                                                                                                                                                                   Most significant 5
        //  R0'0 R1'0 G0'0 G1'0 A0'0 A1'0 B0'0 B1'0   R0'1 R1'1 G0'1 G1'1 A0'1 A1'1 B0'1 B1'1   R0'2 R1'2 G0'2 G1'2 A0'2 A1'2 B0'2 B1'2   R0'3 R1'3 G0'3 G1'3 A0'3 A1'3 B0'3 B1'3   R0'4 R1'4 G0'4 G1'4 A0'4 A1'4 B0'4 B1'4    
        //
        //  Followed by 40 bits of RGBA 2\3:    bits 40 - 79
        //                                                                                                                                                                                   Most significant 5
        //  R2'0 R3'0 G2'0 G3'0 A2'0 A3'0 B2'0 B3'0   R2'1 R3'1 G2'1 G3'1 A2'1 A3'1 B2'1 B3'1   R2'2 R3'2 G2'2 G3'2 A2'2 A3'2 B2'2 B3'2   R2'3 R3'3 G2'3 G3'3 A2'3 A3'3 B2'3 B3'3   R2'4 R3'4 G2'4 G3'4 A2'4 A3'4 B2'4 B3'4    
        //
        // Followed by partition and 2 pbits  (experiments with actual channel order still in process)

        enum ChannelOrder
        {
            R0 = 0,
            R1 = 1,
            G0 = 2,
            G1 = 3,
            B0 = 4,
            B1 = 5,
            A0 = 6,
            A1 = 7,

            R2 = 40,
            R3 = 41,
            G2 = 42,
            G3 = 43,
            B2 = 44,
            B3 = 45,
            A2 = 46,
            A3 = 47,
        };

        CopyBitsOrder ops[] = {
            {   // Mode [10000000]
                CopyBitDestination::Mode,
                0,
                kModeBits
            },
            {   // Partition
                CopyBitDestination::Color,
                80,
                6
            },


            { CopyBitDestination::Color, R0 + (kChannels * 0), 1 },  // R0'0
            { CopyBitDestination::Color, R0 + (kChannels * 1), 1 },  // R0'1
            { CopyBitDestination::Color, R0 + (kChannels * 2), 1 },  // R0'2
            { CopyBitDestination::Color, R0 + (kChannels * 3), 1 },  // R0'3
            { CopyBitDestination::Color, R0 + (kChannels * 4), 1 },  // R0'4

            { CopyBitDestination::Color, R1 + (kChannels * 0), 1 },  // R1'0
            { CopyBitDestination::Color, R1 + (kChannels * 1), 1 },  // R1'1
            { CopyBitDestination::Color, R1 + (kChannels * 2), 1 },  // R1'2
            { CopyBitDestination::Color, R1 + (kChannels * 3), 1 },  // R1'3
            { CopyBitDestination::Color, R1 + (kChannels * 4), 1 },  // R1'4


            { CopyBitDestination::Color, R2 + (kChannels * 0), 1 },  // R2'0
            { CopyBitDestination::Color, R2 + (kChannels * 1), 1 },  // R2'1
            { CopyBitDestination::Color, R2 + (kChannels * 2), 1 },  // R2'2
            { CopyBitDestination::Color, R2 + (kChannels * 3), 1 },  // R2'3
            { CopyBitDestination::Color, R2 + (kChannels * 4), 1 },  // R2'4

            { CopyBitDestination::Color, R3 + (kChannels * 0), 1 },  // R3'0
            { CopyBitDestination::Color, R3 + (kChannels * 1), 1 },  // R3'1
            { CopyBitDestination::Color, R3 + (kChannels * 2), 1 },  // R3'2
            { CopyBitDestination::Color, R3 + (kChannels * 3), 1 },  // R3'3
            { CopyBitDestination::Color, R3 + (kChannels * 4), 1 },  // R3'4


            { CopyBitDestination::Color, G0 + (kChannels * 0), 1 },  // G0'0
            { CopyBitDestination::Color, G0 + (kChannels * 1), 1 },  // G0'1
            { CopyBitDestination::Color, G0 + (kChannels * 2), 1 },  // G0'2
            { CopyBitDestination::Color, G0 + (kChannels * 3), 1 },  // G0'3
            { CopyBitDestination::Color, G0 + (kChannels * 4), 1 },  // G0'4

            { CopyBitDestination::Color, G1 + (kChannels * 0), 1 },  // G1'0
            { CopyBitDestination::Color, G1 + (kChannels * 1), 1 },  // G1'1
            { CopyBitDestination::Color, G1 + (kChannels * 2), 1 },  // G1'2
            { CopyBitDestination::Color, G1 + (kChannels * 3), 1 },  // G1'3
            { CopyBitDestination::Color, G1 + (kChannels * 4), 1 },  // G1'4


            { CopyBitDestination::Color, G2 + (kChannels * 0), 1 },  // G2'0
            { CopyBitDestination::Color, G2 + (kChannels * 1), 1 },  // G2'1
            { CopyBitDestination::Color, G2 + (kChannels * 2), 1 },  // G2'2
            { CopyBitDestination::Color, G2 + (kChannels * 3), 1 },  // G2'3
            { CopyBitDestination::Color, G2 + (kChannels * 4), 1 },  // G2'4

            { CopyBitDestination::Color, G3 + (kChannels * 0), 1 },  // G3'0
            { CopyBitDestination::Color, G3 + (kChannels * 1), 1 },  // G3'1
            { CopyBitDestination::Color, G3 + (kChannels * 2), 1 },  // G3'2
            { CopyBitDestination::Color, G3 + (kChannels * 3), 1 },  // G3'3
            { CopyBitDestination::Color, G3 + (kChannels * 4), 1 },  // G3'4


            { CopyBitDestination::Color, B0 + (kChannels * 0), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 1), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 2), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 3), 1 },  // B0'0
            { CopyBitDestination::Color, B0 + (kChannels * 4), 1 },  // B0'0

            { CopyBitDestination::Color, B1 + (kChannels * 0), 1 },  // B1'0
            { CopyBitDestination::Color, B1 + (kChannels * 1), 1 },  // B1'1
            { CopyBitDestination::Color, B1 + (kChannels * 2), 1 },  // B1'2
            { CopyBitDestination::Color, B1 + (kChannels * 3), 1 },  // B1'3
            { CopyBitDestination::Color, B1 + (kChannels * 4), 1 },  // B1'4


            { CopyBitDestination::Color, B2 + (kChannels * 0), 1 },  // B2'0
            { CopyBitDestination::Color, B2 + (kChannels * 1), 1 },  // B2'1
            { CopyBitDestination::Color, B2 + (kChannels * 2), 1 },  // B2'2
            { CopyBitDestination::Color, B2 + (kChannels * 3), 1 },  // B2'3
            { CopyBitDestination::Color, B2 + (kChannels * 4), 1 },  // B2'4

            { CopyBitDestination::Color, B3 + (kChannels * 0), 1 },  // B3'0
            { CopyBitDestination::Color, B3 + (kChannels * 1), 1 },  // B3'1
            { CopyBitDestination::Color, B3 + (kChannels * 2), 1 },  // B3'2
            { CopyBitDestination::Color, B3 + (kChannels * 3), 1 },  // B3'3
            { CopyBitDestination::Color, B3 + (kChannels * 4), 1 },  // B3'4

            { CopyBitDestination::Color, A0 + (kChannels * 0), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 1), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 2), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 3), 1 },  // A0'0
            { CopyBitDestination::Color, A0 + (kChannels * 4), 1 },  // A0'0

            { CopyBitDestination::Color, A1 + (kChannels * 0), 1 },  // A1'0
            { CopyBitDestination::Color, A1 + (kChannels * 1), 1 },  // A1'1
            { CopyBitDestination::Color, A1 + (kChannels * 2), 1 },  // A1'2
            { CopyBitDestination::Color, A1 + (kChannels * 3), 1 },  // A1'3
            { CopyBitDestination::Color, A1 + (kChannels * 4), 1 },  // A1'4


            { CopyBitDestination::Color, A2 + (kChannels * 0), 1 },  // A2'0
            { CopyBitDestination::Color, A2 + (kChannels * 1), 1 },  // A2'1
            { CopyBitDestination::Color, A2 + (kChannels * 2), 1 },  // A2'2
            { CopyBitDestination::Color, A2 + (kChannels * 3), 1 },  // A2'3
            { CopyBitDestination::Color, A2 + (kChannels * 4), 1 },  // A2'4

            { CopyBitDestination::Color, A3 + (kChannels * 0), 1 },  // A3'0
            { CopyBitDestination::Color, A3 + (kChannels * 1), 1 },  // A3'1
            { CopyBitDestination::Color, A3 + (kChannels * 2), 1 },  // A3'2
            { CopyBitDestination::Color, A3 + (kChannels * 3), 1 },  // A3'3
            { CopyBitDestination::Color, A3 + (kChannels * 4), 1 },  // A3'4

            {   // P bits 0-1
                CopyBitDestination::Color,
                86,
                2
            },

            // above is 8 mode bits, 6 partition, 80 color bits, 2 pbits.   2 pbits, 30 index bits remain

            {   // P bits + index
                CopyBitDestination::Misc,
                0,
                32,

            },
        };
        assert(_countof(ops) <= _countof(sequence.Ops));
        sequence.OpCount = _countof(ops);
        memcpy(sequence.Ops, ops, sizeof(ops));
    }
    else
    {
        assert(false);
    }

    return;
}

void GetBC_ModeSplit_CopyBitsOrderMode8(CopyBitsSequence& sequence, BC7ModeSplitShuffleOptions opt, BC7TextureMetrics metrics)
{
    UNREFERENCED_PARAMETER(metrics);
    UNREFERENCED_PARAMETER(opt);
    memset(&sequence, 0, sizeof(sequence));

    // mode 8 is sometimes used to mark invalid texture regions within layouts that are not linear packed mips

    CopyBitsOrder ops[] = {
        {   // Mode [00000000]
            CopyBitDestination::Mode,
            0,
            8
        },
        {   // Partition 
            CopyBitDestination::Misc,
            0,
            120
        },
    };

    sequence.OpCount = 2;
    memcpy(sequence.Ops, ops, sizeof(ops));
}

void  (*BC7_ModeSplit_Shuffle_OpLists[9])(CopyBitsSequence& sequence, BC7ModeSplitShuffleOptions opt, BC7TextureMetrics metrics) =
{
    &GetBC_ModeSplit_CopyBitsOrderMode0,
    &GetBC_ModeSplit_CopyBitsOrderMode1,
    &GetBC_ModeSplit_CopyBitsOrderMode2,
    &GetBC_ModeSplit_CopyBitsOrderMode3,
    &GetBC_ModeSplit_CopyBitsOrderMode4,
    &GetBC_ModeSplit_CopyBitsOrderMode5,
    &GetBC_ModeSplit_CopyBitsOrderMode6,
    &GetBC_ModeSplit_CopyBitsOrderMode7,
    &GetBC_ModeSplit_CopyBitsOrderMode8,
};

const uint8_t ModeTransformBCountToModeBits[] = { 0,
    0,  //  if there's only one mode, we know which one by the LUT byte
    1,  //  2 modes
    2,  //  3 modes
    2,  //  4 modes
    3,  //  5
    3,  //  6
    3,  //  7
    3,  //  8
    4,  //  9 - only exists in pre-swizzled textures, which fill blank space with mode 8
};

#pragma warning (push)
#pragma warning (disable : 4201)    /*  For now, suppressing warnings for anonymous structs/unions, will revisit if compiler support warants  */

union ModeSplitStartByte
{
    uint8_t Raw;
    struct
    {
        uint8_t ModesUsed : 4;
        uint8_t ModeEncoding : 1;
        uint8_t StaticFields : 1;
        uint8_t LowEntropyFields : 1;
        uint8_t Reserved : 1;
    };
};
static_assert(sizeof(ModeSplitStartByte) == 1, "Split mode header byte struct must remain byte size");

/*
    LUT entry:
    byte
     1  - ModeSplitLutByte1
   [0-1] - ModeSplitLutAdditionalExtensions
   [0-1] - Static fields byte
   [0-1] - Low Entropy fields byte
   [0-2] - ModeSplitLutEndpointOrderEntry
   [0-1] - ModeSplitLutRotationHeader
   [0-n] - rotation bytes
*/

union ModeSplitLutByte1
{
    uint8_t Raw;
    struct
    {
        uint8_t Mode : 4;
        uint8_t ModePattern : 3;
        uint8_t AdditionalExtensions : 1;
    };
};
static_assert(sizeof(ModeSplitLutByte1) == 1, "Split mode lut byte struct must remain byte size");

union ModeSplitLutAdditionalExtensions
{
    uint8_t Raw;
    struct
    {
        uint8_t StaticFields : 1;
        uint8_t LowEntropyFields : 1;
        uint8_t EndpointOrderBytes : 2;  // 0-2 Endpoint Order Entry bytes (following any Low Entropy byte), which define 0-4 bit spend patterns.
        uint8_t Rotation : 1;
        uint8_t Reserved : 3;
    };
};
static_assert(sizeof(ModeSplitLutAdditionalExtensions) == 1, "Split mode lut extensions struct must remain byte size");

union ModeSplitLutEndpointOrderEntry    // describes what channels each bit should flip to restore original ordering
{
    uint8_t Raw;
    struct
    {
        uint8_t Bit0_R : 1;
        uint8_t Bit0_G : 1;
        uint8_t Bit0_B : 1;
        uint8_t Bit0_A : 1;

        uint8_t Bit1_R : 1;
        uint8_t Bit1_G : 1;
        uint8_t Bit1_B : 1;
        uint8_t Bit1_A : 1;
    };
};

union ModeSplitLutRotationHeader    // describes rotation frequency, and what channels are rotated
{
    uint8_t Raw;
    struct
    {
        uint8_t R : 1;
        uint8_t G : 1;
        uint8_t B : 1;
        uint8_t A : 1;
        uint8_t Frequency : 4;     // encodes rotation frequency, 4096*(2^n) bytes  4KB - 128MB
    };
    uint8_t ChannelBits : 4;
};

#pragma warning (pop)

void ComputeRotationToStableRange7bit(size_t counts[8][128], size_t elements, ColorRotation& rotations, size_t* rotationOrders)
{
    UNREFERENCED_PARAMETER(rotationOrders);
    uint8_t bestGroupingStart[8][6] = {};
    size_t bestGroupingCount[8][6] = {};

    for (int e = 0; e < 8; e++)
    {
        // rotated sums for each order 1-6
        size_t rotatedSums[128][6] = {};

        for (uint8_t first = 0; first < 128; first++)
        {
            for (uint8_t i = 0; i < 64; i++)
            {
                //if (((first + i) % 128) == 0) continue;
                if (i < 2)
                {
                    rotatedSums[first][5] += counts[e][(first + i) % 128u];  // 5 - count within 1/64 the range
                }
                if (i < 4)
                {
                    rotatedSums[first][4] += counts[e][(first + i) % 128u];  // 4 - count within 1/32 the range
                }
                if (i < 8)
                {
                    rotatedSums[first][3] += counts[e][(first + i) % 128u];  // 3 - count within 1/16 the range
                }
                if (i < 16)
                {
                    rotatedSums[first][2] += counts[e][(first + i) % 128u];  // 2 - count within 1/8 the range
                }
                if (i < 32)
                {
                    rotatedSums[first][1] += counts[e][(first + i) % 128u];  // 1 - count within 1/4 the range
                }
                if (i < 64)
                {
                    rotatedSums[first][0] += counts[e][(first + i) % 128u];  // 0 - count within 1/2 the range
                }
            }
        }

        for (uint8_t i = 0; i < 128; i++)
        {
            for (uint8_t o = 0; o < 6; o++)
            {
                if (rotatedSums[i][o] > bestGroupingCount[e][o])
                {
                    bestGroupingCount[e][o] = rotatedSums[i][o];
                    bestGroupingStart[e][o] = i;
                }
            }
        }

        // then decide on which "order" to keep, and compute final rotation, below is based on 7 bit fields
        const uint8_t rotDest[] = { 0, 0, 16, 16, 24, 26 };
        // 0 0 0 0 0 0 0    0
        // 0 0 0 0 0 0 1    1
        // 0 0 0 0 0 1 0    2
        // 0 0 0 0 0 1 1    3
        // 0 0 0 0 1 0 0    4
        // 0 0 0 0 1 0 1    5
        // 0 0 0 0 1 1 0    6
        // 0 0 0 0 1 1 1    7
        // 0 0 0 1 0 0 0    8
        // 0 0 0 1 0 0 1    9
        // 0 0 0 1 0 1 0    10
        // 0 0 0 1 0 1 1    11
        // 0 0 0 1 1 0 0    12
        // 0 0 0 1 1 0 1    13
        // 0 0 0 1 1 1 0    14
        // 0 0 0 1 1 1 1    15
        // 0 0 1 0 0 0 0    16       ]      ]
        // 0 0 1 0 0 0 1    17       ]      ]
        // 0 0 1 0 0 1 0    18       ]      ]
        // 0 0 1 0 0 1 1    19       ]      ]
        // 0 0 1 0 1 0 0    20       ]      ]- 4th order range
        // 0 0 1 0 1 0 1    21       ]      ]
        // 0 0 1 0 1 1 0    22       ]      ]
        // 0 0 1 0 1 1 1    23       ]      ]
        // 0 0 1 1 0 0 0    24       ]           ]- 5th order range
        // 0 0 1 1 0 0 1    25       ]           ]
        // 0 0 1 1 0 1 0    26       ]           ]    ]- 6th order range
        // 0 0 1 1 0 1 1    27       ]           ]    ]
        // 0 0 1 1 1 0 0    28       ]
        // 0 0 1 1 1 0 1    29       ]-3rd order range
        // 0 0 1 1 1 1 0    30       ]
        // 0 0 1 1 1 1 1    31       ]


        for (uint32_t o = 0; o < 6; o++)
        {
            if (bestGroupingCount[e][o] > (1 * elements / 2))
            {
                //rotationsOut[e] = (bestGroupingStart[e][o] - rotDest[o]) % 128u;
                rotations.E[e] =
                    bestGroupingStart[e][o] <= rotDest[o] ?
                    rotDest[o] - bestGroupingStart[e][o] :
                    rotDest[o] + (128u - bestGroupingStart[e][o]);
            }
        }
    }
}


void ComputeRotationToLeastSignificantEntropy(size_t counts[8][128], size_t elements, ColorRotation& rotations, uint8_t bitDepth)
{
    assert(bitDepth < 8);
    uint8_t fieldValues = uint8_t(1u << bitDepth);
    //     channel, rotation, bit
    std::vector<size_t> cbitsvec(8 * 128 * 7);
    size_t(*cbits)[128][7] = reinterpret_cast<size_t(*)[128][7]>(cbitsvec.data());   //size_t cbits[8][128][7]

    for (int e = 0; e < 8; e++)
    {
        // compute ideal entropy shift for each color channel
        for (uint8_t rot = 0; rot < fieldValues; rot++)
        {
            // loop through the counts of each 128-bit color value
            for (uint8_t i = 0; i < fieldValues; i++)
            {
                for (uint8_t b = 0; b < 7; b++)
                {
                    // Say there's 217 instances of value 63....  i.e. counts[e][63] == 217
                    // for rotation 0, that would mean bit counts of (MSB)  0, 217, 217, 217, 217, 217, 217 (LSB)
                    // for rotation 1, that would mean bit counts of (MSB) 217,  0,   0,   0,   0,   0,   0 (LSB)
                    const uint8_t rotatedValue = (i + rot) % fieldValues;
                    const uint8_t rotatedValueBitB = (rotatedValue >> b) & 1u;
                    cbits[e][rot][b] += counts[e][i] * rotatedValueBitB;

                    // when rot == 1, and all those 63s become 64s, 
                    // when i = 63, those bits are found: counts[e][63] * ((64 >> b) & 1)
                    //                                  =    217 values of 64
                }
            }
        }

        size_t bestEntropy = 0;
        uint8_t bestShift = 0;

        //size_t bitFlipWeights[] = { 1, 1, 1, 1, 1, 1, 1 };
        size_t bitFlipWeights[] = { 1, 2, 4, 8, 16, 32, 64 };
        //size_t bitFlipWeights[] = { 0, 1, 2, 3, 4, 5, 6 };
        //size_t bitFlipWeights[] = { 1, 10, 100, 1000, 10000, 100000, 1000000 };
        //size_t bitFlipWeights[] = { 1, 1ull << 2, 1ull << 4, 1ull << 6, 1ull << 8, 1ull << 10, 1ull << 12, 1u << 14 };
        //size_t bitFlipWeights[] = { 1, 1ull << 3, 1ull << 6, 1ull << 9, 1ull << 12, 1ull << 15, 1ull << 18 };

        for (uint8_t rot = 0; rot < fieldValues; rot++)
        {
            size_t bitFlips[7] = {};
            for (int b = 0; b < bitDepth; b++)
            {
                bitFlips[b] = std::min<size_t>(cbits[e][rot][b], elements - cbits[e][rot][b]);
            }
            size_t entropy = bitFlipWeights[6] * bitFlips[6] + bitFlipWeights[5] * bitFlips[5] + bitFlipWeights[4] * bitFlips[4] + bitFlipWeights[3] * bitFlips[3] + bitFlipWeights[2] * bitFlips[2] + bitFlipWeights[1] * bitFlips[1] + bitFlipWeights[0] * bitFlips[0];
            if (rot == 0)
            {
                bestEntropy = entropy;
            }
            else if (entropy < bestEntropy)
            {
                bestEntropy = entropy;
                bestShift = rot;
            }
        }
        rotations.E[e] = bestShift;
    }
}


void BC7_ModeSplit_RotateColors(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, uint8_t modeMask, size_t regionSize, std::vector<uint8_t> *colorRotationHeaders)
{
    const size_t block_count = srcSize / 16;

    unsigned long sizeRoundedUpToNearestPowerOfTwo;
    BitScanReverse64(&sizeRoundedUpToNearestPowerOfTwo, regionSize);
    if (__popcnt64(regionSize) != 1) sizeRoundedUpToNearestPowerOfTwo++;
    size_t clampedRegionSizePowerOfTwo = std::min<size_t>(27, std::max<size_t>(12, sizeRoundedUpToNearestPowerOfTwo));
    uint32_t clampedRegionSize = 1 << clampedRegionSizePowerOfTwo;
    const size_t regions = (srcSize + clampedRegionSize - 1) / clampedRegionSize;

    // color point counts
    struct ModeEndpointCounts
    {
        size_t Counts[8][128];
        size_t TotalEndpointPairs;
        ColorRotation Rotation;
        size_t RotationOrder[8];
    };

    struct RegionEndpointCounts
    {
        ModeEndpointCounts M[8] = {};
    };

    std::vector<RegionEndpointCounts> endpointCounts(regions);
    std::vector<size_t> elementCounts(regions);

    // first pass, record color points
    const uint8_t* pSrc = src;
    for (size_t i = 0; i < block_count; ++i, pSrc += 16)
    {
        const size_t r = (i * 16) / clampedRegionSize;
        elementCounts[r]++;
        // Look for first set bit in [7:0].  If none set, return 8 (invalid).
        unsigned long mode;
        _BitScanForward(&mode, *pSrc | 0x100u);

        if (0 == mode)
        {
            const BC7m0* b0 = reinterpret_cast<const BC7m0*>(pSrc);
            endpointCounts[r].M[0].Counts[0][b0->R0]++;
            endpointCounts[r].M[0].Counts[1][b0->R1]++;
            endpointCounts[r].M[0].Counts[0][b0->R2]++;
            endpointCounts[r].M[0].Counts[1][b0->R3]++;
            endpointCounts[r].M[0].Counts[0][b0->R4]++;
            endpointCounts[r].M[0].Counts[1][b0->R5]++;

            endpointCounts[r].M[0].Counts[2][b0->G0]++;
            endpointCounts[r].M[0].Counts[3][b0->G1]++;
            endpointCounts[r].M[0].Counts[2][b0->G2]++;
            endpointCounts[r].M[0].Counts[3][b0->G3]++;
            endpointCounts[r].M[0].Counts[2][b0->G4]++;
            endpointCounts[r].M[0].Counts[3][b0->G5]++;

            endpointCounts[r].M[0].Counts[4][b0->B0]++;
            endpointCounts[r].M[0].Counts[5][b0->B1]++;
            endpointCounts[r].M[0].Counts[4][b0->B2()]++;
            endpointCounts[r].M[0].Counts[5][b0->B3]++;
            endpointCounts[r].M[0].Counts[4][b0->B4]++;
            endpointCounts[r].M[0].Counts[5][b0->B5]++;

            endpointCounts[r].M[0].TotalEndpointPairs += 3;
        }
        else if (1 == mode)
        {
            const BC7m1* b1 = reinterpret_cast<const BC7m1*>(pSrc);
            endpointCounts[r].M[1].Counts[0][b1->R0]++;
            endpointCounts[r].M[1].Counts[1][b1->R1]++;
            endpointCounts[r].M[1].Counts[0][b1->R2]++;
            endpointCounts[r].M[1].Counts[1][b1->R3]++;

            endpointCounts[r].M[1].Counts[2][b1->G0]++;
            endpointCounts[r].M[1].Counts[3][b1->G1]++;
            endpointCounts[r].M[1].Counts[2][b1->G2]++;
            endpointCounts[r].M[1].Counts[3][b1->G3]++;

            endpointCounts[r].M[1].Counts[4][b1->B0]++;
            endpointCounts[r].M[1].Counts[5][b1->B1()]++;
            endpointCounts[r].M[1].Counts[4][b1->B2]++;
            endpointCounts[r].M[1].Counts[5][b1->B3]++;

            endpointCounts[r].M[1].TotalEndpointPairs += 2;
        }
        else if (2 == mode)
        {
            const BC7m2* b2 = reinterpret_cast<const BC7m2*>(pSrc);
            endpointCounts[r].M[2].Counts[0][b2->R0]++;
            endpointCounts[r].M[2].Counts[1][b2->R1]++;
            endpointCounts[r].M[2].Counts[0][b2->R2]++;
            endpointCounts[r].M[2].Counts[1][b2->R3]++;
            endpointCounts[r].M[2].Counts[0][b2->R4]++;
            endpointCounts[r].M[2].Counts[1][b2->R5]++;

            endpointCounts[r].M[2].Counts[2][b2->G0]++;
            endpointCounts[r].M[2].Counts[3][b2->G1]++;
            endpointCounts[r].M[2].Counts[2][b2->G2]++;
            endpointCounts[r].M[2].Counts[3][b2->G3]++;
            endpointCounts[r].M[2].Counts[2][b2->G4]++;
            endpointCounts[r].M[2].Counts[3][b2->G5]++;

            endpointCounts[r].M[2].Counts[4][b2->B0]++;
            endpointCounts[r].M[2].Counts[5][b2->B1]++;
            endpointCounts[r].M[2].Counts[4][b2->B2]++;
            endpointCounts[r].M[2].Counts[5][b2->B3]++;
            endpointCounts[r].M[2].Counts[4][b2->B4]++;
            endpointCounts[r].M[2].Counts[5][b2->B5]++;

            endpointCounts[r].M[2].TotalEndpointPairs += 3;
        }
        else if (3 == mode)
        {
            const BC7m3* b3 = reinterpret_cast<const BC7m3*>(pSrc);
            endpointCounts[r].M[3].Counts[0][b3->R0]++;
            endpointCounts[r].M[3].Counts[1][b3->R1]++;
            endpointCounts[r].M[3].Counts[0][b3->R2]++;
            endpointCounts[r].M[3].Counts[1][b3->R3]++;

            endpointCounts[r].M[3].Counts[2][b3->G0]++;
            endpointCounts[r].M[3].Counts[3][b3->G1]++;
            endpointCounts[r].M[3].Counts[2][b3->G2]++;
            endpointCounts[r].M[3].Counts[3][b3->G3()]++;

            endpointCounts[r].M[3].Counts[4][b3->B0]++;
            endpointCounts[r].M[3].Counts[5][b3->B1]++;
            endpointCounts[r].M[3].Counts[4][b3->B2]++;
            endpointCounts[r].M[3].Counts[5][b3->B3]++;

            endpointCounts[r].M[3].TotalEndpointPairs += 2;
        }
        else if (4 == mode)
        {
            const BC7m4_Derotated* b4 = reinterpret_cast<const BC7m4_Derotated*>(pSrc);
            endpointCounts[r].M[4].Counts[0][b4->R0]++;
            endpointCounts[r].M[4].Counts[1][b4->R1]++;

            endpointCounts[r].M[4].Counts[2][b4->G0]++;
            endpointCounts[r].M[4].Counts[3][b4->G1]++;

            endpointCounts[r].M[4].Counts[4][b4->B0]++;
            endpointCounts[r].M[4].Counts[5][b4->B1]++;

            endpointCounts[r].M[4].Counts[6][b4->A0]++;
            endpointCounts[r].M[4].Counts[7][b4->A1]++;

            endpointCounts[r].M[4].TotalEndpointPairs++;
        }
        else if (5 == mode)
        {
            const BC7m5_Derotated* b5 = reinterpret_cast<const BC7m5_Derotated*>(pSrc);
            endpointCounts[r].M[5].Counts[0][b5->R0]++;
            endpointCounts[r].M[5].Counts[1][b5->R1]++;

            endpointCounts[r].M[5].Counts[2][b5->G0]++;
            endpointCounts[r].M[5].Counts[3][b5->G1]++;

            endpointCounts[r].M[5].Counts[4][b5->B0]++;
            endpointCounts[r].M[5].Counts[5][b5->B1]++;

            endpointCounts[r].M[5].Counts[6][b5->A0]++;
            endpointCounts[r].M[5].Counts[7][b5->A1()]++;

            endpointCounts[r].M[5].TotalEndpointPairs++;
        }
        else if (6 == mode)
        {
            const BC7m6* b6 = reinterpret_cast<const BC7m6*>(pSrc);
            endpointCounts[r].M[6].Counts[0][b6->R0]++;
            endpointCounts[r].M[6].Counts[1][b6->R1]++;

            endpointCounts[r].M[6].Counts[2][b6->G0]++;
            endpointCounts[r].M[6].Counts[3][b6->G1]++;

            endpointCounts[r].M[6].Counts[4][b6->B0]++;
            endpointCounts[r].M[6].Counts[5][b6->B1]++;

            endpointCounts[r].M[6].Counts[6][b6->A0]++;
            endpointCounts[r].M[6].Counts[7][b6->A1]++;

            endpointCounts[r].M[6].TotalEndpointPairs++;
        }
        else if (7 == mode)
        {
            const BC7m7* b7 = reinterpret_cast<const BC7m7*>(pSrc);
            endpointCounts[r].M[7].Counts[0][b7->R0]++;
            endpointCounts[r].M[7].Counts[1][b7->R1]++;
            endpointCounts[r].M[7].Counts[0][b7->R2]++;
            endpointCounts[r].M[7].Counts[1][b7->R3]++;

            endpointCounts[r].M[7].Counts[2][b7->G0]++;
            endpointCounts[r].M[7].Counts[3][b7->G1]++;
            endpointCounts[r].M[7].Counts[2][b7->G2]++;
            endpointCounts[r].M[7].Counts[3][b7->G3]++;

            endpointCounts[r].M[7].Counts[4][b7->B0]++;
            endpointCounts[r].M[7].Counts[5][b7->B1]++;
            endpointCounts[r].M[7].Counts[4][b7->B2]++;
            endpointCounts[r].M[7].Counts[5][b7->B3]++;

            endpointCounts[r].M[7].Counts[6][b7->A0]++;
            endpointCounts[r].M[7].Counts[7][b7->A1]++;
            endpointCounts[r].M[7].Counts[6][b7->A2]++;
            endpointCounts[r].M[7].Counts[7][b7->A3]++;

            endpointCounts[r].M[7].TotalEndpointPairs += 2;
        }
    }

    for (int m = 0; m < 8; m++)
    {
        if (modeMask & (1 << m))
        {
            uint8_t rotationBitDepth[] = { 4, 6, 5, 7, 5, 7, 7, 5 };
            uint8_t rotationChannels[] = { 6, 6, 6, 6, 8, 8, 8, 8 };                 // TODO: allow rotations for less than all channels
            //size_t rotationBitsPerRegion[] = { 24, 36, 30, 42, 40, 56, 56, 8 };

            size_t rotationHeaderSize = rotationChannels[m] * regions + 1;
            colorRotationHeaders[m].resize(rotationHeaderSize);

            // todo, pipe in statics and exclude those rotations
            ModeSplitLutRotationHeader rh = { }; 
            rh.Frequency = clampedRegionSizePowerOfTwo - 12;
            rh.R = rh.G = rh.B = 1;
            rh.A = m < 4 ? 0u : 1u;
            colorRotationHeaders[m][0] = rh.Raw;

            if (regions > 1)
            {
                for (uint32_t r = 0; r < regions; r++)
                {
                    if (endpointCounts[r].M[m].TotalEndpointPairs)
                    {
                        // window size cap
                        if (clampedRegionSize < (32 * 1024))
                        {
                            ComputeRotationToStableRange7bit(endpointCounts[r].M[m].Counts, endpointCounts[r].M[m].TotalEndpointPairs, endpointCounts[r].M[m].Rotation, endpointCounts[r].M[m].RotationOrder);
                        }
                        else
                        {
                            ComputeRotationToLeastSignificantEntropy(endpointCounts[r].M[m].Counts, endpointCounts[r].M[m].TotalEndpointPairs, endpointCounts[r].M[m].Rotation, rotationBitDepth[m]);
                        }
                        const uint32_t rotationHeaderOffset = 1u + r * rotationChannels[m];
                        memcpy(&colorRotationHeaders[m][rotationHeaderOffset], endpointCounts[r].M[m].Rotation.E, rotationChannels[m]);
                    }
                }
            }
            else
            {
                if (endpointCounts[0].M[m].TotalEndpointPairs)
                {
                    ComputeRotationToLeastSignificantEntropy(endpointCounts[0].M[m].Counts, endpointCounts[0].M[m].TotalEndpointPairs, endpointCounts[0].M[m].Rotation, rotationBitDepth[m]);
                    memcpy(&colorRotationHeaders[m][1], endpointCounts[0].M[m].Rotation.E, rotationChannels[m]);
                }
            }
        }
    }

    // second pass, apply rotations
    dest.resize(srcSize);
    uint8_t* pDest = dest.data();

    for (size_t i = 0; i < block_count; ++i, src += 16, pDest += 16)
    {
        const size_t r = (i * 16) / clampedRegionSize;
        // Look for first set bit in [7:0].  If none set, return 8 (invalid).
        unsigned long mode;
        _BitScanForward(&mode, *src | 0x100u);

        if (modeMask & (1 << mode))
        {
            switch (mode) {
            case 0:
                BC7m0 b0 = *reinterpret_cast<const BC7m0*>(src);
                b0.ApplyRotation(endpointCounts[r].M[mode].Rotation.E);
                *reinterpret_cast<BC7m0*>(pDest) = b0;
                break;

            case 1:
                BC7m1 b1 = *reinterpret_cast<const BC7m1*>(src);
                b1.ApplyRotation(endpointCounts[r].M[mode].Rotation.E);
                *reinterpret_cast<BC7m1*>(pDest) = b1;
                break;

            case 2:
                BC7m2 b2 = *reinterpret_cast<const BC7m2*>(src);
                b2.ApplyRotation(endpointCounts[r].M[mode].Rotation.E);
                *reinterpret_cast<BC7m2*>(pDest) = b2;
                break;

            case 3:
                BC7m3 b3 = *reinterpret_cast<const BC7m3*>(src);
                b3.ApplyRotation(endpointCounts[r].M[mode].Rotation.E);
                *reinterpret_cast<BC7m3*>(pDest) = b3;
                break;

            case 4:
                BC7m4_Derotated b4 = *reinterpret_cast<const BC7m4_Derotated*>(src);
                b4.ApplyRotation(endpointCounts[r].M[mode].Rotation.E);
                *reinterpret_cast<BC7m4_Derotated*>(pDest) = b4;
                break;

            case 5:
                BC7m5_Derotated b5 = *reinterpret_cast<const BC7m5_Derotated*>(src);
                b5.ApplyRotation(endpointCounts[r].M[mode].Rotation.E);
                *reinterpret_cast<BC7m5_Derotated*>(pDest) = b5;
                break;

            case 6:
                BC7m6 b6 = *reinterpret_cast<const BC7m6*>(src);
                b6.ApplyRotation(endpointCounts[r].M[mode].Rotation.E);
                *reinterpret_cast<BC7m6*>(pDest) = b6;
                break;

            case 7:
                BC7m7 b7 = *reinterpret_cast<const BC7m7*>(src);
                b7.ApplyRotation(endpointCounts[r].M[mode].Rotation.E);
                *reinterpret_cast<BC7m7*>(pDest) = b7;
                break;

            default:
                assert(false);
            }
        }
        else
        {
            *reinterpret_cast<__m128i*>(pDest) = *reinterpret_cast<const __m128i*>(src);
        }
    }
}


void BC7_ModeSplit_OrderEndpointFields(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, std::vector<bool>& flipBits, BC7TextureMetrics* metrics, uint8_t modeMask, uint16_t strat)
{
    const size_t block_count = srcSize / 16;
    dest.resize(srcSize);
    uint8_t* pDest = dest.data();

    //uint16_t strat_3bit_RGB[4] = { 1, 2, 4, 0 };
    //uint16_t strat_1bit_RGB[4] = { 7, 0, 0, 0 };

    //uint16_t *channelFlipGrouping = bitSpend == 1 ? strat_1bit_RGB : strat_3bit_RGB;

    EndpointOrderStrategy* strategies[8] = {};
    uint16_t channelFlipGroupingCount[8][4] = {};

    for (uint8_t mode = 0; mode < 8; mode++)
    {
        if (1 << mode & modeMask)
        {
            strategies[mode] = &metrics->M[mode].OrderingStrategies[strat - 1];
            if (__popcnt16(metrics->M[mode].OrderingStrategies[strat - 1].Bits.B0))
            {
                channelFlipGroupingCount[mode][0] = __popcnt16(strategies[mode]->Bits.B0);
                channelFlipGroupingCount[mode][1] = __popcnt16(strategies[mode]->Bits.B1);
                channelFlipGroupingCount[mode][2] = __popcnt16(strategies[mode]->Bits.B2);
                channelFlipGroupingCount[mode][3] = __popcnt16(strategies[mode]->Bits.B3);
            }
        }
    }

    for (size_t i = 0; i < block_count; ++i, src += 16, pDest += 16)
    {
        // Look for first set bit in [7:0].  If none set, return 8 (invalid).
        unsigned long mode;
        _BitScanForward(&mode, *src | 0x100u);

        if (0 == (modeMask & (1 << mode)))
        {
            *reinterpret_cast<__m128i*>(pDest) = *reinterpret_cast<const __m128i*>(src);
        }
        else
        {
            uint16_t channelFlipGrouping[4] = {
                strategies[mode] ? strategies[mode]->Bits.B0 : 0u,
                strategies[mode] ? strategies[mode]->Bits.B1 : 0u,
                strategies[mode] ? strategies[mode]->Bits.B2 : 0u,
                strategies[mode] ? strategies[mode]->Bits.B3 : 0u,
            };

            switch (mode)
            {
            case 0:
                {
                    BC7m0 b0 = *reinterpret_cast<const BC7m0*>(src);
                    BC7m0 b0dest = b0;

                    uint8_t idealFlips[4] = {};
                    for (uint8_t g = 0; g < 4 && channelFlipGroupingCount[mode][g] > 0; g++)
                    {

                        if ((channelFlipGrouping[g] & 0x1u) && b0.R0 > b0.R1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x1u) && b0.R2 > b0.R3) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x1u) && b0.R4 > b0.R5) idealFlips[g]++;

                        if ((channelFlipGrouping[g] & 0x2u) && b0.G0 > b0.G1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x2u) && b0.G2 > b0.G3) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x2u) && b0.G4 > b0.G5) idealFlips[g]++;

                        if ((channelFlipGrouping[g] & 0x4u) && b0.B0 > b0.B1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x4u) && b0.B2() > b0.B3) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x4u) && b0.B4 > b0.B5) idealFlips[g]++;
                    }

                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        bool groupFlipsThisBlock = idealFlips[g] >= 3 * channelFlipGroupingCount[mode][g] / 2;
                        flipBits.push_back(groupFlipsThisBlock);
                        if (groupFlipsThisBlock)
                        {
                            if (channelFlipGrouping[g] & 0x1u)
                            {
                                b0dest.R0 = b0.R1;
                                b0dest.R1 = b0.R0;
                                b0dest.R2 = b0.R3;
                                b0dest.R3 = b0.R2;
                                b0dest.R4 = b0.R5;
                                b0dest.R5 = b0.R4;
                            }
                            if (channelFlipGrouping[g] & 0x2u)
                            {
                                b0dest.G0 = b0.G1;
                                b0dest.G1 = b0.G0;
                                b0dest.G2 = b0.G3;
                                b0dest.G3 = b0.G2;
                                b0dest.G4 = b0.G5;
                                b0dest.G5 = b0.G4;
                            }
                            if (channelFlipGrouping[g] & 0x4u)
                            {
                                b0dest.B0 = b0.B1;
                                b0dest.B1 = b0.B0;
                                b0dest.B2_low = 0x7u & b0.B3;
                                b0dest.B3 = b0.B2() >> 3;
                                b0dest.B4 = b0.B5;
                                b0dest.B5 = b0.B4;
                            }
                            b0 = b0dest;
                        }
                    }

                    *reinterpret_cast<BC7m0*>(pDest) = b0dest;
                }
                break;

                case 1:
                {
                    BC7m1 b1 = *reinterpret_cast<const BC7m1*>(src);
                    BC7m1 b1dest = b1;

                    uint8_t idealFlips[4] = {};
                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        if ((channelFlipGrouping[g] & 0x1u) && b1.R0 > b1.R1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x1u) && b1.R2 > b1.R3) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x2u) && b1.G0 > b1.G1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x2u) && b1.G2 > b1.G3) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x4u) && b1.B0 > b1.B1()) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x4u) && b1.B2 > b1.B3) idealFlips[g]++;
                    }

                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        bool groupFlipsThisBlock = idealFlips[g] >= channelFlipGroupingCount[mode][g];
                        flipBits.push_back(groupFlipsThisBlock);
                        if (groupFlipsThisBlock)
                        {
                            if (channelFlipGrouping[g] & 0x1u)
                            {
                                b1dest.R0 = b1.R1;
                                b1dest.R1 = b1.R0;
                                b1dest.R2 = b1.R3;
                                b1dest.R3 = b1.R2;
                            }
                            if (channelFlipGrouping[g] & 0x2u)
                            {
                                b1dest.G0 = b1.G1;
                                b1dest.G1 = b1.G0;
                                b1dest.G2 = b1.G3;
                                b1dest.G3 = b1.G2;

                            }
                            if (channelFlipGrouping[g] & 0x4u)
                            {
                                b1dest.B0 = b1.B1();
                                b1dest.B1_low = b1.B0 & 0x3u;
                                b1dest.B1_high = b1.B0 >> 2u;
                                b1dest.B2 = b1.B3;
                                b1dest.B3 = b1.B2;
                            }
                            b1 = b1dest;
                        }
                    }

                    *reinterpret_cast<BC7m1*>(pDest) = b1dest;
                }
                break;

                case 2:
                {
                    BC7m2 b2 = *reinterpret_cast<const BC7m2*>(src);
                    BC7m2 b2dest = b2;

                    uint8_t idealFlips[4] = {};
                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        if ((channelFlipGrouping[g] & 0x1u) && b2.R0 > b2.R1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x1u) && b2.R2 > b2.R3) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x1u) && b2.R4 > b2.R5) idealFlips[g]++;

                        if ((channelFlipGrouping[g] & 0x2u) && b2.G0 > b2.G1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x2u) && b2.G2 > b2.G3) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x2u) && b2.G4 > b2.G5) idealFlips[g]++;

                        if ((channelFlipGrouping[g] & 0x4u) && b2.B0 > b2.B1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x4u) && b2.B2 > b2.B3) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x4u) && b2.B4 > b2.B5) idealFlips[g]++;
                    }

                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        bool groupFlipsThisBlock = idealFlips[g] >= 3 * channelFlipGroupingCount[mode][g] / 2;
                        flipBits.push_back(groupFlipsThisBlock);
                        if (groupFlipsThisBlock)
                        {
                            if (channelFlipGrouping[g] & 0x1u)
                            {
                                b2dest.R0 = b2.R1;
                                b2dest.R1 = b2.R0;
                                b2dest.R2 = b2.R3;
                                b2dest.R3 = b2.R2;
                                b2dest.R4 = b2.R5;
                                b2dest.R5 = b2.R4;
                            }
                            if (channelFlipGrouping[g] & 0x2u)
                            {
                                b2dest.G0 = b2.G1;
                                b2dest.G1 = b2.G0;
                                b2dest.G2 = b2.G3;
                                b2dest.G3 = b2.G2;
                                b2dest.G4 = b2.G5;
                                b2dest.G5 = b2.G4;
                            }
                            if (channelFlipGrouping[g] & 0x4u)
                            {
                                b2dest.B0 = b2.B1;
                                b2dest.B1 = b2.B0;
                                b2dest.B2 = b2.B3;
                                b2dest.B3 = b2.B2;
                                b2dest.B4 = b2.B5;
                                b2dest.B5 = b2.B4;
                            }
                            b2 = b2dest;
                        }
                    }

                    *reinterpret_cast<BC7m2*>(pDest) = b2dest;
                }
                break;


            case 3:
                {
                    BC7m3 b3 = *reinterpret_cast<const BC7m3*>(src);
                    BC7m3 b3dest = b3;

                    uint8_t idealFlips[4] = {};
                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        if ((channelFlipGrouping[g] & 0x1u) && b3.R0 > b3.R1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x1u) && b3.R2 > b3.R3) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x2u) && b3.G0 > b3.G1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x2u) && b3.G2 > b3.G3()) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x4u) && b3.B0 > b3.B1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x4u) && b3.B2 > b3.B3) idealFlips[g]++;
                    }

                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        bool groupFlipsThisBlock = idealFlips[g] >= channelFlipGroupingCount[mode][g];
                        flipBits.push_back(groupFlipsThisBlock);
                        if (groupFlipsThisBlock)
                        {
                            if (channelFlipGrouping[g] & 0x1u)
                            {
                                b3dest.R0 = b3.R1;
                                b3dest.R1 = b3.R0;
                                b3dest.R2 = b3.R3;
                                b3dest.R3 = b3.R2;
                            }
                            if (channelFlipGrouping[g] & 0x2u)
                            {
                                b3dest.G0 = b3.G1;
                                b3dest.G1 = b3.G0;
                                b3dest.G2 = b3.G3();
                                b3dest.G3_low = uint8_t(b3.G2);
                                b3dest.G3_high = b3.G2 >> 5;

                            }
                            if (channelFlipGrouping[g] & 0x4u)
                            {
                                b3dest.B0 = b3.B1;
                                b3dest.B1 = b3.B0;
                                b3dest.B2 = b3.B3;
                                b3dest.B3 = b3.B2;
                            }
                            b3 = b3dest;
                        }
                    }

                    *reinterpret_cast<BC7m3*>(pDest) = b3dest;
                }
                break;

            case 4:
                {
                    BC7m4_Derotated b4 = *reinterpret_cast<const BC7m4_Derotated*>(src);
                    BC7m4_Derotated b4dest = b4;

                    uint8_t idealFlips[4] = {};
                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        if ((channelFlipGrouping[g] & 0x1u) && b4.R0 > b4.R1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x2u) && b4.G0 > b4.G1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x4u) && b4.B0 > b4.B1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x8u) && b4.A0 > b4.A1) idealFlips[g]++;
                    }

                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        bool groupFlipsThisBlock = idealFlips[g] >= channelFlipGroupingCount[mode][g] / 2;
                        flipBits.push_back(groupFlipsThisBlock);
                        if (groupFlipsThisBlock)
                        {
                            if (channelFlipGrouping[g] & 0x1u)
                            {
                                b4dest.R0 = b4.R1;
                                b4dest.R1 = b4.R0;
                            }
                            if (channelFlipGrouping[g] & 0x2u)
                            {
                                b4dest.G0 = b4.G1;
                                b4dest.G1 = b4.G0;
                            }
                            if (channelFlipGrouping[g] & 0x4u)
                            {
                                b4dest.B0 = b4.B1;
                                b4dest.B1 = b4.B0;
                            }
                            if (channelFlipGrouping[g] & 0x8u)
                            {
                                b4dest.A0 = b4.A1;
                                b4dest.A1 = b4.A0;
                            }
                            b4 = b4dest;
                        }
                    }

                    *reinterpret_cast<BC7m4_Derotated*>(pDest) = b4dest;
                }
                break;

            case 5:
                {
                    BC7m5_Derotated b5 = *reinterpret_cast<const BC7m5_Derotated*>(src);
                    BC7m5_Derotated b5dest = b5;

                    uint8_t idealFlips[4] = {};
                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        if ((channelFlipGrouping[g] & 0x1u) && b5.R0 > b5.R1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x2u) && b5.G0 > b5.G1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x4u) && b5.B0 > b5.B1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x8u) && b5.A0 > b5.A1()) idealFlips[g]++;
                    }

                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        bool groupFlipsThisBlock = idealFlips[g] >= channelFlipGroupingCount[mode][g] / 2;
                        flipBits.push_back(groupFlipsThisBlock);
                        if (groupFlipsThisBlock)
                        {
                            if (channelFlipGrouping[g] & 0x1u)
                            {
                                b5dest.R0 = b5.R1;
                                b5dest.R1 = b5.R0;
                            }
                            if (channelFlipGrouping[g] & 0x2u)
                            {
                                b5dest.G0 = b5.G1;
                                b5dest.G1 = b5.G0;
                            }
                            if (channelFlipGrouping[g] & 0x4u)
                            {
                                b5dest.B0 = b5.B1;
                                b5dest.B1 = b5.B0;
                            }
                            if (channelFlipGrouping[g] & 0x8u)
                            {
                                b5dest.A0 = b5.A1();
                                b5dest.A1_low = b5.A0 & 0x1fu;
                                b5dest.A1_high = b5.A0 >> 5u;
                            }
                            b5 = b5dest;
                        }
                    }
                    *reinterpret_cast<BC7m5_Derotated*>(pDest) = b5dest;
                }
                break;

            case 6:
                {
                    BC7m6 b6 = *reinterpret_cast<const BC7m6*>(src);
                    BC7m6 b6dest = b6;

                    uint8_t idealFlips[4] = {};
                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        if ((channelFlipGrouping[g] & 0x1u) && b6.R0 > b6.R1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x2u) && b6.G0 > b6.G1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x4u) && b6.B0 > b6.B1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x8u) && b6.A0 > b6.A1) idealFlips[g]++;
                    }

                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        bool groupFlipsThisBlock = idealFlips[g] >= channelFlipGroupingCount[mode][g] / 2;
                        flipBits.push_back(groupFlipsThisBlock);
                        if (groupFlipsThisBlock)
                        {
                            if (channelFlipGrouping[g] & 0x1u)
                            {
                                b6dest.R0 = b6.R1;
                                b6dest.R1 = b6.R0;
                            }
                            if (channelFlipGrouping[g] & 0x2u)
                            {
                                b6dest.G0 = b6.G1;
                                b6dest.G1 = b6.G0;
                            }
                            if (channelFlipGrouping[g] & 0x4u)
                            {
                                b6dest.B0 = b6.B1;
                                b6dest.B1 = b6.B0;
                            }
                            if (channelFlipGrouping[g] & 0x8u)
                            {
                                b6dest.A0 = b6.A1;
                                b6dest.A1 = b6.A0;
                            }
                            b6 = b6dest;
                        }
                    }
                    *reinterpret_cast<BC7m6*>(pDest) = b6dest;
                }
                break;

            case 7:
                {
                    BC7m7 b7 = *reinterpret_cast<const BC7m7*>(src);
                    BC7m7 b7dest = b7;

                    uint8_t idealFlips[4] = {};
                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        if ((channelFlipGrouping[g] & 0x1u) && b7.R0 > b7.R1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x1u) && b7.R2 > b7.R3) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x2u) && b7.G0 > b7.G1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x2u) && b7.G2 > b7.G3) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x4u) && b7.B0 > b7.B1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x4u) && b7.B2 > b7.B3) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x8u) && b7.A0 > b7.A1) idealFlips[g]++;
                        if ((channelFlipGrouping[g] & 0x8u) && b7.A2 > b7.A3) idealFlips[g]++;
                    }

                    for (uint8_t g = 0; g < 4 && channelFlipGrouping[g] > 0; g++)
                    {
                        bool groupFlipsThisBlock = idealFlips[g] >= channelFlipGroupingCount[mode][g];
                        flipBits.push_back(groupFlipsThisBlock);
                        if (groupFlipsThisBlock)
                        {
                            if (channelFlipGrouping[g] & 0x1u)
                            {
                                b7dest.R0 = b7.R1;
                                b7dest.R1 = b7.R0;
                                b7dest.R2 = b7.R3;
                                b7dest.R3 = b7.R2;
                            }
                            if (channelFlipGrouping[g] & 0x2u)
                            {
                                b7dest.G0 = b7.G1;
                                b7dest.G1 = b7.G0;
                                b7dest.G2 = b7.G3;
                                b7dest.G3 = b7.G2;

                            }
                            if (channelFlipGrouping[g] & 0x4u)
                            {
                                b7dest.B0 = b7.B1;
                                b7dest.B1 = b7.B0;
                                b7dest.B2 = b7.B3;
                                b7dest.B3 = b7.B2;
                            }
                            if (channelFlipGrouping[g] & 0x8u)
                            {
                                b7dest.A0 = b7.A1;
                                b7dest.A1 = b7.A0;
                                b7dest.A2 = b7.A3;
                                b7dest.A3 = b7.A2;
                            }
                            b7 = b7dest;
                        }
                    }

                    *reinterpret_cast<BC7m7*>(pDest) = b7dest;
                }
                break;

            }
        }
    }

}


struct BC7_ModeSplitBuffers
{
    std::vector<uint8_t> ModeA;
    size_t ModeABitCount = 0;

    std::vector<uint8_t> ModeB;
    size_t ModeBBitCount = 0;

    std::vector<uint64_t> ColorData64[9];
    size_t ColorDataBitCounts[9] = {};

    std::vector<uint64_t> MiscData64[9];
    size_t MiscDataBitCounts[9] = {};

    std::vector<uint64_t> Scraps64;
    size_t ScrapsBitCount = 0;

    BC7_ModeSplitBuffers(size_t elementCount)
    {
        ModeA.reserve(elementCount);
        ModeB.reserve(elementCount);
        for (int m = 0; m < 9; m++)
        {
            ColorData64[m].reserve((elementCount * 3 + 1 ) / 2);
            MiscData64[m].reserve((elementCount * 3 + 1 ) / 2);
        }
        Scraps64.reserve((elementCount + 7) / 8);
    }
};

void BC7_ModeSplit_Shuffle_Slow(uint8_t mode, const void* src, BC7_ModeSplitBuffers& destBuffers, CopyBitsSequence& s)
{
    uint64_t colorMask[2] = {};
    uint64_t miscMask[2] = {};
    uint64_t scrapsMask = 0;

    size_t colorProcessed = 0;
    size_t miscProcessed = 0;
    size_t scrapsProcessed = 0;

    size_t nextReadBit = 0;

#if _DEBUG   /* debug - useful for examining blocks */
    const BC7m0* src0 = reinterpret_cast<const BC7m0*>(src); UNREFERENCED_PARAMETER(src0);
    const BC7m1* src1 = reinterpret_cast<const BC7m1*>(src); UNREFERENCED_PARAMETER(src1);
    const BC7m2* src2 = reinterpret_cast<const BC7m2*>(src); UNREFERENCED_PARAMETER(src2);
    const BC7m3* src3 = reinterpret_cast<const BC7m3*>(src); UNREFERENCED_PARAMETER(src3);
    const BC7m4* src4 = reinterpret_cast<const BC7m4*>(src); UNREFERENCED_PARAMETER(src4);
    const BC7m5* src5 = reinterpret_cast<const BC7m5*>(src); UNREFERENCED_PARAMETER(src5);
    const BC7m6* src6 = reinterpret_cast<const BC7m6*>(src); UNREFERENCED_PARAMETER(src6);
    const BC7m7* src7 = reinterpret_cast<const BC7m7*>(src); UNREFERENCED_PARAMETER(src7);
#endif

    const size_t startColorBits = destBuffers.ColorDataBitCounts[mode];
    const size_t startMiscBits = destBuffers.MiscDataBitCounts[mode];
    const size_t startScrapsBits = destBuffers.ScrapsBitCount;

    size_t colorBitsSet = 0;
    size_t miscBitset = 0;
    size_t scrapsbitsSet = 0;

    uint64_t se[2] = {};
    memcpy_s(se, 16, src, 16);

    for (size_t o = 0; o < s.OpCount; o++)
    {
        const CopyBitsOrder& op = s.Ops[o];

        if (op.DestStream == CopyBitDestination::Mode)
        {
            nextReadBit += mode + 1ull;
            continue;
        }

        std::vector<uint64_t>& dest64 = op.DestStream == CopyBitDestination::Color ? destBuffers.ColorData64[mode] : (op.DestStream == CopyBitDestination::Misc ? destBuffers.MiscData64[mode] : destBuffers.Scraps64);
        size_t destBitsStart = op.DestStream == CopyBitDestination::Color ? startColorBits : (op.DestStream == CopyBitDestination::Misc ? startMiscBits : startScrapsBits);
        size_t& destBitsCount = op.DestStream == CopyBitDestination::Color ? destBuffers.ColorDataBitCounts[mode] : (op.DestStream == CopyBitDestination::Misc ? destBuffers.MiscDataBitCounts[mode] : destBuffers.ScrapsBitCount);
        uint64_t* mask = op.DestStream == CopyBitDestination::Color ? colorMask : (op.DestStream == CopyBitDestination::Misc ? miscMask : &scrapsMask);
        size_t* maskBits = op.DestStream == CopyBitDestination::Color ? &colorProcessed : (op.DestStream == CopyBitDestination::Misc ? &miscProcessed : &scrapsProcessed);

        size_t* bitsSet = op.DestStream == CopyBitDestination::Color ? &colorBitsSet : (op.DestStream == CopyBitDestination::Misc ? &miscBitset : &scrapsbitsSet);

        if (op.CopyBitCount == 120)  // mode 8 special case
        {
            assert(mode == 8 && op.DestStream == CopyBitDestination::Misc);
            dest64.resize(2 + destBitsStart / 64);
            for (int i = 0; i < 15; i++)
            {
                size_t destByte = i + destBitsStart / 8;
                uint8_t srcByte = reinterpret_cast<const uint8_t*>(src)[i + 1];
                dest64[destByte / 8] |= (uint64_t(srcByte) << (destByte % 8));
            }
            destBitsCount += 120;
            mask[0] = ~0ull;
            mask[1] = ~0ull >> 8;
            *maskBits += 120;
            continue;
        }

        assert(op.CopyBitCount <= 64);

        uint64_t bits = 0;
        uint64_t mbits = 0;

        // collect bits
        const size_t firstReadBit = nextReadBit;
        const size_t lastReadBit = firstReadBit + op.CopyBitCount - 1u;

        const size_t firstWriteBit = destBitsStart + op.DestBitOffset;
        const size_t lastWriteBit = firstWriteBit + op.CopyBitCount - 1u;

        {
            assert(lastReadBit < 128);
            if (firstReadBit / 64u != lastReadBit / 64u)
            {
                const size_t firstCopyCount = 64u - firstReadBit;
                const size_t secondCopyCount = lastReadBit - 63u;

                bits = (se[0] >> firstReadBit);                          // high bits from lower 64
                bits |= (se[1] & (~0ull >> (64u - secondCopyCount))) << firstCopyCount;
            }
            else
            {
                bits = (se[firstReadBit / 64u] >> (firstReadBit % 64u)) & (~0ull >> (64u - op.CopyBitCount));
            }
            mbits = ~0ull >> (64 - op.CopyBitCount);
        }

        *bitsSet += _mm_popcnt_u64(bits);

        // write bits
        {

            const size_t sizeForLastBit = (1 + (destBitsStart + lastWriteBit + 4) / 64);  // the extra +4 ensures there's always space for endpoint order scrap bits.
            if (dest64.size() < sizeForLastBit)
            {
                dest64.resize(sizeForLastBit);
            }

            if (firstWriteBit / 64u != lastWriteBit / 64u)
            {
                const size_t firstCopyCount = 64u - (firstWriteBit % 64u);

                dest64[firstWriteBit / 64u] |= (bits << (64u - firstCopyCount));

                dest64[lastWriteBit / 64u] |= (bits >> firstCopyCount);
            }
            else
            {
                dest64[firstWriteBit / 64u] |= (bits << (firstWriteBit % 64u));

            }
            if (op.DestBitOffset / 64u != (op.DestBitOffset + op.CopyBitCount - 1) / 64u)
            {
                const size_t firstMaskBits = 64ull - op.DestBitOffset;
                const size_t secondMaskBits = op.CopyBitCount - firstMaskBits;

                mask[0] |= (mbits << op.DestBitOffset);
                mask[1] |= (~0ull >> (64u - secondMaskBits));
            }
            else
            {
                mask[op.DestBitOffset / 64u] |= ((~0ull >> (64 - op.CopyBitCount)) << (op.DestBitOffset % 64u));
            }
        }
        nextReadBit += op.CopyBitCount;
        destBitsCount += op.CopyBitCount;
    }

#if _DEBUG
    const size_t newColorBits = destBuffers.ColorDataBitCounts[mode] - startColorBits;
    const size_t newMiscBits = destBuffers.MiscDataBitCounts[mode] - startMiscBits;

    assert(destBuffers.ColorDataBitCounts[mode] % 8 == 0);
    assert(colorMask[newColorBits / 64] == (newColorBits % 64 == 0 ? 0 : ~0ull >> (64 - newColorBits % 64)));
    assert(destBuffers.MiscDataBitCounts[mode] % 8 == 0);
    assert(miscMask[newMiscBits / 64] == (newMiscBits % 64 == 0 ? 0 : ~0ull >> (64 - newMiscBits % 64)));
#endif
}

}


void BC7_ModeSplit_Transform(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& destA, std::vector<uint8_t>& destB, BC7ModeSplitShuffleOptions& opt, BC7TextureMetrics& metrics)
{
    const size_t block_count = srcSize / 16;
    const uint8_t* nextStageSrc = src;

    // Step #1 - reorder color fields for mode 4\5
    std::vector<uint8_t> reordered45, epOrdered, rotated;
    std::vector<uint8_t> rotationHeaders[8];

    if (metrics.ModeCounts[4] > 0 || metrics.ModeCounts[5] > 0)
    {
        BC7_Mode45_ReorderRGBA(src, srcSize, reordered45);
        nextStageSrc = reordered45.data();
    }

    // Step #2 - Apply an endpoint ordering strategies
    std::vector<bool> epOrderBits;
    uint8_t endpointOrderModeMask = 0; 
    if (opt.EndpointOrderStrategy)
    {
        endpointOrderModeMask = 0b01110000 /*0b11111111*/;
        BC7_ModeSplit_OrderEndpointFields(nextStageSrc, srcSize, epOrdered, epOrderBits, &metrics, endpointOrderModeMask, opt.EndpointOrderStrategy);
        nextStageSrc = epOrdered.data();
    }

    // Step #3 - Apply any color rotation
    if (opt.RotationRegionSize)
    {
        BC7_ModeSplit_RotateColors(nextStageSrc, srcSize, rotated, 0b01110000 /*0b11111111*/, opt.RotationRegionSize, &rotationHeaders[0]);
        nextStageSrc = rotated.data();
    }

    size_t modeCounts[9] = {};
    bool mode8Clean = true;

    /*  Prepass if necessary for mode counts, needed for mode transforms  */
    if (metrics.ModeCounts[0] + metrics.ModeCounts[1] + 
        metrics.ModeCounts[2] + metrics.ModeCounts[3] + 
        metrics.ModeCounts[4] + metrics.ModeCounts[4] + 
        metrics.ModeCounts[6] + metrics.ModeCounts[7] + metrics.ModeCounts[8])
    {
        memcpy(modeCounts, metrics.ModeCounts, sizeof(modeCounts));
    }
    else
    {
        const uint8_t* modeByte = nextStageSrc;
        for (size_t i = 0; i < block_count; ++i, modeByte += 16)
        {
            // Look for first set bit in [7:0].  If none set, return 8 (invalid).
            unsigned long mode;
            _BitScanForward(&mode, *modeByte | 0x100u);
            modeCounts[mode]++;

            // flag if there is "extra" data in mode 8 blocks that needs to be preserved, or if such blocks are empty\clean, and we can just encode the mode
            if (mode == 8)
            {
                mode8Clean &= (reinterpret_cast<const uint64_t*>(modeByte)[0] == 0x100ull && reinterpret_cast<const uint64_t*>(modeByte)[1] == 0);
            }
        }
    }

    // two alternate encoding methods, based on the number of blocks in each mode.  First we check the mode counts, and then we create a tiny LUT at 
    // the begining of the mode stream.  The next byte indicates the mode encoding variant and number of modes in the look up table.
    // When swizzling, both modes will be encoded, and the one that packs the smallest retained.
    // 
    // A - retains the BC7 style mode selection, but remapps the smallest encoding to the most common block type so as to reduce the bit stream size.
    //     the next highest unused bit sequence will imply a run of the most common mode
    // 
    // 
    // B - encodes the mode in a constant width field of only as many bits are needed for the number of modes.
    //                                                              Encodings
    //   mode   BC mode bits       example block counts         A               B
    //    0         1                       0
    //    1         01                      3000                1000            11
    //    2         001                     0
    //    3         0001                    4000                100             10
    //    4         00001                   8000                10              01
    //    5         000001                  12000               1               00
    //    6         0000001                 0
    //    7         00000001                0
    //                                                      ....10000  
    //                                      

    uint8_t modesUsed = 0;
    for (int i = 0; i < 9; i++) if (modeCounts[i]) modesUsed++;
    uint8_t encodingBitsB = ModeTransformBCountToModeBits[modesUsed];
    uint8_t modeToEncoding[9] = {};
    uint8_t modesOrderedByCount[9] = {};
    {
        size_t modeCountCopy[9] = {};
        memcpy(modeCountCopy, modeCounts, sizeof(modeCounts));

        for (uint8_t lutEntry = 0; lutEntry < uint8_t(modesUsed); lutEntry++)
        {
            uint8_t mostFrequentMode = 0;

            for (uint8_t m = 0; m < _countof(modeCountCopy); m++)
            {
                if (modeCountCopy[m] > modeCountCopy[mostFrequentMode])
                {
                    mostFrequentMode = m;
                }
            }
            modeToEncoding[mostFrequentMode] = lutEntry;
            modesOrderedByCount[lutEntry] = mostFrequentMode;
            modeCountCopy[mostFrequentMode] = 0;
        }
    }


    BC7_ModeSplitBuffers b(block_count);

    unsigned long lastMode = 0xffu;
    size_t lastModeRun = 0;
    size_t lastModeEncodingOffsetA = 0;
    size_t lastEndpointOrderBit = 0;

    for (size_t i = 0; i < block_count; ++i, nextStageSrc += 16)
    {
        unsigned long mode;
        _BitScanForward(&mode, *nextStageSrc | 0x100u);

        if (mode == lastMode)
        {
            lastModeRun++;
        }
        else
        {
            lastMode = mode;
            lastModeRun = 1;
            lastModeEncodingOffsetA = b.ModeABitCount;
        }

        /*  encode mode bits */
        if (modesUsed > 1)  // if all blocks are the same mode, no per-block bits are needed
        {
            {   // transformA

#if defined(ALLOW_RUN_LENGTH_MODE_A)
                if (modeToEncoding[mode] <= 1 && lastModeRun >= 16)
                {
                    // encode run
                    const size_t encodeVal = mode == modesOrderedByCount[0] ? modesUsed : modesUsed + 1;
                    const size_t modeBits = encodeVal + 1;

                    // count encoded is lastModeRun - 16
                    // 
                    const size_t totalBits = modeBits + 7;  // count 0-127  (16-143)
                    size_t encodedModeRun = lastModeRun - 16;
                    const size_t bitsToWrite = ((encodedModeRun << 1) + 1) << encodeVal; // the "1" indicating mode, followed by a 7 bit value

                    const size_t modeWriteFirstByte = (lastModeEncodingOffsetA) / 8;
                    const size_t modeWriteFirstBit = (lastModeEncodingOffsetA) % 8;

                    const size_t combinedWriteLastByte = (lastModeEncodingOffsetA + totalBits - 1) / 8;
                    const size_t combinedWriteLastBit = (lastModeEncodingOffsetA + totalBits - 1) % 8;

                    const size_t cMask = (1ul << totalBits) - 1;

                    if (b.ModeA.size() <= combinedWriteLastByte) b.ModeA.resize(combinedWriteLastByte + 1);

                    size_t bitsWritten = 0;
                    size_t nextBit = modeWriteFirstBit;

                    while (bitsWritten < totalBits)
                    {
                        const size_t bitsThisWrite = std::min<size_t>(8 - (nextBit % 8), totalBits - bitsWritten);
                        uint8_t mask = (cMask >> bitsWritten) << (nextBit % 8);

                        b.ModeA.data()[nextBit / 8] &= ~mask;
                        b.ModeA.data()[nextBit / 8] |= ((bitsToWrite >> bitsWritten) << (nextBit % 8));

                        bitsWritten += bitsThisWrite;
                        nextBit += bitsThisWrite;
                    }

                    b.ModeABitCount = lastModeEncodingOffsetA + totalBits;

                    if (lastModeRun == 143)
                    {
                        lastMode = 0xff;
                    }
                }
                else
#endif
                {
                    const size_t encodeVal = modeToEncoding[mode];
                    const size_t bits = encodeVal + 1;

                    const size_t modeWriteFinalByte = (b.ModeABitCount + bits - 1) / 8;
                    const size_t modeWriteFinalBit = (b.ModeABitCount + bits - 1) % 8;

                    if (b.ModeA.size() <= modeWriteFinalByte) b.ModeA.resize(modeWriteFinalByte + 1);

                    b.ModeA[modeWriteFinalByte] |= (1 << modeWriteFinalBit);
                    b.ModeABitCount += bits;
                }
            }

            {   // transformB
                const size_t encodeVal = modeToEncoding[mode];

                const size_t modeWriteFinalByte = (b.ModeBBitCount + encodingBitsB - 1) / 8;

                const size_t modeWriteFirstByte = (b.ModeBBitCount) / 8;
                const size_t modeWriteFirstBit = (b.ModeBBitCount) % 8;

                if (b.ModeB.size() <= modeWriteFinalByte) b.ModeB.resize(modeWriteFinalByte + 1);

                b.ModeB.data()[modeWriteFirstByte] |= (encodeVal << (modeWriteFirstBit));
                if (modeWriteFinalByte != modeWriteFirstByte)
                {
                    b.ModeB.data()[modeWriteFinalByte] |= (encodeVal >> (8 - modeWriteFirstBit));
                }
                b.ModeBBitCount += encodingBitsB;
            }
        }

        if (mode < 8 || !mode8Clean)
        {
            CopyBitsSequence sequence;
            BC7_ModeSplit_Shuffle_OpLists[mode](sequence, opt, metrics);
            BC7_ModeSplit_Shuffle_Slow((uint8_t)mode, nextStageSrc, b, sequence);

            // add endpoint order bits to scraps (when we grow the buffers we always ensure there's at least 4 bits of extra space, no resize needed here)
            if ((endpointOrderModeMask & (1 << mode)) != 0 && opt.EndpointOrderStrategy)
            {
                size_t endpointOrderBits = metrics.M[mode].OrderingStrategies[opt.EndpointOrderStrategy-1].BitsUsed();
                for (size_t ob = 0; ob < endpointOrderBits; ob++)
                {
                    if (epOrderBits[lastEndpointOrderBit])
                    {
                        b.Scraps64[b.ScrapsBitCount / 64u] |= (1ull << (b.ScrapsBitCount % 64u));
                    }
                    lastEndpointOrderBit++;
                    b.ScrapsBitCount++;
                }
            }
        }
    }

    /*  Create composite streams  */

    // streams may grow by 8 bytes sizes to accomidate int64 intrinsics, trim
    b.ModeA.resize((b.ModeABitCount + 7) / 8);
    b.ModeB.resize((b.ModeBBitCount + 7) / 8);
    const size_t bulkDataVectorUnitBits = 8 * sizeof(b.ColorData64[0][0]);
    for (int m = 0; m < (mode8Clean ? 8 : 9); m++)
    {
        b.ColorData64[m].resize((b.ColorDataBitCounts[m] + bulkDataVectorUnitBits - 1) / bulkDataVectorUnitBits);
        b.MiscData64[m].resize((b.MiscDataBitCounts[m] + bulkDataVectorUnitBits - 1) / bulkDataVectorUnitBits);
    }
    b.Scraps64.resize((b.ScrapsBitCount + bulkDataVectorUnitBits - 1) / bulkDataVectorUnitBits + 1);

    //  ModeA\B encoding
    //  1 Byte
    //      4 bits - size of mode LUT
    //      4 bits - global options
    //          0x1 = global statics byte follows
    //  N bytes LUT[0....n]
    //     4 bits - BC7 mode (most common)
    //     4 bits - Shuffle Pattern\options
    //          high bit = mode-specific option byte follows
    //              0x1 - mode-specific statics byte follows
    //  3 bytes mode stream size  [MODE A ONLY]
    //  <mode stream>
    //  <mode0 color> ... <mode7 color>
    //  <mode0 misc> ... <mode7 misc> [mode8 color]

    std::vector<uint8_t> header;

    ModeSplitStartByte byte1A = {};
    byte1A.ModesUsed = modesUsed;
    byte1A.StaticFields = (metrics.Statics ? 1u : 0u);
    byte1A.LowEntropyFields = (metrics.LowEntropy ? 1u : 0u);
    header.push_back(byte1A.Raw);

    if (byte1A.StaticFields)
    {
        header.push_back(metrics.Statics);
    }
    if (byte1A.LowEntropyFields)
    {
        header.push_back(metrics.LowEntropy);
    }

    // Mode look up table, along with pattern and shuffle options.
    for (uint32_t m = 0; m < modesUsed; m++)
    {
        const uint8_t mode = modesOrderedByCount[m];
        if (mode == 8)
        {
            ModeSplitLutByte1 lutByte = {};
            lutByte.Mode = mode;
            lutByte.ModePattern = mode8Clean ? 0 : 1;
            header.push_back(lutByte.Raw);
        }
        else
        {
//            bool optionsDifferentFromGlobal = metrics.Statics != metrics.M[mode].Statics || metrics.LowEntropy != metrics.M[mode].LowEntropy;
            ModeSplitLutByte1 lutByte = {};
            lutByte.Mode = mode;
            lutByte.ModePattern = opt.Patterns[mode];

            ModeSplitLutAdditionalExtensions byte2 = {};
            byte2.StaticFields = (metrics.Statics != metrics.M[mode].Statics ? 1u : 0u);
            byte2.LowEntropyFields = (metrics.LowEntropy != metrics.M[mode].LowEntropy ? 1u : 0u);
            byte2.Rotation = rotationHeaders[mode].size() > 0;
            //size_t orderBitsForMode = 0;
            if (epOrderBits.size() > 0 && opt.EndpointOrderStrategy && ((1 << mode) & endpointOrderModeMask))
            {
                size_t orderBitsForMode = metrics.M[mode].OrderingStrategies[opt.EndpointOrderStrategy - 1].BitsUsed();
                byte2.EndpointOrderBytes = (orderBitsForMode + 1) / 2;
            }

            lutByte.AdditionalExtensions = 
                (byte2.StaticFields ||
                byte2.LowEntropyFields ||
                byte2.EndpointOrderBytes ||
                byte2.Rotation) ? 1 : 0;

            header.push_back(lutByte.Raw);

            if (lutByte.AdditionalExtensions)
            {
                header.push_back(byte2.Raw);

                if (byte2.StaticFields)
                {
                    header.push_back(metrics.M[mode].Statics);
                }
                if (byte2.LowEntropyFields)
                {
                    header.push_back(metrics.M[mode].LowEntropy);
                }
                if (byte2.EndpointOrderBytes)
                {
                    header.push_back(metrics.M[mode].OrderingStrategies[opt.EndpointOrderStrategy - 1].BitPairs.B01);
                    if (byte2.EndpointOrderBytes == 2)
                    {
                        header.push_back(metrics.M[mode].OrderingStrategies[opt.EndpointOrderStrategy - 1].BitPairs.B23);
                    }
                }
                if (byte2.Rotation)
                {
                    header.insert(header.end(), rotationHeaders[mode].begin(), rotationHeaders[mode].end());
                }
            }
        }
    }

    if (opt.ModeTransform == BC7ModeSplitModeTransformAny || opt.ModeTransform == BC7ModeSplitModeTransformA)
    {
        destA.assign(header.begin(), header.end());
        if (modesUsed > 1)  // 3 byte mode stream length omitted if single mode exists
        {
            const size_t modeStreamBytesA = (b.ModeABitCount + 7) / 8;
            destA.push_back(uint8_t(modeStreamBytesA >> 0u));
            destA.push_back(uint8_t(modeStreamBytesA >> 8u));
            destA.push_back(uint8_t(modeStreamBytesA >> 16u));
        }
        destA.insert(destA.end(), b.ModeA.begin(), b.ModeA.end());
    }
    if (opt.ModeTransform == BC7ModeSplitModeTransformAny || opt.ModeTransform == BC7ModeSplitModeTransformB)
    {
        reinterpret_cast<ModeSplitStartByte*>(&header[0])->ModeEncoding = 1;  //  <-- mode B indicator  update when we have a full list of global options
        destB.assign(header.begin(), header.end());
        destB.insert(destB.end(), b.ModeB.begin(), b.ModeB.end());
    }

    wchar_t message[256];
    swprintf_s(message, _countof(message), L"mode header size   A:%I64u  B:%I64u\n", destA.size(), destB.size());
    OutputDebugString(message);

    for (auto out : { &destA, &destB })
    {
        if (out->size() == 0) continue;
        // TODO: switch stream order and measure a set of texture.  0 & 2 should be near
        for (int m = 0; m < 8; m++)
            //for (int m : {0, 2, 1, 3, 4, 5, 6, 7, 8 })
        {
            swprintf_s(message, _countof(message), L"color[%d]  %I64u\n", m, out->size());
            OutputDebugString(message);
            size_t currentSize = out->size();
            size_t colorDataSize = (b.ColorDataBitCounts[m] + 7) / 8;

            out->resize(currentSize + colorDataSize);
            memcpy_s(out->data() + currentSize, colorDataSize, b.ColorData64[m].data(), colorDataSize);
        }
        for (int m = 0; m < (mode8Clean ? 8 : 9); m++)
        {
            swprintf_s(message, _countof(message), L"misc[%d]  %I64u\n", m, out->size());
            OutputDebugString(message);
            size_t currentSize = out->size();
            size_t miscDataSize = (b.MiscDataBitCounts[m] + 7) / 8;

            out->resize(currentSize + miscDataSize);
            memcpy_s(out->data() + currentSize, miscDataSize, b.MiscData64[m].data(), miscDataSize);
        }

        {
            swprintf_s(message, _countof(message), L"scraps  %I64u\n", out->size());
            OutputDebugString(message);
            size_t currentSize = out->size();
            size_t scrapDataSize = (b.ScrapsBitCount + 7) / 8;

            out->resize(currentSize + scrapDataSize);
            memcpy_s(out->data() + currentSize, scrapDataSize, b.Scraps64.data(), scrapDataSize);
        }
    }
}

void BC7_ModeSplit_Reverse(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, size_t destSize, const uint8_t* ref)
{
    UNREFERENCED_PARAMETER(ref); // useful when debugging
    BC7ModeSplitShuffleOptions opt = {};
    BC7TextureMetrics metrics = {};

    ModeSplitStartByte firstByte = *reinterpret_cast<const ModeSplitStartByte*>(src);
    EndpointOrderStrategy endpointOrders[8] = {};
    const ModeSplitLutRotationHeader* rotationHeaders[8] = {};
    size_t rotationRegionSizes[8] = {};
    size_t rotationBytesPerRegion[8] = {};

    size_t modesUsed = firstByte.ModesUsed;
    size_t encodingBitsB = ModeTransformBCountToModeBits[modesUsed];
    uint8_t modeUsed[9] = {};
    uint8_t modesOrderedByCount[9] = {};

    opt.ModeTransform = (firstByte.ModeEncoding) ? BC7ModeSplitModeTransformB : BC7ModeSplitModeTransformA;
    const uint8_t* pLut = src + 1;
    size_t lutBytes = 0;

    if (firstByte.StaticFields)
    {
        metrics.Statics = *pLut;
        pLut++;
    }
    if (firstByte.LowEntropyFields)
    {
        metrics.LowEntropy = *pLut;
        pLut++;
    }

    bool mode8Clean = true;

    for (uint8_t m = 0; m < modesUsed; m++)
    {
        ModeSplitLutByte1 lutByte = *reinterpret_cast<const ModeSplitLutByte1*>(pLut + lutBytes);
        lutBytes++;

        modesOrderedByCount[m] = lutByte.Mode;
        modeUsed[modesOrderedByCount[m]] = true;
        if (modesOrderedByCount[m] == 8)
        {
            mode8Clean = lutByte.ModePattern == 0;
        }
        else
        {
            opt.Patterns[lutByte.Mode] = static_cast<BC7ModeSplitShufflePattern>(lutByte.ModePattern);
            if (lutByte.AdditionalExtensions)
            {
                ModeSplitLutAdditionalExtensions lutEntryOpt = *reinterpret_cast<const ModeSplitLutAdditionalExtensions*>(pLut + lutBytes);
                lutBytes++;
                if (lutEntryOpt.StaticFields)
                {
                    metrics.M[lutByte.Mode].Statics = *(pLut + lutBytes);
                    lutBytes++;
                }
                else
                {
                    metrics.M[lutByte.Mode].Statics = metrics.Statics;
                }
                if (lutEntryOpt.LowEntropyFields)
                {
                    metrics.M[lutByte.Mode].LowEntropy = *(pLut + lutBytes);
                    lutBytes++;
                }
                else
                {
                    metrics.M[lutByte.Mode].LowEntropy = metrics.LowEntropy;
                }
                if (lutEntryOpt.EndpointOrderBytes == 2)
                {
                    endpointOrders[lutByte.Mode].Raw = *reinterpret_cast<const uint16_t*>(pLut + lutBytes);
                    lutBytes += 2;
                }
                else if (lutEntryOpt.EndpointOrderBytes == 1)
                {
                    endpointOrders[lutByte.Mode].BitPairs.B01 = *reinterpret_cast<const uint8_t*>(pLut + lutBytes);
                    lutBytes++;
                }
                if (lutEntryOpt.Rotation)
                {
                    rotationHeaders[lutByte.Mode] = reinterpret_cast<const ModeSplitLutRotationHeader*>(pLut + lutBytes);
                    rotationRegionSizes[lutByte.Mode] = (4096ull << rotationHeaders[lutByte.Mode]->Frequency);
                    rotationBytesPerRegion[lutByte.Mode] = 2ull * __popcnt16(rotationHeaders[lutByte.Mode]->ChannelBits);
                    size_t rotationBytes = ((destSize + rotationRegionSizes[lutByte.Mode] - 1) / rotationRegionSizes[lutByte.Mode]) * rotationBytesPerRegion[lutByte.Mode];
                    lutBytes += 1 + rotationBytes;
                }
            }
            else
            {
                metrics.M[lutByte.Mode].Statics = metrics.Statics;
                metrics.M[lutByte.Mode].LowEntropy = metrics.LowEntropy;
            }
        }
    }

#ifdef ALLOW_RUN_LENGTH_MODE_A
    assert(("Not supported", false));
#endif

    struct ElementReferenceInfo
    {
        uint64_t ModeByte : 8;          // identifies which streams to pull from
        uint64_t ModeElementCount : 24; // the offsets within Color\Misc streams
        uint64_t FirstScrapBit : 32;    // the offset within the combined scraps stream
    } eriw[9] = {
        {0x01}, {0x02}, {0x04}, {0x08}, {0x10}, {0x20}, {0x40}, {0x80}, {0x0}
    };


    CopyBitsSequence opSequences[9] = {};
    size_t bitsPerElementColor[9] = {};
    size_t bitsPerElementMisc[9] = {};
    size_t bitsPerElementScaps[9] = {};
    for (int m = 0; m < (mode8Clean ? 8 : 9); m++)
    {
        if (!modeUsed[m]) continue;

        BC7_ModeSplit_Shuffle_OpLists[m](opSequences[m], opt, metrics);
        for (auto op : opSequences[m].Ops)
        {
            switch (op.DestStream)
            {
            case CopyBitDestination::Color:
                bitsPerElementColor[m] += op.CopyBitCount;
                break;
            case CopyBitDestination::Misc:
                bitsPerElementMisc[m] += op.CopyBitCount;
                break;
            case CopyBitDestination::Scraps:
                bitsPerElementScaps[m] += op.CopyBitCount;
                break;
            }
        }
        bitsPerElementScaps[m] += endpointOrders[m].BitsUsed();
        assert(bitsPerElementColor[m] % 8 == 0);
        assert(bitsPerElementMisc[m] % 8 == 0);
    }

    const size_t blockCount = destSize / 16;
    size_t modeStreamSize = 0;
    size_t modeCounts[9] = {};
    size_t firstScrapBitForElement = 0;

    const uint8_t* pModeStream = pLut + lutBytes;

    if (modesUsed > 1)
    {
        switch (opt.ModeTransform)
        {
        case BC7ModeSplitModeTransformA:
        {
            const uint8_t* pModeSizeBytes = pModeStream;
            pModeStream += 3;
            modeStreamSize =
                (size_t(pModeSizeBytes[0]) << 0u) +
                (size_t(pModeSizeBytes[1]) << 8u) +
                (size_t(pModeSizeBytes[2]) << 16u);
        }
        break;

        case BC7ModeSplitModeTransformB:
            modeStreamSize = (encodingBitsB * blockCount + 7) / 8;
            break;

        default:
            assert(false);
        }

        const uint32_t modeBytesFirstRead = uint32_t(4 - (uintptr_t)pModeStream % 4);
        const uint32_t modeBytesFirstReadShift = 8u * (4u - modeBytesFirstRead);
        const uint32_t* pModeStream32 = reinterpret_cast<const uint32_t*>(pModeStream - (uintptr_t)pModeStream % 4);
        uint64_t cachedModeStreamBits = *pModeStream32 >> modeBytesFirstReadShift;
        size_t cachedModeBitsCount = modeBytesFirstRead * 8ull;
        pModeStream32++;

        for (size_t i = 0; i < blockCount; ++i)
        {
            if (cachedModeBitsCount <= 16)
            {
                cachedModeStreamBits |= ((uint64_t)*pModeStream32) << cachedModeBitsCount;
                cachedModeBitsCount += 32;
                pModeStream32++;
            }

            unsigned long encodedModeId = 0;
            switch (opt.ModeTransform)
            {
            case BC7ModeSplitModeTransformA:
                _BitScanForward64(&encodedModeId, cachedModeStreamBits | 0x100u);
                //nextModeBit += encodedModeId + 1;
                cachedModeStreamBits >>= (encodedModeId + 1ull);
                cachedModeBitsCount -= (encodedModeId + 1ull);
                break;

            case BC7ModeSplitModeTransformB:
                encodedModeId = cachedModeStreamBits & ((1ull << encodingBitsB) - 1ull);
                //nextModeBit += encodingBitsB;
                cachedModeStreamBits >>= encodingBitsB;
                cachedModeBitsCount -= encodingBitsB;
                break;

            default:
                assert(false);
            }
            const uint8_t decodedMode = modesOrderedByCount[encodedModeId];
            modeCounts[decodedMode]++;

            // write out the color\misc\scrap offsets to run the decode in parallel

            eriw[decodedMode].FirstScrapBit = firstScrapBitForElement;
            firstScrapBitForElement += bitsPerElementScaps[decodedMode];

            *reinterpret_cast<ElementReferenceInfo*>(dest.data() + i * 16) = eriw[decodedMode];
            eriw[decodedMode].ModeElementCount++;
        }
    }
    else    // else all blocks are the same mode, and the mode encoding is skipped
    {
        const uint8_t mode = modesOrderedByCount[0];
        modeCounts[mode] = blockCount;
        for (size_t i = 0; i < blockCount; ++i)
        {
            eriw[mode].FirstScrapBit = firstScrapBitForElement;
            firstScrapBitForElement += bitsPerElementScaps[mode];
            *reinterpret_cast<ElementReferenceInfo*>(dest.data() + i * 16) = eriw[mode];
            eriw[mode].ModeElementCount++;
        }
    }


    size_t firstColorByte[9] = {};
    size_t firstMiscByte[8] = {};
    size_t firstScrapByte = 0;

    firstColorByte[0] = (pModeStream - src) + modeStreamSize;
    size_t totalScrapBits = modeCounts[0] * bitsPerElementScaps[0];
    for (int m = 1; m < 8; m++)
    {
        firstColorByte[m] = firstColorByte[m - 1] + (modeCounts[m - 1] * bitsPerElementColor[m - 1]) / 8;
        totalScrapBits += modeCounts[m] * bitsPerElementScaps[m];
    }

    firstMiscByte[0] = firstColorByte[7] + (modeCounts[7] * bitsPerElementColor[7]) / 8;
    for (int m = 1; m < 8; m++)
    {
        firstMiscByte[m] = firstMiscByte[m - 1] + (modeCounts[m - 1] * bitsPerElementMisc[m - 1]) / 8;
    }

    firstScrapByte = firstMiscByte[7] + (modeCounts[7] * bitsPerElementMisc[7]) / 8;
    firstColorByte[8] = firstScrapByte + (totalScrapBits + 7) / 8;


    for (size_t i = 0; i < blockCount; ++i)
    {
        ElementReferenceInfo eri = { *reinterpret_cast<ElementReferenceInfo*>(dest.data() + i * 16)};

        unsigned long mode;
        _BitScanForward(&mode, (uint8_t)eri.ModeByte | 0x100u);

        if (mode == 8)
        {
            if (firstColorByte[8] < srcSize)
            {
                for (int b = 1; b < 16; b++)
                {
                    dest[i * 16 + b] = *(src + firstColorByte[8] + i * 16 + b);
                }
            }
        }
        else
        {
            uint64_t de[2] = { eri.ModeByte };

            const size_t colorOffset = firstColorByte[mode] + (eri.ModeElementCount * bitsPerElementColor[mode] / 8);
            const size_t miscOffset = firstMiscByte[mode] + (eri.ModeElementCount * bitsPerElementMisc[mode] / 8);
            const size_t scrapsOffset = firstScrapByte + (eri.FirstScrapBit / 8);

            const uint64_t* pColor = reinterpret_cast<const uint64_t*>(src + (colorOffset & ~7ull));
            const size_t firstColorBit = (colorOffset & 7ull) * 8;

            const uint64_t* pMisc = reinterpret_cast<const uint64_t*>(src + (miscOffset & ~7ull));
            const size_t firstMiscBit = (miscOffset & 7ull) * 8;

            const uint64_t* pScraps = reinterpret_cast<const uint64_t*>(src + (scrapsOffset & ~7ull));
            const size_t firstScrapBit = (scrapsOffset & 7ull) * 8 + eri.FirstScrapBit % 8;

            size_t nextDestBit = mode + 1ull;

            for (uint32_t o = 0; o < opSequences[mode].OpCount; o++)
            {
                auto& op = opSequences[mode].Ops[o];

                assert(op.CopyBitCount <= 64);

                if (op.DestStream == CopyBitDestination::Mode) continue;

                uint64_t bits = 0;
                uint64_t mbits = 0;

                // collect bits
                {
                    const uint64_t* s = op.DestStream == CopyBitDestination::Color ? pColor :
                        (op.DestStream == CopyBitDestination::Misc ? pMisc : pScraps);

                    const size_t srcBitStart = op.DestStream == CopyBitDestination::Color ? firstColorBit :
                        (op.DestStream == CopyBitDestination::Misc ? firstMiscBit : firstScrapBit);

                    const size_t firstReadBit = srcBitStart + op.DestBitOffset;
                    const size_t lastReadBit = firstReadBit + op.CopyBitCount - 1;

                    if (firstReadBit / 64u != lastReadBit / 64u)
                    {
                        const size_t firstCopyCount = 64u - (firstReadBit % 64u);
                        const size_t secondCopyCount = op.CopyBitCount - firstCopyCount;

                        bits = (s[firstReadBit / 64u] >> firstReadBit);                          // high bits from lower 64
                        bits |= (s[lastReadBit / 64u] & (~0ull >> (64u - secondCopyCount))) << firstCopyCount;
                    }
                    else
                    {
                        bits = (s[firstReadBit / 64u] >> (firstReadBit % 64u)) & (~0ull >> (64u - op.CopyBitCount));
                    }
                    mbits = ~0ull >> (64 - op.CopyBitCount);
                }

                // write bits
                {
                    const size_t firstWriteBit = nextDestBit;
                    const size_t lastWriteBit = firstWriteBit + op.CopyBitCount - 1;
                    assert(lastWriteBit < 128);

                    if (firstWriteBit / 64u != lastWriteBit / 64u)
                    {
                        const size_t firstCopyCount = 64u - (firstWriteBit % 64u);

                        de[firstWriteBit / 64u] |= (bits << (64 - firstCopyCount));

                        de[lastWriteBit / 64u] |= (bits >> firstCopyCount);
                    }
                    else
                    {
                        de[firstWriteBit / 64u] |= (bits << (firstWriteBit % 64u));

                    }
                    // TODO: remove?  
                    //if (op.DestBitOffset / 64u != (op.DestBitOffset + op.CopyBitCount - 1) / 64u)
                    //{
                    //    const size_t firstMaskBits = 64u - op.DestBitOffset;
                    //    const size_t secondMaskBits = op.CopyBitCount - firstMaskBits;

                    //    mask[0] |= (mbits << op.DestBitOffset);
                    //    mask[1] |= (~0ull >> (64u - secondMaskBits));
                    //}
                    //else
                    //{
                    //    mask[op.DestBitOffset / 64u] |= ((~0ull >> (64 - op.CopyBitCount)) << (op.DestBitOffset % 64u));
                    //}
                }

                nextDestBit += op.CopyBitCount;
            }

            // post-unshuffle stage #1 - color field rotation

            if (rotationHeaders[mode])
            {
                size_t rotationBitDepth[] =    { 4, 6, 5, 7, 5, 7, 7, 5 };
                //size_t rotationDepthRoundFixup[] = {7, 1, 3, 0, 0, 0, 0, 3};  // 2^(7-bitdepth) - 1
                size_t region = 16 * blockCount / rotationRegionSizes[mode];
                const uint8_t* firstRotation = reinterpret_cast<const uint8_t*>(rotationHeaders[mode]) + 1 + region * rotationBytesPerRegion[mode];
                uint8_t rotationByte = 0;
                uint8_t rotations[8] = {};
                if (rotationHeaders[mode]->R)
                {
                    rotations[0] = (1ul << rotationBitDepth[mode]) - firstRotation[rotationByte++];
                    rotations[1] = (1ul << rotationBitDepth[mode]) - firstRotation[rotationByte++];
                }
                if (rotationHeaders[mode]->G)
                {
                    rotations[2] = (1ul << rotationBitDepth[mode]) - firstRotation[rotationByte++];
                    rotations[3] = (1ul << rotationBitDepth[mode]) - firstRotation[rotationByte++];
                }
                if (rotationHeaders[mode]->B)
                {
                    rotations[4] = (1ul << rotationBitDepth[mode]) - firstRotation[rotationByte++];
                    rotations[5] = (1ul << rotationBitDepth[mode]) - firstRotation[rotationByte++];
                }
                if (rotationHeaders[mode]->A)
                {
                    rotations[6] = (1ul << rotationBitDepth[mode]) - firstRotation[rotationByte++];
                    rotations[7] = (1ul << rotationBitDepth[mode]) - firstRotation[rotationByte++];
                }

                switch (mode)
                {
                case 0:
                    reinterpret_cast<BC7m0*>(de)->ApplyRotation(rotations);
                    break;

                case 1:
                    reinterpret_cast<BC7m1*>(de)->ApplyRotation(rotations);
                    break;

                case 2:
                    reinterpret_cast<BC7m2*>(de)->ApplyRotation(rotations);
                    break;

                case 3:
                    reinterpret_cast<BC7m3*>(de)->ApplyRotation(rotations);
                    break;

                case 4:
                    reinterpret_cast<BC7m4_Derotated*>(de)->ApplyRotation(rotations);
                    break;

                case 5:
                    reinterpret_cast<BC7m5_Derotated*>(de)->ApplyRotation(rotations);
                    break;

                case 6:
                    reinterpret_cast<BC7m6*>(de)->ApplyRotation(rotations);
                    break;

                case 7:
                    reinterpret_cast<BC7m7*>(de)->ApplyRotation(rotations);
                    break;
                }
            }


            // post-unshuffle stage #2 - endpoint reordering

            uint16_t epRules[] = {
                endpointOrders[mode].Bits.B0,
                endpointOrders[mode].Bits.B1,
                endpointOrders[mode].Bits.B2,
                endpointOrders[mode].Bits.B3
            };
            uint16_t orderBits = endpointOrders[mode].BitsUsed();
            for (uint8_t eob = 0; eob < uint8_t(orderBits); eob++)
            {
                size_t scrapBit = firstScrapBit + bitsPerElementScaps[mode] - orderBits + eob;
                if (pScraps[scrapBit / 64u] & (1ull << (scrapBit % 64u)))
                {
                    switch (mode)
                    {
                    case 0:
                        BC7m0 b0 = *reinterpret_cast<BC7m0*>(de);
                        if (epRules[eob] & 0x1)
                        {
                            uint8_t swap = b0.R0;
                            b0.R0 = b0.R1;
                            b0.R1 = swap;

                            swap = b0.R2;
                            b0.R2 = b0.R3;
                            b0.R3 = swap;

                            swap = b0.R4;
                            b0.R4 = b0.R5;
                            b0.R5 = swap;
                        }
                        if (epRules[eob] & 0x2)
                        {
                            uint8_t swap = b0.G0;
                            b0.G0 = b0.G1;
                            b0.G1 = swap;

                            swap = b0.G2;
                            b0.G2 = b0.G3;
                            b0.G3 = swap;

                            swap = b0.G4;
                            b0.G4 = b0.G5;
                            b0.G5 = swap;
                        }
                        if (epRules[eob] & 0x4)
                        {
                            uint8_t swap = b0.B0;
                            b0.B0 = b0.B1;
                            b0.B1 = swap;

                            swap = uint8_t(b0.B2());
                            b0.B2_low = b0.B3 & 0x7u;
                            b0.B2_high = b0.B3 >> 3u;
                            b0.B3 = swap;

                            swap = b0.B4;
                            b0.B4 = b0.B5;
                            b0.B5 = swap;
                        }
                        *reinterpret_cast<BC7m0*>(de) = b0;
                        break;

                    case 1:
                        BC7m1 b1 = *reinterpret_cast<BC7m1*>(de);
                        if (epRules[eob] & 0x1)
                        {
                            uint8_t swap = b1.R0;
                            b1.R0 = b1.R1;
                            b1.R1 = swap;

                            swap = b1.R2;
                            b1.R2 = b1.R3;
                            b1.R3 = swap;
                        }
                        if (epRules[eob] & 0x2)
                        {
                            uint8_t swap = b1.G0;
                            b1.G0 = b1.G1;
                            b1.G1 = swap;

                            swap = b1.G2;
                            b1.G2 = b1.G3;
                            b1.G3 = swap;
                        }
                        if (epRules[eob] & 0x4)
                        {
                            uint8_t swap = b1.B0;
                            b1.B0 = b1.B1();
                            b1.B1_low = swap;
                            b1.B1_high = uint8_t(swap >> 2u);

                            swap = b1.B2;
                            b1.B2 = b1.B3;
                            b1.B3 = swap;
                        }
                        *reinterpret_cast<BC7m1*>(de) = b1;
                        break;

                    case 2:
                        BC7m2 b2 = *reinterpret_cast<BC7m2*>(de);
                        if (epRules[eob] & 0x1)
                        {
                            uint8_t swap = b2.R0;
                            b2.R0 = b2.R1;
                            b2.R1 = swap;

                            swap = b2.R2;
                            b2.R2 = b2.R3;
                            b2.R3 = swap;

                            swap = b2.R4;
                            b2.R4 = b2.R5;
                            b2.R5 = swap;
                        }
                        if (epRules[eob] & 0x2)
                        {
                            uint8_t swap = b2.G0;
                            b2.G0 = b2.G1;
                            b2.G1 = swap;

                            swap = b2.G2;
                            b2.G2 = b2.G3;
                            b2.G3 = swap;

                            swap = b2.G4;
                            b2.G4 = b2.G5;
                            b2.G5 = swap;
                        }
                        if (epRules[eob] & 0x4)
                        {
                            uint8_t swap = b2.B0;
                            b2.B0 = b2.B1;
                            b2.B1 = swap;

                            swap = b2.B2;
                            b2.B2 = b2.B3;
                            b2.B3 = swap;

                            swap = b2.B4;
                            b2.B4 = b2.B5;
                            b2.B5 = swap;
                        }
                        *reinterpret_cast<BC7m2*>(de) = b2;
                        break;

                    case 3:
                        BC7m3 b3 = *reinterpret_cast<BC7m3*>(de);
                        if (epRules[eob] & 0x1)
                        {
                            uint8_t swap = b3.R0;
                            b3.R0 = b3.R1;
                            b3.R1 = swap;

                            swap = b3.R2;
                            b3.R2 = b3.R3;
                            b3.R3 = swap;
                        }
                        if (epRules[eob] & 0x2)
                        {
                            uint8_t swap = b3.G0;
                            b3.G0 = b3.G1;
                            b3.G1 = swap;

                            swap = b3.G2;
                            b3.G2 = b3.G3();
                            b3.G3_low = swap;
                            b3.G3_high = uint8_t(swap >> 5u);
                        }
                        if (epRules[eob] & 0x4)
                        {
                            uint8_t swap = b3.B0;
                            b3.B0 = b3.B1;
                            b3.B1 = swap;

                            swap = b3.B2;
                            b3.B2 = b3.B3;
                            b3.B3 = swap;
                        }
                        *reinterpret_cast<BC7m3*>(de) = b3;
                        break;

                    case 4:
                        BC7m4_Derotated b4 = *reinterpret_cast<BC7m4_Derotated*>(de);
                        if (epRules[eob] & 0x1)
                        {
                            uint8_t swap = b4.R0;
                            b4.R0 = b4.R1;
                            b4.R1 = swap;
                        }
                        if (epRules[eob] & 0x2)
                        {
                            uint8_t swap = b4.G0;
                            b4.G0 = b4.G1;
                            b4.G1 = swap;
                        }
                        if (epRules[eob] & 0x4)
                        {
                            uint8_t swap = b4.B0;
                            b4.B0 = b4.B1;
                            b4.B1 = swap;
                        }
                        if (epRules[eob] & 0x8)
                        {
                            uint8_t swap = b4.A0;
                            b4.A0 = b4.A1;
                            b4.A1 = swap;
                        }
                        *reinterpret_cast<BC7m4_Derotated*>(de) = b4;
                        break;

                    case 5:
                        BC7m5_Derotated b5 = *reinterpret_cast<BC7m5_Derotated*>(de);
                        if (epRules[eob] & 0x1)
                        {
                            uint8_t swap = b5.R0;
                            b5.R0 = b5.R1;
                            b5.R1 = swap;
                        }
                        if (epRules[eob] & 0x2)
                        {
                            uint8_t swap = b5.G0;
                            b5.G0 = b5.G1;
                            b5.G1 = swap;
                        }
                        if (epRules[eob] & 0x4)
                        {
                            uint8_t swap = b5.B0;
                            b5.B0 = b5.B1;
                            b5.B1 = swap;
                        }
                        if (epRules[eob] & 0x8)
                        {
                            uint8_t swap = b5.A0;
                            b5.A0 = b5.A1();
                            b5.A1_low = swap;
                            b5.A1_high = uint8_t(swap >> 5u);
                        }
                        *reinterpret_cast<BC7m5_Derotated*>(de) = b5;
                        break;

                    case 6:
                        BC7m6 b6 = *reinterpret_cast<BC7m6*>(de);
                        if (epRules[eob] & 0x1)
                        {
                            uint8_t swap = b6.R0;
                            b6.R0 = b6.R1;
                            b6.R1 = swap;
                        }
                        if (epRules[eob] & 0x2)
                        {
                            uint8_t swap = b6.G0;
                            b6.G0 = b6.G1;
                            b6.G1 = swap;
                        }
                        if (epRules[eob] & 0x4)
                        {
                            uint8_t swap = b6.B0;
                            b6.B0 = b6.B1;
                            b6.B1 = swap;
                        }
                        if (epRules[eob] & 0x8)
                        {
                            uint8_t swap = b6.A0;
                            b6.A0 = b6.A1;
                            b6.A1 = swap;
                        }
                        *reinterpret_cast<BC7m6*>(de) = b6;
                        break;

                    case 7:
                        BC7m7 b7 = *reinterpret_cast<BC7m7*>(de);
                        if (epRules[eob] & 0x1)
                        {
                            uint8_t swap = b7.R0;
                            b7.R0 = b7.R1;
                            b7.R1 = swap;

                            swap = b7.R2;
                            b7.R2 = b7.R3;
                            b7.R3 = swap;
                        }
                        if (epRules[eob] & 0x2)
                        {
                            uint8_t swap = b7.G0;
                            b7.G0 = b7.G1;
                            b7.G1 = swap;

                            swap = b7.G2;
                            b7.G2 = b7.G3;
                            b7.G3 = swap;
                        }
                        if (epRules[eob] & 0x4)
                        {
                            uint8_t swap = b7.B0;
                            b7.B0 = b7.B1;
                            b7.B1 = swap;

                            swap = b7.B2;
                            b7.B2 = b7.B3;
                            b7.B3 = swap;
                        }
                        if (epRules[eob] & 0x8)
                        {
                            uint8_t swap = b7.A0;
                            b7.A0 = b7.A1;
                            b7.A1 = swap;

                            swap = b7.A2;
                            b7.A2 = b7.A3;
                            b7.A3 = swap;
                        }
                        *reinterpret_cast<BC7m7*>(de) = b7;
                        break;
                    }
                }
            }


            // post-unshuffle stage #3 - 4/5 color channel fixup

            if (mode == 4)
            {
                BC7m4_Derotated drb = *reinterpret_cast<const BC7m4_Derotated*>(de);

                uint8_t swap0, swap1;
                if (drb.Rotation)
                {
                    switch (drb.Rotation)
                    {
                    case 1:
                        swap0 = drb.R0;
                        swap1 = drb.R1;
                        drb.R0 = drb.A0;
                        drb.R1 = drb.A1;
                        break;
                    case 2:
                        swap0 = drb.G0;
                        swap1 = drb.G1;
                        drb.G0 = drb.A0;
                        drb.G1 = drb.A1;
                        break;
                    default:
                    case 3:
                        swap0 = drb.B0;
                        swap1 = drb.B1;
                        drb.B0 = drb.A0;
                        drb.B1 = drb.A1;
                        break;
                    }
                    drb.A0 = swap0;
                    drb.A1 = swap1;
                }
                *reinterpret_cast<BC7m4_Derotated*>(de) = drb;
            }
            else if (mode == 5)
            {
                BC7m5_Derotated drb = *reinterpret_cast<const BC7m5_Derotated*>(de);

                uint8_t swap0, swap1;
                if (drb.Rotation)
                {
                    switch (drb.Rotation)
                    {
                    case 1:
                        swap0 = drb.R0;
                        swap1 = drb.R1;
                        drb.R0 = drb.A0;
                        drb.R1 = drb.A1();
                        break;
                    case 2:
                        swap0 = drb.G0;
                        swap1 = drb.G1;
                        drb.G0 = drb.A0;
                        drb.G1 = drb.A1();
                        break;
                    default:
                    case 3:
                        swap0 = drb.B0;
                        swap1 = drb.B1;
                        drb.B0 = drb.A0;
                        drb.B1 = drb.A1();
                        break;
                    }
                    drb.A0 = swap0;
                    drb.SetA1(swap1);
                }
                *reinterpret_cast<BC7m5_Derotated*>(de) = drb;
            }


            reinterpret_cast<uint64_t*>(dest.data() + i * 16)[0] = de[0];
            reinterpret_cast<uint64_t*>(dest.data() + i * 16)[1] = de[1];

            if (ref != nullptr)
            {
                assert(reinterpret_cast<uint64_t*>(dest.data() + i * 16)[0] == reinterpret_cast<const uint64_t*>(ref + i * 16)[0]);
                assert(reinterpret_cast<uint64_t*>(dest.data() + i * 16)[1] == reinterpret_cast<const uint64_t*>(ref + i * 16)[1]);
            }
        }
    }
}


void BC7_ModeSplit_Validate_CopyBitOrders()
{
    // supported patterns:                          0   1   2   3   4   5   6   7
    //                          EndpointPair4bit    Y       Y                     
    //                            ColorPlane4bit    Y       Y                     
    //     EndpointPairSignificantBitInderleaved    Y   Y   Y   Y   Y   Y   Y   Y       
    //     EndpointQuadSignificantBitInderleaved        Y       Y               Y  
    //  EndpointQuadSignificantBitInderleavedAlt                                Y
    //                              StableIsland                    Y   Y   Y      

    BC7ModeSplitShuffleOptions options[] =
    {
        {BC7ModeSplitModeTransformA, {EndpointPair4bit, EndpointPairSignificantBitInderleaved, EndpointPair4bit, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved}},
        {BC7ModeSplitModeTransformB, {ColorPlane4bit, EndpointQuadSignificantBitInderleaved, ColorPlane4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleaved}},
        {BC7ModeSplitModeTransformB, {EndpointPairSignificantBitInderleaved, EndpointQuadSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt}},
    };


    for (uint32_t o = 0; o < _countof(options); o++)
    {
        BC7ModeSplitShuffleOptions& opt = options[o];
        BC7TextureMetrics emptyMetrics = {};
        for (int m = 0; m < 8; m++)
        {
            CopyBitsSequence sequence = {};
            BC7_ModeSplit_Shuffle_OpLists[m](sequence, opt, emptyMetrics);

            uint64_t destColor[2] = {};
            size_t   bitsColor = 0;

            uint64_t destMisc[2] = {};
            size_t   bitsMisc = 0;

            uint64_t destMode = 0;
            size_t   bitsMode = 0;

            uint64_t destScraps = 0;
            size_t   bitsScraps = 0;

            for (uint32_t i = 0; i < sequence.OpCount; i++)
            {
                CopyBitsOrder& op = sequence.Ops[i];
                uint64_t* dest = 0;
                size_t* bitsDest = nullptr;
                switch (op.DestStream)
                {
                case CopyBitDestination::Mode:
                    dest = &destMode;
                    bitsDest = &bitsMode;
                    break;

                case CopyBitDestination::Color:
                    dest = destColor;
                    bitsDest = &bitsColor;
                    break;

                case CopyBitDestination::Misc:
                    dest = destMisc;
                    bitsDest = &bitsMisc;
                    break;

                case CopyBitDestination::Scraps:
                    dest = &destScraps;
                    bitsDest = &bitsScraps;
                    break;
                }

                uint64_t maskThisOp = (~0ull) >> (64 - op.CopyBitCount);
                size_t lastBit = size_t(op.DestBitOffset) + op.CopyBitCount - 1;

                if (op.DestBitOffset / 64u != lastBit / 64u)
                {
                    size_t bitsFirstWrite = 64ull - op.DestBitOffset;
                    dest[0] |= (maskThisOp << (op.DestBitOffset % 64));
                    dest[1] |= (maskThisOp >> bitsFirstWrite);
                }
                else
                {
                    dest[op.DestBitOffset / 64] |= (maskThisOp << (op.DestBitOffset % 64));
                }
                *bitsDest += op.CopyBitCount;
            } //op loop
            assert(bitsMode + bitsColor + bitsMisc + bitsScraps == 128);
            assert(bitsMode == m + 1);
            assert(destMode == (0xff >> (7 - m)));
            assert(bitsColor % 8 == 0);
            if (bitsColor <= 64)
            {
                assert(destColor[0] == (~0ull >> (64 - bitsColor)));
            }
            else
            {
                assert(destColor[0] == ~0ull);
                assert(destColor[1] == (~0ull >> (128 - bitsColor)));
            }
            assert(bitsMisc % 8 == 0);
            if (bitsMisc <= 64)
            {
                assert(destMisc[0] == (~0ull >> (64 - bitsMisc)));
            }
            else
            {
                assert(destMisc[0] == ~0ull);
                assert(destMisc[1] == (~0ull >> (128 - bitsMisc)));
            }
            assert(bitsScraps == 8 - (m + 1));
            assert(destScraps == (0x7f >> m));
        }
    }
}

